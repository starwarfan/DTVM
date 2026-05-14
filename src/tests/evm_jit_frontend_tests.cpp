// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "action/evm_bytecode_visitor.h"
#include "compiler/evm_frontend/evm_analyzer.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>

namespace {

using COMPILER::EVMAnalyzer;

EVMAnalyzer analyzeBytecode(const std::vector<uint8_t> &Bytecode) {
  EVMAnalyzer Analyzer(EVMC_CANCUN);
  const uint8_t *Data = Bytecode.empty() ? nullptr : Bytecode.data();
  Analyzer.analyze(Data, Bytecode.size());
  return Analyzer;
}

EVMAnalyzer
analyzeSuitabilityOnlyBytecode(const std::vector<uint8_t> &Bytecode) {
  EVMAnalyzer Analyzer(EVMC_CANCUN);
  const uint8_t *Data = Bytecode.empty() ? nullptr : Bytecode.data();
  Analyzer.analyzeSuitabilityOnly(Data, Bytecode.size());
  return Analyzer;
}

const EVMAnalyzer::BlockInfo *findBlock(const EVMAnalyzer &Analyzer,
                                        uint64_t EntryPC) {
  const auto &Blocks = Analyzer.getBlockInfos();
  auto It = Blocks.find(EntryPC);
  if (It == Blocks.end()) {
    return nullptr;
  }
  return &It->second;
}

void expectPCList(const std::vector<uint64_t> &Actual,
                  std::initializer_list<uint64_t> Expected) {
  ASSERT_EQ(Actual.size(), Expected.size());
  size_t Index = 0;
  for (uint64_t ExpectedPC : Expected) {
    EXPECT_EQ(Actual[Index], ExpectedPC) << "mismatch at index " << Index;
    ++Index;
  }
}

struct MockOperand {
  using U256Value = std::array<uint64_t, 4>;

  MockOperand() = default;
  explicit MockOperand(uint64_t Low) : Value{Low, 0, 0, 0}, Constant(true) {}
  explicit MockOperand(std::shared_ptr<U256Value> Slot)
      : Slot(std::move(Slot)) {}

  bool isConstant() const { return Constant; }

  const U256Value &getConstValue() const {
    ZEN_ASSERT(Constant && "mock operand must be constant");
    return Value;
  }

  U256Value resolvedValue() const {
    if (Slot) {
      return *Slot;
    }
    return Value;
  }

  void assign(const MockOperand &Other) {
    ZEN_ASSERT(Slot && "mock operand slot missing");
    *Slot = Other.resolvedValue();
  }

private:
  U256Value Value = {0, 0, 0, 0};
  bool Constant = false;
  std::shared_ptr<U256Value> Slot;
};

struct MockStackAccessStats {
  uint32_t StackPopCount = 0;
  uint32_t StackPushCount = 0;
  uint32_t StackGetCount = 0;
  uint32_t StackSetCount = 0;
};

class MockEVMBuilder {
public:
  using CompilerContext = COMPILER::EVMFrontendContext;
  using Operand = MockOperand;

#define MOCK_OPERAND_STUB(Name)                                                \
  template <typename... Args> Operand Name(Args...) { return Operand(0); }

#define MOCK_VOID_STUB(Name)                                                   \
  template <typename... Args> void Name(Args...) {}

  void initEVM(CompilerContext *) {
    CurrentOpcode = 0xff;
    Trapped = false;
    Undefined = false;
  }

  void finalizeEVMBase() {}

  bool isOpcodeDefined(evmc_opcode Opcode) const {
    const auto *InstructionNames =
        evmc_get_instruction_names_table(EVMC_CANCUN);
    return InstructionNames && InstructionNames[Opcode] != nullptr;
  }

  void meterOpcode(evmc_opcode Opcode, uint64_t) {
    CurrentOpcode = static_cast<uint8_t>(Opcode);
  }

  void meterOpcodeRange(uint64_t, uint64_t) {}

  void enableRuntimeStackChecks() { EnableRuntimeStackChecks = true; }

  void createStackCheckBlock(int32_t MinSize, int32_t MaxSize) {
    if (!EnableRuntimeStackChecks) {
      return;
    }
    if (RuntimeStack.size() < static_cast<size_t>(std::max(MinSize, 0))) {
      Trapped = true;
      return;
    }
    if (RuntimeStack.size() > static_cast<size_t>(std::max(MaxSize, 0))) {
      Trapped = true;
    }
  }

  Operand handlePush(const zen::common::Bytes &Data) {
    uint64_t Low = 0;
    for (zen::common::Byte Byte : Data) {
      Low = (Low << 8) | static_cast<uint64_t>(std::to_integer<uint8_t>(Byte));
    }
    LastPushValue = {Low, 0, 0, 0};
    HasLastPushValue = true;
    return Operand(Low);
  }

