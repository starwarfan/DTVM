---
name: debug-test-failure
description: Comprehensive test failure debugging and analysis for DTVM. Use when tests fail in any execution mode (interpreter, singlepass JIT, multipass JIT) to analyze failure patterns, extract error logs, and generate debugging reports, and provide fix if possible. Triggers on test failures, ctest errors, or when investigating why specific test cases fail.
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

### 4. Fix the test

After understand the cause, provide a fix and ask users if they want to apply it.

## References

- [DTVM Architecture](references/dtvm_architecture.md) - Understanding execution modes
- [Example](examples.md) - Example of debugging test failure
