// Copyright (C) 2026 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "evm/evm.h"
#include "evm/opcode_gas_trace.h"
#include "evm_test_host.hpp"
#include "runtime/runtime.h"
#include "utils/evm.h"
#include "vm/dt_evmc_vm.h"

#include <cstring>
#include <functional>
#include <evmc/evmc.hpp>

using namespace zen;
using namespace zen::evm;
using namespace zen::runtime;

namespace {

struct ParsedFixture {
  std::string CaseName;
  std::string FixturePath;
  evmc_revision Revision = EVMC_FRONTIER;
  evmc_tx_context TxContext{};
  evmc_message Message{};
  uint64_t GasLimit = 0;
  uint64_t IntrinsicGas = 0;
  evmc::bytes Input;
  evmc::bytes Bytecode;
  std::vector<ZenMockedEVMHost::AccountInitEntry> Accounts;
  std::string ExpectedStatus;
  uint64_t ExpectedTxGas = 0;
  std::unordered_map<int64_t, evmc::bytes32> BlockHashes;
};

struct ParsedTxSpec {
  evmc_message Message{};
  evmc::uint256be GasPrice{};
  uint64_t GasLimit = 0;
  uint64_t IntrinsicGas = 0;
  evmc::bytes Input;
  bool IsCreate = false;
};

struct ParsedPrefixReplayFixture {
  std::string CaseName;
  std::string FixturePath;
  evmc_revision Revision = EVMC_FRONTIER;
  evmc_tx_context TxContext{};
  std::vector<ZenMockedEVMHost::AccountInitEntry> Accounts;
  std::unordered_map<int64_t, evmc::bytes32> BlockHashes;
  std::vector<ParsedTxSpec> PrefixTransactions;
  ParsedTxSpec TargetTx;
  std::string ExpectedStatus;
  uint64_t ExpectedTxGas = 0;
};

struct SilkwormGasResult {
  bool Success = false;
  evmc_status_code Status = EVMC_INTERNAL_ERROR;
  uint64_t TxGas = 0;
  std::string Error;
};

class FixtureHost : public ZenMockedEVMHost {
public:
  std::unordered_map<int64_t, evmc::bytes32> BlockHashOverrides;

