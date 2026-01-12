// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "compiler/evm_compiler.h"
#include "common/thread_pool.h"
#include "compiler/cgir/cg_function.h"
#include "compiler/evm_frontend/evm_analyzer.h"
#include "compiler/mir/module.h"
#include "compiler/target/x86/x86_mc_lowering.h"
#include "platform/map.h"
#include "utils/statistics.h"

#ifdef ZEN_ENABLE_LINUX_PERF
#include "utils/perf.h"
#endif // ZEN_ENABLE_LINUX_PERF

#ifdef ZEN_ENABLE_MULTIPASS_JIT_LOGGING
#include "llvm/Support/Debug.h"
#endif // ZEN_ENABLE_MULTIPASS_JIT_LOGGING
#include "llvm/ADT/SmallVector.h"

// Constants for memory protection alignment
const size_t MPROTECT_CHUNK_SIZE = 0x1000;
#define TO_MPROTECT_CODE_SIZE(CodeSize)                                        \
  ((((CodeSize) + MPROTECT_CHUNK_SIZE - 1) / MPROTECT_CHUNK_SIZE) *            \
   MPROTECT_CHUNK_SIZE)

namespace COMPILER {

void EVMJITCompiler::compileEVMToMC(EVMFrontendContext &Ctx, MModule &Mod,
                                    uint32_t FuncIdx, bool DisableGreedyRA) {
  if (Ctx.Inited) {
    // Release all memory allocated by previous function compilation
    Ctx.MemPool = CompileMemPool();
    if (Ctx.Lazy) {
      Ctx.reinitialize();
    }
  } else {
    Ctx.initialize();
  }

  // Create MFunction for EVM bytecode compilation
  MFunction MFunc(Ctx, FuncIdx);
  CgFunction CgFunc(Ctx, MFunc);
  MFunc.setFunctionType(Mod.getFuncType(FuncIdx));
  EVMMirBuilder MIRBuilder(Ctx, MFunc);
  MIRBuilder.compile(&Ctx);

  // Apply MIR optimizations and generate machine code
  compileMIRToCgIR(Mod, MFunc, CgFunc, DisableGreedyRA);

  // Generate machine code
  Ctx.getMCLowering().runOnCgFunction(CgFunc);
}

void EagerEVMJITCompiler::compile() {
  auto Timer = Stats.startRecord(zen::utils::StatisticPhase::JITCompilation);

  EVMFrontendContext Ctx;
  Ctx.setGasMeteringEnabled(Config.EnableEvmGasMetering);
#ifdef ZEN_ENABLE_EVM_GAS_REGISTER
  Ctx.setGasRegisterEnabled(true);
#endif
  Ctx.setBytecode(reinterpret_cast<const Byte *>(EVMMod->Code),
                  EVMMod->CodeSize);
  const auto &Cache = EVMMod->getBytecodeCache();
  Ctx.setGasChunkInfo(Cache.GasChunkEnd.data(), Cache.GasChunkCost.data(),
                      EVMMod->CodeSize);

  // Analyze bytecode for potential splitting
  // Use stack-allocated analyzer and store pointer in Context
  EVMAnalyzer Analyzer;
  bool AnalysisSuccess = Analyzer.analyze(
      reinterpret_cast<const uint8_t *>(EVMMod->Code), EVMMod->CodeSize);

  // Store analyzer pointer in context for use by buildEVMFunction and bytecode
  // visitor
  Ctx.SplitAnalyzer = &Analyzer;

  MModule Mod(Ctx);

  // Check if there are split functions to determine if splitting should be used
  const auto &splitFunctions = Analyzer.getSplitFunctions();
  if (AnalysisSuccess && !splitFunctions.empty()) {
    // Split analysis successful, use splitting mode
    Ctx.UseSplitting = true;

    printf("[EagerEVMJITCompiler] Split analysis successful, found %zu split "
           "functions\n",
           splitFunctions.size());
  } else {
    // Single function compilation path (backward compatibility)
    Ctx.UseSplitting = false;

    printf("[EagerEVMJITCompiler] No split functions found, using single "
           "function mode\n");
  }

  buildEVMFunction(Ctx, Mod, *EVMMod);
  Ctx.CodeMPool = &EVMMod->getJITCodeMemPool();

#ifdef ZEN_ENABLE_LINUX_PERF
  utils::JitDumpWriter JitDumpWriter;
#define JIT_DUMP_WRITE_FUNC(FuncName, FuncAddr, FuncSize)                      \
  JitDumpWriter.writeFunc(FuncName, reinterpret_cast<uint64_t>(FuncAddr),      \
                          FuncSize)
#else
#define JIT_DUMP_WRITE_FUNC(...)
#endif // ZEN_ENABLE_LINUX_PERF

  auto &CodeMPool = EVMMod->getJITCodeMemPool();
  uint8_t *JITCode = const_cast<uint8_t *>(CodeMPool.getMemStart());

  // Compile all functions (single or multiple based on analysis)
  uint32_t NumFunctions = Mod.getNumFunctions();
  for (uint32_t FuncIdx = 0; FuncIdx < NumFunctions; ++FuncIdx) {
    compileEVMToMC(Ctx, Mod, FuncIdx, Config.DisableMultipassGreedyRA);
  }
  emitObjectBuffer(&Ctx);
  ZEN_ASSERT(Ctx.ExternRelocs.empty());

  uint8_t *JITFuncPtr = Ctx.CodePtr + Ctx.FuncOffsetMap[0];
  EVMMod->setJITCodeAndSize(JITFuncPtr, Ctx.CodeSize);
#ifdef ZEN_ENABLE_LINUX_PERF
  // Write block symbols instead of EVM_Main
  // JIT_DUMP_WRITE_FUNC("EVM_Main", JITFuncPtr, Ctx.FuncSizeMap[0]);
  for (const auto &[BBIdx, BBSymOffset] : Ctx.FuncOffsetMap) {
    if (BBIdx == 0) {
      continue;
    }
    uint8_t *BBCode = Ctx.CodePtr + BBSymOffset;
    JIT_DUMP_WRITE_FUNC(Ctx.FuncNameMap[BBIdx], BBCode, Ctx.FuncSizeMap[BBIdx]);
  }
#endif
  size_t CodeSize = CodeMPool.getMemEnd() - JITCode;
  platform::mprotect(JITCode, TO_MPROTECT_CODE_SIZE(CodeSize),
                     PROT_READ | PROT_EXEC);
  EVMMod->setJITCodeAndSize(JITCode, CodeSize);

  Stats.stopRecord(Timer);
}
} // namespace COMPILER
