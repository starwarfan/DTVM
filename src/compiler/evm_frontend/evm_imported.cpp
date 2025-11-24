// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "compiler/evm_frontend/evm_imported.h"
#include "common/errors.h"
#include "host/evm/crypto.h"
#include "runtime/evm_instance.h"
#include "runtime/evm_module.h"
#include <cstdint>
#include <evmc/evmc.h>
#include <vector>

namespace COMPILER {

namespace {
thread_local intx::uint256 Uint256ReturnBuffer;

const intx::uint256 *storeUint256Result(const intx::uint256 &Value) {
  Uint256ReturnBuffer = Value;
  return &Uint256ReturnBuffer;
}
inline uint64_t calculateWordCopyGas(uint64_t Size) {
  if (Size == 0) {
    return 0;
  }
  constexpr uint64_t WordBytes = 32;
  uint64_t Words = (Size + (WordBytes - 1)) / WordBytes;
  return Words * static_cast<uint64_t>(zen::evm::WORD_COPY_COST);
}
} // namespace

const RuntimeFunctions &getRuntimeFunctionTable() {
  static const RuntimeFunctions Table = {
      .GetMul = &evmGetMul,
      .GetDiv = &evmGetDiv,
      .GetSDiv = &evmGetSDiv,
      .GetMod = &evmGetMod,
      .GetSMod = &evmGetSMod,
      .GetAddMod = &evmGetAddMod,
      .GetMulMod = &evmGetMulMod,
      .GetExp = &evmGetExp,
      .GetAddress = &evmGetAddress,
      .GetBalance = &evmGetBalance,
      .GetOrigin = &evmGetOrigin,
      .GetCaller = &evmGetCaller,
      .GetCallValue = &evmGetCallValue,
      .GetCallDataLoad = &evmGetCallDataLoad,
      .GetCallDataSize = &evmGetCallDataSize,
      .GetCodeSize = &evmGetCodeSize,
      .SetCodeCopy = &evmSetCodeCopy,
      .GetGasPrice = &evmGetGasPrice,
      .GetExtCodeSize = &evmGetExtCodeSize,
      .GetExtCodeHash = &evmGetExtCodeHash,
      .GetBlockHash = &evmGetBlockHash,
      .GetCoinBase = &evmGetCoinBase,
      .GetTimestamp = &evmGetTimestamp,
      .GetNumber = &evmGetNumber,
      .GetPrevRandao = &evmGetPrevRandao,
      .GetGasLimit = &evmGetGasLimit,
      .GetChainId = &evmGetChainId,
      .GetSelfBalance = &evmGetSelfBalance,
      .GetBaseFee = &evmGetBaseFee,
      .GetBlobHash = &evmGetBlobHash,
      .GetBlobBaseFee = &evmGetBlobBaseFee,
      .GetMSize = &evmGetMSize,
      .GetMLoad = &evmGetMLoad,
      .SetMStore = &evmSetMStore,
      .SetMStore8 = &evmSetMStore8,
      .GetSLoad = &evmGetSLoad,
      .SetSStore = &evmSetSStore,
      .GetGas = &evmGetGas,
      .GetTLoad = &evmGetTLoad,
      .SetTStore = &evmSetTStore,
      .SetMCopy = &evmSetMCopy,
      .SetCallDataCopy = &evmSetCallDataCopy,
      .SetExtCodeCopy = &evmSetExtCodeCopy,
      .SetReturnDataCopy = &evmSetReturnDataCopy,
      .GetReturnDataSize = &evmGetReturnDataSize,
      .EmitLog = &evmEmitLog,
      .HandleCreate = &evmHandleCreate,
      .HandleCreate2 = &evmHandleCreate2,
      .HandleCall = &evmHandleCall,
      .HandleCallCode = &evmHandleCallCode,
      .SetReturn = &evmSetReturn,
      .HandleDelegateCall = &evmHandleDelegateCall,
      .HandleStaticCall = &evmHandleStaticCall,
      .SetRevert = &evmSetRevert,
      .HandleInvalid = &evmHandleInvalid,
      .HandleSelfDestruct = &evmHandleSelfDestruct,
      .GetKeccak256 = &evmGetKeccak256,
      .HandleDebug = &evmHandleDebug};
  return Table;
}

void printFunctionTable() {
  const RuntimeFunctions &Table = getRuntimeFunctionTable();

  printf("Runtime Function Table:\n");
  printf("======================\n");

  // Print each function name and its address
  printf("GetMul: %p\n", reinterpret_cast<void *>(Table.GetMul));
  printf("GetDiv: %p\n", reinterpret_cast<void *>(Table.GetDiv));
  printf("GetSDiv: %p\n", reinterpret_cast<void *>(Table.GetSDiv));
  printf("GetMod: %p\n", reinterpret_cast<void *>(Table.GetMod));
  printf("GetSMod: %p\n", reinterpret_cast<void *>(Table.GetSMod));
  printf("GetAddMod: %p\n", reinterpret_cast<void *>(Table.GetAddMod));
  printf("GetMulMod: %p\n", reinterpret_cast<void *>(Table.GetMulMod));
  printf("GetExp: %p\n", reinterpret_cast<void *>(Table.GetExp));
  printf("GetAddress: %p\n", reinterpret_cast<void *>(Table.GetAddress));
  printf("GetBalance: %p\n", reinterpret_cast<void *>(Table.GetBalance));
  printf("GetOrigin: %p\n", reinterpret_cast<void *>(Table.GetOrigin));
  printf("GetCaller: %p\n", reinterpret_cast<void *>(Table.GetCaller));
  printf("GetCallValue: %p\n", reinterpret_cast<void *>(Table.GetCallValue));
  printf("GetCallDataLoad: %p\n",
         reinterpret_cast<void *>(Table.GetCallDataLoad));
  printf("GetCallDataSize: %p\n",
         reinterpret_cast<void *>(Table.GetCallDataSize));
  printf("GetCodeSize: %p\n", reinterpret_cast<void *>(Table.GetCodeSize));
  printf("SetCodeCopy: %p\n", reinterpret_cast<void *>(Table.SetCodeCopy));
  printf("GetGasPrice: %p\n", reinterpret_cast<void *>(Table.GetGasPrice));
  printf("GetExtCodeSize: %p\n",
         reinterpret_cast<void *>(Table.GetExtCodeSize));
  printf("GetExtCodeHash: %p\n",
         reinterpret_cast<void *>(Table.GetExtCodeHash));
  printf("GetBlockHash: %p\n", reinterpret_cast<void *>(Table.GetBlockHash));
  printf("GetCoinBase: %p\n", reinterpret_cast<void *>(Table.GetCoinBase));
  printf("GetTimestamp: %p\n", reinterpret_cast<void *>(Table.GetTimestamp));
  printf("GetNumber: %p\n", reinterpret_cast<void *>(Table.GetNumber));
  printf("GetPrevRandao: %p\n", reinterpret_cast<void *>(Table.GetPrevRandao));
  printf("GetGasLimit: %p\n", reinterpret_cast<void *>(Table.GetGasLimit));
  printf("GetChainId: %p\n", reinterpret_cast<void *>(Table.GetChainId));
  printf("GetSelfBalance: %p\n",
         reinterpret_cast<void *>(Table.GetSelfBalance));
  printf("GetBaseFee: %p\n", reinterpret_cast<void *>(Table.GetBaseFee));
  printf("GetBlobHash: %p\n", reinterpret_cast<void *>(Table.GetBlobHash));
  printf("GetBlobBaseFee: %p\n",
         reinterpret_cast<void *>(Table.GetBlobBaseFee));
  printf("GetMSize: %p\n", reinterpret_cast<void *>(Table.GetMSize));
  printf("GetMLoad: %p\n", reinterpret_cast<void *>(Table.GetMLoad));
  printf("SetMStore: %p\n", reinterpret_cast<void *>(Table.SetMStore));
  printf("SetMStore8: %p\n", reinterpret_cast<void *>(Table.SetMStore8));
  printf("GetSLoad: %p\n", reinterpret_cast<void *>(Table.GetSLoad));
  printf("SetSStore: %p\n", reinterpret_cast<void *>(Table.SetSStore));
  printf("GetGas: %p\n", reinterpret_cast<void *>(Table.GetGas));
  printf("GetTLoad: %p\n", reinterpret_cast<void *>(Table.GetTLoad));
  printf("SetTStore: %p\n", reinterpret_cast<void *>(Table.SetTStore));
  printf("SetMCopy: %p\n", reinterpret_cast<void *>(Table.SetMCopy));
  printf("SetCallDataCopy: %p\n",
         reinterpret_cast<void *>(Table.SetCallDataCopy));
  printf("SetExtCodeCopy: %p\n",
         reinterpret_cast<void *>(Table.SetExtCodeCopy));
  printf("SetReturnDataCopy: %p\n",
         reinterpret_cast<void *>(Table.SetReturnDataCopy));
  printf("GetReturnDataSize: %p\n",
         reinterpret_cast<void *>(Table.GetReturnDataSize));
  printf("EmitLog: %p\n", reinterpret_cast<void *>(Table.EmitLog));
  printf("HandleCreate: %p\n", reinterpret_cast<void *>(Table.HandleCreate));
  printf("HandleCreate2: %p\n", reinterpret_cast<void *>(Table.HandleCreate2));
  printf("HandleCall: %p\n", reinterpret_cast<void *>(Table.HandleCall));
  printf("HandleCallCode: %p\n",
         reinterpret_cast<void *>(Table.HandleCallCode));
  printf("SetReturn: %p\n", reinterpret_cast<void *>(Table.SetReturn));
  printf("HandleDelegateCall: %p\n",
         reinterpret_cast<void *>(Table.HandleDelegateCall));
  printf("HandleStaticCall: %p\n",
         reinterpret_cast<void *>(Table.HandleStaticCall));
  printf("SetRevert: %p\n", reinterpret_cast<void *>(Table.SetRevert));
  printf("HandleInvalid: %p\n", reinterpret_cast<void *>(Table.HandleInvalid));
  printf("HandleSelfDestruct: %p\n",
         reinterpret_cast<void *>(Table.HandleSelfDestruct));
  printf("GetKeccak256: %p\n", reinterpret_cast<void *>(Table.GetKeccak256));
  printf("HandleDebug: %p\n", reinterpret_cast<void *>(Table.HandleDebug));
}

const intx::uint256 *evmGetMul(zen::runtime::EVMInstance *Instance,
                               const intx::uint256 &Multiplicand,
                               const intx::uint256 &Multiplier) {
  // EVM: Multiplicand * Multiplier % (2^256)
  return storeUint256Result(Multiplicand * Multiplier);
}

const intx::uint256 *evmGetDiv(zen::runtime::EVMInstance *Instance,
                               const intx::uint256 &Dividend,
                               const intx::uint256 &Divisor) {
  if (Divisor == 0) {
    return storeUint256Result(intx::uint256{0});
  }
  return storeUint256Result(Dividend / Divisor);
}

const intx::uint256 *evmGetSDiv(zen::runtime::EVMInstance *Instance,
                                const intx::uint256 &Dividend,
                                const intx::uint256 &Divisor) {
  if (Divisor == 0) {
    return storeUint256Result(intx::uint256{0});
  }

  // Check if dividend is negative (MSB set)
  bool isDividendNegative = (Dividend >> 255) != 0;
  bool isDivisorNegative = (Divisor >> 255) != 0;

  // Convert to absolute values
  intx::uint256 absDividend = isDividendNegative ? (~Dividend + 1) : Dividend;
  intx::uint256 absDivisor = isDivisorNegative ? (~Divisor + 1) : Divisor;

  // Perform unsigned division
  intx::uint256 absResult = absDividend / absDivisor;

  // Apply sign: result is negative if signs differ
  bool isResultNegative = isDividendNegative != isDivisorNegative;

  return storeUint256Result(isResultNegative ? (~absResult + 1) : absResult);
}

const intx::uint256 *evmGetMod(zen::runtime::EVMInstance *Instance,
                               const intx::uint256 &Dividend,
                               const intx::uint256 &Divisor) {
  if (Divisor == 0) {
    return storeUint256Result(intx::uint256{0});
  }
  return storeUint256Result(Dividend % Divisor);
}

const intx::uint256 *evmGetSMod(zen::runtime::EVMInstance *Instance,
                                const intx::uint256 &Dividend,
                                const intx::uint256 &Divisor) {
  if (Divisor == 0) {
    return storeUint256Result(intx::uint256{0});
  }

  // Check if dividend is negative (MSB set)
  bool isDividendNegative = (Dividend >> 255) != 0;

  // Convert to absolute values
  intx::uint256 absDividend = isDividendNegative ? (~Dividend + 1) : Dividend;
  intx::uint256 absDivisor = Divisor; // Divisor sign doesn't affect modulo

  // Perform unsigned modulo
  intx::uint256 absResult = absDividend % absDivisor;

  // Apply sign: result has same sign as dividend
  return storeUint256Result(isDividendNegative ? (~absResult + 1) : absResult);
}

const intx::uint256 *evmGetAddMod(zen::runtime::EVMInstance *Instance,
                                  const intx::uint256 &Augend,
                                  const intx::uint256 &Addend,
                                  const intx::uint256 &Modulus) {
  // Handle edge case: modulo 0
  if (Modulus == 0) {
    return storeUint256Result(intx::uint256{0});
  }

  // (Augend + Addend) % Modulus
  // Use 512-bit intermediate to prevent overflow
  intx::uint512 Sum = intx::uint512(Augend) + intx::uint512(Addend);
  intx::uint256 Result = intx::uint256(Sum % Modulus);
  return storeUint256Result(Result);
}

const intx::uint256 *evmGetMulMod(zen::runtime::EVMInstance *Instance,
                                  const intx::uint256 &Multiplicand,
                                  const intx::uint256 &Multiplier,
                                  const intx::uint256 &Modulus) {
  // Handle edge case: modulo 0
  if (Modulus == 0) {
    return storeUint256Result(intx::uint256{0});
  }

  // (Multiplicand * Multiplier) % Modulus
  // Use 512-bit intermediate to prevent overflow
  intx::uint512 Product =
      intx::uint512(Multiplicand) * intx::uint512(Multiplier);
  intx::uint256 Result = intx::uint256(Product % Modulus);
  return storeUint256Result(Result);
}

const intx::uint256 *evmGetExp(zen::runtime::EVMInstance *Instance,
                               const intx::uint256 &Base,
                               const intx::uint256 &Exponent) {
  // Handle edge cases
  if (Exponent == 0) {
    return storeUint256Result(intx::uint256{1});
  }
  if (Base == 0) {
    return storeUint256Result(intx::uint256{0});
  }
  if (Exponent == 1) {
    return storeUint256Result(Base);
  }

  // EVM: (Base ^ Exponent) % (2^256)
  intx::uint256 Result = 1;
  intx::uint256 CurrentBase = Base;
  intx::uint256 ExponentCopy = Exponent;

  while (ExponentCopy > 0) {
    if (ExponentCopy & 1) {
      Result *= CurrentBase;
    }
    CurrentBase *= CurrentBase;
    ExponentCopy >>= 1;
  }

  return storeUint256Result(Result);
}

const uint8_t *evmGetAddress(zen::runtime::EVMInstance *Instance) {
  const evmc_message *Msg = Instance->getCurrentMessage();
  ZEN_ASSERT(Msg && "No current message set in EVMInstance");
  return Msg->recipient.bytes;
}

const intx::uint256 *evmGetBalance(zen::runtime::EVMInstance *Instance,
                                   const uint8_t *Address) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);

  evmc::address Addr;
  std::memcpy(Addr.bytes, Address, sizeof(Addr.bytes));

  evmc::bytes32 BalanceBytes = Module->Host->get_balance(Addr);
  intx::uint256 Balance = intx::be::load<intx::uint256>(BalanceBytes);
  return storeUint256Result(Balance);
}

