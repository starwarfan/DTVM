# Debug Test Failure - Examples

This document provides usage examples for the debug-test-failure skill.

## Example 1: Multipass Mode evmone unittest failure

### User Request
```
The multipass test "./build/bin/evmone-unittests --gtest_filter=*evm.undefined_instructions*external*" fails. Help me debug it.
```

### Test Command
According to .ci/run_test_suite.sh, the evmone multipass test command is "./run_unittests.sh ../tests/evmone_unittests/EVMOneMultipassUnitTestsRunList.txt mode=multipass", so we read the run_unittests.sh, the command is `./build/bin/evmone-unittests --gtest_filter="$FILTER_PARAM"`, which matches the user request, and we need export environment variable for it.

```bash
export EVMONE_OPTIONS=mode=multipass
cd evmone
./build/bin/evmone-unittests --gtest_filter=*evm.undefined_instructions*external*
```

### Expected Output
All [ PASSED ], no failures in output.

### Analysis
There're several failures in output:
```
evm_test.cpp:637: Failure
Expected equality of these values:
  res.status_code
    Which is: stack underflow
  EVMC_UNDEFINED_INSTRUCTION
    Which is: undefined instruction
 for opcode 1b on revision Frontier
```

First we read the evm_test.cpp, and read the code related to it, understand how it runs. We see the evm_test.cpp is built in evmone directory, and it will load the library libdtvmapi.so which is built in DTVM. From the test name, we know it's related to undefined instructions, the result of undefined instruction is not correct. From the evm_test.cpp, we know the evm bytecode contains an undefined opcode. From the output, we see that the actual result is stack underflow rather than undefined instruction.
Then we read the code in DTVM, the entrance is dt_evmc_vm.cpp, since we will asume the backend is correct, the failure is in front end, we read evm_bytecode_visitor.h, the actual result is stack underflow, so it is probably caused by `Builder.handleTrap(common::ErrorCode::EVMStackUnderflow);`. And in EVMMirBuilder::createStackCheckBlock, the stack underflow can also be caused by MIR instruction, if the stack size is less than MinSize. But the expect output is undefined instruction, which is caused by `Builder.handleUndefined();` in evm_bytecode_visitor.h. So probable reason is that the underflow check is before the undefined instruction check.
So we add print statement before `Builder.createStackCheckBlock` in evm_bytecode_visitor.h to see the min size requirement and verify that the min size is not correct. Finally we see that the evm_analyzer.h does not process revision, it use same opcode tables for all bytecode revision, so the min size requirement is not correct and undefined instruction should be returned before the min size check.

### Fix
We need check undefined instruction in evm_analyzer.h, since it's a big change, just tell user the reason of failure and ask if we need fix it.

---

## Example 2: Fuzzer Blockhash Recording Mismatch

### User Request
```
Analyze fuzzer crash file crash-0be3bc84feec8e8e36c6d55f1ac44cfd11d2213c
```

### Error Pattern
```
ASSERTION FAILED: "ref_host.recorded_blockhashes.size() == host.recorded_blockhashes.size()"
	with 2 != 1
```

### Root Cause Analysis

**The Issue**: DTVM calls `get_block_hash()` 1 time, while the reference evmone calls it 2 times.

**Key Insight**: Fuzzer tests compare host call counts (like `recorded_blockhashes`). When DTVM caches results internally but evmone does not, the counts mismatch even if execution is logically correct.

### Debugging Steps

#### 1. Extract Actual Bytecode from Crash File

**Wrong approach**: Parsing raw file bytes directly.
```bash
# This is WRONG - includes 24-byte header
$ xxd crash-0be3bc84
80 e6 00 00 00 01 00 50 38 38 38 00 00 01 00 00...
```

**Correct approach**: Use GDB at `EVMAnalyzer::analyze` to get actual bytecode.
```bash
gdb --args evmone-fuzzer crash-0be3bc84
(gdb) r              # Let libdtvmapi.so load
(gdb) b EVMAnalyzer::analyze
(gdb) r              # Restart and stop at breakpoint
(gdb) p BytecodeSize
$1 = 20
(gdb) x/20xb Bytecode
0x...: 0x38 0x38 0x40 0x38... 0x40 0x01 0x00
```

Actual bytecode: `38 38 40 38 38 38 38 38 38 38 38 38 38 38 38 38 38 40 01 00` (20 bytes)

#### 2. Disassemble Bytecode
```
Offset  Opcode  Name        Stack Effect
----------------------------------------
 0      0x38    CODESIZE    Push: 1
 1      0x38    CODESIZE    Push: 1
 2      0x40    BLOCKHASH   Pop: 1, Push: 1  <-- First BLOCKHASH
 3-16   0x38    CODESIZE    (13 times)
17      0x40    BLOCKHASH   Pop: 1, Push: 1  <-- Second BLOCKHASH
18      0x01    ADD         Pop: 2, Push: 1
19      0x00    STOP
```

Two BLOCKHASH opcodes at offsets 2 and 17. Both should use the same block number.

#### 3. Check evmone Recording Behavior

In `evmc/mocked_host.hpp`:
```cpp
bytes32 get_block_hash(int64_t block_number) const noexcept override
{
    recorded_blockhashes.emplace_back(block_number);  // ALWAYS records
    return block_hash;
}
```

evmone records every host call unconditionally.

#### 4. Check DTVM Implementation

In `src/compiler/evm_frontend/evm_imported.cpp`:
```cpp
const uint8_t *evmGetBlockHash(zen::runtime::EVMInstance *Instance,
                               int64_t BlockNumber) {
  auto &Cache = Instance->getMessageCache();
  auto It = Cache.BlockHashes.find(BlockNumber);
  if (It == Cache.BlockHashes.end()) {
    // First call - calls host
    evmc::bytes32 Hash = Module->Host->get_block_hash(BlockNumber);
    Cache.BlockHashes[BlockNumber] = Hash;  // Cache result
    return Cache.BlockHashes[BlockNumber].bytes;
  }
  return It->second.bytes;  // Second call - cached, NO host call!
}
```

**Problem Identified**: DTVM caches blockhash results. Second BLOCKHASH with same block number returns cached value without calling host. Fuzzer sees 1 host call, expects 2.

### The Fix

Remove caching from `evmGetBlockHash()` to match evmone behavior:
```cpp
const uint8_t *evmGetBlockHash(zen::runtime::EVMInstance *Instance,
                               int64_t BlockNumber) {
  // Always call host to match evmone recording behavior
  evmc::bytes32 Hash = Module->Host->get_block_hash(BlockNumber);
  return ...;  // Return without caching
}
```

### Key Lessons

1. **Fuzzer tests compare host call counts**, not just execution results. Any caching that skips host calls causes mismatches.

2. **Always use GDB to extract bytecode** from fuzzer crash files - raw file contains headers, not just bytecode.

3. **For dynamic libraries**: Must run once to load `libdtvmapi.so`, then set breakpoints, then run again.

4. **Cache behavior matters for fuzzer compatibility**: Even if caching is logically correct, it may break test expectations.

---

