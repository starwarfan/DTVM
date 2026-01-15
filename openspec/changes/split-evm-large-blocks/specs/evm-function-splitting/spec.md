# EVM Function Splitting Specification

## Overview

This specification defines the function splitting capabilities required to generate multiple MIR functions from large EVM bytecode blocks. The splitting process integrates with the compilation pipeline to create additional functions based on split points identified by the block analysis.

## ADDED Requirements

### Requirement: Split-Aware Function Generation

The EVM compiler MUST generate multiple MIR functions when split points are identified in large blocks.

#### Scenario: Multiple Function Creation from Split Metadata

**Given** split metadata from block analysis indicating function boundaries  
**When** the compiler processes the EVM bytecode  
**Then** it MUST create separate MIR functions for each split segment  
**And** each split function MUST have a unique function index  
**And** all functions MUST share the same signature as the main EVM function  
**And** function indices MUST be sequential starting from the main function  

**Function Generation Requirements:**
- Main function index: 0
- Split function indices: 1, 2, 3, ...
- Identical function signatures across all split functions
- Proper function metadata registration

### Requirement: Function Signature Consistency

All split functions MUST maintain identical signatures to ensure seamless function calls.

#### Scenario: Consistent Function Signatures

**Given** a main EVM function with specific parameter and return types  
**When** split functions are generated  
**Then** each split function MUST have identical parameter types  
**And** each split function MUST have identical return types  
**And** calling conventions MUST be consistent across all functions  
**And** stack layout expectations MUST be preserved  

**Signature Requirements:**
```cpp
// All functions must have identical signatures
FunctionType* mainFuncType = module.getFuncType(0);
for (uint32_t i = 1; i <= splitCount; ++i) {
  module.setFuncType(i, mainFuncType);
}
```

### Requirement: Split Function Boundary Management

The compiler MUST properly handle bytecode boundaries for each split function.

#### Scenario: Accurate Bytecode Segmentation

**Given** split points defining function boundaries (StartPC, EndPC)  
**When** generating split functions  
**Then** each function MUST process only opcodes within its defined range  
**And** no opcodes MUST be duplicated across functions  
**And** no opcodes MUST be omitted from processing  
**And** boundary validation MUST ensure complete coverage  

**Boundary Validation:**
- Verify StartPC < EndPC for each split
- Ensure no gaps between consecutive splits
- Validate complete opcode coverage from 0 to bytecode end

### Requirement: Function Index Management

The compiler MUST maintain proper mapping between split functions and their indices.

#### Scenario: Function Index Allocation and Tracking

**Given** identified split functions with PC boundaries  
**When** the compiler allocates function indices  
**Then** it MUST assign unique sequential indices starting from 1  
**And** it MUST maintain a mapping from PC ranges to function indices  
**And** the mapping MUST be accessible during bytecode processing  
**And** index allocation MUST be deterministic and reproducible  

**Index Management Interface:**
```cpp
class SplitFunctionManager {
  std::map<uint64_t, uint32_t> pcToFunctionIndex;
  uint32_t nextFunctionIndex = 1;
  
  uint32_t allocateFunctionIndex(uint64_t startPC, uint64_t endPC);
  uint32_t getFunctionIndexForPC(uint64_t pc) const;
  bool isAtSplitBoundary(uint64_t pc) const;
};
```

## MODIFIED Requirements

### Requirement: Enhanced EVM Function Building

The existing `buildEVMFunction` process MUST be extended to handle multiple function generation.

#### Scenario: Multi-Function Build Process Integration

**Given** the existing single-function build process  
**When** split metadata indicates multiple functions are required  
**Then** the build process MUST create all required functions  
**And** existing single-function behavior MUST be preserved when no splits exist  
**And** the main function (index 0) MUST remain the entry point  
**And** all functions MUST be properly registered in the module  

**Integration Points:**
- Pre-analysis call to determine split requirements
- Conditional multi-function generation
- Backward compatibility with existing single-function path
- Proper module function registration

### Requirement: Compilation Pipeline Integration

The compilation pipeline MUST be enhanced to handle multiple EVM functions.

#### Scenario: Multi-Function Compilation Flow

**Given** multiple EVM functions generated from splits  
**When** the compilation pipeline processes the module  
**Then** it MUST compile each function independently  
**And** it MUST maintain proper function linkage  
**And** it MUST handle function calls between split functions  
**And** compilation order MUST ensure dependencies are resolved  

**Pipeline Modifications:**
```cpp
// Enhanced compilation flow
EVMAnalyzer analyzer;
analyzer.analyze(bytecode, size);

if (analyzer.shouldSplitBlock()) {
  buildMultipleEVMFunctions(ctx, module, analyzer.getSplitInfo());
} else {
  buildSingleEVMFunction(ctx, module);
}

// Compile all functions
for (uint32_t i = 0; i < module.getFunctionCount(); ++i) {
  compileEVMToMC(ctx, module, i);
}
```

## Implementation Requirements

### Function Generation Process

1. **Pre-Analysis**: Call EVMAnalyzer to determine split requirements
2. **Function Creation**: Generate MIR functions for each split segment
3. **Signature Setup**: Ensure all functions have identical signatures
4. **Index Assignment**: Allocate and track function indices
5. **Boundary Validation**: Verify complete and non-overlapping coverage

### Memory Management

- Split functions share the same memory pool as the main function
- Function metadata stored in compilation context
- Cleanup handled by existing module destruction

### Error Handling

- Invalid split boundaries result in fallback to single function
- Function generation failures are reported with detailed error messages
- Partial split generation is not allowed (all-or-nothing approach)

## Performance Requirements

### Compilation Performance

- Multi-function generation MUST NOT increase compilation time by more than 30%
- Function creation overhead MUST be minimal and proportional to split count
- Memory allocation patterns MUST remain efficient

### Runtime Performance

- Function call overhead between splits MUST be minimal
- No additional runtime memory overhead beyond normal function calls
- Stack management MUST remain efficient across function boundaries

## Testing Requirements

### Unit Test Coverage

- Function signature consistency validation
- Boundary management correctness
- Index allocation and mapping accuracy
- Error condition handling

### Integration Test Scenarios

- Single function (no splits) backward compatibility
- Multiple split functions with various sizes
- Edge cases: minimal splits, maximum splits
- Error recovery from invalid split metadata

### Performance Validation

- Compilation time regression testing
- Memory usage validation
- Runtime performance comparison (split vs. non-split)

## Cross-References

This specification depends on:
- **evm-block-analysis**: Requires split metadata for function generation
- **evmc-vm-interface**: Must maintain compatibility with EVM interface requirements

This specification is used by:
- **evm-internal-calls**: Provides function indices for call generation
- EVM compilation pipeline for multi-function processing
- MIR module management for function registration

## Configuration

### Compile-Time Configuration

```cpp
// Function splitting configuration
static constexpr bool ENABLE_FUNCTION_SPLITTING = true;
static constexpr uint32_t MAX_SPLIT_FUNCTIONS = 100;
static constexpr bool VALIDATE_SPLIT_BOUNDARIES = true;
```

### Runtime Configuration

- Function splitting can be disabled for debugging
- Maximum split count can be limited for resource management
- Boundary validation can be toggled for performance tuning