const uint8_t *evmGetOrigin(zen::runtime::EVMInstance *Instance) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);

  auto &Cache = Instance->getMessageCache();
  if (!Cache.TxContextCached) {
    Cache.TxContext = Module->Host->get_tx_context();
    Cache.TxContextCached = true;
  }
  return Cache.TxContext.tx_origin.bytes;
}

const uint8_t *evmGetCaller(zen::runtime::EVMInstance *Instance) {
  const evmc_message *Msg = Instance->getCurrentMessage();
  ZEN_ASSERT(Msg && "No current message set in EVMInstance");
  return Msg->sender.bytes;
}

const uint8_t *evmGetCallValue(zen::runtime::EVMInstance *Instance) {
  const evmc_message *Msg = Instance->getCurrentMessage();
  ZEN_ASSERT(Msg && "No current message set in EVMInstance");
  return Msg->value.bytes;
}

const uint8_t *evmGetCallDataLoad(zen::runtime::EVMInstance *Instance,
                                  uint64_t Offset) {
  const evmc_message *Msg = Instance->getCurrentMessage();
  ZEN_ASSERT(Msg && "No current message set in EVMInstance");

  auto &Cache = Instance->getMessageCache();
  auto Key = std::make_pair(Msg, Offset);
  auto It = Cache.CalldataLoads.find(Key);
  if (It == Cache.CalldataLoads.end()) {
    evmc::bytes32 Result{};
    if (Offset < Msg->input_size) {
      size_t CopySize = std::min<size_t>(32, Msg->input_size - Offset);
      std::memcpy(Result.bytes, Msg->input_data + Offset, CopySize);
    }
    Cache.CalldataLoads[Key] = Result;
    return Cache.CalldataLoads[Key].bytes;
  }
  return It->second.bytes;
}