  evmc::bytes32 get_block_hash(int64_t BlockNumber) const noexcept override {
    auto It = BlockHashOverrides.find(BlockNumber);
    if (It != BlockHashOverrides.end()) {
      return It->second;
    }
    return ZenMockedEVMHost::get_block_hash(BlockNumber);
  }
};

uint64_t computeFixtureIntrinsicGas(evmc_revision Revision,
                                    evmc_call_kind MsgKind,
                                    const uint8_t *InputData, size_t InputSize) {
  constexpr int64_t TxDataZeroGas = 4;
  constexpr int64_t TxDataNonZeroGas = 68;
  constexpr int64_t TxCreateCost = 32000;
  constexpr int64_t InitcodeWordGas = 2;
  int64_t Gas = BASIC_EXECUTION_COST;
  const int64_t NonZeroCost =
      Revision >= EVMC_ISTANBUL ? 16 : TxDataNonZeroGas;
  for (size_t Idx = 0; Idx < InputSize; ++Idx) {
    Gas += InputData[Idx] == 0 ? TxDataZeroGas : NonZeroCost;
  }
  const bool IsCreate = MsgKind == EVMC_CREATE || MsgKind == EVMC_CREATE2;
  if (IsCreate && Revision >= EVMC_HOMESTEAD) {
    Gas += TxCreateCost;
    if (Revision >= EVMC_SHANGHAI && InputSize > 0) {
      Gas += static_cast<int64_t>((InputSize + 31) / 32) * InitcodeWordGas;
    }
  }
  return static_cast<uint64_t>(Gas);
}

evmc_revision parseRevision(const std::string &Revision) {
  if (Revision == "EVMC_FRONTIER")
    return EVMC_FRONTIER;
  if (Revision == "EVMC_HOMESTEAD")
    return EVMC_HOMESTEAD;
  if (Revision == "EVMC_TANGERINE_WHISTLE")
    return EVMC_TANGERINE_WHISTLE;
  if (Revision == "EVMC_SPURIOUS_DRAGON")
    return EVMC_SPURIOUS_DRAGON;
  if (Revision == "EVMC_BYZANTIUM")
    return EVMC_BYZANTIUM;
  if (Revision == "EVMC_CONSTANTINOPLE")
    return EVMC_CONSTANTINOPLE;
  if (Revision == "EVMC_PETERSBURG")
    return EVMC_PETERSBURG;
  if (Revision == "EVMC_ISTANBUL")
    return EVMC_ISTANBUL;
  if (Revision == "EVMC_BERLIN")
    return EVMC_BERLIN;
  if (Revision == "EVMC_LONDON")
    return EVMC_LONDON;
  if (Revision == "EVMC_PARIS")
    return EVMC_PARIS;
  if (Revision == "EVMC_SHANGHAI")
    return EVMC_SHANGHAI;
  if (Revision == "EVMC_CANCUN")
    return EVMC_CANCUN;
  return EVMC_FRONTIER;
}

evmc_revision revisionForBlock(uint64_t BlockNumber) {
  struct Fork {
    uint64_t Height;
    evmc_revision Rev;
  };
  static const Fork Forks[] = {
      {2'463'000, EVMC_TANGERINE_WHISTLE}, {2'675'000, EVMC_SPURIOUS_DRAGON},
      {4'370'000, EVMC_BYZANTIUM},         {7'280'000, EVMC_CONSTANTINOPLE},
      {9'069'000, EVMC_ISTANBUL},           {12'244'000, EVMC_BERLIN},
      {12'965'000, EVMC_LONDON},            {15'537'394, EVMC_PARIS},
      {17'034'470, EVMC_SHANGHAI},          {19'426'587, EVMC_CANCUN},
  };
  evmc_revision Rev = EVMC_FRONTIER;
  if (BlockNumber >= 1'150'000) {
    Rev = EVMC_HOMESTEAD;
  }
  for (const auto &F : Forks) {
    if (BlockNumber >= F.Height) {
      Rev = F.Rev;
    }
  }
  return Rev;
}

std::string padHexToEvenLength(const std::string &Hex) {
  if (Hex.size() < 3 || Hex[0] != '0' || Hex[1] != 'x') {
    return Hex;
  }
  if ((Hex.size() - 2) % 2 == 0) {
    return Hex;
  }
  return "0x0" + Hex.substr(2);
}

std::filesystem::path getLegacyReproFixtureDir() {
  return std::filesystem::path(__FILE__).parent_path() /
         std::filesystem::path("../../tests/evm/fixtures/legacy_call_repro");
}

void applyFixtureHostEnv(FixtureHost &Host, const ParsedFixture &Fixture) {
  const auto DocPath = std::filesystem::path(Fixture.FixturePath);
  std::ifstream F(DocPath);
  rapidjson::IStreamWrapper ISW(F);
  rapidjson::Document D;
  D.ParseStream(ISW);
  if (D.IsObject() && D.HasMember("env") && D["env"].IsObject() &&
      D["env"].HasMember("block_hash") && D["env"]["block_hash"].IsString()) {
    Host.block_hash =
        zen::utils::parseBytes32(D["env"]["block_hash"].GetString());
  }
  Host.BlockHashOverrides = Fixture.BlockHashes;
}

ParsedFixture loadFixture(const std::filesystem::path &Path) {
  std::ifstream File(Path);
  EXPECT_TRUE(File.is_open()) << "failed to open fixture: " << Path.string();

  rapidjson::IStreamWrapper ISW(File);
  rapidjson::Document Doc;
  Doc.ParseStream(ISW);
  EXPECT_FALSE(Doc.HasParseError()) << "parse error in fixture: " << Path.string();
  EXPECT_TRUE(Doc.IsObject()) << "fixture root must be object: " << Path.string();

  ParsedFixture Fixture;
  Fixture.FixturePath = Path.string();
  Fixture.CaseName = Doc["case_name"].GetString();

  const auto &Tx = Doc["tx"];
  const auto &Env = Doc["env"];
  const auto &Prestate = Doc["prestate"];
  const auto &Expected = Doc["expected"];

  const uint64_t BlockNumber = Env["block_number"].GetUint64();
  Fixture.Revision = Doc.HasMember("revision")
                         ? parseRevision(Doc["revision"].GetString())
                         : revisionForBlock(BlockNumber);
  if (BlockNumber >= 1'150'000 && Fixture.Revision == EVMC_FRONTIER) {
    Fixture.Revision = revisionForBlock(BlockNumber);
  }

  const std::string From = Tx["from"].GetString();
  const std::string InputHex = Tx["input"].GetString();
  const std::string TxKind =
      Tx.HasMember("kind") && Tx["kind"].IsString() ? Tx["kind"].GetString()
                                                    : "call";
  const bool IsCreateTx = TxKind == "create";
  std::string To = "0x0000000000000000000000000000000000000000";
  if (Tx.HasMember("to") && Tx["to"].IsString()) {
    To = Tx["to"].GetString();
  }

  Fixture.GasLimit = Tx["gas_limit"].GetUint64();
  Fixture.Input = zen::utils::hexToBytes(InputHex);

  Fixture.TxContext.tx_gas_price =
      zen::utils::parseUint256(padHexToEvenLength(Tx["gas_price"].GetString()));
  Fixture.TxContext.block_number = BlockNumber;
  Fixture.TxContext.block_timestamp = Env["block_timestamp"].GetUint64();
  Fixture.TxContext.block_coinbase =
      zen::utils::parseAddress(Env["block_coinbase"].GetString());
  Fixture.TxContext.block_prev_randao = zen::utils::parseUint256(
      padHexToEvenLength(Env["block_prev_randao"].GetString()));
  Fixture.TxContext.block_gas_limit = Env["block_gas_limit"].GetUint64();
  Fixture.TxContext.block_base_fee = zen::utils::parseUint256(
      padHexToEvenLength(Env["block_base_fee"].GetString()));
  Fixture.TxContext.tx_origin =
      zen::utils::parseAddress(Env["tx_origin"].GetString());
  if (Env.HasMember("block_hashes") && Env["block_hashes"].IsObject()) {
    for (auto It = Env["block_hashes"].MemberBegin();
         It != Env["block_hashes"].MemberEnd(); ++It) {
      int64_t BlockNum = std::stoll(It->name.GetString());
      Fixture.BlockHashes[BlockNum] =
          zen::utils::parseBytes32(padHexToEvenLength(It->value.GetString()));
    }
  }

  Fixture.Message = {};
  Fixture.Message.kind = IsCreateTx ? EVMC_CREATE : EVMC_CALL;
  Fixture.Message.flags = 0u;
  Fixture.Message.depth = 0;
  Fixture.Message.gas = static_cast<int64_t>(Fixture.GasLimit);
  Fixture.Message.recipient = zen::utils::parseAddress(To);
  Fixture.Message.sender = zen::utils::parseAddress(From);
  Fixture.Message.value =
      zen::utils::parseUint256(padHexToEvenLength(Tx["value"].GetString()));
  Fixture.Message.code = nullptr;
  Fixture.Message.code_size = 0;
  Fixture.Message.input_data =
      Fixture.Input.empty() ? nullptr : Fixture.Input.data();
  Fixture.Message.input_size = Fixture.Input.size();

  for (auto It = Prestate.MemberBegin(); It != Prestate.MemberEnd(); ++It) {
    const std::string AddressStr = It->name.GetString();
    const auto &AccountVal = It->value;

    ZenMockedEVMHost::AccountInitEntry Entry;
    Entry.Address = zen::utils::parseAddress(AddressStr);
    Entry.Account.balance = zen::utils::parseUint256(
        padHexToEvenLength(AccountVal["balance"].GetString()));
    Entry.Account.nonce = AccountVal["nonce"].GetUint64();
    Entry.Account.code = zen::utils::hexToBytes(AccountVal["code"].GetString());

    const auto &Storage = AccountVal["storage"];
    for (auto Sit = Storage.MemberBegin(); Sit != Storage.MemberEnd(); ++Sit) {
      evmc::StorageValue SV{};
      SV.current =
          zen::utils::parseBytes32(padHexToEvenLength(Sit->value.GetString()));
      Entry.Account.storage[zen::utils::parseBytes32(
          padHexToEvenLength(Sit->name.GetString()))] = SV;
    }

    if (!IsCreateTx && AddressStr == To) {
      Fixture.Bytecode = Entry.Account.code;
    }

    Fixture.Accounts.push_back(std::move(Entry));
  }
  if (IsCreateTx) {
    Fixture.Bytecode = Fixture.Input;
  }

  Fixture.ExpectedStatus = Expected["status"].GetString();
  Fixture.ExpectedTxGas = Expected["tx_gas"].GetUint64();
  Fixture.IntrinsicGas = computeFixtureIntrinsicGas(
      Fixture.Revision, Fixture.Message.kind, Fixture.Message.input_data,
      Fixture.Message.input_size);

  return Fixture;
}

void loadAccountsFromPrestate(
    const rapidjson::Value &Prestate,
    std::vector<ZenMockedEVMHost::AccountInitEntry> &Accounts) {
  for (auto It = Prestate.MemberBegin(); It != Prestate.MemberEnd(); ++It) {
    const std::string AddressStr = It->name.GetString();
    const auto &AccountVal = It->value;

    ZenMockedEVMHost::AccountInitEntry Entry;
    Entry.Address = zen::utils::parseAddress(AddressStr);
    Entry.Account.balance = zen::utils::parseUint256(
        padHexToEvenLength(AccountVal["balance"].GetString()));
    Entry.Account.nonce = AccountVal["nonce"].GetUint64();
    Entry.Account.code = zen::utils::hexToBytes(AccountVal["code"].GetString());

    const auto &Storage = AccountVal["storage"];
    for (auto Sit = Storage.MemberBegin(); Sit != Storage.MemberEnd(); ++Sit) {
      evmc::StorageValue SV{};
      SV.current =
          zen::utils::parseBytes32(padHexToEvenLength(Sit->value.GetString()));
      Entry.Account.storage[zen::utils::parseBytes32(
          padHexToEvenLength(Sit->name.GetString()))] = SV;
    }
    Accounts.push_back(std::move(Entry));
  }
}

ParsedTxSpec parseTxSpec(const rapidjson::Value &Tx, evmc_revision Revision) {
  ParsedTxSpec Spec;
  const std::string From = Tx["from"].GetString();
  const std::string InputHex = Tx["input"].GetString();
  const std::string TxKind =
      Tx.HasMember("kind") && Tx["kind"].IsString() ? Tx["kind"].GetString()
                                                    : "call";
  Spec.IsCreate = TxKind == "create";
  std::string To = "0x0000000000000000000000000000000000000000";
  if (Tx.HasMember("to") && Tx["to"].IsString()) {
    To = Tx["to"].GetString();
  }

  Spec.GasLimit = Tx["gas_limit"].GetUint64();
  Spec.GasPrice =
      zen::utils::parseUint256(padHexToEvenLength(Tx["gas_price"].GetString()));
  Spec.Input = zen::utils::hexToBytes(InputHex);

  Spec.Message = {};
  Spec.Message.kind = Spec.IsCreate ? EVMC_CREATE : EVMC_CALL;
  Spec.Message.flags = 0u;
  Spec.Message.depth = 0;
  Spec.Message.gas = static_cast<int64_t>(Spec.GasLimit);
  Spec.Message.recipient = zen::utils::parseAddress(To);
  Spec.Message.sender = zen::utils::parseAddress(From);
  Spec.Message.value =
      zen::utils::parseUint256(padHexToEvenLength(Tx["value"].GetString()));
  Spec.Message.code = nullptr;
  Spec.Message.code_size = 0;
  Spec.Message.input_data =
      Spec.Input.empty() ? nullptr : Spec.Input.data();
  Spec.Message.input_size = Spec.Input.size();
  Spec.IntrinsicGas = computeFixtureIntrinsicGas(
      Revision, Spec.Message.kind, Spec.Message.input_data,
      Spec.Message.input_size);
  return Spec;
}

ParsedPrefixReplayFixture loadPrefixReplayFixture(
    const std::filesystem::path &Path) {
  std::ifstream File(Path);
  EXPECT_TRUE(File.is_open()) << "failed to open fixture: " << Path.string();

  rapidjson::IStreamWrapper ISW(File);
  rapidjson::Document Doc;
  Doc.ParseStream(ISW);
  EXPECT_FALSE(Doc.HasParseError()) << "parse error in fixture: " << Path.string();
  EXPECT_TRUE(Doc.IsObject()) << "fixture root must be object: " << Path.string();
  EXPECT_TRUE(Doc.HasMember("prefix_transactions"))
      << "missing prefix_transactions: " << Path.string();

  ParsedPrefixReplayFixture Fixture;
  Fixture.FixturePath = Path.string();
  Fixture.CaseName = Doc["case_name"].GetString();

  const auto &Env = Doc["env"];
  const auto &Prestate = Doc["prestate"];
  const auto &Expected = Doc["expected"];

  const uint64_t BlockNumber = Env["block_number"].GetUint64();
  Fixture.Revision = Doc.HasMember("revision")
                         ? parseRevision(Doc["revision"].GetString())
                         : revisionForBlock(BlockNumber);
  if (BlockNumber >= 1'150'000 && Fixture.Revision == EVMC_FRONTIER) {
    Fixture.Revision = revisionForBlock(BlockNumber);
  }

  Fixture.TxContext.block_number = BlockNumber;
  Fixture.TxContext.block_timestamp = Env["block_timestamp"].GetUint64();
  Fixture.TxContext.block_coinbase =
      zen::utils::parseAddress(Env["block_coinbase"].GetString());
  Fixture.TxContext.block_prev_randao = zen::utils::parseUint256(
      padHexToEvenLength(Env["block_prev_randao"].GetString()));
  Fixture.TxContext.block_gas_limit = Env["block_gas_limit"].GetUint64();
  Fixture.TxContext.block_base_fee = zen::utils::parseUint256(
      padHexToEvenLength(Env["block_base_fee"].GetString()));
  if (Env.HasMember("block_hashes") && Env["block_hashes"].IsObject()) {
    for (auto It = Env["block_hashes"].MemberBegin();
         It != Env["block_hashes"].MemberEnd(); ++It) {
      int64_t BlockNum = std::stoll(It->name.GetString());
      Fixture.BlockHashes[BlockNum] =
          zen::utils::parseBytes32(padHexToEvenLength(It->value.GetString()));
    }
  }

  loadAccountsFromPrestate(Prestate, Fixture.Accounts);

  const auto &Prefix = Doc["prefix_transactions"];
  EXPECT_TRUE(Prefix.IsArray()) << "prefix_transactions must be array";
  for (rapidjson::SizeType I = 0; I < Prefix.Size(); ++I) {
    Fixture.PrefixTransactions.push_back(
        parseTxSpec(Prefix[I], Fixture.Revision));
  }
  Fixture.TargetTx = parseTxSpec(Doc["tx"], Fixture.Revision);
  Fixture.TxContext.tx_gas_price = zen::utils::parseUint256(
      padHexToEvenLength(Doc["tx"]["gas_price"].GetString()));
  Fixture.TxContext.tx_origin = Fixture.TargetTx.Message.sender;

  Fixture.ExpectedStatus = Expected["status"].GetString();
  Fixture.ExpectedTxGas = Expected["tx_gas"].GetUint64();
  return Fixture;
}

/// Load a prefix-replay fixture but execute only the target tx (prestate already
/// includes canonical prefix state via stateDiff at extraction time).
ParsedFixture loadPrefixFixtureAsTarget(const std::filesystem::path &Path) {
  std::ifstream File(Path);
  EXPECT_TRUE(File.is_open()) << "failed to open fixture: " << Path.string();

  rapidjson::IStreamWrapper ISW(File);
  rapidjson::Document Doc;
  Doc.ParseStream(ISW);
  EXPECT_FALSE(Doc.HasParseError()) << "parse error in fixture: " << Path.string();

  rapidjson::Document TargetOnly(rapidjson::kObjectType);
  auto &Alloc = TargetOnly.GetAllocator();
  for (const char *Key :
       {"case_name", "chain_id", "block_number", "tx_index", "tx_hash",
        "revision", "tx", "env", "prestate", "expected", "meta"}) {
    if (Doc.HasMember(Key)) {
      TargetOnly.AddMember(rapidjson::StringRef(Key), Doc[Key], Alloc);
    }
  }

  const auto TempPath =
      std::filesystem::temp_directory_path() /
      ("legacy_prefix_target_" + Path.filename().string());
  {
    std::ofstream Out(TempPath);
    rapidjson::StringBuffer Buffer;
    rapidjson::Writer<rapidjson::StringBuffer> Writer(Buffer);
    TargetOnly.Accept(Writer);
    Out << Buffer.GetString() << '\n';
  }
  ParsedFixture Fixture = loadFixture(TempPath);
  Fixture.FixturePath = Path.string();
  std::error_code EC;
  std::filesystem::remove(TempPath, EC);
  return Fixture;
}

ZenMockedEVMHost::TransactionExecutionConfig
buildPrefixExecConfig(const ParsedPrefixReplayFixture &Fixture,
                      const ParsedTxSpec &Spec, FixtureHost &Host,
                      const std::string &ModuleSuffix) {
  ZenMockedEVMHost::TransactionExecutionConfig Config;
  Config.ModuleName = Fixture.CaseName + "-" + ModuleSuffix;
  Config.Message = Spec.Message;
  Config.GasLimit = Spec.GasLimit;
  Config.IntrinsicGas = Spec.IntrinsicGas;
  Config.Revision = Fixture.Revision;

  const uint64_t ExecutionGas =
      Spec.GasLimit > Spec.IntrinsicGas ? Spec.GasLimit - Spec.IntrinsicGas : 0;
  Config.Message.gas = static_cast<int64_t>(ExecutionGas);

  static const uint8_t STOP_BYTE = 0x00;
  if (Spec.IsCreate) {
    Config.Bytecode =
        Spec.Input.empty() ? &STOP_BYTE : Spec.Input.data();
    Config.BytecodeSize = Spec.Input.empty() ? 1 : Spec.Input.size();
  } else {
    const auto It = Host.accounts.find(Spec.Message.recipient);
    if (It == Host.accounts.end() || It->second.code.empty()) {
      Config.Bytecode = &STOP_BYTE;
      Config.BytecodeSize = 1;
    } else {
      Config.Bytecode = It->second.code.data();
      Config.BytecodeSize = It->second.code.size();
    }
  }
  return Config;
}

void applyPrefixFixtureHostEnv(FixtureHost &Host,
                               const ParsedPrefixReplayFixture &Fixture) {
  const auto DocPath = std::filesystem::path(Fixture.FixturePath);
  std::ifstream F(DocPath);
  rapidjson::IStreamWrapper ISW(F);
  rapidjson::Document D;
  D.ParseStream(ISW);
  if (D.IsObject() && D.HasMember("env") && D["env"].IsObject() &&
      D["env"].HasMember("block_hash") && D["env"]["block_hash"].IsString()) {
    Host.block_hash =
        zen::utils::parseBytes32(D["env"]["block_hash"].GetString());
  }
  Host.BlockHashOverrides = Fixture.BlockHashes;
}

/// Silkworm-style tx gas: g0 is deducted before VM; tx_gas = gas_limit - gas_left
/// after refund cap (Frontier/Homestead: gas_used/2, London+: gas_used/5).
uint64_t silkwormTxGasFromResult(uint64_t GasLimit, int64_t GasLeft,
                                 int64_t GasRefund,
                                 evmc_revision Revision = EVMC_FRONTIER) {
  const uint64_t Gl = GasLeft > 0 ? static_cast<uint64_t>(GasLeft) : 0;
  const uint64_t Gr = GasRefund > 0 ? static_cast<uint64_t>(GasRefund) : 0;
  const uint64_t GasUsedBeforeRefund = GasLimit > Gl ? GasLimit - Gl : GasLimit;
  const uint64_t RefundQuotient = Revision >= EVMC_LONDON ? 5 : 2;
  const uint64_t RefundCap = GasUsedBeforeRefund / RefundQuotient;
  const uint64_t RefundApplied = std::min(Gr, RefundCap);
  const uint64_t GlAfterRefund = Gl + RefundApplied;
  return GasLimit > GlAfterRefund ? GasLimit - GlAfterRefund : 0;
}

SilkwormGasResult runPrefixReplayViaSilkwormHost(
    const ParsedPrefixReplayFixture &Fixture, common::RunMode Mode) {
  SilkwormGasResult Out;
  RuntimeConfig Config;
  Config.Format = common::InputFormat::EVM;
  Config.Mode = Mode;
  Config.EnableEvmGasMetering = true;

  auto Host = std::make_unique<FixtureHost>();
  Host->loadInitialState(Fixture.TxContext, Fixture.Accounts, true);
  Host->setRevision(Fixture.Revision);
  applyPrefixFixtureHostEnv(*Host, Fixture);
  auto RT = Runtime::newEVMRuntime(Config, Host.get());
  if (!RT) {
    Out.Error = "failed to create EVM runtime";
    return Out;
  }
  Host->setRuntime(RT.get());

  // Prefix txs are not re-executed here: the fixture prestate already applies
  // canonical stateDiff from txs [0, target) at extraction time.

  const auto &Target = Fixture.TargetTx;
  Host->tx_context.tx_gas_price = Target.GasPrice;
  Host->tx_context.tx_origin = Target.Message.sender;
  auto TargetExec = buildPrefixExecConfig(Fixture, Target, *Host, "target");
  const auto Result = Host->executeTransaction(TargetExec);
  Out.Success = Result.Success;
  Out.Status = Result.Status;
  Out.Error = Result.ErrorMessage;
  const uint64_t ExecutionGas =
      Target.GasLimit > Target.IntrinsicGas ? Target.GasLimit - Target.IntrinsicGas
                                            : 0;
  Out.TxGas =
      Target.IntrinsicGas +
      silkwormTxGasFromResult(ExecutionGas,
                              static_cast<int64_t>(Result.RemainingGas),
                              static_cast<int64_t>(Result.GasRefund),
                              Fixture.Revision);
  return Out;
}

SilkwormGasResult runPrefixReplayViaDtvmApi(const ParsedPrefixReplayFixture &Fixture,
                                            const char *ModeValue) {
  const common::RunMode Mode =
      (ModeValue[0] == 'm' && ModeValue[1] == 'u') ? common::RunMode::MultipassMode
                                                   : common::RunMode::InterpMode;
  return runPrefixReplayViaSilkwormHost(Fixture, Mode);
}

bool isCreateFixture(const ParsedFixture &Fixture) {
  return Fixture.Message.kind == EVMC_CREATE ||
         Fixture.Message.kind == EVMC_CREATE2;
}

constexpr uint64_t kCodeDepositCostPerByte = 200;

void prepareSilkwormDeployRecipient(FixtureHost &Host,
                                    const evmc::address &ContractAddr) {
  auto &Acc = Host.accounts[ContractAddr];
  Acc.code.clear();
  Acc.nonce = 0;
  Acc.codehash = evmc::bytes32{};
  std::memset(Acc.codehash.bytes, 0, sizeof(Acc.codehash.bytes));
  Acc.balance = evmc::bytes32{};
}

/// Silkworm CREATE: g0 is charged before VM; initcode runs as EVMC_CALL with
/// code_address zeroed (deploy_message in silkworm/core/execution/evm.cpp).
SilkwormGasResult runFixtureCreateDeploy(
    const ParsedFixture &Fixture,
    const std::function<evmc_vm *(void)> &CreateVm,
    const std::function<void(evmc_vm *)> &ConfigureVm) {
  SilkwormGasResult Out;
  auto Host = std::make_unique<FixtureHost>();
  Host->loadInitialState(Fixture.TxContext, Fixture.Accounts, true);
  applyFixtureHostEnv(*Host, Fixture);
  prepareSilkwormDeployRecipient(*Host, Fixture.Message.recipient);

  evmc_vm *Vm = CreateVm();
  if (!Vm) {
    Out.Error = "failed to create evmc vm";
    return Out;
  }
  ConfigureVm(Vm);

  const uint64_t ExecutionGas =
      Fixture.GasLimit > Fixture.IntrinsicGas
          ? Fixture.GasLimit - Fixture.IntrinsicGas
          : 0;

  evmc_message Deploy = {};
  Deploy.kind = EVMC_CALL;
  Deploy.depth = 0;
  Deploy.gas = static_cast<int64_t>(ExecutionGas);
  Deploy.sender = Fixture.Message.sender;
  Deploy.recipient = Fixture.Message.recipient;
  Deploy.value = Fixture.Message.value;
  Deploy.code_address = {};
  Deploy.input_data =
      Fixture.Input.empty() ? nullptr : Fixture.Input.data();
  Deploy.input_size = Fixture.Input.size();

  evmc_result Raw = Vm->execute(
      Vm, &evmc::MockedHost::get_interface(),
      reinterpret_cast<evmc_host_context *>(Host.get()), Fixture.Revision, &Deploy,
      Fixture.Input.data(), Fixture.Input.size());

  Out.Success = true;
  Out.Status = Raw.status_code;

  int64_t GasLeft = Raw.gas_left;
  if (Raw.status_code == EVMC_SUCCESS && Raw.output_size > 0) {
    const uint64_t DepositCost = kCodeDepositCostPerByte * Raw.output_size;
    if (GasLeft >= static_cast<int64_t>(DepositCost)) {
      GasLeft -= static_cast<int64_t>(DepositCost);
    }
  }

  Out.TxGas =
      Fixture.IntrinsicGas +
      silkwormTxGasFromResult(ExecutionGas, GasLeft, Raw.gas_refund,
                              Fixture.Revision);

  if (Raw.release) {
    Raw.release(&Raw);
  }
  Vm->destroy(Vm);
  return Out;
}

/// Run via ZenMockedEVMHost with Silkworm gas split (g0 outside, execution gas only).
SilkwormGasResult runFixtureSilkwormHost(const ParsedFixture &Fixture,
                                         common::RunMode Mode) {
  SilkwormGasResult Out;
  RuntimeConfig Config;
  Config.Format = common::InputFormat::EVM;
  Config.Mode = Mode;
  Config.EnableEvmGasMetering = true;

  auto Host = std::make_unique<FixtureHost>();
  Host->loadInitialState(Fixture.TxContext, Fixture.Accounts, true);
  Host->setRevision(Fixture.Revision);
  applyFixtureHostEnv(*Host, Fixture);
  auto RT = Runtime::newEVMRuntime(Config, Host.get());
  if (!RT) {
    Out.Error = "failed to create EVM runtime";
    return Out;
  }
  Host->setRuntime(RT.get());

  const uint64_t ExecutionGas =
      Fixture.GasLimit > Fixture.IntrinsicGas
          ? Fixture.GasLimit - Fixture.IntrinsicGas
          : 0;

  ZenMockedEVMHost::TransactionExecutionConfig ExecConfig;
  ExecConfig.ModuleName = Fixture.CaseName + "-silkworm-host";
  ExecConfig.Bytecode =
      reinterpret_cast<const uint8_t *>(Fixture.Bytecode.data());
  ExecConfig.BytecodeSize = Fixture.Bytecode.size();
  ExecConfig.Message = Fixture.Message;
  ExecConfig.Message.gas = static_cast<int64_t>(ExecutionGas);
  ExecConfig.GasLimit = ExecutionGas;
  ExecConfig.IntrinsicGas = 0;
  ExecConfig.Revision = Fixture.Revision;

  const auto Result = Host->executeTransaction(ExecConfig);
  Out.Success = Result.Success;
  Out.Status = Result.Status;
  Out.Error = Result.ErrorMessage;
  // Host meters only the execution budget; add g0 for Silkworm total tx_gas.
  Out.TxGas =
      Fixture.IntrinsicGas +
      silkwormTxGasFromResult(ExecutionGas,
                              static_cast<int64_t>(Result.RemainingGas),
                              static_cast<int64_t>(Result.GasRefund),
                              Fixture.Revision);
  return Out;
}

SilkwormGasResult runFixtureViaEvmcVm(
    const ParsedFixture &Fixture,
    const std::function<evmc_vm *(void)> &CreateVm,
    const std::function<void(evmc_vm *)> &ConfigureVm,
    bool AttachDtvmRuntimeToHost = false,
    common::RunMode HostRuntimeMode = common::RunMode::InterpMode) {
  if (isCreateFixture(Fixture)) {
    return runFixtureCreateDeploy(Fixture, CreateVm, ConfigureVm);
  }

  SilkwormGasResult Out;
  auto Host = std::make_unique<FixtureHost>();
  Host->loadInitialState(Fixture.TxContext, Fixture.Accounts, true);
  Host->setRevision(Fixture.Revision);
  applyFixtureHostEnv(*Host, Fixture);

  std::unique_ptr<Runtime> HostRuntime;
  if (AttachDtvmRuntimeToHost) {
    RuntimeConfig Config;
    Config.Format = common::InputFormat::EVM;
    Config.Mode = HostRuntimeMode;
    Config.EnableEvmGasMetering = true;
    HostRuntime = Runtime::newEVMRuntime(Config, Host.get());
    if (!HostRuntime) {
      Out.Error = "failed to create runtime for nested host calls";
      return Out;
    }
    Host->setRuntime(HostRuntime.get());
  }

  evmc_vm *Vm = CreateVm();
  if (!Vm) {
    Out.Error = "failed to create evmc vm";
    return Out;
  }
  ConfigureVm(Vm);

  if (getOpcodeGasTraceConfig().Enabled) {
    resetOpcodeGasTraceAll();
  }

  evmc_message Msg = Fixture.Message;
  const uint64_t ExecutionGas =
      Fixture.GasLimit > Fixture.IntrinsicGas
          ? Fixture.GasLimit - Fixture.IntrinsicGas
          : 0;
  Msg.gas = static_cast<int64_t>(ExecutionGas);

  const auto It = Host->accounts.find(Msg.recipient);
  if (It == Host->accounts.end()) {
    Vm->destroy(Vm);
    Out.Error = "recipient missing from prestate";
    return Out;
  }

  evmc_result Raw =
      Vm->execute(Vm, &evmc::MockedHost::get_interface(),
                  reinterpret_cast<evmc_host_context *>(Host.get()),
                  Fixture.Revision, &Msg, It->second.code.data(),
                  It->second.code.size());

  Out.Success = true;
  Out.Status = Raw.status_code;
  Out.TxGas = Fixture.IntrinsicGas +
              silkwormTxGasFromResult(ExecutionGas, Raw.gas_left, Raw.gas_refund,
                                      Fixture.Revision);

  if (getOpcodeGasTraceConfig().Enabled) {
    dumpOpcodeGasTraceDiff("fixture-exec");
  }

  if (std::getenv("DTVM_DEBUG_TX_GAS") != nullptr) {
    const uint64_t ExecUsed =
        silkwormTxGasFromResult(ExecutionGas, Raw.gas_left, Raw.gas_refund,
                                Fixture.Revision);
    std::cerr << "[tx-gas-debug] revision=" << static_cast<int>(Fixture.Revision)
              << " status=" << static_cast<int>(Raw.status_code)
              << " intrinsic=" << Fixture.IntrinsicGas
              << " execution_gas_limit=" << ExecutionGas
              << " gas_left=" << Raw.gas_left << " gas_refund=" << Raw.gas_refund
              << " exec_used_after_refund=" << ExecUsed
              << " tx_gas=" << Out.TxGas << " expected=" << Fixture.ExpectedTxGas
              << "\n";
  }

  if (Raw.release) {
    Raw.release(&Raw);
  }
  Vm->destroy(Vm);
  return Out;
}

SilkwormGasResult runFixtureViaDtvmApi(const ParsedFixture &Fixture,
                                       const char *ModeValue) {
  if (isCreateFixture(Fixture)) {
    return runFixtureSilkwormHost(
        Fixture, ModeValue[0] == 'm' && ModeValue[1] == 'u'
                           ? common::RunMode::MultipassMode
                           : common::RunMode::InterpMode);
  }
  const common::RunMode HostMode =
      (ModeValue[0] == 'm' && ModeValue[1] == 'u') ? common::RunMode::MultipassMode
                                                   : common::RunMode::InterpMode;
  return runFixtureViaEvmcVm(
      Fixture,
      []() { return evmc_create_dtvmapi(); },
      [ModeValue](evmc_vm *Vm) {
        Vm->set_option(Vm, "mode", ModeValue);
        Vm->set_option(Vm, "enable_gas_metering", "true");
      },
      /*AttachDtvmRuntimeToHost=*/true, HostMode);
}

struct EvmoneLib {
  void *Handle = nullptr;
  evmc_vm *(*Create)() = nullptr;

