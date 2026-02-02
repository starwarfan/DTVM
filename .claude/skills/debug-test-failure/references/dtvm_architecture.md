# DTVM Architecture Reference

## Execution Modes Overview

DTVM provides three execution modes, each with distinct characteristics:

### 1. Interpreter Mode
- Direct bytecode interpretation
- Highest compatibility, lowest performance
- No compilation overhead
- Best for debugging and verification

### 2. Singlepass JIT Mode
- Fast single-pass compilation
- No LLVM dependency
- Moderate performance improvement
- Good for quick execution without heavy optimization

### 3. Multipass JIT Mode (LLVM-based)
- Multiple optimization passes
- Requires LLVM 15
- Highest performance
- Two sub-modes:
  - **FLAT (Function Level fAst Transpile)**: Fast compilation
  - **FLAS (Function Level Adaptive hot-Switching)**: Adaptive optimization

## Compilation Pipeline

```
Input (Wasm/EVM) → Frontend → dMIR → Execution Modes → Native Code
                              ↓
                    Deterministic MIR
```

### Key Components

**dMIR (Deterministic MIR)**
- Middle Intermediate Representation
- Ensures deterministic behavior
- Common IR for all execution modes
- Platform-independent

**Frontend**
- Wasm frontend: Parses WebAssembly binary/text format
- EVM frontend: Parses EVM bytecode
- Validation and normalization

**Target Code Generation**
- Interpreter: Direct dMIR execution
- Singlepass JIT: Assembly code generation
- Multipass JIT: LLVM IR generation → Machine code

## Memory Management

### Memory Pool System
- Uses mmap for large allocations
- Reduces fragmentation
- Supports deterministic allocation patterns
- Configurable pool sizes

### Deterministic Allocation
- Same input always produces same memory layout
- No reliance on malloc/free ordering
- Predictable address space usage
- Critical for consensus systems

## JIT Implementation Details

### Singlepass JIT
- Linear scan register allocation
- Minimal optimization passes
- Direct dMIR to assembly translation
- Fallback mechanism for unsupported operations

### Multipass JIT
- LLVM-based optimization pipeline
- Typical passes:
  - Dead code elimination
  - Constant folding
  - Common subexpression elimination
  - Loop optimizations
- Hotness detection for FLAS mode
- Profiling-guided optimization

## Build Configuration

### Essential Options
```bash
# Interpreter only (minimum)
cmake -B build

# With Singlepass JIT
cmake -B build -DZEN_ENABLE_SINGLEPASS_JIT=ON

# With Multipass JIT (requires LLVM 15)
cmake -B build -DZEN_ENABLE_MULTIPASS_JIT=ON \
      -DLLVM_DIR=/path/to/llvm/lib/cmake/llvm

# Debugging options
cmake -B build -DZEN_ENABLE_ASAN=ON         # AddressSanitizer
cmake -B build -DZEN_ENABLE_PROFILER=ON     # Performance profiling
cmake -B build -DZEN_ENABLE_SPEC_TEST=ON    # WebAssembly tests
cmake -B build -DZEN_ENABLE_EVM=ON          # EVM support
cmake -B build -DZEN_ENABLE_LIBEVM=ON       # EVMC library support
```

## Testing Infrastructure

### Test Scripts
- `.ci/run_test_suite.sh`: Main test script
- `.github/workflows/dtvm_wasm_test_x86.yml`: Test environment for wasm
- `.github/workflows/dtvm_evm_test_x86.yml`: Test environment for evm

### Test Categories
1. **microsuite**: ctest suite for wasm
2. **evmtestsuite**: ctest suite for evm
3. **evmrealsuite**: test for real evm bytecode
4. **evmonetestsuite**: run in evmone's unittest
