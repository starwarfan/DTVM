// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef EVM_FRONTEND_EVM_ANALYZER_H
#define EVM_FRONTEND_EVM_ANALYZER_H

#include "compiler/common/common_defs.h"
#include "evmc/evmc.h"
#include "evmc/instructions.h"
#include <algorithm>
#include <climits>
#include <cstdlib>

namespace COMPILER {

class EVMAnalyzer {
  using Byte = zen::common::Byte;
  using Bytes = zen::common::Bytes;

public:
  EVMAnalyzer() {}

  // Configuration constants for block splitting
  static constexpr uint32_t DEFAULT_BLOCK_SIZE_THRESHOLD = 1000;
  static constexpr uint32_t SPLIT_SEARCH_WINDOW = 50;

  struct BlockInfo {
    uint64_t EntryPC = 0;
    int32_t MaxStackHeight = 0;
    int32_t MinStackHeight = 0;
    int32_t MinPopHeight = 0;
    int32_t StackHeightDiff = 0;
    bool IsJumpDest = false;

    BlockInfo() = default;
    BlockInfo(uint64_t PC) : EntryPC(PC) {}
  };

  // Structure to store split function metadata
  struct SplitInfo {
    uint64_t StartPC = 0;
    uint64_t EndPC = 0;
    uint32_t FunctionIndex = 0;
    int32_t StackHeightAtStart = 0;
    int32_t StackHeightAtEnd = 0;

    SplitInfo() = default;
    SplitInfo(uint64_t start, uint64_t end, uint32_t funcIdx,
              int32_t startHeight, int32_t endHeight)
        : StartPC(start), EndPC(end), FunctionIndex(funcIdx),
          StackHeightAtStart(startHeight), StackHeightAtEnd(endHeight) {}
  };

  const std::map<uint64_t, BlockInfo> &getBlockInfos() const {
    return BlockInfos;
  }

  // Get the total opcode count from the last analysis
  uint32_t getOpcodeCount() const { return OpcodeCount; }

  // Check if the analyzed block should be split based on opcode count
  bool shouldSplitBlock() const { return shouldSplitBlock(OpcodeCount); }

  // Check if a given opcode count exceeds the threshold
  bool shouldSplitBlock(uint32_t opcodeCount) const {
    return opcodeCount > DEFAULT_BLOCK_SIZE_THRESHOLD;
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
    OpcodeCount = 0;            // Reset opcode counter
    OpcodeStackHeights.clear(); // Reset stack height tracking
    const uint8_t *Ip = Bytecode;
    const uint8_t *IpEnd = Bytecode + BytecodeSize;

    // Initialize block info for the first block
    BlockInfo CurInfo(0);

    while (Ip < IpEnd) {
      evmc_opcode Opcode = static_cast<evmc_opcode>(*Ip);
      ptrdiff_t Diff = Ip - Bytecode;
      PC = static_cast<uint64_t>(Diff >= 0 ? Diff : 0);

      Ip++;

      // Count this opcode (excluding PUSH instruction data bytes)
      OpcodeCount++;

      // Record stack height at this PC for split analysis
      OpcodeStackHeights.emplace_back(PC, CurInfo.StackHeightDiff);

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
          BlockInfos.emplace(CurInfo.EntryPC, CurInfo);
        }
        // Create new block info
        CurInfo = BlockInfo(PC);
        if (Opcode == OP_JUMPDEST) {
          CurInfo.IsJumpDest = true;
        }
      } else if (IsBlockEnd) {
        // Save current block info
        BlockInfos.emplace(CurInfo.EntryPC, CurInfo);
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
      }
    }
    if (BlockInfos.count(CurInfo.EntryPC) == 0) {
      BlockInfos.emplace(CurInfo.EntryPC, CurInfo);
    }

    // After analysis, if splitting is needed, find optimal split points
    if (shouldSplitBlock()) {
      findOptimalSplitPoints(Bytecode, BytecodeSize);
    }