  bool load() {
    static const char *Candidates[] = {
        "/mnt/data/zhonghao/evmone/build/lib/libevmone.so",
        "/mnt/data/zhonghao/evmone/build/lib/evmone/libevmone.so",
        "/mnt/data/zhonghao/DTVM/libevmone.so",
    };
    for (const char *Path : Candidates) {
      Handle = dlopen(Path, RTLD_NOW);
      if (Handle) {
        Create = reinterpret_cast<evmc_vm *(*)()>(
            dlsym(Handle, "evmc_create_evmone"));
        if (Create) {
          return true;
        }
        dlclose(Handle);
        Handle = nullptr;
      }
    }
    return false;
  }

  ~EvmoneLib() {
    if (Handle) {
      dlclose(Handle);
    }
  }
};

SilkwormGasResult runFixtureViaEvmone(const ParsedFixture &Fixture) {
  static EvmoneLib Lib;
  static const bool Loaded = Lib.load();
  if (!Loaded) {
    SilkwormGasResult Out;
    Out.Error = "evmone shared library not found";
    return Out;
  }
  return runFixtureViaEvmcVm(
      Fixture, []() { return Lib.Create(); }, [](evmc_vm *) {},
      /*AttachDtvmRuntimeToHost=*/true, common::RunMode::InterpMode);
}

void assertExpectedStatus(const std::string &ExpectedStatus,
                          const evmc_status_code ActualStatus) {
  if (ExpectedStatus == "success") {
    EXPECT_EQ(ActualStatus, EVMC_SUCCESS);
    return;
  }
  if (ExpectedStatus == "revert") {
    EXPECT_EQ(ActualStatus, EVMC_REVERT);
    return;
  }
  EXPECT_NE(ActualStatus, EVMC_SUCCESS);
}

} // namespace