  void stackPush(Operand PushValue) {
    Stats[CurrentOpcode].StackPushCount++;
    RuntimeStack.push_back(PushValue);
  }

  Operand stackPop() {
    Stats[CurrentOpcode].StackPopCount++;
    ZEN_ASSERT(!RuntimeStack.empty() && "mock runtime stack underflow");
    Operand Top = RuntimeStack.back();
    RuntimeStack.pop_back();
    return Top;
  }

  void stackSet(int32_t IndexFromTop, Operand SetValue) {
    Stats[CurrentOpcode].StackSetCount++;
    size_t Index = RuntimeStack.size() - static_cast<size_t>(IndexFromTop) - 1;
    RuntimeStack[Index] = SetValue;
  }

  Operand stackGet(int32_t IndexFromTop) {
    Stats[CurrentOpcode].StackGetCount++;
    size_t Index = RuntimeStack.size() - static_cast<size_t>(IndexFromTop) - 1;
    return RuntimeStack[Index];
  }

  void setTrackedStackDepth(uint32_t Depth) {
    if (RuntimeStack.size() > Depth) {
      RuntimeStack.resize(Depth);
    }
  }

  Operand createStackEntryOperand() {
    return Operand(std::make_shared<MockOperand::U256Value>(
        MockOperand::U256Value{0, 0, 0, 0}));
  }

  void assignStackEntryOperand(const Operand &Dest, const Operand &Value) {
    Operand Copy = Dest;
    Copy.assign(Value);
  }

  void spillTrackedStack(const std::vector<Operand> &TrackedStack) {
    RuntimeStack = TrackedStack;
  }

  void setCurrentDebugBlockPC(uint64_t) {}

  template <zen::common::BinaryOperator Opr>
  Operand handleBinaryArithmetic(Operand, Operand) {
    return Operand(0);
  }

  template <zen::common::CompareOperator Opr>
  Operand handleCompareOp(Operand, Operand) {
    return Operand(0);
  }

  template <zen::common::BinaryOperator Opr>
  Operand handleBitwiseOp(Operand, Operand) {
    return Operand(0);
  }

  template <zen::common::BinaryOperator Opr>
  Operand handleShift(Operand, Operand) {
    return Operand(0);
  }

  template <size_t NumTopics, typename... Args>
  void handleLogWithTopics(Args...) {}

  Operand handleCall(Operand Gas, Operand ToAddr, Operand Value,
                     Operand ArgsOffset, Operand ArgsSize, Operand RetOffset,
                     Operand RetSize) {
    LastCallArgs[0] = Gas.resolvedValue();
    LastCallArgs[1] = ToAddr.resolvedValue();
    LastCallArgs[2] = Value.resolvedValue();
    LastCallArgs[3] = ArgsOffset.resolvedValue();
    LastCallArgs[4] = ArgsSize.resolvedValue();
    LastCallArgs[5] = RetOffset.resolvedValue();
    LastCallArgs[6] = RetSize.resolvedValue();
    HasCallArgs = true;
    CallCount++;
    return Operand(0);
  }

  Operand handleCallCode(Operand, Operand, Operand, Operand, Operand, Operand,
                         Operand) {
    return Operand(0);
  }

  Operand handleDelegateCall(Operand, Operand, Operand, Operand, Operand,
                             Operand) {
    return Operand(0);
  }

  Operand handleStaticCall(Operand, Operand, Operand, Operand, Operand,
                           Operand) {
    return Operand(0);
  }

