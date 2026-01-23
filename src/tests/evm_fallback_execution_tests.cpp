// Copyright (C) 2025 the DTVM authors. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vm/dt_evmc_vm.h"
#include <evmc/evmc.h>
#include <evmc/mocked_host.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

using namespace evmc::literals;

inline evmc::bytes operator""_hex(const char *s, size_t size) {
  return evmc::from_spaced_hex({s, size}).value();
}

class EVMFallbackExecutionTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create DTVM using the correct API
    vm = evmc_create_dtvmapi();
    ASSERT_NE(vm, nullptr) << "Failed to create DTVM instance";

    // Initialize mocked host for testing
    host = std::make_unique<evmc::MockedHost>();
  }

  void TearDown() override {
    if (vm) {
      vm->destroy(vm);
      vm = nullptr;
    }
  }

  // Helper method to execute bytecode and return result
  evmc_result executeBytecode(const std::vector<uint8_t> &bytecode,
                              int64_t gas_limit = 1000000) {
    // Create execution message
    evmc_message msg = {};
    msg.kind = EVMC_CALL;
    msg.flags = 0;
    msg.depth = 0;
    msg.gas = gas_limit;
    msg.recipient = {};
    msg.sender = {};
    msg.input_data = nullptr;
    msg.input_size = 0;
    msg.value = {};
    msg.code = bytecode.data();
    msg.code_size = bytecode.size();

    // Execute bytecode using DTVM with correct EVMC API signature
    // The EVMC execute function signature is:
    // evmc_result (*execute)(struct evmc_vm* vm, const struct
    // evmc_host_interface* host,
    //                        struct evmc_host_context* context, enum
    //                        evmc_revision rev, const struct evmc_message* msg,
    //                        const uint8_t* code, size_t code_size)
    return vm->execute(vm, &evmc::MockedHost::get_interface(),
                       reinterpret_cast<evmc_host_context *>(host.get()),
                       EVMC_LATEST_STABLE_REVISION, &msg, bytecode.data(),
                       bytecode.size());
  }

  struct evmc_vm *vm = nullptr;
  std::unique_ptr<evmc::MockedHost> host;
};

// Test 1: Basic 0xEE Fallback Execution Test
TEST_F(EVMFallbackExecutionTest, BasicFallbackExecution) {
#ifdef ZEN_ENABLE_JIT_FALLBACK_TEST
  // Test bytecode: PUSH1 42, FALLBACK(0xEE), STOP
  std::vector<uint8_t> bytecode = {
      0x60, 0x2A, // PUSH1 42
      0xEE,       // FALLBACK trigger
      0x00        // STOP
  };

  evmc_result result = executeBytecode(bytecode);

  // When fallback is triggered, execution should continue in interpreter
  // The exact behavior is succeed because next instruction is STOP
  EXPECT_EQ(result.status_code, EVMC_SUCCESS);

  // Gas should be consumed
  EXPECT_LT(result.gas_left, 1000000); // Some gas should be used

  // Release result resources
  if (result.release) {
    result.release(&result);
  }
#else
  // When fallback testing is disabled, 0xEE should be treated as undefined
  std::vector<uint8_t> bytecode = {
      0x60, 0x2A, // PUSH1 42
      0xEE,       // Should be treated as undefined opcode
      0x00        // STOP (won't be reached)
  };

  evmc_result result = executeBytecode(bytecode);

  // Should result in undefined instruction error
  EXPECT_EQ(result.status_code, EVMC_UNDEFINED_INSTRUCTION);

  // Release result resources
  if (result.release) {
    result.release(&result);
  }
#endif
}

// Test 2: Fallback with Stack Operations
TEST_F(EVMFallbackExecutionTest, FallbackWithStackOperations) {
#ifdef ZEN_ENABLE_JIT_FALLBACK_TEST
  // Test bytecode: PUSH1 10, PUSH1 20, ADD, FALLBACK, PUSH1 5, ADD, STOP
  std::vector<uint8_t> bytecode = {
      0x60, 0x0A, // PUSH1 10
      0x60, 0x14, // PUSH1 20
      0x01,       // ADD (stack: [30])
      0xEE,       // FALLBACK trigger
      0x60, 0x05, // PUSH1 5 (should execute in interpreter)
      0x01,       // ADD (stack: [35])
      0x00        // STOP
  };

  evmc_result result = executeBytecode(bytecode);

  // Execution should succeed with fallback
  EXPECT_TRUE(result.status_code == EVMC_SUCCESS);

  // Verify gas consumption
  EXPECT_LT(result.gas_left, 1000000);

  // Release result resources
  if (result.release) {
    result.release(&result);
  }
#else
  GTEST_SKIP() << "ZEN_ENABLE_JIT_FALLBACK_TEST not enabled";
#endif
}