TEST(EVMLegacyCallReproTest, EvmoneMatchesCanonicalOn433153Tx0) {
  const ParsedFixture Fixture =
      loadFixture(getLegacyReproFixtureDir() / "block_433153_tx_0.json");
  auto Result = runFixtureViaEvmone(Fixture);
  ASSERT_TRUE(Result.Success) << Result.Error;
  assertExpectedStatus(Fixture.ExpectedStatus, Result.Status);
  EXPECT_EQ(Result.TxGas, Fixture.ExpectedTxGas);
}

// evmone on this fixture currently SIGSEGV in baseline::execute (large prestate).
// Canonical 845701 is checked via receipt in tools/compare_1431916_tx38_gas.sh
// and was verified in Silkworm (builtin evmone, datadir through block 1431915).
TEST(EVMLegacyCallReproTest, EvmoneMatchesCanonicalOn1431916Tx38) {
  const auto FixturePath =
      getLegacyReproFixtureDir() / "block_1431916_prefix_tx_38.json";
  const ParsedFixture Fixture = loadPrefixFixtureAsTarget(FixturePath);
  auto Result = runFixtureViaEvmone(Fixture);
  if (!Result.Success) {
    GTEST_SKIP() << Result.Error;
  }
  if (Result.Status != EVMC_SUCCESS) {
    GTEST_SKIP() << "evmone status=" << static_cast<int>(Result.Status);
  }
  assertExpectedStatus(Fixture.ExpectedStatus, Result.Status);
  EXPECT_EQ(Result.TxGas, Fixture.ExpectedTxGas);
}