const intx::uint256 *evmGetGasPrice(zen::runtime::EVMInstance *Instance) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);
  evmc_tx_context TxContext = Module->Host->get_tx_context();
  return storeUint256Result(
      intx::be::load<intx::uint256>(TxContext.tx_gas_price));
}

uint64_t evmGetExtCodeSize(zen::runtime::EVMInstance *Instance,
                           const uint8_t *Address) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);

  evmc::address Addr;
  std::memcpy(Addr.bytes, Address, sizeof(Addr.bytes));

  uint64_t Size = Module->Host->get_code_size(Addr);
  return Size;
}

const uint8_t *evmGetExtCodeHash(zen::runtime::EVMInstance *Instance,
                                 const uint8_t *Address) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);

  evmc::address Addr;
  std::memcpy(Addr.bytes, Address, sizeof(Addr.bytes));

  auto &Cache = Instance->getMessageCache();
  evmc::bytes32 Hash = Module->Host->get_code_hash(Addr);
  Cache.ExtcodeHashes.push_back(Hash);

  return Cache.ExtcodeHashes.back().bytes;
}

uint64_t evmGetCallDataSize(zen::runtime::EVMInstance *Instance) {
  const evmc_message *Msg = Instance->getCurrentMessage();
  ZEN_ASSERT(Msg && "No current message set in EVMInstance");
  return Msg->input_size;
}

uint64_t evmGetCodeSize(zen::runtime::EVMInstance *Instance) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module);
  return Module->CodeSize;
}

const uint8_t *evmGetBlockHash(zen::runtime::EVMInstance *Instance,
                               int64_t BlockNumber) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);

  evmc_tx_context TxContext = Module->Host->get_tx_context();
  const auto UpperBound = TxContext.block_number;
  const auto LowerBound = std::max(UpperBound - 256, decltype(UpperBound){0});

  auto &Cache = Instance->getMessageCache();
  auto It = Cache.BlockHashes.find(BlockNumber);
  if (It == Cache.BlockHashes.end()) {
    evmc::bytes32 Hash = (BlockNumber < UpperBound && BlockNumber >= LowerBound)
                             ? Module->Host->get_block_hash(BlockNumber)
                             : evmc::bytes32{};
    Cache.BlockHashes[BlockNumber] = Hash;
    return Cache.BlockHashes[BlockNumber].bytes;
  }
  return It->second.bytes;
}

const uint8_t *evmGetCoinBase(zen::runtime::EVMInstance *Instance) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);

  auto &Cache = Instance->getMessageCache();
  if (!Cache.TxContextCached) {
    Cache.TxContext = Module->Host->get_tx_context();
    Cache.TxContextCached = true;
  }
  return Cache.TxContext.block_coinbase.bytes;
}

const intx::uint256 *evmGetTimestamp(zen::runtime::EVMInstance *Instance) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);
  evmc_tx_context TxContext = Module->Host->get_tx_context();
  return storeUint256Result(intx::uint256(TxContext.block_timestamp));
}

const intx::uint256 *evmGetNumber(zen::runtime::EVMInstance *Instance) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);
  evmc_tx_context TxContext = Module->Host->get_tx_context();
  return storeUint256Result(intx::uint256(TxContext.block_number));
}

const uint8_t *evmGetPrevRandao(zen::runtime::EVMInstance *Instance) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);

  auto &Cache = Instance->getMessageCache();
  if (!Cache.TxContextCached) {
    Cache.TxContext = Module->Host->get_tx_context();
    Cache.TxContextCached = true;
  }
  return Cache.TxContext.block_prev_randao.bytes;
}

const intx::uint256 *evmGetGasLimit(zen::runtime::EVMInstance *Instance) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);
  evmc_tx_context TxContext = Module->Host->get_tx_context();
  return storeUint256Result(intx::uint256(TxContext.block_gas_limit));
}

const uint8_t *evmGetChainId(zen::runtime::EVMInstance *Instance) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);

  auto &Cache = Instance->getMessageCache();
  if (!Cache.TxContextCached) {
    Cache.TxContext = Module->Host->get_tx_context();
    Cache.TxContextCached = true;
  }
  return Cache.TxContext.chain_id.bytes;
}

const intx::uint256 *evmGetSelfBalance(zen::runtime::EVMInstance *Instance) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);
  const evmc_message *Msg = Instance->getCurrentMessage();
  ZEN_ASSERT(Msg && "No current message set in EVMInstance");
  evmc::bytes32 Balance = Module->Host->get_balance(Msg->recipient);
  return storeUint256Result(intx::be::load<intx::uint256>(Balance));
}

const intx::uint256 *evmGetBaseFee(zen::runtime::EVMInstance *Instance) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);
  evmc_tx_context TxContext = Module->Host->get_tx_context();
  return storeUint256Result(
      intx::be::load<intx::uint256>(TxContext.block_base_fee));
}

const uint8_t *evmGetBlobHash(zen::runtime::EVMInstance *Instance,
                              uint64_t Index) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);
  evmc_tx_context TxContext = Module->Host->get_tx_context();

  auto &Cache = Instance->getMessageCache();
  auto It = Cache.BlobHashes.find(Index);
  if (It == Cache.BlobHashes.end()) {
    evmc::bytes32 Hash;
    if (Index >= TxContext.blob_hashes_count) {
      Hash = evmc::bytes32{};
    } else {
      // TODO: havn't implemented in evmc
      // Hash = Module->Host->get_blob_hash(Index);
    }
    Cache.BlobHashes[Index] = Hash;
    return Cache.BlobHashes[Index].bytes;
  }
  return It->second.bytes;
}