  MOCK_OPERAND_STUB(handleAddMod);
  MOCK_OPERAND_STUB(handleAddress);
  MOCK_OPERAND_STUB(handleBalance);
  MOCK_OPERAND_STUB(handleBaseFee);
  MOCK_OPERAND_STUB(handleBlobBaseFee);
  MOCK_OPERAND_STUB(handleBlobHash);
  MOCK_OPERAND_STUB(handleBlockHash);
  MOCK_OPERAND_STUB(handleByte);
  MOCK_OPERAND_STUB(handleCallDataLoad);
  MOCK_OPERAND_STUB(handleCallDataSize);
  MOCK_OPERAND_STUB(handleCallValue);
  MOCK_OPERAND_STUB(handleCaller);
  MOCK_OPERAND_STUB(handleChainId);
  MOCK_OPERAND_STUB(handleClz);
  MOCK_OPERAND_STUB(handleCodeSize);
  MOCK_OPERAND_STUB(handleCoinBase);
  MOCK_OPERAND_STUB(handleCreate);
  MOCK_OPERAND_STUB(handleCreate2);
  MOCK_OPERAND_STUB(handleDiv);
  MOCK_OPERAND_STUB(handleExp);
  MOCK_OPERAND_STUB(handleExtCodeHash);
  MOCK_OPERAND_STUB(handleExtCodeSize);
  MOCK_OPERAND_STUB(handleGas);
  MOCK_OPERAND_STUB(handleGasLimit);
  MOCK_OPERAND_STUB(handleGasPrice);
  MOCK_OPERAND_STUB(handleKeccak256);
  MOCK_OPERAND_STUB(handleMLoad);
  MOCK_OPERAND_STUB(handleMSize);
  MOCK_OPERAND_STUB(handleMod);
  MOCK_OPERAND_STUB(handleMul);
  MOCK_OPERAND_STUB(handleMulMod);
  MOCK_OPERAND_STUB(handleNot);
  MOCK_OPERAND_STUB(handleNumber);
  MOCK_OPERAND_STUB(handleOrigin);
  MOCK_OPERAND_STUB(handlePC);
  MOCK_OPERAND_STUB(handlePrevRandao);
  MOCK_OPERAND_STUB(handleReturnDataSize);
  MOCK_OPERAND_STUB(handleSDiv);
  MOCK_OPERAND_STUB(handleSLoad);
  MOCK_OPERAND_STUB(handleSMod);
  MOCK_OPERAND_STUB(handleSelfBalance);
  MOCK_OPERAND_STUB(handleSignextend);
  MOCK_OPERAND_STUB(handleTimestamp);
  MOCK_OPERAND_STUB(handleTLoad);

  MOCK_VOID_STUB(handleCallDataCopy);
  MOCK_VOID_STUB(handleCodeCopy);
  MOCK_VOID_STUB(handleExtCodeCopy);
  MOCK_VOID_STUB(handleMCopy);
  MOCK_VOID_STUB(handleMStore);
  MOCK_VOID_STUB(handleMStore8);
  MOCK_VOID_STUB(handleReturn);
  MOCK_VOID_STUB(handleReturnDataCopy);
  MOCK_VOID_STUB(handleRevert);
  MOCK_VOID_STUB(handleSStore);
  MOCK_VOID_STUB(handleSelfDestruct);
  MOCK_VOID_STUB(handleTStore);

  void beginMemoryCompileBlock(uint64_t) {}
  void setMemoryCompileBlockConstPrecheckPlan(uint64_t, uint64_t) {}
  void setMemoryCompileBlockLinearPrecheckPlan(uint64_t, uint64_t, bool) {}
  void prepareLinearBlockMemoryPrecheck(Operand) {}
  void noteMemoryOpcodeInBlock(evmc_opcode, uint64_t) {}
  void noteHelperOpcodeInBlock(evmc_opcode, uint64_t) {}
  void endMemoryCompileBlock() {}

  void handleJump(Operand) {}
  void handleJumpI(Operand, Operand) {}
  void handleJumpDest(const uint64_t &) {}
  void handleStop() {}
  void handleUndefined() { Undefined = true; }
  void handleInvalid() { Undefined = true; }
  void handleTrap(zen::common::ErrorCode) { Trapped = true; }
  void fallbackToInterpreter(uint64_t) {}
  void releaseOperand(Operand) {}

  const MockStackAccessStats &accessStats(evmc_opcode Opcode) const {
    return Stats[static_cast<uint8_t>(Opcode)];
  }

  bool hasLastPushValue() const { return HasLastPushValue; }

  bool hasLastCallArgs() const { return HasCallArgs; }

  size_t callCount() const { return CallCount; }

  MockOperand::U256Value lastCallArg(size_t Index) const {
    ZEN_ASSERT(Index < LastCallArgs.size() && "call arg index out of range");
    return LastCallArgs[Index];
  }

  MockOperand::U256Value lastPushValue() const {
    ZEN_ASSERT(HasLastPushValue && "mock push value is missing");
    return LastPushValue;
  }

  size_t runtimeStackDepth() const { return RuntimeStack.size(); }

  MockOperand::U256Value topStackValue() const {
    ZEN_ASSERT(!RuntimeStack.empty() && "mock runtime stack is empty");
    return RuntimeStack.back().resolvedValue();
  }