    return true;
  }

  // Find optimal split points for large blocks
  std::vector<uint64_t> findOptimalSplitPoints(const uint8_t *Bytecode,
                                               size_t BytecodeSize) {
    std::vector<uint64_t> splitPoints;
    SplitFunctions.clear();
    NextFunctionIndex = 1;

    if (!shouldSplitBlock()) {
      return splitPoints;
    }

    // Calculate target split intervals
    uint32_t numSplits = (OpcodeCount + DEFAULT_BLOCK_SIZE_THRESHOLD - 1) /
                         DEFAULT_BLOCK_SIZE_THRESHOLD;
    if (numSplits <= 1) {
      return splitPoints;
    }

    // Find split points at regular intervals
    for (uint32_t i = 1; i < numSplits; i++) {
      uint64_t targetPC = (i * OpcodeCount) / numSplits;
      uint64_t optimalPC = findBestSplitPoint(targetPC, Bytecode, BytecodeSize);
      if (optimalPC != UINT64_MAX) {
        splitPoints.push_back(optimalPC);
      }
    }

    // Create split function metadata
    uint64_t lastPC = 0;
    for (size_t i = 0; i < splitPoints.size(); i++) {
      uint64_t startPC = (i == 0) ? 0 : splitPoints[i - 1];
      uint64_t endPC = splitPoints[i];
      int32_t startHeight = getStackHeightAtPC(startPC);
      int32_t endHeight = getStackHeightAtPC(endPC);

      SplitFunctions.emplace(startPC,
                             SplitInfo(startPC, endPC, NextFunctionIndex++,
                                       startHeight, endHeight));
      lastPC = endPC;
    }

    // Add the final function
    if (!splitPoints.empty()) {
      int32_t startHeight = getStackHeightAtPC(lastPC);
      int32_t endHeight = getStackHeightAtPC(BytecodeSize);
      SplitFunctions.emplace(lastPC, SplitInfo(lastPC, BytecodeSize,
                                               NextFunctionIndex++, startHeight,
                                               endHeight));
    }

    // Validate split boundaries for correctness
    if (!validateSplitBoundaries(BytecodeSize)) {
      // If validation fails, clear split functions and return empty split
      // points
      SplitFunctions.clear();
      NextFunctionIndex = 1;
      return std::vector<uint64_t>();
    }

    return splitPoints;
  }

  // Find the best split point within a search window around target PC
  uint64_t findBestSplitPoint(uint64_t targetPC, const uint8_t *Bytecode,
                              size_t BytecodeSize) {
    uint64_t bestPC = UINT64_MAX;
    int32_t bestStackHeightDiff = INT32_MAX;

    // Define search window
    uint64_t windowStart =
        (targetPC > SPLIT_SEARCH_WINDOW) ? (targetPC - SPLIT_SEARCH_WINDOW) : 0;
    uint64_t windowEnd = std::min(targetPC + SPLIT_SEARCH_WINDOW,
                                  static_cast<uint64_t>(BytecodeSize));

    // Search for the best split point within the window
    for (uint64_t pc = windowStart; pc < windowEnd; pc++) {
      if (isValidSplitPoint(pc, Bytecode, BytecodeSize)) {
        int32_t stackHeight = getStackHeightAtPC(pc);
        int32_t stackHeightDiff = std::abs(stackHeight);

        if (stackHeightDiff < bestStackHeightDiff) {
          bestStackHeightDiff = stackHeightDiff;
          bestPC = pc;

          // If we find a perfect split point (stack height 0), use it
          if (stackHeightDiff == 0) {
            break;
          }
        }
      }
    }

    return bestPC;
  }

private:
  std::map<uint64_t, BlockInfo> BlockInfos;
  std::map<uint64_t, SplitInfo> SplitFunctions; // Split function metadata
  std::vector<std::pair<uint64_t, int32_t>>
      OpcodeStackHeights; // PC -> StackHeight mapping for split analysis
  uint64_t PC = 0;
  uint32_t OpcodeCount = 0; // Total opcode count for splitting analysis
  uint32_t NextFunctionIndex =
      1; // Next available function index (0 is main function)

  // Helper methods for split point analysis
  bool isValidSplitPoint(uint64_t pc, const uint8_t *Bytecode,
                         size_t BytecodeSize) const {
    if (pc >= BytecodeSize) {
      return false;
    }

    // Check if this PC is at the start of an instruction (not in the middle of
    // PUSH data)
    const uint8_t *Ip = Bytecode;
    const uint8_t *IpEnd = Bytecode + BytecodeSize;
    uint64_t currentPC = 0;

    while (Ip < IpEnd && currentPC <= pc) {
      if (currentPC == pc) {
        // This PC is at the start of an instruction
        evmc_opcode opcode = static_cast<evmc_opcode>(*Ip);

        // Don't split in the middle of PUSH instructions or at invalid opcodes
        if (opcode >= OP_PUSH0 && opcode <= OP_PUSH32) {
          return false;
        }
        if (opcode == OP_INVALID) {
          return false;
        }

        // Don't split at jump destinations (they should remain as block entry
        // points)
        if (opcode == OP_JUMPDEST) {
          return false;
        }

        return true;
      }

      // Advance to next instruction
      evmc_opcode opcode = static_cast<evmc_opcode>(*Ip);
      Ip++;
      currentPC++;

      if (opcode >= OP_PUSH0 && opcode <= OP_PUSH32) {
        uint8_t pushBytes = opcode - OP_PUSH0;
        Ip += pushBytes;
        currentPC += pushBytes;
      }
    }

    return false;
  }

  int32_t getStackHeightAtPC(uint64_t pc) const {
    // Find the stack height at the given PC from our recorded data
    for (const auto &entry : OpcodeStackHeights) {
      if (entry.first == pc) {
        return entry.second;
      }
    }

    // If not found, return 0 (this shouldn't happen in normal operation)
    return 0;
  }
};

} // namespace COMPILER

#endif // EVM_FRONTEND_EVM_ANALYZER_H