const intx::uint256 *evmGetBlobBaseFee(zen::runtime::EVMInstance *Instance) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);
  evmc_tx_context TxContext = Module->Host->get_tx_context();
  return storeUint256Result(
      intx::be::load<intx::uint256>(TxContext.blob_base_fee));
}

uint64_t evmGetMSize(zen::runtime::EVMInstance *Instance) {
  return Instance->getMemorySize();
}
const intx::uint256 *evmGetMLoad(zen::runtime::EVMInstance *Instance,
                                 uint64_t Offset) {
  uint64_t RequiredSize = Offset + 32;
  Instance->expandMemory(RequiredSize);
  auto &Memory = Instance->getMemory();

  uint8_t ValueBytes[32];
  std::memcpy(ValueBytes, Memory.data() + Offset, 32);

  intx::uint256 Result = intx::be::load<intx::uint256>(ValueBytes);
  return storeUint256Result(Result);
}
void evmSetMStore(zen::runtime::EVMInstance *Instance, uint64_t Offset,
                  const intx::uint256 &Value) {
  uint64_t RequiredSize = Offset + 32;
  Instance->expandMemory(RequiredSize);

  auto &Memory = Instance->getMemory();
  uint8_t ValueBytes[32];
  intx::be::store(ValueBytes, Value);
  std::memcpy(Memory.data() + Offset, ValueBytes, sizeof(ValueBytes));
}

void evmSetMStore8(zen::runtime::EVMInstance *Instance, uint64_t Offset,
                   const intx::uint256 &Value) {
  uint64_t RequiredSize = Offset + 1;

  Instance->expandMemory(RequiredSize);

  auto &Memory = Instance->getMemory();
  uint8_t ByteValue = static_cast<uint8_t>(Value & intx::uint256{0xFF});
  Memory[Offset] = ByteValue;
}

void evmSetMCopy(zen::runtime::EVMInstance *Instance, uint64_t Dest,
                 uint64_t Src, uint64_t Len) {
  if (Len == 0) {
    return;
  }
  if (uint64_t CopyGas = calculateWordCopyGas(Len)) {
    Instance->chargeGas(CopyGas);
  }
  uint64_t RequiredSize = std::max(Dest + Len, Src + Len);

  Instance->expandMemory(RequiredSize);

  auto &Memory = Instance->getMemory();
  std::memmove(&Memory[Dest], &Memory[Src], Len);
}
void evmSetReturn(zen::runtime::EVMInstance *Instance, uint64_t Offset,
                  uint64_t Len) {
  Instance->expandMemory(Offset + Len);
  auto &Memory = Instance->getMemory();
  std::vector<uint8_t> ReturnData(Memory.begin() + Offset,
                                  Memory.begin() + Offset + Len);
  Instance->setReturnData(ReturnData);

  evmc::Result ExeResult(EVMC_SUCCESS, 0,
                         Instance ? Instance->getGasRefund() : 0,
                         ReturnData.data(), ReturnData.size());
  Instance->setExeResult(std::move(ExeResult));
  // Immediately terminate the execution and return the success code (0)
  Instance->exit(0);
}
void evmSetCallDataCopy(zen::runtime::EVMInstance *Instance,
                        uint64_t DestOffset, uint64_t Offset, uint64_t Size) {
  uint64_t RequiredSize = DestOffset + Size;
  Instance->expandMemory(RequiredSize);
  if (uint64_t CopyGas = calculateWordCopyGas(Size)) {
    Instance->chargeGas(CopyGas);
  }

  const evmc_message *Msg = Instance->getCurrentMessage();
  ZEN_ASSERT(Msg && "No current message set in EVMInstance");

  auto &Memory = Instance->getMemory();

  // Calculate actual source offset and copy size
  uint64_t ActualOffset =
      std::min(Offset, static_cast<uint64_t>(Msg->input_size));
  uint64_t CopySize =
      (ActualOffset < Msg->input_size)
          ? std::min<uint64_t>(Size, static_cast<uint64_t>(Msg->input_size) -
                                         ActualOffset)
          : 0;

  if (CopySize > 0) {
    std::memcpy(Memory.data() + DestOffset, Msg->input_data + ActualOffset,
                CopySize);
  }

  // Fill remaining bytes with zeros if needed
  if (Size > CopySize) {
    std::memset(Memory.data() + DestOffset + CopySize, 0, Size - CopySize);
  }
}

void evmSetExtCodeCopy(zen::runtime::EVMInstance *Instance,
                       const uint8_t *Address, uint64_t DestOffset,
                       uint64_t Offset, uint64_t Size) {
  uint64_t RequiredSize = DestOffset + Size;
  Instance->expandMemory(RequiredSize);
  if (uint64_t CopyGas = calculateWordCopyGas(Size)) {
    Instance->chargeGas(CopyGas);
  }

  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);
  evmc::address Addr;
  std::memcpy(Addr.bytes, Address, sizeof(Addr.bytes));

  auto &Memory = Instance->getMemory();
  size_t CodeSize = Module->Host->get_code_size(Addr);

  if (Offset >= CodeSize) {
    // If offset is beyond code size, fill with zeros
    std::memset(Memory.data() + DestOffset, 0, Size);
  } else {
    uint64_t CopySize =
        std::min<uint64_t>(Size, static_cast<uint64_t>(CodeSize) - Offset);
    size_t CopiedSize = Module->Host->copy_code(
        Addr, Offset, Memory.data() + DestOffset, CopySize);

    // Fill remaining bytes with zeros if needed
    if (Size > CopiedSize) {
      std::memset(Memory.data() + DestOffset + CopiedSize, 0,
                  Size - CopiedSize);
    }
  }
}

void evmSetReturnDataCopy(zen::runtime::EVMInstance *Instance,
                          uint64_t DestOffset, uint64_t Offset, uint64_t Size) {
  uint64_t RequiredSize = DestOffset + Size;
  Instance->expandMemory(RequiredSize);
  if (uint64_t CopyGas = calculateWordCopyGas(Size)) {
    Instance->chargeGas(CopyGas);
  }

  const auto &ReturnData = Instance->getReturnData();
  auto &Memory = Instance->getMemory();

  if (Offset >= ReturnData.size()) {
    std::memset(Memory.data() + DestOffset, 0, Size);
  } else {
    uint64_t CopySize = std::min<uint64_t>(
        Size, static_cast<uint64_t>(ReturnData.size()) - Offset);
    std::memcpy(Memory.data() + DestOffset, ReturnData.data() + Offset,
                CopySize);

    // Fill remaining bytes with zeros
    if (Size > CopySize) {
      std::memset(Memory.data() + DestOffset + CopySize, 0, Size - CopySize);
    }
  }
}

uint64_t evmGetReturnDataSize(zen::runtime::EVMInstance *Instance) {
  const auto &ReturnData = Instance->getReturnData();
  return ReturnData.size();
}