  bool Trapped = false;
  bool Undefined = false;

private:
  bool EnableRuntimeStackChecks = false;
  uint8_t CurrentOpcode = 0xff;
  std::array<MockStackAccessStats, 256> Stats = {};
  std::vector<Operand> RuntimeStack;
  MockOperand::U256Value LastPushValue = {0, 0, 0, 0};
  bool HasLastPushValue = false;
  std::array<MockOperand::U256Value, 7> LastCallArgs = {
      MockOperand::U256Value{0, 0, 0, 0}, MockOperand::U256Value{0, 0, 0, 0},
      MockOperand::U256Value{0, 0, 0, 0}, MockOperand::U256Value{0, 0, 0, 0},
      MockOperand::U256Value{0, 0, 0, 0}, MockOperand::U256Value{0, 0, 0, 0},
      MockOperand::U256Value{0, 0, 0, 0}};
  bool HasCallArgs = false;
  size_t CallCount = 0;

#undef MOCK_OPERAND_STUB
#undef MOCK_VOID_STUB
};

TEST(EVMJITFrontendAnalyzerTest, ConstantJumpCanonicalizesJumpDestRuns) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x04, // PUSH1 0x04
      0x56,       // JUMP
      0x5b,       // JUMPDEST
      0x5b,       // JUMPDEST
      0x00        // STOP
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);

  EXPECT_TRUE(Analyzer.hasCanonicalJumpDest(3));
  EXPECT_TRUE(Analyzer.hasCanonicalJumpDest(4));
  EXPECT_EQ(Analyzer.getCanonicalJumpDestPC(3), 4U);
  EXPECT_EQ(Analyzer.getCanonicalJumpDestPC(4), 4U);

  const auto *EntryBlock = findBlock(Analyzer, 0);
  const auto *JumpDestBlock = findBlock(Analyzer, 4);
  ASSERT_NE(EntryBlock, nullptr);
  ASSERT_NE(JumpDestBlock, nullptr);
  EXPECT_EQ(findBlock(Analyzer, 3), nullptr);

  EXPECT_TRUE(EntryBlock->HasConstantJump);
  EXPECT_EQ(EntryBlock->ConstantJumpTargetPC, 4U);
  expectPCList(EntryBlock->Successors, {4});

  EXPECT_TRUE(JumpDestBlock->IsJumpDest);
  expectPCList(JumpDestBlock->Predecessors, {0});
}

TEST(EVMJITFrontendAnalyzerTest,
     SuitabilityOnlyKeepsFallbackMetricsWithoutBuildingCfg) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x04, // PUSH1 0x04
      0x56,       // JUMP
      0x5b,       // JUMPDEST
      0x5b,       // JUMPDEST
      0x00        // STOP
  };

  const EVMAnalyzer FullAnalyzer = analyzeBytecode(Bytecode);
  const EVMAnalyzer SuitabilityOnlyAnalyzer =
      analyzeSuitabilityOnlyBytecode(Bytecode);

  const auto &Full = FullAnalyzer.getJITSuitability();
  const auto &SuitabilityOnly = SuitabilityOnlyAnalyzer.getJITSuitability();

  EXPECT_EQ(SuitabilityOnly.ShouldFallback, Full.ShouldFallback);
  EXPECT_EQ(SuitabilityOnly.BytecodeSize, Full.BytecodeSize);
  EXPECT_EQ(SuitabilityOnly.MirEstimate, Full.MirEstimate);
  EXPECT_EQ(SuitabilityOnly.RAExpensiveCount, Full.RAExpensiveCount);
  EXPECT_EQ(SuitabilityOnly.MaxConsecutiveExpensive,
            Full.MaxConsecutiveExpensive);
  EXPECT_EQ(SuitabilityOnly.MaxBlockExpensiveCount,
            Full.MaxBlockExpensiveCount);
  EXPECT_EQ(SuitabilityOnly.DupFeedbackPatternCount,
            Full.DupFeedbackPatternCount);

  EXPECT_TRUE(SuitabilityOnlyAnalyzer.getBlockInfos().empty());
  EXPECT_FALSE(SuitabilityOnlyAnalyzer.hasCanonicalJumpDest(3));
  EXPECT_FALSE(SuitabilityOnlyAnalyzer.hasCanonicalJumpDest(4));
  EXPECT_FALSE(SuitabilityOnlyAnalyzer.hasUnknownDynamicJumpTargets());
}

