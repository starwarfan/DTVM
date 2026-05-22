// Copyright (C) 2026 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Reproduces Silkworm CREATE initcode deployment: outer EVMC_CREATE, inner
// deploy is EVMC_CALL with code_address == 0 (see silkworm/core/execution/evm.cpp).
// Persistent cache on the zero address must not alias different initcodes.

#include "vm/dt_evmc_vm.h"

#include <evmc/evmc.h>
#include <evmc/evmc.hpp>
#include <evmc/mocked_host.hpp>

#include <cstring>
#include <gtest/gtest.h>
#include <vector>

namespace {

using evmc::operator""_address;

void releaseResult(evmc_result &R) {
  if (R.release) {
    R.release(&R);
  }
}

/// Minimal initcode: MSTORE ReturnByte at word MSB (offset 31), RETURN 1 byte.
std::vector<uint8_t> makeInitcode(uint8_t ReturnByte) {
  return {0x60, ReturnByte, 0x60, 0x1f, 0x52,
          0x60, 0x01, 0x60, 0x1f, 0xf3};
}

evmc_message makeSilkwormDeployCall(int64_t Gas, const uint8_t *Initcode,
                                    size_t InitcodeSize) {
  evmc_message Msg{};
  Msg.kind = EVMC_CALL;
  Msg.depth = 0;
  Msg.gas = Gas;
  Msg.sender = 0x0047a8033cc6d6ca2ed5044674fd421f44884de8_address;
  Msg.recipient = 0x5b12101ae40c16aeb3cc6d2fc4c1974a6fdd6e97_address;
  Msg.code_address = {}; // Silkworm deploy_message leaves this zeroed
  Msg.input_data = Initcode;
  Msg.input_size = InitcodeSize;
  return Msg;
}

} // namespace

class EVMCreateDeployCacheTest : public ::testing::Test {
protected:
  void SetUp() override {
    Vm = evmc_create_dtvmapi();
    ASSERT_NE(Vm, nullptr);
    ASSERT_EQ(EVMC_SET_OPTION_SUCCESS,
              Vm->set_option(Vm, "mode", "interpreter"));
    Host = std::make_unique<evmc::MockedHost>();
  }

  void TearDown() override {
    if (Vm) {
      Vm->destroy(Vm);
      Vm = nullptr;
    }
  }

  evmc_vm *Vm = nullptr;
  std::unique_ptr<evmc::MockedHost> Host;
};

TEST_F(EVMCreateDeployCacheTest, ZeroCodeAddressInitcodeNotAliased) {
  const auto InitA = makeInitcode(0x11);
  const auto InitB = makeInitcode(0x22);
  ASSERT_EQ(InitA.size(), InitB.size());

  const int64_t Gas = 2'000'000;
  const evmc_revision Rev = EVMC_HOMESTEAD;

  evmc_message MsgA = makeSilkwormDeployCall(Gas, InitA.data(), InitA.size());
  evmc_result ResA = Vm->execute(
      Vm, &evmc::MockedHost::get_interface(),
      reinterpret_cast<evmc_host_context *>(Host.get()), Rev, &MsgA, InitA.data(),
      InitA.size());
  ASSERT_EQ(ResA.status_code, EVMC_SUCCESS) << "first initcode deploy";
  ASSERT_GT(ResA.output_size, 0U);
  const uint8_t ByteA = ResA.output_data[ResA.output_size - 1];
  releaseResult(ResA);

  evmc_message MsgB = makeSilkwormDeployCall(Gas, InitB.data(), InitB.size());
  evmc_result ResB = Vm->execute(
      Vm, &evmc::MockedHost::get_interface(),
      reinterpret_cast<evmc_host_context *>(Host.get()), Rev, &MsgB, InitB.data(),
      InitB.size());
  ASSERT_EQ(ResB.status_code, EVMC_SUCCESS) << "second initcode deploy";
  ASSERT_GT(ResB.output_size, 0U);
  const uint8_t ByteB = ResB.output_data[ResB.output_size - 1];
  releaseResult(ResB);

  EXPECT_EQ(ByteA, 0x11);
  EXPECT_EQ(ByteB, 0x22);
}
