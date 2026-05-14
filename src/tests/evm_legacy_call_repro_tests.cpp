// Copyright (C) 2026 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

#include "evm/evm.h"
#include "evm_test_host.hpp"
#include "runtime/runtime.h"
#include "utils/evm.h"
#include "vm/dt_evmc_vm.h"
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
  uint64_t ExpectedDTVMInterpGas = 0;
  uint64_t ExpectedDTVMMultipassGas = 0;
  std::unordered_map<int64_t, evmc::bytes32> BlockHashes;
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

evmc_revision parseRevision(const std::string &Revision) {
  if (Revision == "EVMC_FRONTIER")
    return EVMC_FRONTIER;
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

std::filesystem::path getLegacyReproFixtureDir() {
  return std::filesystem::path(__FILE__).parent_path() /
         std::filesystem::path("../../tests/evm/fixtures/legacy_call_repro");
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
  Fixture.Revision = parseRevision(Doc["revision"].GetString());

  const auto &Tx = Doc["tx"];
  const auto &Env = Doc["env"];
  const auto &Prestate = Doc["prestate"];
  const auto &Expected = Doc["expected"];

  const std::string From = Tx["from"].GetString();
  const std::string To = Tx["to"].GetString();
  const std::string InputHex = Tx["input"].GetString();

  Fixture.GasLimit = Tx["gas_limit"].GetUint64();
  Fixture.Input = zen::utils::hexToBytes(InputHex);

  Fixture.TxContext.tx_gas_price =
      zen::utils::parseUint256(Tx["gas_price"].GetString());
  Fixture.TxContext.block_number = Env["block_number"].GetUint64();
  Fixture.TxContext.block_timestamp = Env["block_timestamp"].GetUint64();
  Fixture.TxContext.block_coinbase =
      zen::utils::parseAddress(Env["block_coinbase"].GetString());
  Fixture.TxContext.block_prev_randao =
      zen::utils::parseUint256(Env["block_prev_randao"].GetString());
  Fixture.TxContext.block_gas_limit = Env["block_gas_limit"].GetUint64();
  Fixture.TxContext.block_base_fee =
      zen::utils::parseUint256(Env["block_base_fee"].GetString());
  Fixture.TxContext.tx_origin =
      zen::utils::parseAddress(Env["tx_origin"].GetString());
  if (Env.HasMember("block_hash") && Env["block_hash"].IsString()) {
    // Parsed later into host.block_hash (MockedHost has single block_hash slot).
  }
  if (Env.HasMember("block_hashes") && Env["block_hashes"].IsObject()) {
    for (auto It = Env["block_hashes"].MemberBegin();
         It != Env["block_hashes"].MemberEnd(); ++It) {
      int64_t BlockNum = std::stoll(It->name.GetString());
      Fixture.BlockHashes[BlockNum] =
          zen::utils::parseBytes32(It->value.GetString());
    }
  }

  Fixture.Message = {};
  Fixture.Message.kind = EVMC_CALL;
  Fixture.Message.flags = 0u;
  Fixture.Message.depth = 0;
  Fixture.Message.gas = static_cast<int64_t>(Fixture.GasLimit);
  Fixture.Message.recipient = zen::utils::parseAddress(To);
  Fixture.Message.sender = zen::utils::parseAddress(From);
  Fixture.Message.value = zen::utils::parseUint256(Tx["value"].GetString());
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
    Entry.Account.balance =
        zen::utils::parseUint256(AccountVal["balance"].GetString());
    Entry.Account.nonce = AccountVal["nonce"].GetUint64();
    Entry.Account.code = zen::utils::hexToBytes(AccountVal["code"].GetString());

    const auto &Storage = AccountVal["storage"];
    for (auto Sit = Storage.MemberBegin(); Sit != Storage.MemberEnd(); ++Sit) {
      evmc::StorageValue SV{};
      SV.current = zen::utils::parseBytes32(Sit->value.GetString());
      Entry.Account.storage[zen::utils::parseBytes32(Sit->name.GetString())] = SV;
    }

    if (AddressStr == To) {
      Fixture.Bytecode = Entry.Account.code;
    }

    Fixture.Accounts.push_back(std::move(Entry));
  }

  Fixture.ExpectedStatus = Expected["status"].GetString();
  Fixture.ExpectedTxGas = Expected["tx_gas"].GetUint64();
  Fixture.ExpectedDTVMInterpGas =
      Expected.HasMember("dtvm_interpreter_gas")
          ? Expected["dtvm_interpreter_gas"].GetUint64()
          : Fixture.ExpectedTxGas;
  Fixture.ExpectedDTVMMultipassGas =
      Expected.HasMember("dtvm_multipass_gas")
          ? Expected["dtvm_multipass_gas"].GetUint64()
          : Fixture.ExpectedTxGas;
  Fixture.IntrinsicGas = zen::utils::computeIntrinsicGas(
      Fixture.Revision, EVMC_CALL, Fixture.Message.input_data,
      Fixture.Message.input_size);

  return Fixture;
}