TEST(EVMJITFrontendAnalyzerTest,
     ConstantJumpiKeepsMatchingEntryDepthSuccessorsLiftable) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x01, // PUSH1 0x01
      0x60, 0x06, // PUSH1 0x06
      0x57,       // JUMPI
      0x00,       // STOP
      0x5b,       // JUMPDEST
      0x00        // STOP
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);

  const auto *EntryBlock = findBlock(Analyzer, 0);
  const auto *FallthroughBlock = findBlock(Analyzer, 5);
  const auto *JumpDestBlock = findBlock(Analyzer, 6);
  ASSERT_NE(EntryBlock, nullptr);
  ASSERT_NE(FallthroughBlock, nullptr);
  ASSERT_NE(JumpDestBlock, nullptr);

  EXPECT_TRUE(EntryBlock->HasConditionalJump);
  EXPECT_TRUE(EntryBlock->HasConstantJump);
  EXPECT_FALSE(EntryBlock->HasDynamicJump);
  expectPCList(EntryBlock->Successors, {5, 6});

  EXPECT_EQ(FallthroughBlock->ResolvedEntryStackDepth, 0);
  EXPECT_TRUE(FallthroughBlock->CanLiftStack);
  expectPCList(FallthroughBlock->Predecessors, {0});

  EXPECT_TRUE(JumpDestBlock->IsJumpDest);
  EXPECT_EQ(JumpDestBlock->ResolvedEntryStackDepth, 0);
  EXPECT_TRUE(JumpDestBlock->CanLiftStack);
  expectPCList(JumpDestBlock->Predecessors, {0});
}

TEST(EVMJITFrontendAnalyzerTest,
     DynamicJumpForcesReachableJumpDestsToFallback) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x01, // PUSH1 0x01
      0x60, 0x07, // PUSH1 0x07
      0x57,       // JUMPI
      0x35,       // CALLDATALOAD
      0x56,       // JUMP
      0x5b,       // JUMPDEST
      0x00        // STOP
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);

  const auto *EntryBlock = findBlock(Analyzer, 0);
  const auto *DynamicJumpBlock = findBlock(Analyzer, 5);
  const auto *JumpDestBlock = findBlock(Analyzer, 7);
  ASSERT_NE(EntryBlock, nullptr);
  ASSERT_NE(DynamicJumpBlock, nullptr);
  ASSERT_NE(JumpDestBlock, nullptr);

  EXPECT_TRUE(Analyzer.hasUnknownDynamicJumpTargets());
  EXPECT_TRUE(EntryBlock->HasConditionalJump);
  EXPECT_TRUE(DynamicJumpBlock->HasDynamicJump);
  EXPECT_TRUE(DynamicJumpBlock->CanLiftStack);

  EXPECT_TRUE(JumpDestBlock->IsJumpDest);
  EXPECT_EQ(JumpDestBlock->ResolvedEntryStackDepth, 0);
  EXPECT_FALSE(JumpDestBlock->CanLiftStack);
}

TEST(EVMJITFrontendAnalyzerTest, HiddenEntryPrefixKeepsStaticMergesLiftable) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0xaa, // PUSH1 preserved prefix
      0x60, 0x01, // PUSH1 cond
      0x60, 0x0a, // PUSH1 jumpdest
      0x57,       // JUMPI
      0x60, 0x00, // PUSH1 0x00
      0x00,       // STOP
      0x5b,       // JUMPDEST
      0x00        // STOP
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);
  const auto *FallthroughBlock = findBlock(Analyzer, 7);
  const auto *JumpDestBlock = findBlock(Analyzer, 10);
  ASSERT_NE(FallthroughBlock, nullptr);
  ASSERT_NE(JumpDestBlock, nullptr);

  EXPECT_EQ(FallthroughBlock->ResolvedEntryStackDepth, 1);
  EXPECT_EQ(FallthroughBlock->EntryStackDepth, 0);
  EXPECT_TRUE(FallthroughBlock->CanLiftStack);

  EXPECT_EQ(JumpDestBlock->ResolvedEntryStackDepth, 1);
  EXPECT_EQ(JumpDestBlock->EntryStackDepth, 0);
  EXPECT_TRUE(JumpDestBlock->CanLiftStack);
}

TEST(EVMJITFrontendAnalyzerTest, MergeDepthConflictDisablesLiftedEntry) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x01, // PUSH1 0x01
      0x60, 0x0a, // PUSH1 0x0a
      0x57,       // JUMPI
      0x60, 0x02, // PUSH1 0x02
      0x60, 0x0a, // PUSH1 0x0a
      0x56,       // JUMP
      0x5b,       // JUMPDEST
      0x00        // STOP
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);

  const auto *EntryBlock = findBlock(Analyzer, 0);
  const auto *FallthroughBlock = findBlock(Analyzer, 5);
  const auto *MergeBlock = findBlock(Analyzer, 10);
  ASSERT_NE(EntryBlock, nullptr);
  ASSERT_NE(FallthroughBlock, nullptr);
  ASSERT_NE(MergeBlock, nullptr);

  EXPECT_TRUE(EntryBlock->HasConditionalJump);
  EXPECT_EQ(FallthroughBlock->ResolvedEntryStackDepth, 0);
  EXPECT_EQ(FallthroughBlock->ResolvedExitStackDepth, 1);
  EXPECT_TRUE(FallthroughBlock->CanLiftStack);

  EXPECT_TRUE(MergeBlock->IsJumpDest);
  EXPECT_EQ(MergeBlock->ResolvedEntryStackDepth, -1);
  EXPECT_EQ(MergeBlock->ResolvedExitStackDepth, -1);
  EXPECT_TRUE(MergeBlock->HasInconsistentEntryDepth);
  EXPECT_FALSE(MergeBlock->CanLiftStack);
  expectPCList(MergeBlock->Predecessors, {0, 5});
}