void evmEmitLog(zen::runtime::EVMInstance *Instance, uint64_t Offset,
                uint64_t Size, const uint8_t *Topic1, const uint8_t *Topic2,
                const uint8_t *Topic3, const uint8_t *Topic4) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);

  const evmc_message *Msg = Instance->getCurrentMessage();
  ZEN_ASSERT(Msg && "No current message set in EVMInstance");

  // Calculate required memory size and charge gas
  uint64_t RequiredSize = Offset + Size;
  Instance->expandMemory(RequiredSize);

  auto &Memory = Instance->getMemory();
  const uint8_t *Data = Memory.data() + Offset;

  // Build topic array - only include non-null topics
  evmc::bytes32 Topics[4] = {};
  size_t NumTopics = 0;

  if (Topic1) {
    std::memcpy(Topics[NumTopics].bytes, Topic1, 32);
    NumTopics++;
  }
  if (Topic2) {
    std::memcpy(Topics[NumTopics].bytes, Topic2, 32);
    NumTopics++;
  }
  if (Topic3) {
    std::memcpy(Topics[NumTopics].bytes, Topic3, 32);
    NumTopics++;
  }
  if (Topic4) {
    std::memcpy(Topics[NumTopics].bytes, Topic4, 32);
    NumTopics++;
  }

  Module->Host->emit_log(Msg->recipient, Data, Size, Topics, NumTopics);
}

const uint8_t *evmHandleCreateInternal(zen::runtime::EVMInstance *Instance,
                                       evmc_call_kind CallKind,
                                       intx::uint128 Value, uint64_t Offset,
                                       uint64_t Size,
                                       const uint8_t *Salt = nullptr) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);

  const evmc_message *Msg = Instance->getCurrentMessage();
  ZEN_ASSERT(Msg && "No current message set in EVMInstance");

  // Calculate required memory size and charge gas
  uint64_t RequiredSize = Offset + Size;
  Instance->expandMemory(RequiredSize);

  auto &Memory = Instance->getMemory();
  const uint8_t *InitCode = Memory.data() + Offset;

  // Create message for CREATE/CREATE2
  evmc_message CreateMsg = {};
  CreateMsg.kind = CallKind;
  CreateMsg.flags = Msg->flags;
  CreateMsg.depth = Msg->depth + 1;
  CreateMsg.gas = Msg->gas;
  CreateMsg.sender = Msg->recipient;
  std::memcpy(CreateMsg.value.bytes, &Value, 32);
  CreateMsg.input_data = InitCode;
  CreateMsg.input_size = Size;

  // Set salt for CREATE2
  if (CallKind == EVMC_CREATE2 && Salt != nullptr) {
    std::memcpy(CreateMsg.create2_salt.bytes, Salt, 32);
  }

  Instance->pushMessage(&CreateMsg);
  evmc::Result Result = Module->Host->call(CreateMsg);
  Instance->popMessage();

  // Store return data
  std::vector<uint8_t> ReturnData(Result.output_data,
                                  Result.output_data + Result.output_size);
  Instance->setReturnData(std::move(ReturnData));
  if (Result.status_code == EVMC_SUCCESS) {
    // Return created contract address
    static evmc::address CreatedAddr = Result.create_address;
    return CreatedAddr.bytes;
  } else {
    // Return zero address on failure
    static evmc::address ZeroAddr = {};
    return ZeroAddr.bytes;
  }
}

const uint8_t *evmHandleCreate(zen::runtime::EVMInstance *Instance,
                               intx::uint128 Value, uint64_t Offset,
                               uint64_t Size) {
  return evmHandleCreateInternal(Instance, EVMC_CREATE, Value, Offset, Size);
}

const uint8_t *evmHandleCreate2(zen::runtime::EVMInstance *Instance,
                                intx::uint128 Value, uint64_t Offset,
                                uint64_t Size, const uint8_t *Salt) {
  return evmHandleCreateInternal(Instance, EVMC_CREATE2, Value, Offset, Size,
                                 Salt);
}

// Helper function for all call types
static uint64_t evmHandleCallInternal(zen::runtime::EVMInstance *Instance,
                                      evmc_call_kind CallKind, uint64_t Gas,
                                      const uint8_t *ToAddr,
                                      intx::uint128 Value, uint64_t ArgsOffset,
                                      uint64_t ArgsSize, uint64_t RetOffset,
                                      uint64_t RetSize, bool ForceStatic) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);

  const evmc_message *CurrentMsg = Instance->getCurrentMessage();
  ZEN_ASSERT(CurrentMsg && "No current message set in EVMInstance");
  evmc::address TargetAddr{};
  if (ToAddr) {
    constexpr size_t AddrSize = sizeof(TargetAddr.bytes);
    for (size_t I = 0; I < AddrSize; ++I) {
      // Copy the low 20 bytes and reverse to produce the big-endian address.
      TargetAddr.bytes[I] = ToAddr[AddrSize - 1 - I];
    }
  }
  evmc_revision Rev = Instance->getRevision();
  if (Rev >= EVMC_BERLIN &&
      Module->Host->access_account(TargetAddr) == EVMC_ACCESS_COLD) {
    Instance->chargeGas(zen::evm::ADDITIONAL_COLD_ACCOUNT_ACCESS_COST);
  }

  const bool TransfersValue =
      (CallKind == EVMC_CALL || CallKind == EVMC_CALLCODE) && Value != 0;
  if (TransfersValue && Instance->isStaticMode()) {
    throw zen::common::getError(zen::common::ErrorCode::EVMStaticModeViolation);
  }

  bool HasEnoughBalance = true;
  if (TransfersValue) {
    const auto CallerBalance = Module->Host->get_balance(CurrentMsg->recipient);
    const intx::uint256 CallerValue =
        intx::be::load<intx::uint256>(CallerBalance);
    HasEnoughBalance = CallerValue >= intx::uint256(Value);
    uint64_t ValueCost = zen::evm::CALL_VALUE_COST;
    if (!HasEnoughBalance) {
      ValueCost -= zen::evm::CALL_GAS_STIPEND;
    }
    Instance->chargeGas(ValueCost);
    if (CallKind == EVMC_CALL && HasEnoughBalance &&
        !Module->Host->account_exists(TargetAddr)) {
      Instance->chargeGas(zen::evm::ACCOUNT_CREATION_COST);
    }
  }

  if (TransfersValue && !HasEnoughBalance) {
    Instance->setReturnData({});
    return 0;
  }

  // Calculate required memory sizes for input and output
  uint64_t InputRequiredSize = ArgsOffset + ArgsSize;
  uint64_t OutputRequiredSize = RetOffset + RetSize;
  uint64_t MaxRequiredSize = std::max(InputRequiredSize, OutputRequiredSize);

  // Expand memory and charge gas
  Instance->expandMemory(MaxRequiredSize);

  auto &Memory = Instance->getMemory();
  const uint8_t *InputData =
      (ArgsSize > 0) ? Memory.data() + ArgsOffset : nullptr;

  // Create message for call
  evmc_message CallMsg{
      .kind = CallKind,
      .flags = (CallKind == EVMC_CALL && ForceStatic) ? uint32_t{EVMC_STATIC}
                                                      : CurrentMsg->flags,
      .depth = CurrentMsg->depth + 1,
      .gas = static_cast<int64_t>(Gas),
      .recipient = (CallKind == EVMC_CALL || ForceStatic)
                       ? TargetAddr
                       : CurrentMsg->recipient,
      .sender = (CallKind == EVMC_DELEGATECALL) ? CurrentMsg->sender
                                                : CurrentMsg->recipient,
      .input_data = Memory.data() + ArgsOffset,
      .input_size = ArgsSize,
      .value = (CallKind == EVMC_DELEGATECALL)
                   ? CurrentMsg->value
                   : intx::be::store<evmc::bytes32>(intx::uint256{Value}),
      .create2_salt = {},
      .code_address = TargetAddr,
      .code = nullptr,
      .code_size = 0,
  };

  Instance->pushMessage(&CallMsg);
  evmc::Result Result = Module->Host->call(CallMsg);
  Instance->popMessage();

  // Charge the caller for the gas actually consumed by the callee.
  uint64_t CallGas = Gas;
  uint64_t GasLeft =
      Result.gas_left > 0 ? static_cast<uint64_t>(Result.gas_left) : 0;
  uint64_t GasUsed = CallGas > GasLeft ? CallGas - GasLeft : 0;
  if (GasUsed >= zen::evm::BASIC_EXECUTION_COST) {
    GasUsed -= zen::evm::BASIC_EXECUTION_COST;
  } else {
    GasUsed = 0;
  }
  if (GasUsed > 0) {
    Instance->chargeGas(GasUsed);
  }
  if (Result.gas_refund > 0) {
    Instance->addGasRefund(Result.gas_refund);
  }

  // Copy return data to memory if output area is specified
  if (RetSize > 0 && Result.output_size > 0) {
    size_t CopySize =
        std::min(static_cast<size_t>(RetSize), Result.output_size);
    std::memcpy(Memory.data() + RetOffset, Result.output_data, CopySize);

    // Zero out remaining output area if needed
    if (RetSize > CopySize) {
      std::memset(Memory.data() + RetOffset + CopySize, 0, RetSize - CopySize);
    }
  }

  // Store full return data for RETURNDATASIZE/RETURNDATACOPY
  std::vector<uint8_t> ReturnData(Result.output_data,
                                  Result.output_data + Result.output_size);
  Instance->setReturnData(std::move(ReturnData));

  // Determine success (1) or failure (0)
  uint64_t Success = (Result.status_code == EVMC_SUCCESS) ? 1 : 0;

  return Success;
}