TEST(EVMLegacyCallReproTest, DtvmApiInterpreterVsMultipassOn1431916Tx38) {
  const ParsedFixture Fixture =
      loadFixture(getLegacyReproFixtureDir() / "block_1431916_tx_38.json");
  auto Interp = runFixtureViaDtvmApi(Fixture, "interpreter");
  auto Multi = runFixtureViaDtvmApi(Fixture, "multipass");
  ASSERT_TRUE(Interp.Success) << Interp.Error;
  ASSERT_TRUE(Multi.Success) << Multi.Error;
  assertExpectedStatus(Fixture.ExpectedStatus, Interp.Status);
  assertExpectedStatus(Fixture.ExpectedStatus, Multi.Status);
  EXPECT_EQ(Interp.TxGas, Fixture.ExpectedTxGas) << "interpreter gas mismatch";
  EXPECT_EQ(Multi.TxGas, Fixture.ExpectedTxGas) << "multipass gas mismatch";
  EXPECT_EQ(Interp.TxGas, Multi.TxGas) << "interpreter vs multipass gas";
}

TEST(EVMLegacyCallReproTest, DtvmPrefixReplayInterpreterVsMultipassOn1431916Tx38) {
  const auto FixturePath =
      getLegacyReproFixtureDir() / "block_1431916_prefix_tx_38.json";
  const ParsedFixture TargetFixture = loadPrefixFixtureAsTarget(FixturePath);
  auto Interp = runFixtureViaDtvmApi(TargetFixture, "interpreter");
  auto Multi = runFixtureViaDtvmApi(TargetFixture, "multipass");
  ASSERT_TRUE(Interp.Success) << Interp.Error;
  ASSERT_TRUE(Multi.Success) << Multi.Error;
  assertExpectedStatus(TargetFixture.ExpectedStatus, Interp.Status);
  assertExpectedStatus(TargetFixture.ExpectedStatus, Multi.Status);
  EXPECT_EQ(Interp.TxGas, Multi.TxGas) << "interpreter vs multipass gas";
}