TEST(EVMJITFrontendAnalyzerTest,
     InconsistentMergeInvalidatesReachableSuccessors) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x01, // PUSH1 0x01
      0x60, 0x0a, // PUSH1 0x0a
      0x57,       // JUMPI
      0x60, 0x02, // PUSH1 0x02
      0x60, 0x0a, // PUSH1 0x0a
      0x56,       // JUMP
      0x5b,       // JUMPDEST
      0x60, 0x03, // PUSH1 0x03
      0x5b,       // JUMPDEST
      0x00        // STOP
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);

  const auto *MergeBlock = findBlock(Analyzer, 10);
  const auto *SuccessorBlock = findBlock(Analyzer, 13);
  ASSERT_NE(MergeBlock, nullptr);
  ASSERT_NE(SuccessorBlock, nullptr);

  EXPECT_EQ(MergeBlock->ResolvedEntryStackDepth, -1);
  EXPECT_EQ(MergeBlock->ResolvedExitStackDepth, -1);
  EXPECT_TRUE(MergeBlock->HasInconsistentEntryDepth);
  EXPECT_FALSE(MergeBlock->CanLiftStack);

  EXPECT_TRUE(SuccessorBlock->IsJumpDest);
  EXPECT_EQ(SuccessorBlock->ResolvedEntryStackDepth, -1);
  EXPECT_EQ(SuccessorBlock->ResolvedExitStackDepth, -1);
  EXPECT_TRUE(SuccessorBlock->HasInconsistentEntryDepth);
  EXPECT_FALSE(SuccessorBlock->CanLiftStack);
  expectPCList(SuccessorBlock->Predecessors, {10});
}

TEST(EVMJITFrontendVisitorTest,
     MaterializedBlockKeepsPopDupSwapAndAddOnLogicalStack) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0xaa, // PUSH1 0xaa
      0x60, 0xbb, // PUSH1 0xbb
      0x60, 0x01, // PUSH1 cond
      0x60, 0x0e, // PUSH1 jumpdest
      0x57,       // JUMPI
      0x60, 0xcc, // PUSH1 0xcc
      0x60, 0x0e, // PUSH1 jumpdest
      0x56,       // JUMP
      0x5b,       // JUMPDEST
      0x50,       // POP
      0x80,       // DUP1
      0x90,       // SWAP1
      0x01,       // ADD
      0x00        // STOP
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);
  const auto *TargetBlock = findBlock(Analyzer, 14);
  ASSERT_NE(TargetBlock, nullptr);
  EXPECT_FALSE(TargetBlock->CanLiftStack);
  EXPECT_EQ(TargetBlock->ResolvedEntryStackDepth, -1);
  EXPECT_EQ(TargetBlock->ResolvedExitStackDepth, -1);
  EXPECT_TRUE(TargetBlock->HasInconsistentEntryDepth);

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);

  const auto &PopStats = Builder.accessStats(OP_POP);
  EXPECT_EQ(PopStats.StackPopCount, 0U);
  EXPECT_EQ(PopStats.StackGetCount, 0U);
  EXPECT_EQ(PopStats.StackSetCount, 0U);

  const auto &DupStats = Builder.accessStats(OP_DUP1);
  EXPECT_EQ(DupStats.StackPopCount, 0U);
  EXPECT_EQ(DupStats.StackGetCount, 0U);
  EXPECT_EQ(DupStats.StackSetCount, 0U);

  const auto &SwapStats = Builder.accessStats(OP_SWAP1);
  EXPECT_EQ(SwapStats.StackPopCount, 0U);
  EXPECT_EQ(SwapStats.StackGetCount, 0U);
  EXPECT_EQ(SwapStats.StackSetCount, 0U);

  const auto &AddStats = Builder.accessStats(OP_ADD);
  EXPECT_EQ(AddStats.StackPopCount, 0U);
  EXPECT_EQ(AddStats.StackGetCount, 0U);
  EXPECT_EQ(AddStats.StackSetCount, 0U);
}