uint64_t evmHandleCall(zen::runtime::EVMInstance *Instance, uint64_t Gas,
                       const uint8_t *ToAddr, intx::uint128 Value,
                       uint64_t ArgsOffset, uint64_t ArgsSize,
                       uint64_t RetOffset, uint64_t RetSize) {
  return evmHandleCallInternal(Instance, EVMC_CALL, Gas, ToAddr, Value,
                               ArgsOffset, ArgsSize, RetOffset, RetSize, false);
}

uint64_t evmHandleCallCode(zen::runtime::EVMInstance *Instance, uint64_t Gas,
                           const uint8_t *ToAddr, intx::uint128 Value,
                           uint64_t ArgsOffset, uint64_t ArgsSize,
                           uint64_t RetOffset, uint64_t RetSize) {
  return evmHandleCallInternal(Instance, EVMC_CALLCODE, Gas, ToAddr, Value,
                               ArgsOffset, ArgsSize, RetOffset, RetSize, false);
}

void evmHandleInvalid(zen::runtime::EVMInstance *Instance) {
  // Immediately terminate the execution and return the revert code (2)
  evmc::Result ExeResult(
      EVMC_INVALID_INSTRUCTION, 0, Instance ? Instance->getGasRefund() : 0,
      Instance->getReturnData().data(), Instance->getReturnData().size());
  Instance->setExeResult(std::move(ExeResult));
  Instance->exit(4);
}

uint64_t evmHandleDelegateCall(zen::runtime::EVMInstance *Instance,
                               uint64_t Gas, const uint8_t *ToAddr,
                               uint64_t ArgsOffset, uint64_t ArgsSize,
                               uint64_t RetOffset, uint64_t RetSize) {
  return evmHandleCallInternal(Instance, EVMC_DELEGATECALL, Gas, ToAddr,
                               intx::uint128{0}, ArgsOffset, ArgsSize,
                               RetOffset, RetSize, false);
}

uint64_t evmHandleStaticCall(zen::runtime::EVMInstance *Instance, uint64_t Gas,
                             const uint8_t *ToAddr, uint64_t ArgsOffset,
                             uint64_t ArgsSize, uint64_t RetOffset,
                             uint64_t RetSize) {
  return evmHandleCallInternal(Instance, EVMC_CALL, Gas, ToAddr,
                               intx::uint128{0}, ArgsOffset, ArgsSize,
                               RetOffset, RetSize, true);
}

void evmSetRevert(zen::runtime::EVMInstance *Instance, uint64_t Offset,
                  uint64_t Size) {
  auto &Memory = Instance->getMemory();
  std::vector<uint8_t> ReturnData(Memory.begin() + Offset,
                                  Memory.begin() + Offset + Size);
  Instance->setReturnData(std::move(ReturnData));
  // Immediately terminate the execution and return the revert code (2)
  evmc::Result ExeResult(
      EVMC_REVERT, 0, Instance ? Instance->getGasRefund() : 0,
      Instance->getReturnData().data(), Instance->getReturnData().size());
  Instance->setExeResult(std::move(ExeResult));
  Instance->exit(2);
}

void evmSetCodeCopy(zen::runtime::EVMInstance *Instance, uint64_t DestOffset,
                    uint64_t Offset, uint64_t Size) {
  uint64_t RequiredSize = DestOffset + Size;
  Instance->expandMemory(RequiredSize);
  if (uint64_t CopyGas = calculateWordCopyGas(Size)) {
    Instance->chargeGas(CopyGas);
  }

  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module);
  const zen::common::Byte *Code = Module->Code;
  size_t CodeSize = Module->CodeSize;

  auto &Memory = Instance->getMemory();

  if (Offset < CodeSize) {
    auto CopySize = std::min(Size, CodeSize - Offset);
    std::memcpy(Memory.data() + DestOffset, Code + Offset, CopySize);
    if (Size > CopySize) {
      std::memset(Memory.data() + DestOffset + CopySize, 0, Size - CopySize);
    }
  } else {
    if (Size > 0) {
      std::memset(Memory.data() + DestOffset, 0, Size);
    }
  }
}

const uint8_t *evmGetKeccak256(zen::runtime::EVMInstance *Instance,
                               uint64_t Offset, uint64_t Length) {
  uint64_t RequiredSize = Offset + Length;
  Instance->expandMemory(RequiredSize);

  auto &Memory = Instance->getMemory();
  const uint8_t *InputData = Memory.data() + Offset;

  auto &Cache = Instance->getMessageCache();
  evmc::bytes32 HashResult;
  zen::host::evm::crypto::keccak256(InputData, Length, HashResult.bytes);
  Cache.Keccak256Results.push_back(HashResult);

  return Cache.Keccak256Results.back().bytes;
}
const intx::uint256 *evmGetSLoad(zen::runtime::EVMInstance *Instance,
                                 const intx::uint256 &Index) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);
  const evmc_message *Msg = Instance->getCurrentMessage();
  evmc_revision Rev = Instance->getRevision();

  const auto Key = intx::be::store<evmc::bytes32>(Index);
  if (Rev >= EVMC_BERLIN &&
      Module->Host->access_storage(Msg->recipient, Key) == EVMC_ACCESS_COLD) {
    Instance->chargeGas(zen::evm::ADDITIONAL_COLD_ACCOUNT_ACCESS_COST);
  }
  const auto Value = Module->Host->get_storage(Msg->recipient, Key);
  return storeUint256Result(intx::be::load<intx::uint256>(Value));
}
void evmSetSStore(zen::runtime::EVMInstance *Instance,
                  const intx::uint256 &Index, const intx::uint256 &Value) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);
  if (Instance->isStaticMode()) {
    throw zen::common::getError(zen::common::ErrorCode::EVMStaticModeViolation);
  }
  const evmc_message *Msg = Instance->getCurrentMessage();
  evmc_revision Rev = Instance->getRevision();
  const auto Key = intx::be::store<evmc::bytes32>(Index);
  const auto Val = intx::be::store<evmc::bytes32>(Value);

  const auto GasCostCold =
      (Rev >= EVMC_BERLIN &&
       Module->Host->access_storage(Msg->recipient, Key) == EVMC_ACCESS_COLD)
          ? zen::evm::COLD_SLOAD_COST
          : 0;
  const auto Status = Module->Host->set_storage(Msg->recipient, Key, Val);

  const auto [GasCostWarm, GasReFund] = zen::evm::SSTORE_COSTS[Rev][Status];

  const auto GasCost = GasCostCold + GasCostWarm;
  Instance->chargeGas(GasCost);
  Instance->addGasRefund(GasReFund);
}

