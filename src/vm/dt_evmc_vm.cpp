// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "dt_evmc_vm.h"
#include "common/enums.h"
#include "common/errors.h"
#include "runtime/config.h"
#include "runtime/evm_instance.h"
#include "runtime/isolation.h"
#include "runtime/runtime.h"
#include "wrapped_host.h"

#include <evmc/evmc.h>
#include <evmc/evmc.hpp>
#include <evmc/helpers.h>

#include <cstring>

namespace {

using namespace zen::runtime;
using namespace zen::common;

#ifdef ZEN_ENABLE_JIT_PRECOMPILE_FALLBACK

// JIT compilation limits (95% < 10KB)
const size_t MAX_JIT_BYTECODE_SIZE = 0x6000;

// Maximum estimated MIR instruction count for JIT compilation.
// Some EVM opcodes (e.g. MUL) expand to many MIR instructions (~80 per
// opcode), which can cause register allocation to take extremely long on
// large basic blocks. This threshold triggers an interpreter fallback
// before compilation even starts.
const size_t MAX_JIT_MIR_ESTIMATE = 50000;

// Approximate MIR instruction count generated per EVM opcode.
// Derived from the compiler frontend: inline arithmetic expands to many
// instructions while runtime-call opcodes are cheap.
// clang-format off
static constexpr uint32_t MIR_OPCODE_WEIGHT[256] = {
  // 0x00 STOP    ADD     MUL     SUB     DIV     SDIV    MOD     SMOD
         5,       12,     80,     20,     5,      5,      5,      5,
  // 0x08 ADDMOD  MULMOD  EXP     SIGNEXT (0x0c-0x0f undefined)
         5,       5,      5,      20,     2,      2,      2,      2,
  // 0x10 LT      GT      SLT     SGT     EQ      ISZERO  AND     OR
         12,      12,     12,     12,     12,     8,      8,      8,
  // 0x18 XOR     NOT     BYTE    SHL     SHR     SAR     CLZ     (0x1f)
         8,       8,      8,      15,     15,     15,     8,      2,
  // 0x20 KECCAK256  (0x21-0x2f undefined)
         5,       2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
  // 0x30 ADDRESS BALANCE ORIGIN  CALLER  CALLVAL CLDLOAD CLDSIZE CLDCOPY
         5,       5,      5,      5,      5,      5,      5,      8,
  // 0x38 CODESIZE CODECOPY GASPRICE EXTCDSZ EXTCDCP RETDSZ  RETDCP  EXTCDHASH
         5,       8,       5,       5,       8,      5,      8,      5,
  // 0x40 BLKHASH COINBASE TIMESTAMP NUMBER PREVRAND GASLIM CHAINID SELFBAL
         5,       5,       5,        5,     5,       5,     5,      5,
  // 0x48 BASEFEE BLOBHASH BLOBBASE (0x4b-0x4f undefined)
         5,       5,       5,       2,      2,      2,      2,      2,
  // 0x50 POP     MLOAD   MSTORE  MSTORE8 SLOAD   SSTORE  JUMP    JUMPI
         2,       8,      8,      8,      5,      5,      5,      5,
  // 0x58 PC      MSIZE   GAS     JMPDEST TLOAD   TSTORE  MCOPY   (PUSH0)
         5,       5,      5,      2,      5,      5,      8,      4,
  // 0x60 PUSH1 .. PUSH32 (0x60-0x7f): all weight 4
         4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,  // PUSH1-PUSH16
         4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,  // PUSH17-PUSH32
  // 0x80 DUP1 .. DUP16 (0x80-0x8f): all weight 4
         4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
  // 0x90 SWAP1 .. SWAP16 (0x90-0x9f): all weight 4
         4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
  // 0xa0 LOG0-LOG4 (0xa0-0xa4), rest undefined
         8, 8, 8, 8, 8,  2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  // 0xb0-0xef: undefined / reserved, weight 2
         2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,  // 0xb0-0xbf
         2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,  // 0xc0-0xcf
         2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,  // 0xd0-0xdf
         2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,  // 0xe0-0xef
  // 0xf0 CREATE  CALL    CALLCODE RETURN  DELCALL (0xf5) CREAT2  (0xf7)
         5,       5,      5,       5,      5,      2,     5,      2,
  // 0xf8 (undef) (undef) STATIC   (undef) (undef) REVERT (INVALID) SELFDEST
         2,       2,      5,       2,      2,      5,     2,       5,
};
// clang-format on

/// Estimate the total MIR instruction count for an EVM bytecode sequence.
/// This is a fast O(n) scan that uses per-opcode weights -- no actual MIR
/// generation is performed.
size_t estimateMirInstructionCount(const uint8_t *Code, size_t CodeSize) {
  size_t Estimate = 0;
  size_t I = 0;
  while (I < CodeSize) {
    uint8_t Op = Code[I];
    Estimate += MIR_OPCODE_WEIGHT[Op];
    // Skip over PUSH immediate data bytes (PUSH1=0x60 .. PUSH32=0x7f)
    if (Op >= 0x60 && Op <= 0x7f) {
      I += static_cast<size_t>(Op - 0x5f); // 1 + N immediate bytes
    } else {
      I += 1;
    }
  }
  return Estimate;
}

#endif // ZEN_ENABLE_JIT_PRECOMPILE_FALLBACK

// RAII helper for temporarily changing runtime configuration
class ScopedConfig {
public:
  ScopedConfig(Runtime *Runtime, const RuntimeConfig &NewConfig)
      : RT(Runtime), PreviousConfig(Runtime->getConfig()) {
    RT->setConfig(NewConfig);
  }