TEST(EVMLegacyCallReproTest, SilkwormHostCanonicalGasOn1431916PrefixTx38) {
  const auto FixturePath =
      getLegacyReproFixtureDir() / "block_1431916_prefix_tx_38.json";
  const ParsedFixture Fixture = loadPrefixFixtureAsTarget(FixturePath);
  auto Multi = runFixtureSilkwormHost(Fixture, common::RunMode::MultipassMode);
  ASSERT_TRUE(Multi.Success) << Multi.Error;
  assertExpectedStatus(Fixture.ExpectedStatus, Multi.Status);
  EXPECT_EQ(Multi.TxGas, Fixture.ExpectedTxGas);
}

TEST(EVMLegacyCallReproTest, SilkwormHostInterpCanonicalGasOn1431916FixedPrestate) {
  const auto FixturePath =
      getLegacyReproFixtureDir() / "block_1431916_prefix_tx_38_fixed.json";
  const ParsedFixture Fixture = loadPrefixFixtureAsTarget(FixturePath);
  auto Interp = runFixtureSilkwormHost(Fixture, common::RunMode::InterpMode);
  ASSERT_TRUE(Interp.Success) << Interp.Error;
  assertExpectedStatus(Fixture.ExpectedStatus, Interp.Status);
  EXPECT_EQ(Interp.TxGas, Fixture.ExpectedTxGas);
}

