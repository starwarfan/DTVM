// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef EVM_FRONTEND_EVM_ANALYZER_H
#define EVM_FRONTEND_EVM_ANALYZER_H

#include "compiler/common/common_defs.h"
#include "evmc/evmc.h"
#include "evmc/instructions.h"
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>

namespace COMPILER {

class EVMAnalyzer {
  using Byte = zen::common::Byte;
  using Bytes = zen::common::Bytes;

public:
  EVMAnalyzer() {}

  // Configuration constants for block splitting
  static constexpr uint32_t DEFAULT_BLOCK_SIZE_THRESHOLD = 1000;
  static constexpr uint32_t SPLIT_SEARCH_WINDOW = 20;

  struct BlockInfo {
    uint64_t EntryPC = 0;
    uint64_t EndPC = 0;
    int32_t MaxStackHeight = 0;
    int32_t MinStackHeight = 0;
    int32_t MinPopHeight = 0;
    int32_t StackHeightDiff = 0;

    BlockInfo() = default;
    BlockInfo(uint64_t PC) : EntryPC(PC) {}
  };

  struct SplitState {
    std::vector<uint64_t> SplitPoints;
    uint32_t LastOpcodeCount = 0;
    uint64_t LastStackDiff = 0;
  };

  // Structure to store split function metadata
  struct SplitInfo {
    uint64_t StartPC = 0;
    uint64_t EndPC = 0;
    uint32_t FunctionIndex = 0;

    SplitInfo() = default;
    SplitInfo(uint64_t start, uint64_t end, uint32_t funcIdx)
        : StartPC(start), EndPC(end), FunctionIndex(funcIdx) {}
  };

  const std::map<uint64_t, BlockInfo> &getBlockInfos() const {
    return BlockInfos;
  }

  // Get split function information
  const std::map<uint64_t, SplitInfo> &getSplitFunctions() const {
    return SplitFunctions;
  }

  // Split metadata management methods

  // Query split information by PC address
  const SplitInfo *getSplitInfoByPC(uint64_t pc) const {
    for (const auto &entry : SplitFunctions) {
      const SplitInfo &info = entry.second;
      if (pc >= info.StartPC && pc < info.EndPC) {
        return &info;
      }
    }
    return nullptr;
  }

  // Get function index for a given PC
  uint32_t getFunctionIndexByPC(uint64_t pc) const {
    const SplitInfo *info = getSplitInfoByPC(pc);
    return info ? info->FunctionIndex : 0; // 0 is main function
  }

  // Check if PC is at a split boundary
  bool isSplitBoundary(uint64_t pc) const {
    for (const auto &entry : SplitFunctions) {
      if (entry.second.StartPC == pc) {
        return true;
      }
    }
    return false;
  }

  // Get total number of split functions (including main function)
  uint32_t getTotalFunctionCount() const {
    return SplitFunctions.empty() ? 1 : NextFunctionIndex;
  }

  // Validate split boundary correctness
  bool validateSplitBoundaries(size_t bytecodeSize) const {
    if (SplitFunctions.empty()) {
      return true; // No splits, always valid
    }

    std::vector<std::pair<uint64_t, uint64_t>> ranges;

    // Collect all ranges
    for (const auto &entry : SplitFunctions) {
      ranges.emplace_back(entry.second.StartPC, entry.second.EndPC);
    }

    // Sort ranges by start PC
    std::sort(ranges.begin(), ranges.end());

    // Check for gaps and overlaps
    uint64_t expectedStart = 0;
    for (const auto &range : ranges) {
      if (range.first != expectedStart) {
        return false; // Gap found
      }
      if (range.first >= range.second) {
        return false; // Invalid range
      }
      expectedStart = range.second;
    }

    // Check if last range covers to the end
    return expectedStart >= bytecodeSize;
  }

  // Get all split points in sorted order
  std::vector<uint64_t> getAllSplitPoints() const {
    std::vector<uint64_t> splitPoints;
    for (const auto &entry : SplitFunctions) {
      if (entry.second.StartPC > 0) { // Skip main function start
        splitPoints.push_back(entry.second.StartPC);
      }
    }
    std::sort(splitPoints.begin(), splitPoints.end());
    return splitPoints;
  }