  ~ScopedConfig() { RT->setConfig(PreviousConfig); }

private:
  Runtime *RT;
  RuntimeConfig PreviousConfig;
};

// CRC32 checksum
uint32_t crc32(const uint8_t *Data, size_t Size) {
  static uint32_t Table[256];
  static bool TableInitialized = false;
  if (!TableInitialized) {
    for (uint32_t I = 0; I < 256; ++I) {
      uint32_t C = I;
      for (int J = 0; J < 8; ++J)
        C = (C & 1) ? (0xEDB88320u ^ (C >> 1)) : (C >> 1);
      Table[I] = C;
    }
    TableInitialized = true;
  }
  uint32_t Crc = 0xFFFFFFFFu;
  for (size_t I = 0; I < Size; ++I)
    Crc = Table[(Crc ^ Data[I]) & 0xFFu] ^ (Crc >> 8);
  return Crc ^ 0xFFFFFFFFu;
}

// VM interface for DTVM
struct DTVM : evmc_vm {
  DTVM();
  ~DTVM() {
    for (auto &P : LoadedMods) {
      EVMModule *Mod = P.second;
      if (!RT->unloadEVMModule(Mod)) {
        ZEN_LOG_ERROR("failed to unload EVM module");
      }
    }
    if (Iso) {
      RT->deleteManagedIsolation(Iso);
    }
  }
  RuntimeConfig Config = {.Format = InputFormat::EVM,
                          .Mode = RunMode::MultipassMode,
                          .EnableEvmGasMetering = true};
  std::unique_ptr<Runtime> RT;
  std::unique_ptr<WrappedHost> ExecHost;
  std::unordered_map<uint64_t, EVMModule *> LoadedMods;
  Isolation *Iso = nullptr;
};

/// The implementation of the evmc_vm::destroy() method.
void destroy(evmc_vm *VMInstance) { delete static_cast<DTVM *>(VMInstance); }

/// The implementation of the evmc_vm::get_capabilities() method.
evmc_capabilities_flagset get_capabilities(evmc_vm * /*instance*/) {
  return EVMC_CAPABILITY_EVM1;
}

/// VM options.
///
/// The implementation of the evmc_vm::set_option() method.
/// VMs are allowed to omit this method implementation.
enum evmc_set_option_result set_option(evmc_vm *VMInstance, const char *Name,
                                       const char *Value) {
  auto *VM = static_cast<DTVM *>(VMInstance);
  if (std::strcmp(Name, "mode") == 0) {
    if (std::strcmp(Value, "interpreter") == 0) {
      VM->Config.Mode = RunMode::InterpMode;
      return EVMC_SET_OPTION_SUCCESS;
    } else if (std::strcmp(Value, "multipass") == 0) {
      VM->Config.Mode = RunMode::MultipassMode;
      return EVMC_SET_OPTION_SUCCESS;
    } else {
      return EVMC_SET_OPTION_INVALID_VALUE;
    }
  } else if (std::strcmp(Name, "enable_gas_metering") == 0) {
    if (std::strcmp(Value, "true") == 0) {
      VM->Config.EnableEvmGasMetering = true;
      return EVMC_SET_OPTION_SUCCESS;
    } else if (std::strcmp(Value, "false") == 0) {
      VM->Config.EnableEvmGasMetering = false;
      return EVMC_SET_OPTION_SUCCESS;
    } else {
      return EVMC_SET_OPTION_INVALID_VALUE;
    }
  }
  return EVMC_SET_OPTION_INVALID_NAME;
}

/// The implementation of the evmc_vm::execute() method.
evmc_result execute(evmc_vm *EVMInstance, const evmc_host_interface *Host,
                    evmc_host_context *Context, enum evmc_revision Rev,
                    const evmc_message *Msg, const uint8_t *Code,
                    size_t CodeSize) {
  auto *VM = static_cast<DTVM *>(EVMInstance);
  struct HostContextScope {
    WrappedHost *ExecHost;
    const evmc_host_interface *PrevInterface;
    evmc_host_context *PrevContext;
    HostContextScope(WrappedHost *Host, const evmc_host_interface *Interface,
                     evmc_host_context *Context)
        : ExecHost(Host), PrevInterface(Host->getInterface()),
          PrevContext(Host->getContext()) {
      ExecHost->reinitialize(Interface, Context);
    }
    ~HostContextScope() { ExecHost->reinitialize(PrevInterface, PrevContext); }
  };

  HostContextScope HostScope(VM->ExecHost.get(), Host, Context);

  if (!VM->RT) {
    VM->RT = Runtime::newEVMRuntime(VM->Config, VM->ExecHost.get());
    if (!VM->RT) {
      return evmc_make_result(EVMC_FAILURE, 0, 0, nullptr, 0);
    }
  }
#ifdef ZEN_ENABLE_JIT_PRECOMPILE_FALLBACK
  // Use interpreter mode for bytecode that would be too expensive to JIT:
  // either the raw bytecode is very large, or opcodes like MUL would expand
  // to so many MIR instructions that register allocation becomes impractical.
  std::unique_ptr<ScopedConfig> TempConfig;
  if (VM->Config.Mode == RunMode::MultipassMode &&
      (CodeSize > MAX_JIT_BYTECODE_SIZE ||
       estimateMirInstructionCount(Code, CodeSize) > MAX_JIT_MIR_ESTIMATE)) {
    RuntimeConfig NewConfig = VM->Config;
    NewConfig.Mode = RunMode::InterpMode;
    TempConfig = std::make_unique<ScopedConfig>(VM->RT.get(), NewConfig);
  }
#endif // ZEN_ENABLE_JIT_PRECOMPILE_FALLBACK

  uint32_t CheckSum = crc32(Code, CodeSize);
  uint64_t ModKey = (static_cast<uint64_t>(Rev) << 32) | CheckSum;
  std::string ModName =
      std::to_string(CheckSum) + "_" + std::to_string(static_cast<int>(Rev));
  auto ModRet = VM->RT->loadEVMModule(ModName, Code, CodeSize, Rev);
  if (!ModRet) {
    const Error &Err = ModRet.getError();
    ZEN_ASSERT(!Err.isEmpty());
    const auto &ErrMsg = Err.getFormattedMessage(false);
    return evmc_make_result(EVMC_FAILURE, 0, 0, nullptr, 0);
  }

  EVMModule *Mod = *ModRet;
  VM->LoadedMods[ModKey] = Mod;
  if (!VM->Iso) {
    VM->Iso = VM->RT->createManagedIsolation();
  }
  if (!VM->Iso) {
    return evmc_make_result(EVMC_FAILURE, 0, 0, nullptr, 0);
  }

  auto InstRet = VM->Iso->createEVMInstance(*Mod, 1000000000);
  if (!InstRet) {
    return evmc_make_result(EVMC_FAILURE, 0, 0, nullptr, 0);
  }

  auto *TheInst = *InstRet;
  if (!TheInst) {
    return evmc_make_result(EVMC_FAILURE, 0, 0, nullptr, 0);
  }
  TheInst->setRevision(Rev);

  evmc_message Message = *Msg;
  evmc::Result Result;
  VM->RT->callEVMMain(*TheInst, Message, Result);
  VM->Iso->deleteEVMInstance(TheInst);

  return Result.release_raw();
}

/// @cond internal
#if !defined(PROJECT_VERSION)
/// The dummy project version if not provided by the build system.
#define PROJECT_VERSION "0.0.0"
#endif
/// @endcond

DTVM::DTVM()
    : evmc_vm{EVMC_ABI_VERSION, "dtvm",    PROJECT_VERSION,
              ::destroy,        ::execute, ::get_capabilities,
              ::set_option},
      ExecHost(new WrappedHost) {}
} // namespace

extern "C" evmc_vm *evmc_create_dtvmapi() { return new DTVM; }
