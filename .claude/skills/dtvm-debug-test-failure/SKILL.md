---
name: dtvm-debug-test-failure
description: Comprehensive test failure debugging and analysis for DTVM. Use when tests fail in any execution mode (interpreter, singlepass JIT, multipass JIT) to analyze failure patterns, extract error logs, and generate debugging reports, and provide fix if possible. Triggers on test failures, ctest errors, or when investigating why specific test cases fail.
allowed-tools: Bash, Read, Grep
---

# Debug Test Failure Skill

This skill helps analyze and debug test failures in DTVM's multi-mode execution environment.

## Quick Start

When a test fails, use this skill to:
1. Parse test output and identify failure patterns
2. Address related code and analyze the cause of the failure
3. Add print in related code or use gdb to verify the cause
4. Provide fix to test failures

## Failure Analysis Workflow

### 1. Identify Test Failure

Understand which test fails and how to run test. Let user provide a command or a script file to run the test, check the test output and search for failure patterns(such as fail, failure, etc). If there's a failure, read error information for it.

### 2. Address the code and understand the test case

Find code that causes the failure. Locate code according to the test command or output. E.g., a gtest failure often prints the file and line, analyze the code in that file and related files and think why the failure happens. Ask user if we're not sure about which file to locate.
Read the test file and see what the test case is doing and what the test expectation is. Make sure the test expectation is reasonable. Ask user if you are not sure where the test file is.
Understand which code is related to the test case. E.g., if a test is about EVM signextend opcode in multipass mode, then read the code about the signextend opcode implementation in multipass(JIT) mode. Ask user if you are not sure where the execution code is.

### 3. Analyze the cause and debug the failure

After analyze the code, we should know what input values cause the failure(ask user to provide input values if we don't know). Then we think why these input values can lead to the test failure. For JIT mode, we will asume the backend is correct at first, the failure mostly occurs in front end(bytecode visitor and mir builder). 
We can add print in code or use gdb non-interactive mode to set breakpoint and print at certain lines to verify our assumption. But it's better to give suggestions to users to let them perform these operations themselves.

#### GDB Debugging with Dynamic Libraries (e.g., libdtvmapi.so)

When debugging tests that use dynamically loaded libraries (like fuzzer tests with `libdtvmapi.so`), breakpoints in the dynamic library cannot be set until the library is loaded. Use this workflow:

**Interactive GDB Session:**
```bash
gdb --args ../build/bin/evmone-fuzzer crash-xxx
```

1. **First `r` (run)**: Let the program run so that `libdtvmapi.so` gets dynamically loaded
2. **Set breakpoint `b EVMAnalyzer::analyze`**: Now the dynamic library is loaded, breakpoints can be set correctly
3. **Second `r` (run)**: Restart the program, it will stop at the breakpoint
4. **Print variables `p Bytecode` and `p BytecodeSize`**: Inspect the actual bytecode content

**Example commands in GDB:**
```
(gdb) r
... program runs and loads libdtvmapi.so ...
(gdb) b EVMAnalyzer::analyze
Breakpoint 1 at 0x...
(gdb) r
... program restarts and stops at breakpoint ...
(gdb) p BytecodeSize
$1 = 3
(gdb) x/3xb Bytecode
0x...: 0x49 0x00 0x00
```

**Key Points:**
- Dynamic libraries are loaded at runtime, not at program start
- Breakpoints in dynamic library functions cannot be set before the library is loaded
- Must run once to load the library, then set breakpoints and run again
- Use `x/Nxb Bytecode` to examine N bytes of bytecode in hex format

#### Getting Correct Bytecode from Fuzzer Crash Files

**Problem:** When analyzing fuzzer crash files, the raw file content is NOT the actual bytecode executed by the VM.

Fuzzer input files (like `crash-xxx`) contain structured data that includes:
- EVM revision information (first few bytes)
- Message parameters (gas, sender, recipient, etc.)
- Host context data
- **The actual bytecode is only a portion of the file, extracted by `populate_input()`**

For example, a crash file might be 27 bytes, but the actual bytecode executed could be only 3 bytes.

**Two Correct Approaches:**

1. **Analyze `populate_input()` in fuzzer.cpp**: Read the fuzzer's input parsing logic to understand how bytecode is extracted from the raw file. The bytecode typically starts at a specific offset after the header fields are parsed.

2. **Use GDB to inspect at runtime**: Break at the point where bytecode is actually used (e.g., `EVMAnalyzer::analyze`) and print `Bytecode` and `BytecodeSize` to get the real values.

**Wrong approach**: Directly parsing the crash file with `xxd` or `hexdump` and assuming all bytes are bytecode - this will lead to incorrect root cause analysis.

**Example of the difference:**
```
# Wrong: Raw file content (27 bytes) - this is NOT the bytecode!
$ xxd crash-xxx
fbff600200000000000000f3fff702ad00f3ff0000000000490012

# Correct: Actual bytecode (3 bytes) - obtained by analyzing populate_input() or using GDB
BytecodeSize = 3
Bytecode: 0x49 0x00 0x00   # BLOBHASH, STOP, STOP
```

**Lesson Learned:** Always understand how the test framework processes input data before analyzing. For evmone-fuzzer, read `populate_input()` in `fuzzer.cpp` to understand the input format, or use GDB to verify the actual values at runtime.

### 4. Fix the test

After understand the cause, provide a fix and ask users if they want to apply it.

## References

- [DTVM Architecture](references/dtvm_architecture.md) - Understanding execution modes
- [Fuzzer Testing](references/fuzzer_testing.md) - Fuzzer testing setup and debugging guide
- [Example](examples.md) - Example of debugging test failure