TEST(EVMLegacyCallReproTest, DtvmPrefixReplayCanonicalGasOn1431916Tx38) {
  const auto FixturePath =
      getLegacyReproFixtureDir() / "block_1431916_prefix_tx_38.json";
  const ParsedFixture Fixture = loadPrefixFixtureAsTarget(FixturePath);
  auto Multi = runFixtureViaDtvmApi(Fixture, "multipass");
  ASSERT_TRUE(Multi.Success) << Multi.Error;
  assertExpectedStatus(Fixture.ExpectedStatus, Multi.Status);
  EXPECT_EQ(Multi.TxGas, Fixture.ExpectedTxGas);
}

TEST(EVMLegacyCallReproTest, EvmoneVsDtvmOn1431916Tx38MinimalPrestate) {
  const ParsedFixture Fixture = loadFixture(
      getLegacyReproFixtureDir() / "block_1431916_tx_38_minimal.json");
  auto Evmone = runFixtureViaEvmone(Fixture);
  auto Multi = runFixtureViaDtvmApi(Fixture, "multipass");
  if (!Evmone.Success) {
    GTEST_SKIP() << "evmone: " << Evmone.Error;
  }
  ASSERT_TRUE(Multi.Success) << Multi.Error;
  std::cerr << "[minimal-compare] evmone=" << Evmone.TxGas
            << " dtvm=" << Multi.TxGas << " expected=" << Fixture.ExpectedTxGas
            << " evmone_status=" << static_cast<int>(Evmone.Status)
            << " dtvm_status=" << static_cast<int>(Multi.Status) << "\n";
  EXPECT_EQ(Evmone.Status, Multi.Status);
  EXPECT_EQ(Evmone.TxGas, Multi.TxGas);
  EXPECT_EQ(Multi.TxGas, Fixture.ExpectedTxGas);
}