uint64_t evmGetGas(zen::runtime::EVMInstance *Instance) {
  return Instance->getGas();
}

const intx::uint256 *evmGetTLoad(zen::runtime::EVMInstance *Instance,
                                 const intx::uint256 &Index) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);
  const evmc_message *Msg = Instance->getCurrentMessage();
  const auto Key = intx::be::store<evmc::bytes32>(Index);
  const auto Value = Module->Host->get_transient_storage(Msg->recipient, Key);
  return storeUint256Result(intx::be::load<intx::uint256>(Value));
}
void evmSetTStore(zen::runtime::EVMInstance *Instance,
                  const intx::uint256 &Index, const intx::uint256 &Value) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);
  if (Instance->isStaticMode()) {
    throw zen::common::getError(zen::common::ErrorCode::EVMStaticModeViolation);
  }
  const evmc_message *Msg = Instance->getCurrentMessage();
  const auto Key = intx::be::store<evmc::bytes32>(Index);
  const auto Val = intx::be::store<evmc::bytes32>(Value);
  Module->Host->set_transient_storage(Msg->recipient, Key, Val);
}
void evmHandleSelfDestruct(zen::runtime::EVMInstance *Instance,
                           const uint8_t *Beneficiary) {
  const zen::runtime::EVMModule *Module = Instance->getModule();
  ZEN_ASSERT(Module && Module->Host);
  if (Instance->isStaticMode()) {
    throw zen::common::getError(zen::common::ErrorCode::EVMStaticModeViolation);
  }
  const evmc_message *Msg = Instance->getCurrentMessage();
  evmc_revision Rev = Instance->getRevision();

  evmc::address BenefAddr;
  std::memcpy(BenefAddr.bytes, Beneficiary, sizeof(BenefAddr.bytes));

  // EIP-161: if target account does not exist, charge account creation cost
  if (Rev >= EVMC_SPURIOUS_DRAGON && !Module->Host->account_exists(BenefAddr)) {
    Instance->chargeGas(zen::evm::ACCOUNT_CREATION_COST);
  }

  // EIP-2929: Charge cold account access cost if needed
  if (Rev >= EVMC_BERLIN &&
      Module->Host->access_account(BenefAddr) == EVMC_ACCESS_COLD) {
    Instance->chargeGas(zen::evm::ADDITIONAL_COLD_ACCOUNT_ACCESS_COST);
  }

  Module->Host->selfdestruct(Msg->recipient, BenefAddr);
  uint64_t RemainingGas = Msg->gas;
  Instance->popMessage();

  if (const evmc_message *Parent = Instance->getCurrentMessage()) {
    const_cast<evmc_message *>(Parent)->gas += RemainingGas;
  } else {
    evmc::Result ExeResult(
        EVMC_SUCCESS, 0, Instance ? Instance->getGasRefund() : 0,
        Instance->getReturnData().data(), Instance->getReturnData().size());
    Instance->setExeResult(std::move(ExeResult));
    Instance->exit(0);
  }
}