// Test 3: Multiple Fallback Triggers
TEST_F(EVMFallbackExecutionTest, MultipleFallbackTriggers) {
#ifdef ZEN_ENABLE_JIT_FALLBACK_TEST
  // Test bytecode with multiple 0xEE triggers
  std::vector<uint8_t> bytecode = {
      0x60, 0x01, // PUSH1 1
      0xEE,       // FALLBACK 1
      0x60, 0x02, // PUSH1 2
      0x01,       // ADD
      0xEE,       // FALLBACK 2
      0x60, 0x03, // PUSH1 3
      0x01,       // ADD
      0x00        // STOP
  };

  evmc_result result = executeBytecode(bytecode);

  // Should handle multiple fallbacks
  EXPECT_EQ(result.status_code, EVMC_INVALID_INSTRUCTION);

  // Verify gas consumption
  EXPECT_LT(result.gas_left, 1000000);

  // Release result resources
  if (result.release) {
    result.release(&result);
  }
#else
  GTEST_SKIP() << "ZEN_ENABLE_JIT_FALLBACK_TEST not enabled";
#endif
}

// Test 4: Fallback at Different PC Positions
TEST_F(EVMFallbackExecutionTest, FallbackAtDifferentPositions) {
#ifdef ZEN_ENABLE_JIT_FALLBACK_TEST
  struct TestCase {
    std::vector<uint8_t> bytecode;
    std::string description;
  };

  std::vector<TestCase> test_cases = {
      {{0xEE, 0x00}, // FALLBACK at PC=0, STOP
       "Fallback at beginning"},
      {{0x60, 0x01, 0xEE, 0x00}, // PUSH1 1, FALLBACK at PC=2, STOP
       "Fallback after PUSH"},
      {{0x60, 0x01, 0x60, 0x02, 0x01, 0xEE,
        0x00}, // PUSH1 1, PUSH1 2, ADD, FALLBACK at PC=5, STOP
       "Fallback after arithmetic"}};

  for (const auto &test_case : test_cases) {
    evmc_result result = executeBytecode(test_case.bytecode);

    // Each case should handle fallback appropriately
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);

    // Release result resources
    if (result.release) {
      result.release(&result);
    }
  }
#else
  GTEST_SKIP() << "ZEN_ENABLE_JIT_FALLBACK_TEST not enabled";
#endif
}

// Test 5: Fallback with Memory Operations
TEST_F(EVMFallbackExecutionTest, FallbackWithMemoryOperations) {
#ifdef ZEN_ENABLE_JIT_FALLBACK_TEST
  // Test bytecode: PUSH1 0x42, PUSH1 0, MSTORE, FALLBACK, PUSH1 0, MLOAD, STOP
  std::vector<uint8_t> bytecode = {
      0x60, 0x42, // PUSH1 0x42
      0x60, 0x00, // PUSH1 0
      0x52,       // MSTORE (store 0x42 at memory position 0)
      0x60, 0x03,
      0xEE,       // FALLBACK trigger
      0x60, 0x20, // PUSH1 0x20
      0x60, 0x00, // PUSH1 0
      0xF3        // RETURN
  };

  evmc_result result = executeBytecode(bytecode);

  // Memory operations should work across fallback
  EXPECT_EQ(result.status_code, EVMC_SUCCESS);
  EXPECT_EQ(result.output_size, 32);
  EXPECT_EQ(
      evmc::bytes_view(&result.output_data[0], 32),
      "0000000000000000000000000000000000000000000000000000000000000042"_hex);

  // Release result resources
  if (result.release) {
    result.release(&result);
  }
#else
  GTEST_SKIP() << "ZEN_ENABLE_JIT_FALLBACK_TEST not enabled";
#endif
}

