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