TEST(EVMJITFrontendVisitorTest,
     LiftedJumpDestEntryDoesNotDependOnRuntimeStackDepth) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0x01, // PUSH1 0x01
      0x5b,       // JUMPDEST
      0x80,       // DUP1
      0x50,       // POP
      0x00        // STOP
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);
  const auto *EntryBlock = findBlock(Analyzer, 0);
  const auto *JumpDestBlock = findBlock(Analyzer, 2);
  ASSERT_NE(EntryBlock, nullptr);
  ASSERT_NE(JumpDestBlock, nullptr);
  EXPECT_TRUE(EntryBlock->CanLiftStack);
  EXPECT_TRUE(JumpDestBlock->CanLiftStack);
  EXPECT_EQ(JumpDestBlock->ResolvedEntryStackDepth, 1);

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  Builder.enableRuntimeStackChecks();
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);
}

TEST(EVMJITFrontendVisitorTest, LegacyRevisionDupSwapPreservesOperandOrder) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0xaa, // PUSH1 0xaa
      0x60, 0xbb, // PUSH1 0xbb
      0x81,       // DUP2
      0x90,       // SWAP1
      0x50,       // POP
      0x00        // STOP
  };

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_FRONTIER);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);
  EXPECT_EQ(Builder.runtimeStackDepth(), 2U);
  EXPECT_EQ(Builder.topStackValue()[0], 0xaaU);

  const auto &DupStats = Builder.accessStats(OP_DUP2);
  EXPECT_EQ(DupStats.StackPopCount, 0U);
  EXPECT_EQ(DupStats.StackGetCount, 0U);

  const auto &SwapStats = Builder.accessStats(OP_SWAP1);
  EXPECT_EQ(SwapStats.StackPopCount, 0U);
  EXPECT_EQ(SwapStats.StackGetCount, 0U);
  EXPECT_EQ(SwapStats.StackSetCount, 0U);
}

TEST(EVMJITFrontendVisitorTest,
     LowRevisionMaterializedMergePreservesCallOperandsAfterDeepDupSwap) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0xaa, // PUSH1 0xaa
      0x60, 0xbb, // PUSH1 0xbb
      0x60, 0x00, // PUSH1 0x00
      0x35,       // CALLDATALOAD
      0x60, 0x0f, // PUSH1 target
      0x57,       // JUMPI
      0x60, 0xcc, // PUSH1 0xcc
      0x60, 0x0f, // PUSH1 target
      0x56,       // JUMP
      0x5b,       // JUMPDEST
      0x60, 0x00, // PUSH1 retSize
      0x60, 0x00, // PUSH1 retOffset
      0x60, 0x00, // PUSH1 argsSize
      0x60, 0x00, // PUSH1 argsOffset
      0x60, 0x00, // PUSH1 value
      0x86,       // DUP7 (duplicate deep merged value as to-address)
      0x90,       // SWAP1
      0x90,       // SWAP1
      0x60, 0x20, // PUSH1 gas
      0xf1,       // CALL
      0x00        // STOP
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);
  const auto *TargetBlock = findBlock(Analyzer, 15);
  ASSERT_NE(TargetBlock, nullptr);
  EXPECT_FALSE(TargetBlock->CanLiftStack);
  EXPECT_EQ(TargetBlock->ResolvedEntryStackDepth, -1);
  EXPECT_TRUE(TargetBlock->HasInconsistentEntryDepth);

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_FRONTIER);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);
  ASSERT_TRUE(Builder.hasLastCallArgs());
  EXPECT_EQ(Builder.callCount(), 1U);
  EXPECT_EQ(Builder.lastCallArg(0)[0], 0x20U); // gas
  EXPECT_EQ(Builder.lastCallArg(1)[0], 0xbbU); // to
  EXPECT_EQ(Builder.lastCallArg(2)[0], 0x0U);  // value
  EXPECT_EQ(Builder.lastCallArg(3)[0], 0x0U);  // args offset
  EXPECT_EQ(Builder.lastCallArg(4)[0], 0x0U);  // args size
  EXPECT_EQ(Builder.lastCallArg(5)[0], 0x0U);  // ret offset
  EXPECT_EQ(Builder.lastCallArg(6)[0], 0x0U);  // ret size
}