  bool analyze(const uint8_t *Bytecode, size_t BytecodeSize) {
    BlockInfos.clear();
    const uint8_t *Ip = Bytecode;
    const uint8_t *IpEnd = Bytecode + BytecodeSize;

    // Initialize block info for the first block
    BlockInfo CurInfo(0);
    // Track opcode count per block
    uint32_t CurrentBlockOpcodeCount = 0;
    SplitState SplitState;

    while (Ip < IpEnd) {
      evmc_opcode Opcode = static_cast<evmc_opcode>(*Ip);
      ptrdiff_t Diff = Ip - Bytecode;
      PC = static_cast<uint64_t>(Diff >= 0 ? Diff : 0);

      Ip++;

      // Count this opcode (excluding PUSH instruction data bytes)
      CurrentBlockOpcodeCount++;

      // Calculate stack operations for each opcode
      int PopCount = 0;
      int PushCount = 0;

      // Determine stack effects based on opcode
      switch (Opcode) {
      case OP_STOP:
      case OP_INVALID:
        // No stack operations
        break;
      case OP_SELFDESTRUCT:
        PopCount = 1;
        break;
      case OP_ADD:
      case OP_MUL:
      case OP_SUB:
      case OP_DIV:
      case OP_SDIV:
      case OP_MOD:
      case OP_SMOD:
      case OP_EXP:
      case OP_SIGNEXTEND:
      case OP_LT:
      case OP_GT:
      case OP_SLT:
      case OP_SGT:
      case OP_EQ:
      case OP_AND:
      case OP_OR:
      case OP_XOR:
      case OP_BYTE:
      case OP_SHL:
      case OP_SHR:
      case OP_SAR:
        PopCount = 2;
        PushCount = 1;
        break;
      case OP_ADDMOD:
      case OP_MULMOD:
        PopCount = 3;
        PushCount = 1;
        break;
      case OP_ISZERO:
      case OP_NOT:
      case OP_CALLDATALOAD:
      case OP_EXTCODESIZE:
      case OP_EXTCODEHASH:
      case OP_BLOCKHASH:
      case OP_MLOAD:
      case OP_TLOAD:
      case OP_BALANCE:
      case OP_SLOAD:
      case OP_BLOBHASH:
        PopCount = 1;
        PushCount = 1;
        break;
      case OP_MSIZE:
      case OP_CALLDATASIZE:
      case OP_ADDRESS:
      case OP_ORIGIN:
      case OP_CALLER:
      case OP_CALLVALUE:
      case OP_GASPRICE:
      case OP_NUMBER:
      case OP_PREVRANDAO:
      case OP_GASLIMIT:
      case OP_CHAINID:
      case OP_SELFBALANCE:
      case OP_BASEFEE:
      case OP_BLOBBASEFEE:
      case OP_TIMESTAMP:
      case OP_COINBASE:
        PushCount = 1;
        break;
      case OP_KECCAK256:
        PopCount = 2;
        PushCount = 1;
        break;
      case OP_MSTORE:
      case OP_MSTORE8:
      case OP_SSTORE:
      case OP_TSTORE:
        PopCount = 2;
        break;
      case OP_MCOPY:
        PopCount = 3;
        break;
      case OP_PC:
      case OP_GAS:
      case OP_CODESIZE:
      case OP_RETURNDATASIZE:
        PopCount = 0;
        PushCount = 1;
        break;
      case OP_POP:
        PopCount = 1;
        PushCount = 0;
        break;
      case OP_JUMP:
        PopCount = 1;
        PushCount = 0;
        break;
      case OP_RETURN:
      case OP_REVERT:
      case OP_JUMPI:
        PopCount = 2;
        PushCount = 0;
        break;
      case OP_PUSH0:
        PopCount = 0;
        PushCount = 1;
        break;
      case OP_PUSH1:
      case OP_PUSH2:
      case OP_PUSH3:
      case OP_PUSH4:
      case OP_PUSH5:
      case OP_PUSH6:
      case OP_PUSH7:
      case OP_PUSH8:
      case OP_PUSH9:
      case OP_PUSH10:
      case OP_PUSH11:
      case OP_PUSH12:
      case OP_PUSH13:
      case OP_PUSH14:
      case OP_PUSH15:
      case OP_PUSH16:
      case OP_PUSH17:
      case OP_PUSH18:
      case OP_PUSH19:
      case OP_PUSH20:
      case OP_PUSH21:
      case OP_PUSH22:
      case OP_PUSH23:
      case OP_PUSH24:
      case OP_PUSH25:
      case OP_PUSH26:
      case OP_PUSH27:
      case OP_PUSH28:
      case OP_PUSH29:
      case OP_PUSH30:
      case OP_PUSH31:
      case OP_PUSH32: {
        PopCount = 0;
        PushCount = 1;
        uint8_t PushBytes = Opcode - OP_PUSH0;
        Ip += PushBytes;
        break;
      }
      case OP_DUP1:
      case OP_DUP2:
      case OP_DUP3:
      case OP_DUP4:
      case OP_DUP5:
      case OP_DUP6:
      case OP_DUP7:
      case OP_DUP8:
      case OP_DUP9:
      case OP_DUP10:
      case OP_DUP11:
      case OP_DUP12:
      case OP_DUP13:
      case OP_DUP14:
      case OP_DUP15:
      case OP_DUP16: {
        uint8_t BaseN = Opcode - OP_DUP1;
        PopCount = BaseN + 1;
        PushCount = BaseN + 2;
        break;
      }
      case OP_SWAP1:
      case OP_SWAP2:
      case OP_SWAP3:
      case OP_SWAP4:
      case OP_SWAP5:
      case OP_SWAP6:
      case OP_SWAP7:
      case OP_SWAP8:
      case OP_SWAP9:
      case OP_SWAP10:
      case OP_SWAP11:
      case OP_SWAP12:
      case OP_SWAP13:
      case OP_SWAP14:
      case OP_SWAP15:
      case OP_SWAP16: {
        uint8_t BaseN = Opcode - OP_SWAP1;
        PopCount = BaseN + 2;
        PushCount = BaseN + 2;
        break;
      }
      case OP_LOG0:
      case OP_LOG1:
      case OP_LOG2:
      case OP_LOG3:
      case OP_LOG4: {
        uint8_t BaseN = Opcode - OP_LOG0;
        PopCount = BaseN + 2;
        break;
      }
      case OP_CREATE:
        PopCount = 3;
        PushCount = 1;
        break;
      case OP_CREATE2:
        PopCount = 4;
        PushCount = 1;
        break;
      case OP_CALL:
      case OP_CALLCODE:
        PopCount = 7;
        PushCount = 1;
        break;
      case OP_DELEGATECALL:
      case OP_STATICCALL:
        PopCount = 6;
        PushCount = 1;
        break;
      case OP_CALLDATACOPY:
      case OP_CODECOPY:
      case OP_RETURNDATACOPY:
        PopCount = 3;
        break;
      case OP_EXTCODECOPY:
        PopCount = 4;
        break;
      case OP_JUMPDEST:
        break;
      default:
        // For unhandled opcodes, treat as invalid
        Opcode = OP_INVALID;
        break;
      }

      // Update stack height
      CurInfo.StackHeightDiff -= PopCount;
      if (CurInfo.StackHeightDiff < CurInfo.MinStackHeight) {
        CurInfo.MinStackHeight = CurInfo.StackHeightDiff;
      }
      if (!(Opcode >= OP_SWAP1 && Opcode <= OP_SWAP16) &&
          !(Opcode >= OP_DUP1 && Opcode <= OP_DUP16)) {
        CurInfo.MinPopHeight =
            std::min(CurInfo.StackHeightDiff, CurInfo.MinPopHeight);
      }
      CurInfo.StackHeightDiff += PushCount;
      if (CurInfo.StackHeightDiff > CurInfo.MaxStackHeight) {
        CurInfo.MaxStackHeight = CurInfo.StackHeightDiff;
      }

      // Check if this is a block starting opcode
      bool IsBlockStart = (Opcode == OP_JUMPDEST || Opcode == OP_JUMPI);
      // Check if this is a block ending opcode
      bool IsBlockEnd = (Opcode == OP_JUMP || Opcode == OP_RETURN ||
                         Opcode == OP_STOP || Opcode == OP_INVALID ||
                         Opcode == OP_REVERT || Opcode == OP_SELFDESTRUCT);

      if (IsBlockStart) {
        if (PC != CurInfo.EntryPC) {
          CurInfo.EndPC = PC;
          // Check if current block should be split before saving
          updateSplitFunctionsIfNeeded(SplitState, CurInfo,
                                       CurrentBlockOpcodeCount);
          BlockInfos.emplace(CurInfo.EntryPC, CurInfo);
        }
        // Create new block info and reset opcode count
        CurInfo = BlockInfo(PC);
        CurrentBlockOpcodeCount = 0; // Reset opcode count for new block
      } else if (IsBlockEnd) {
        CurInfo.EndPC = PC;
        // Check if current block should be split before saving
        updateSplitFunctionsIfNeeded(SplitState, CurInfo,
                                     CurrentBlockOpcodeCount);
        // Save current block info
        BlockInfos.emplace(CurInfo.EntryPC, CurInfo);
        // Reset opcode count for next block
        CurrentBlockOpcodeCount = 0;
        // Skip dead code
        while (Ip < IpEnd) {
          evmc_opcode NextOp = static_cast<evmc_opcode>(*Ip);
          if (NextOp == OP_JUMPDEST) {
            break;
          }
          Ip++;
          if (NextOp >= OP_PUSH0 && NextOp <= OP_PUSH32) {
            uint8_t NumBytes =
                static_cast<uint8_t>(NextOp) - static_cast<uint8_t>(OP_PUSH0);
            Ip += NumBytes;
          }
        }
      } else {
        updateSplitPointsIfNeeded(SplitState, PC, CurrentBlockOpcodeCount,
                                  CurInfo.StackHeightDiff);
      }
    }
    if (BlockInfos.count(CurInfo.EntryPC) == 0) {
      CurInfo.EndPC = Ip - Bytecode;
      // Check if final block should be split before saving
      updateSplitFunctionsIfNeeded(SplitState, CurInfo,
                                   CurrentBlockOpcodeCount);
      BlockInfos.emplace(CurInfo.EntryPC, CurInfo);
    }

    // Print split functions information
    if (!SplitFunctions.empty()) {
      printf("[EVMAnalyzer] Split Functions Information:\n");
      printf("  Total split functions: %u\n",
             static_cast<uint32_t>(SplitFunctions.size()));
      for (const auto &entry : SplitFunctions) {
        const SplitInfo &info = entry.second;
        printf("  Function %u: PC [%lu, %lu), \n", info.FunctionIndex,
               info.StartPC, info.EndPC);
      }
    } else {
      printf("[EVMAnalyzer] No split functions generated\n");
    }

    return true;
  }

