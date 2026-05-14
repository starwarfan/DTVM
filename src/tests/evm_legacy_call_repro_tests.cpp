// Copyright (C) 2026 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

#include "evm/evm.h"
#include "evm_test_host.hpp"
#include "runtime/runtime.h"
#include "utils/evm.h"
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

  auto Host = std::make_unique<ZenMockedEVMHost>();
  Host->loadInitialState(Fixture.TxContext, Fixture.Accounts, true);
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
      EXPECT_EQ(Result.GasCharged, Fixture.ExpectedDTVMInterpGas)
          << "fixture=" << Fixture.FixturePath << " mode=interpreter";
    }

#ifdef ZEN_ENABLE_MULTIPASS_JIT
    {
      auto Result = runFixture(Fixture, common::RunMode::MultipassMode);
      ASSERT_TRUE(Result.Success) << Result.ErrorMessage;
      assertExpectedStatus(Fixture.ExpectedStatus, Result.Status);
      EXPECT_EQ(Result.GasCharged, Fixture.ExpectedDTVMMultipassGas)
          << "fixture=" << Fixture.FixturePath << " mode=multipass";
    }
#endif
  }
}
