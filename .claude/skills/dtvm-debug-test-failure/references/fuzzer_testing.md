# Fuzzer Testing Guide

DTVM's fuzzer is implemented by integrating with evmone's fuzzer test framework. We use the evmone fork (https://github.com/DTVMStack/evmone) to perform fuzz testing by loading DTVM's library (libdtvmapi.so).

## Environment Setup

### 1. Clone evmone fork

```bash
git clone https://github.com/DTVMStack/evmone
cd evmone
```

### 2. CMake Configuration

```bash
cmake -S . -B build -DEVMONE_TESTING=ON -DEVMONE_FUZZING=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=/opt/llvm17/bin/clang-17 \
  -DCMAKE_CXX_COMPILER=/opt/llvm17/bin/clang++
```

**Note**: Clang 17 or above is required. You can download the binary directly.

### 3. Build

```bash
cmake --build build -j16
```

## Running Fuzz Tests

### 1. Configure libdtvmapi.so Path and Mode

```bash
export EVMONE_EXTERNAL_OPTIONS="/path/to/libdtvmapi.so,mode=multipass"
```

Supported modes:
- `mode=interpreter`: Interpreter mode
- `mode=singlepass`: Singlepass JIT mode (Note: Not supported for EVM in DTVM)
- `mode=multipass`: Multipass JIT mode

**Important**: DTVM does not support singlepass JIT mode for EVM execution. When fuzzing EVM code, only use `mode=interpreter` or `mode=multipass`.

### 2. Create Fuzz Output Directory

```bash
mkdir fuzz_multipass
cd fuzz_multipass
```

### 3. Run Fuzzer

```bash
evmone/build/bin/evmone-fuzzer -jobs=64
```

After running, the fuzz directory may contain the following types of failure cases:
- `crash-xxx`: Crash cases
- `timeout-xxx`: Timeout cases
- `slow-xxx`: Slow execution cases

## Debugging Fuzz Test Cases

### Single Test Case Debugging

For a single fuzz test case (e.g., `crash-0be3c`), run it individually:

```bash
export EVMONE_EXTERNAL_OPTIONS="/path/to/libdtvmapi.so,mode=multipass"
evmone/build/bin/evmone-fuzzer crash-0be3c
```

Analyze the output to trace the root cause of the error.

### GDB Debugging for Fuzz Test Cases

Since `libdtvmapi.so` is dynamically loaded, use this special GDB workflow:

```bash
gdb --args evmone/build/bin/evmone-fuzzer crash-xxx
```

**Steps:**
1. **First `r` (run)**: Let the program run until `libdtvmapi.so` is dynamically loaded
2. **Set breakpoint `b EVMAnalyzer::analyze`**: After the dynamic library loads, breakpoints can be set correctly
3. **Second `r` (run)**: Restart the program; it will stop at the breakpoint
4. **Print variables**: Use `p Bytecode` and `p BytecodeSize` to view the actual bytecode content

**Example commands:**
```
(gdb) r
... program runs and loads libdtvmapi.so ...
(gdb) b EVMAnalyzer::analyze
Breakpoint 1 at 0x...
(gdb) r
... program stops at breakpoint ...
(gdb) p BytecodeSize
$1 = 3
(gdb) x/3xb Bytecode
0x...: 0x49 0x00 0x00
```

### Obtaining Correct Bytecode

**Important**: The raw content of fuzzer crash files is NOT the actual bytecode executed by the VM.

Fuzzer input files contain structured data including:
- EVM revision information (first few bytes)
- Message parameters (gas, sender, recipient, etc.)
- Host context data
- **The actual bytecode is only a portion of the file, extracted by `populate_input()`**

**Correct Approaches:**

1. **Analyze `populate_input()` function**: Read the input parsing logic in fuzzer.cpp to understand how bytecode is extracted from the raw file

2. **Use GDB for runtime inspection**: Set a breakpoint where bytecode is actually used (e.g., `EVMAnalyzer::analyze`) and print `Bytecode` and `BytecodeSize`

**Incorrect approach**: Directly parsing the crash file with `xxd` or `hexdump` and assuming all bytes are bytecode - this leads to incorrect root cause analysis.

## Common Issues

### Dynamic Library Loading Failure

If you see an error like:
```
Failed to load external VM: libdtvmapi.so
```

Check:
1. Is the `libdtvmapi.so` path correct?
2. Can all dependent libraries be found? (Use `ldd libdtvmapi.so` to check)
3. Does `LD_LIBRARY_PATH` include the necessary library paths?