  bool updateSplitPointsIfNeeded(SplitState &State, uint64_t PC,
                                 uint32_t OpcodeCount, uint64_t StackDiff) {
    if (OpcodeCount - State.LastOpcodeCount >= DEFAULT_BLOCK_SIZE_THRESHOLD) {
      State.SplitPoints.push_back(PC);
      State.LastOpcodeCount = OpcodeCount;
      State.LastStackDiff = StackDiff;
      return true;
    }
    // if (!State.SplitPoints.empty() &&
    //     OpcodeCount - State.LastOpcodeCount <= SPLIT_SEARCH_WINDOW &&
    //     StackDiff < State.LastStackDiff) {
    //   State.SplitPoints.back() = PC;
    //   State.LastOpcodeCount = OpcodeCount;
    //   State.LastStackDiff = StackDiff;
    //   return true;
    // }
    return false;
  }

  bool updateSplitFunctionsIfNeeded(SplitState &State, const BlockInfo &CurInfo,
                                    uint32_t OpcodeCount) {
    // Update split functions based on BlockInfo
    auto &SplitPoints = State.SplitPoints;
    if (!SplitPoints.empty() && OpcodeCount - State.LastOpcodeCount <
                                    DEFAULT_BLOCK_SIZE_THRESHOLD / 2) {
      SplitPoints.pop_back();
    }
    if (SplitPoints.empty()) {
      return false;
    }
    SplitPoints.push_back(CurInfo.EndPC);

    for (size_t I = 0; I + 1 < SplitPoints.size(); I++) {
      SplitInfo Split(SplitPoints[I], SplitPoints[I + 1], NextFunctionIndex++);
      SplitFunctions.emplace(Split.StartPC, Split);
    }
    SplitPoints.clear();
    State.LastOpcodeCount = 0;
    State.LastStackDiff = 0;
    return true;
  }

private:
  std::map<uint64_t, BlockInfo> BlockInfos;
  std::map<uint64_t, SplitInfo> SplitFunctions; // Split function metadata
  uint64_t PC = 0;
  // Next available function index (0 is main function)
  uint32_t NextFunctionIndex = 1;
};

} // namespace COMPILER

#endif // EVM_FRONTEND_EVM_ANALYZER_H