// Test 6: Fallback Gas Consumption Test
TEST_F(EVMFallbackExecutionTest, FallbackGasConsumption) {
#ifdef ZEN_ENABLE_JIT_FALLBACK_TEST
  // Test that fallback operations consume appropriate gas

  // Bytecode without fallback
  std::vector<uint8_t> normal_bytecode = {
      0x60, 0x01, // PUSH1 1
      0x60, 0x02, // PUSH1 2
      0x01,       // ADD
      0x00        // STOP
  };

  // Bytecode with fallback
  std::vector<uint8_t> fallback_bytecode = {
      0x60, 0x01, // PUSH1 1
      0xEE,       // FALLBACK
      0x60, 0x02, // PUSH1 2
      0x01,       // ADD
      0x00        // STOP
  };

  evmc_result normal_result = executeBytecode(normal_bytecode);
  evmc_result fallback_result = executeBytecode(fallback_bytecode);

  // Both should succeed (or both fail consistently)
  EXPECT_EQ(normal_result.status_code, EVMC_SUCCESS);
  EXPECT_EQ(fallback_result.status_code, EVMC_SUCCESS);

  // Fallback might consume different gas due to interpreter switch
  EXPECT_GT(normal_result.gas_left, 0);
  EXPECT_GT(fallback_result.gas_left, 0);

  // Release result resources
  if (normal_result.release) {
    normal_result.release(&normal_result);
  }
  if (fallback_result.release) {
    fallback_result.release(&fallback_result);
  }
#else
  GTEST_SKIP() << "ZEN_ENABLE_JIT_FALLBACK_TEST not enabled";
#endif
}

// Test 7: Fallback Error Handling
TEST_F(EVMFallbackExecutionTest, FallbackErrorHandling) {
#ifdef ZEN_ENABLE_JIT_FALLBACK_TEST
  // Test fallback behavior with stack underflow after fallback
  std::vector<uint8_t> bytecode = {
      0x60, 0x01, // PUSH1 1
      0x50,       // POP
      0xEE,       // FALLBACK
      0x50,       // POP (should cause stack underflow)
      0x00        // STOP
  };

  evmc_result result = executeBytecode(bytecode);

  // Should handle stack underflow appropriately
  EXPECT_TRUE(result.status_code == EVMC_STACK_UNDERFLOW ||
              result.status_code == EVMC_UNDEFINED_INSTRUCTION);

  // Release result resources
  if (result.release) {
    result.release(&result);
  }
#else
  GTEST_SKIP() << "ZEN_ENABLE_JIT_FALLBACK_TEST not enabled";
#endif
}

// Test 8: Comprehensive Fallback Workflow Test
TEST_F(EVMFallbackExecutionTest, ComprehensiveFallbackWorkflow) {
#ifdef ZEN_ENABLE_JIT_FALLBACK_TEST
  // Complex bytecode testing complete fallback workflow
  std::vector<uint8_t> complex_bytecode = {
      0x60, 0x10, // PUSH1 16      (PC = 0, 1)
      0x60, 0x20, // PUSH1 32      (PC = 2, 3)
      0x01,       // ADD           (PC = 4)     -> stack: [48]
      0x80,       // DUP1          (PC = 5)     -> stack: [48, 48]
      0x60, 0x00, // PUSH1 0       (PC = 6, 7)  -> stack: [48, 48, 0]
      0x52, // MSTORE        (PC = 8)     -> store 48 at memory[0], stack: [48]
      0xEE, // FALLBACK      (PC = 9)     -> should continue from PC = 10
      0x60, 0x05, // PUSH1 5       (PC = 10, 11) -> stack: [48, 5]
      0x01,       // ADD           (PC = 12)    -> stack: [53]
      0x60, 0x00, // PUSH1 0       (PC = 13, 14) -> stack: [53, 0]
      0x51, // MLOAD         (PC = 15)    -> load from memory[0], stack: [53,
            // 48]
      0x90, // SWAP1         (PC = 16)    -> stack: [48, 53]
      0x60, 0x20, // PUSH1 0x20
      0x60, 0x00, // PUSH1 0
      0xF3        // RETURN
  };

  evmc_result result = executeBytecode(
      complex_bytecode, 2000000); // More gas for complex operations

  // Should execute the complete workflow
  EXPECT_EQ(result.status_code, EVMC_SUCCESS);

  // Verify significant gas consumption
  EXPECT_LT(result.gas_left, 2000000);

  // If successful, verify no output (STOP doesn't return data)
  EXPECT_EQ(result.status_code, EVMC_SUCCESS);
  EXPECT_EQ(result.output_size, 32);
  EXPECT_EQ(
      evmc::bytes_view(&result.output_data[0], 32),
      "0000000000000000000000000000000000000000000000000000000000000035"_hex);

  // Release result resources
  if (result.release) {
    result.release(&result);
  }
#else
  GTEST_SKIP() << "ZEN_ENABLE_JIT_FALLBACK_TEST not enabled";
#endif
}