TEST(EVMLegacyCallReproTest, DtvmPrefixReplayCanonicalGasOn1431916Tx38FixedPrestate) {
  const auto FixturePath =
      getLegacyReproFixtureDir() / "block_1431916_prefix_tx_38_fixed.json";
  const ParsedFixture Fixture = loadPrefixFixtureAsTarget(FixturePath);
  auto Multi = runFixtureViaDtvmApi(Fixture, "multipass");
  ASSERT_TRUE(Multi.Success) << Multi.Error;
  assertExpectedStatus(Fixture.ExpectedStatus, Multi.Status);
  EXPECT_EQ(Multi.TxGas, Fixture.ExpectedTxGas);
}

TEST(EVMLegacyCallReproTest, DtvmApiSilkwormGasOn433153Tx1) {
  const ParsedFixture Fixture =
      loadFixture(getLegacyReproFixtureDir() / "block_433153_tx_1.json");
  auto Interp = runFixtureViaDtvmApi(Fixture, "interpreter");
  auto Multi = runFixtureViaDtvmApi(Fixture, "multipass");
  ASSERT_TRUE(Interp.Success) << Interp.Error;
  ASSERT_TRUE(Multi.Success) << Multi.Error;
  assertExpectedStatus(Fixture.ExpectedStatus, Interp.Status);
  assertExpectedStatus(Fixture.ExpectedStatus, Multi.Status);
  EXPECT_EQ(Interp.TxGas, Fixture.ExpectedTxGas);
  EXPECT_EQ(Multi.TxGas, Fixture.ExpectedTxGas);
}

TEST(EVMLegacyCallReproTest, SilkwormGasAllFixtures) {
  const auto FixtureDir = getLegacyReproFixtureDir();
  const std::vector<std::pair<std::string, uint64_t>> FixtureFiles = {
      {"block_254277_tx_0.json", 57956},
      {"block_254297_tx_0.json", 94849},
      {"block_433153_tx_0.json", 21000},
      {"block_433153_tx_1.json", 587578},
      {"block_1431916_tx_38.json", 845701},
  };

  for (const auto &[Name, CanonicalTxGas] : FixtureFiles) {
    SCOPED_TRACE(Name);
    const ParsedFixture Fixture = loadFixture(FixtureDir / Name);
    EXPECT_EQ(Fixture.ExpectedTxGas, CanonicalTxGas);

    if (Name == "block_433153_tx_1.json") {
      auto Interp = runFixtureViaDtvmApi(Fixture, "interpreter");
      ASSERT_TRUE(Interp.Success) << Interp.Error;
      assertExpectedStatus(Fixture.ExpectedStatus, Interp.Status);
      EXPECT_EQ(Interp.TxGas, CanonicalTxGas);
      auto Multi = runFixtureViaDtvmApi(Fixture, "multipass");
      ASSERT_TRUE(Multi.Success) << Multi.Error;
      assertExpectedStatus(Fixture.ExpectedStatus, Multi.Status);
      EXPECT_EQ(Multi.TxGas, CanonicalTxGas);
      auto Evmone = runFixtureViaEvmone(Fixture);
      ASSERT_TRUE(Evmone.Success) << Evmone.Error;
      assertExpectedStatus(Fixture.ExpectedStatus, Evmone.Status);
      EXPECT_EQ(Evmone.TxGas, CanonicalTxGas);
      continue;
    }

    auto Multi = runFixtureViaDtvmApi(Fixture, "multipass");
    ASSERT_TRUE(Multi.Success) << Multi.Error;
    assertExpectedStatus(Fixture.ExpectedStatus, Multi.Status);
    EXPECT_EQ(Multi.TxGas, CanonicalTxGas)
        << "dtvm multipass fixture=" << Fixture.FixturePath;

    auto Evmone = runFixtureViaEvmone(Fixture);
    ASSERT_TRUE(Evmone.Success) << Evmone.Error;
    assertExpectedStatus(Fixture.ExpectedStatus, Evmone.Status);
    EXPECT_EQ(Evmone.TxGas, CanonicalTxGas)
        << "evmone fixture=" << Fixture.FixturePath;
  }
}