TEST(EVMJITFrontendVisitorTest,
     ModernMaterializedMergePreservesCallOperandsAfterDeepDupSwap) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0xaa, // PUSH1 0xaa
      0x60, 0xbb, // PUSH1 0xbb
      0x5f,       // PUSH0
      0x35,       // CALLDATALOAD
      0x60, 0x0e, // PUSH1 target
      0x57,       // JUMPI
      0x60, 0xcc, // PUSH1 0xcc
      0x60, 0x0e, // PUSH1 target
      0x56,       // JUMP
      0x5b,       // JUMPDEST (target)
      0x5f,       // PUSH0 retSize
      0x5f,       // PUSH0 retOffset
      0x5f,       // PUSH0 argsSize
      0x5f,       // PUSH0 argsOffset
      0x5f,       // PUSH0 value
      0x86,       // DUP7 (duplicate deep merged value as to-address)
      0x90,       // SWAP1
      0x90,       // SWAP1 (net no-op, keeps swap path exercised)
      0x60, 0x20, // PUSH1 gas
      0xf1,       // CALL
      0x00        // STOP
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);
  const auto *TargetBlock = findBlock(Analyzer, 14);
  ASSERT_NE(TargetBlock, nullptr);
  EXPECT_FALSE(TargetBlock->CanLiftStack);
  EXPECT_EQ(TargetBlock->ResolvedEntryStackDepth, -1);
  EXPECT_TRUE(TargetBlock->HasInconsistentEntryDepth);

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);
  ASSERT_TRUE(Builder.hasLastCallArgs());
  EXPECT_EQ(Builder.callCount(), 1U);
  EXPECT_EQ(Builder.lastCallArg(0)[0], 0x20U); // gas
  EXPECT_EQ(Builder.lastCallArg(1)[0], 0xbbU); // to
  EXPECT_EQ(Builder.lastCallArg(2)[0], 0x0U);  // value
  EXPECT_EQ(Builder.lastCallArg(3)[0], 0x0U);  // args offset
  EXPECT_EQ(Builder.lastCallArg(4)[0], 0x0U);  // args size
  EXPECT_EQ(Builder.lastCallArg(5)[0], 0x0U);  // ret offset
  EXPECT_EQ(Builder.lastCallArg(6)[0], 0x0U);  // ret size
}

TEST(EVMJITFrontendVisitorTest,
     ImplicitStopMaterializesLiftedStackOnFallthrough) {
  const std::vector<uint8_t> Bytecode = {
      0x60, 0xaa // PUSH1 0xaa
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);
  const auto *EntryBlock = findBlock(Analyzer, 0);
  ASSERT_NE(EntryBlock, nullptr);
  EXPECT_TRUE(EntryBlock->CanLiftStack);
  EXPECT_EQ(EntryBlock->ResolvedExitStackDepth, 1);

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);
  EXPECT_EQ(Builder.runtimeStackDepth(), 1U);
  EXPECT_EQ(Builder.topStackValue()[0], 0xaaU);
}

TEST(EVMJITFrontendVisitorTest,
     UndefinedInstructionAfterProducerDoesNotTriggerStackOverflowTrap) {
  const std::vector<uint8_t> Bytecode = {
      0x30, // ADDRESS
      0x2a  // undefined
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);
  const auto *EntryBlock = findBlock(Analyzer, 0);
  ASSERT_NE(EntryBlock, nullptr);
  EXPECT_TRUE(EntryBlock->HasUndefinedInstr);
  EXPECT_EQ(EntryBlock->MaxStackHeight, 1);

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  Builder.enableRuntimeStackChecks();
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_TRUE(Builder.Undefined);
}

TEST(EVMJITFrontendVisitorTest, TruncatedPushIsRightPaddedWithZeros) {
  const std::vector<uint8_t> Bytecode = {
      0x62, 0x12, 0x34 // PUSH3 0x12 0x34 <missing byte>
  };

  const EVMAnalyzer Analyzer = analyzeBytecode(Bytecode);
  const auto *EntryBlock = findBlock(Analyzer, 0);
  ASSERT_NE(EntryBlock, nullptr);
  EXPECT_EQ(EntryBlock->ResolvedExitStackDepth, 1);

  COMPILER::EVMFrontendContext Ctx;
  Ctx.setRevision(EVMC_CANCUN);
  Ctx.setBytecode(reinterpret_cast<const zen::common::Byte *>(Bytecode.data()),
                  Bytecode.size());

  MockEVMBuilder Builder;
  COMPILER::EVMByteCodeVisitor<MockEVMBuilder> Visitor(Builder, &Ctx);
  EXPECT_TRUE(Visitor.compile());
  EXPECT_FALSE(Builder.Trapped);
  EXPECT_FALSE(Builder.Undefined);
  ASSERT_TRUE(Builder.hasLastPushValue());
  EXPECT_EQ(Builder.lastPushValue()[0], 0x123400ULL);
  EXPECT_EQ(Builder.lastPushValue()[1], 0ULL);
  EXPECT_EQ(Builder.lastPushValue()[2], 0ULL);
  EXPECT_EQ(Builder.lastPushValue()[3], 0ULL);
}

} // namespace