static const char *op_enum_names[256] = {
    /* 0x00 */ "STOP",
    /* 0x01 */ "ADD",
    /* 0x02 */ "MUL",
    /* 0x03 */ "SUB",
    /* 0x04 */ "DIV",
    /* 0x05 */ "SDIV",
    /* 0x06 */ "MOD",
    /* 0x07 */ "SMOD",
    /* 0x08 */ "ADDMOD",
    /* 0x09 */ "MULMOD",
    /* 0x0a */ "EXP",
    /* 0x0b */ "SIGNEXTEND",
    /* 0x0c */ NULL,
    /* 0x0d */ NULL,
    /* 0x0e */ NULL,
    /* 0x0f */ NULL,
    /* 0x10 */ "LT",
    /* 0x11 */ "GT",
    /* 0x12 */ "SLT",
    /* 0x13 */ "SGT",
    /* 0x14 */ "EQ",
    /* 0x15 */ "ISZERO",
    /* 0x16 */ "AND",
    /* 0x17 */ "OR",
    /* 0x18 */ "XOR",
    /* 0x19 */ "NOT",
    /* 0x1a */ "BYTE",
    /* 0x1b */ "SHL",
    /* 0x1c */ "SHR",
    /* 0x1d */ "SAR",
    /* 0x1e */ NULL,
    /* 0x1f */ NULL,
    /* 0x20 */ "KECCAK256",
    /* 0x21 */ NULL,
    /* 0x22 */ NULL,
    /* 0x23 */ NULL,
    /* 0x24 */ NULL,
    /* 0x25 */ NULL,
    /* 0x26 */ NULL,
    /* 0x27 */ NULL,
    /* 0x28 */ NULL,
    /* 0x29 */ NULL,
    /* 0x2a */ NULL,
    /* 0x2b */ NULL,
    /* 0x2c */ NULL,
    /* 0x2d */ NULL,
    /* 0x2e */ NULL,
    /* 0x2f */ NULL,
    /* 0x30 */ "ADDRESS",
    /* 0x31 */ "BALANCE",
    /* 0x32 */ "ORIGIN",
    /* 0x33 */ "CALLER",
    /* 0x34 */ "CALLVALUE",
    /* 0x35 */ "CALLDATALOAD",
    /* 0x36 */ "CALLDATASIZE",
    /* 0x37 */ "CALLDATACOPY",
    /* 0x38 */ "CODESIZE",
    /* 0x39 */ "CODECOPY",
    /* 0x3a */ "GASPRICE",
    /* 0x3b */ "EXTCODESIZE",
    /* 0x3c */ "EXTCODECOPY",
    /* 0x3d */ "RETURNDATASIZE",
    /* 0x3e */ "RETURNDATACOPY",
    /* 0x3f */ "EXTCODEHASH",
    /* 0x40 */ "BLOCKHASH",
    /* 0x41 */ "COINBASE",
    /* 0x42 */ "TIMESTAMP",
    /* 0x43 */ "NUMBER",
    /* 0x44 */ "PREVRANDAO",
    /* 0x45 */ "GASLIMIT",
    /* 0x46 */ "CHAINID",
    /* 0x47 */ "SELFBALANCE",
    /* 0x48 */ "BASEFEE",
    /* 0x49 */ "BLOBHASH",
    /* 0x4a */ "BLOBBASEFEE",
    /* 0x4b */ NULL,
    /* 0x4c */ NULL,
    /* 0x4d */ NULL,
    /* 0x4e */ NULL,
    /* 0x4f */ NULL,
    /* 0x50 */ "POP",
    /* 0x51 */ "MLOAD",
    /* 0x52 */ "MSTORE",
    /* 0x53 */ "MSTORE8",
    /* 0x54 */ "SLOAD",
    /* 0x55 */ "SSTORE",
    /* 0x56 */ "JUMP",
    /* 0x57 */ "JUMPI",
    /* 0x58 */ "PC",
    /* 0x59 */ "MSIZE",
    /* 0x5a */ "GAS",
    /* 0x5b */ "JUMPDEST",
    /* 0x5c */ "TLOAD",
    /* 0x5d */ "TSTORE",
    /* 0x5e */ "MCOPY",
    /* 0x5f */ "PUSH0",
    /* 0x60 */ "PUSH1",
    /* 0x61 */ "PUSH2",
    /* 0x62 */ "PUSH3",
    /* 0x63 */ "PUSH4",
    /* 0x64 */ "PUSH5",
    /* 0x65 */ "PUSH6",
    /* 0x66 */ "PUSH7",
    /* 0x67 */ "PUSH8",
    /* 0x68 */ "PUSH9",
    /* 0x69 */ "PUSH10",
    /* 0x6a */ "PUSH11",
    /* 0x6b */ "PUSH12",
    /* 0x6c */ "PUSH13",
    /* 0x6d */ "PUSH14",
    /* 0x6e */ "PUSH15",
    /* 0x6f */ "PUSH16",
    /* 0x70 */ "PUSH17",
    /* 0x71 */ "PUSH18",
    /* 0x72 */ "PUSH19",
    /* 0x73 */ "PUSH20",
    /* 0x74 */ "PUSH21",
    /* 0x75 */ "PUSH22",
    /* 0x76 */ "PUSH23",
    /* 0x77 */ "PUSH24",
    /* 0x78 */ "PUSH25",
    /* 0x79 */ "PUSH26",
    /* 0x7a */ "PUSH27",
    /* 0x7b */ "PUSH28",
    /* 0x7c */ "PUSH29",
    /* 0x7d */ "PUSH30",
    /* 0x7e */ "PUSH31",
    /* 0x7f */ "PUSH32",
    /* 0x80 */ "DUP1",
    /* 0x81 */ "DUP2",
    /* 0x82 */ "DUP3",
    /* 0x83 */ "DUP4",
    /* 0x84 */ "DUP5",
    /* 0x85 */ "DUP6",
    /* 0x86 */ "DUP7",
    /* 0x87 */ "DUP8",
    /* 0x88 */ "DUP9",
    /* 0x89 */ "DUP10",
    /* 0x8a */ "DUP11",
    /* 0x8b */ "DUP12",
    /* 0x8c */ "DUP13",
    /* 0x8d */ "DUP14",
    /* 0x8e */ "DUP15",
    /* 0x8f */ "DUP16",
    /* 0x90 */ "SWAP1",
    /* 0x91 */ "SWAP2",
    /* 0x92 */ "SWAP3",
    /* 0x93 */ "SWAP4",
    /* 0x94 */ "SWAP5",
    /* 0x95 */ "SWAP6",
    /* 0x96 */ "SWAP7",
    /* 0x97 */ "SWAP8",
    /* 0x98 */ "SWAP9",
    /* 0x99 */ "SWAP10",
    /* 0x9a */ "SWAP11",
    /* 0x9b */ "SWAP12",
    /* 0x9c */ "SWAP13",
    /* 0x9d */ "SWAP14",
    /* 0x9e */ "SWAP15",
    /* 0x9f */ "SWAP16",
    /* 0xa0 */ "LOG0",
    /* 0xa1 */ "LOG1",
    /* 0xa2 */ "LOG2",
    /* 0xa3 */ "LOG3",
    /* 0xa4 */ "LOG4",
    /* 0xa5 */ NULL,
    /* 0xa6 */ NULL,
    /* 0xa7 */ NULL,
    /* 0xa8 */ NULL,
    /* 0xa9 */ NULL,
    /* 0xaa */ NULL,
    /* 0xab */ NULL,
    /* 0xac */ NULL,
    /* 0xad */ NULL,
    /* 0xae */ NULL,
    /* 0xaf */ NULL,
    /* 0xb0 */ NULL,
    /* 0xb1 */ NULL,
    /* 0xb2 */ NULL,
    /* 0xb3 */ NULL,
    /* 0xb4 */ NULL,
    /* 0xb5 */ NULL,
    /* 0xb6 */ NULL,
    /* 0xb7 */ NULL,
    /* 0xb8 */ NULL,
    /* 0xb9 */ NULL,
    /* 0xba */ NULL,
    /* 0xbb */ NULL,
    /* 0xbc */ NULL,
    /* 0xbd */ NULL,
    /* 0xbe */ NULL,
    /* 0xbf */ NULL,
    /* 0xc0 */ NULL,
    /* 0xc1 */ NULL,
    /* 0xc2 */ NULL,
    /* 0xc3 */ NULL,
    /* 0xc4 */ NULL,
    /* 0xc5 */ NULL,
    /* 0xc6 */ NULL,
    /* 0xc7 */ NULL,
    /* 0xc8 */ NULL,
    /* 0xc9 */ NULL,
    /* 0xca */ NULL,
    /* 0xcb */ NULL,
    /* 0xcc */ NULL,
    /* 0xcd */ NULL,
    /* 0xce */ NULL,
    /* 0xcf */ NULL,
    /* 0xd0 */ NULL,
    /* 0xd1 */ NULL,
    /* 0xd2 */ NULL,
    /* 0xd3 */ NULL,
    /* 0xd4 */ NULL,
    /* 0xd5 */ NULL,
    /* 0xd6 */ NULL,
    /* 0xd7 */ NULL,
    /* 0xd8 */ NULL,
    /* 0xd9 */ NULL,
    /* 0xda */ NULL,
    /* 0xdb */ NULL,
    /* 0xdc */ NULL,
    /* 0xdd */ NULL,
    /* 0xde */ NULL,
    /* 0xdf */ NULL,
    /* 0xe0 */ NULL,
    /* 0xe1 */ NULL,
    /* 0xe2 */ NULL,
    /* 0xe3 */ NULL,
    /* 0xe4 */ NULL,
    /* 0xe5 */ NULL,
    /* 0xe6 */ NULL,
    /* 0xe7 */ NULL,
    /* 0xe8 */ NULL,
    /* 0xe9 */ NULL,
    /* 0xea */ NULL,
    /* 0xeb */ NULL,
    /* 0xec */ NULL,
    /* 0xed */ NULL,
    /* 0xee */ NULL,
    /* 0xef */ NULL,
    /* 0xf0 */ "CREATE",
    /* 0xf1 */ "CALL",
    /* 0xf2 */ "CALLCODE",
    /* 0xf3 */ "RETURN",
    /* 0xf4 */ "DELEGATECALL",
    /* 0xf5 */ "CREATE2",
    /* 0xf6 */ NULL,
    /* 0xf7 */ NULL,
    /* 0xf8 */ NULL,
    /* 0xf9 */ NULL,
    /* 0xfa */ "STATICCALL",
    /* 0xfb */ NULL,
    /* 0xfc */ NULL,
    /* 0xfd */ "REVERT",
    /* 0xfe */ "INVALID",
    /* 0xff */ "SELFDESTRUCT",
};

void evmHandleDebug(zen::runtime::EVMInstance *Instance, uint64_t Opcode,
                    uint64_t Offset) {
  // Print debug information about the opcode and its offset
  printf("DEBUG: Opcode=%s(%d), Offset=%x\n", op_enum_names[Opcode], Opcode,
         Offset);
  // Print instance's memory information
  if (Instance) {
    auto &Memory = Instance->getMemory();
    uint64_t MemorySize = Instance->getMemorySize();
    // Print first 32 bytes of memory as an example (or up to memory size if
    // smaller)
    printf("DEBUG: Memory Content: \n");
    for (uint64_t i = 0; i < MemorySize; i++) {
      printf("%02x", Memory[i]);
    }
    printf("\n");

    uint64_t StackSize = Instance->getEVMStackSize();
    uint8_t *Stack = Instance->getEVMStack();
    printf("DEBUG: Stack Content: \n");
    int count = 0;
    for (uint64_t i = 0; i < StackSize; i += 32) {
      printf("%d: ", count++);
      for (uint64_t j = 0; j < 32; j++) {
        printf("%02x", Stack[StackSize - (j + i) - 1]);
      }
      printf("\n");
    }
    printf("DEBUG: Stack End\n");
  }
}

} // namespace COMPILER