ZenMockedEVMHost::TransactionExecutionResult runFixture(
    const ParsedFixture &Fixture, common::RunMode Mode) {
  RuntimeConfig Config;
  Config.Format = common::InputFormat::EVM;
  Config.Mode = Mode;
  Config.EnableEvmGasMetering = true;

  auto Host = std::make_unique<FixtureHost>();
  Host->loadInitialState(Fixture.TxContext, Fixture.Accounts, true);
  // Most legacy contracts use BLOCKHASH(block.number-1); mocked host exposes
  // one block_hash value for all get_block_hash() queries.
  const auto DocPath = std::filesystem::path(Fixture.FixturePath);
  std::ifstream F(DocPath);
  rapidjson::IStreamWrapper ISW(F);
  rapidjson::Document D;
  D.ParseStream(ISW);
  if (D.IsObject() && D.HasMember("env") && D["env"].IsObject() &&
      D["env"].HasMember("block_hash") && D["env"]["block_hash"].IsString()) {
    Host->block_hash =
        zen::utils::parseBytes32(D["env"]["block_hash"].GetString());
  }
  Host->BlockHashOverrides = Fixture.BlockHashes;
  auto RT = Runtime::newEVMRuntime(Config, Host.get());
  EXPECT_TRUE(RT != nullptr);
  Host->setRuntime(RT.get());

  ZenMockedEVMHost::TransactionExecutionConfig ExecConfig;
  ExecConfig.ModuleName = Fixture.CaseName + "-" +
                          (Mode == common::RunMode::InterpMode ? "interp"
                                                                : "multipass");
  ExecConfig.Bytecode = reinterpret_cast<const uint8_t *>(Fixture.Bytecode.data());
  ExecConfig.BytecodeSize = Fixture.Bytecode.size();
  ExecConfig.Message = Fixture.Message;
  ExecConfig.GasLimit = Fixture.GasLimit;
  ExecConfig.IntrinsicGas = Fixture.IntrinsicGas;
  ExecConfig.Revision = Fixture.Revision;

  return Host->executeTransaction(ExecConfig);
}

struct VmExecutionResult {
  bool Success = false;
  evmc_status_code Status = EVMC_INTERNAL_ERROR;
  uint64_t GasCharged = 0;
};

VmExecutionResult runFixtureViaDTVMApi(const ParsedFixture &Fixture,
                                       const char *ModeValue) {
  auto Host = std::make_unique<FixtureHost>();
  Host->loadInitialState(Fixture.TxContext, Fixture.Accounts, true);
  Host->BlockHashOverrides = Fixture.BlockHashes;
  auto Vm = evmc_create_dtvmapi();
  EXPECT_NE(Vm, nullptr);
  if (!Vm) {
    return {};
  }
  Vm->set_option(Vm, "mode", ModeValue);
  Vm->set_option(Vm, "enable_gas_metering", "true");

  evmc_message Msg = Fixture.Message;
  Msg.gas = static_cast<int64_t>(Fixture.GasLimit);

  const auto To = Msg.recipient;
  const auto It = Host->accounts.find(To);
  if (It == Host->accounts.end()) {
    Vm->destroy(Vm);
    return {};
  }
  const auto &Code = It->second.code;
  evmc_result Raw = Vm->execute(
      Vm, &evmc::MockedHost::get_interface(),
      reinterpret_cast<evmc_host_context *>(Host.get()), Fixture.Revision, &Msg,
      Code.data(), Code.size());

  VmExecutionResult Result;
  Result.Success = true;
  Result.Status = Raw.status_code;
  if (Raw.gas_left >= 0) {
    Result.GasCharged = Fixture.GasLimit - static_cast<uint64_t>(Raw.gas_left);
  }
  if (Raw.release) {
    Raw.release(&Raw);
  }
  Vm->destroy(Vm);
  return Result;
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

TEST(EVMLegacyCallReproTest, ExecuteFixturesInInterpreterAndMultipass) {
  const auto FixtureDir = getLegacyReproFixtureDir();
  const std::vector<std::pair<std::string, uint64_t>> FixtureFiles = {
      {"block_254277_tx_0.json", 57956},
      {"block_254297_tx_0.json", 94849},
  };

  for (const auto &[Name, CanonicalTxGas] : FixtureFiles) {
    SCOPED_TRACE(Name);
    const ParsedFixture Fixture = loadFixture(FixtureDir / Name);
    EXPECT_EQ(Fixture.ExpectedTxGas, CanonicalTxGas);

    {
      auto Result = runFixture(Fixture, common::RunMode::InterpMode);
      ASSERT_TRUE(Result.Success) << Result.ErrorMessage;
      assertExpectedStatus(Fixture.ExpectedStatus, Result.Status);
      EXPECT_GT(Result.GasCharged, 0U)
          << "fixture=" << Fixture.FixturePath << " mode=interpreter";
    }

#ifdef ZEN_ENABLE_MULTIPASS_JIT
    {
      auto Result = runFixture(Fixture, common::RunMode::MultipassMode);
      ASSERT_TRUE(Result.Success) << Result.ErrorMessage;
      assertExpectedStatus(Fixture.ExpectedStatus, Result.Status);
      EXPECT_GT(Result.GasCharged, 0U)
          << "fixture=" << Fixture.FixturePath << " mode=multipass";
    }
#endif
  }
}

TEST(EVMLegacyCallReproTest, ExecuteFixturesViaDTVMApi) {
  const auto FixtureDir = getLegacyReproFixtureDir();
  const std::vector<std::string> FixtureFiles = {"block_254277_tx_0.json"};
  for (const auto &Name : FixtureFiles) {
    SCOPED_TRACE(Name);
    const ParsedFixture Fixture = loadFixture(FixtureDir / Name);
    auto Interp = runFixtureViaDTVMApi(Fixture, "interpreter");
    auto Multi = runFixtureViaDTVMApi(Fixture, "multipass");
    ASSERT_TRUE(Interp.Success);
    ASSERT_TRUE(Multi.Success);
    EXPECT_EQ(Interp.Status, EVMC_SUCCESS);
    EXPECT_EQ(Multi.Status, EVMC_SUCCESS);
    EXPECT_EQ(Interp.GasCharged, Multi.GasCharged);
  }
}
