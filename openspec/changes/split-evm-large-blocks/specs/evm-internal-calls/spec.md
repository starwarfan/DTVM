# EVM Internal Calls Specification

## Overview

This specification defines the internal function call mechanism required to connect split EVM functions. The implementation provides seamless function calls between split segments while maintaining stack consistency and control flow correctness.

## ADDED Requirements

### Requirement: Internal Call Interface

The EVM MIR builder MUST provide an interface for generating internal function calls to split functions.

#### Scenario: Internal Call Generation

**Given** a split function with a specific function index  
**When** the bytecode visitor encounters a split point  
**Then** the MIR builder MUST generate a CallInstruction targeting the split function  
**And** the call MUST use the correct function index  
**And** the call MUST maintain stack consistency  
**And** control flow MUST transfer correctly to the target function  

**Interface Requirements:**
```cpp
class EVMMirBuilder {
  // New method for internal function calls
  void handleInternalCall(uint32_t funcIdx);
  
  // Supporting methods
  void setSplitInfo(const std::map<uint64_t, SplitInfo>& splitInfo);
  bool isAtSplitPoint(uint64_t pc) const;
  uint32_t getFunctionIndexForPC(uint64_t pc) const;
  uint64_t getSplitEndPC(uint64_t pc) const;
};
```

### Requirement: Call Instruction Generation

The internal call interface MUST generate proper MIR CallInstruction objects for split function invocation.

#### Scenario: MIR Call Instruction Creation

**Given** a function index for a split function  
**When** handleInternalCall is invoked  
**Then** it MUST create a CallInstruction with the specified function index  
**And** the instruction MUST be properly inserted into the current basic block  
**And** the call MUST preserve the current execution context  
**And** return handling MUST be properly configured  

**Call Instruction Properties:**
- Target function index correctly set
- Proper operand handling for parameters
- Return value management
- Exception handling preservation

### Requirement: Stack Consistency Management

Internal calls MUST maintain stack consistency across function boundaries without explicit stack marshalling.

#### Scenario: Transparent Stack Management

**Given** a function call at a split point with optimal stack height  
**When** the internal call is executed  
**Then** the stack state MUST be preserved across the call boundary  
**And** no additional stack operations MUST be required  
**And** the called function MUST see the same stack state  
**And** return from the call MUST restore the original stack state  

**Stack Management Requirements:**
- No explicit stack save/restore operations
- Identical function signatures ensure stack compatibility
- Split points chosen for minimal stack height difference
- Runtime stack pointer consistency

### Requirement: Split Point Detection and Handling

The bytecode visitor MUST detect split points and generate appropriate internal calls.

#### Scenario: Split Point Recognition During Bytecode Processing

**Given** bytecode processing at a specific program counter (PC)  
**When** the visitor checks for split points  
**Then** it MUST query the split information to determine if a split exists  
**And** if a split is found, it MUST generate an internal call  
**And** it MUST skip processing opcodes within the split function range  
**And** processing MUST resume after the split function end  

**Detection Algorithm:**
```cpp
// In EVMByteCodeVisitor::decode()
while (Ip < IpEnd) {
  uint64_t PC = static_cast<uint64_t>(Ip - Bytecode);
  
  if (Builder.isAtSplitPoint(PC)) {
    uint32_t funcIdx = Builder.getFunctionIndexForPC(PC);
    Builder.handleInternalCall(funcIdx);
    
    // Skip to end of split function
    uint64_t endPC = Builder.getSplitEndPC(PC);
    Ip = Bytecode + endPC;
    continue;
  }
  
  // Normal opcode processing...
}
```

### Requirement: Control Flow Management

Internal calls MUST properly manage control flow transfer and return handling.

#### Scenario: Seamless Control Flow Transfer

**Given** an internal call to a split function  
**When** the call is executed  
**Then** control MUST transfer to the beginning of the target function  
**And** the target function MUST execute its complete opcode sequence  
**And** upon completion, control MUST return to the calling function  
**And** execution MUST continue with the next instruction after the call  

**Control Flow Requirements:**
- Proper function entry point targeting
- Complete function execution
- Correct return address management
- Exception propagation handling

## MODIFIED Requirements

### Requirement: Enhanced Bytecode Visitor Integration

The existing bytecode visitor MUST be enhanced to support split-aware processing.

#### Scenario: Split-Aware Bytecode Traversal

**Given** the existing bytecode visitor decode loop  
**When** processing bytecode with split functions  
**Then** the visitor MUST check for split points at each PC  
**And** existing opcode processing MUST remain unchanged for non-split code  
**And** split point handling MUST be seamlessly integrated  
**And** backward compatibility MUST be maintained for code without splits  

**Integration Requirements:**
- Minimal changes to existing decode loop
- Split point checking before opcode processing
- Conditional split handling
- Preservation of existing functionality

### Requirement: MIR Builder Context Enhancement

The existing MIR builder MUST be enhanced with split-aware context management.

#### Scenario: Split Context Integration

**Given** the existing EVMMirBuilder functionality  
**When** split information is available  
**Then** the builder MUST maintain split context throughout compilation  
**And** existing instruction generation MUST remain unaffected  
**And** split-specific functionality MUST be additive  
**And** context cleanup MUST handle split-related state  

**Context Enhancements:**
```cpp
class EVMMirBuilder {
  // Existing members preserved...
  
  // New split-related context
  const std::map<uint64_t, SplitInfo>* splitInfo = nullptr;
  std::map<uint64_t, uint32_t> pcToFunctionIndex;
  
  // Enhanced context management
  void initializeSplitContext(const std::map<uint64_t, SplitInfo>& info);
  void cleanupSplitContext();
};
```

## Implementation Requirements

### Function Call Mechanics

1. **Call Generation**: Create CallInstruction with target function index
2. **Parameter Passing**: Leverage identical signatures for seamless parameter transfer
3. **Return Handling**: Ensure proper return value and control flow management
4. **Exception Handling**: Maintain exception propagation across call boundaries

### Performance Considerations

- Internal calls MUST have minimal overhead compared to direct execution
- No additional memory allocation during call generation
- Efficient PC-to-function-index lookup (O(log n) or better)
- Minimal impact on existing bytecode processing performance

### Error Handling

- Invalid function indices MUST be detected and reported
- Missing split information MUST result in graceful fallback
- Call generation failures MUST be properly propagated
- Debugging information MUST be preserved across calls

## Testing Requirements

### Unit Test Coverage

- Internal call generation correctness
- Split point detection accuracy
- Stack consistency validation
- Control flow transfer verification
- Error condition handling

### Integration Test Scenarios

- End-to-end split function execution
- Complex control flow with multiple splits
- Edge cases: single opcode splits, maximum splits
- Performance comparison: split vs. non-split execution
- Exception handling across split boundaries

### Validation Criteria

- Semantic equivalence: split execution produces identical results to non-split
- Performance: internal call overhead within acceptable limits (< 5%)
- Memory: no memory leaks or excessive allocation
- Correctness: all opcodes executed exactly once

## Cross-References

This specification depends on:
- **evm-block-analysis**: Requires split metadata for call generation
- **evm-function-splitting**: Requires function indices for call targeting

This specification is used by:
- **evmc-vm-interface**: Must maintain compatibility with EVM execution semantics
- Bytecode visitor for split-aware processing
- MIR compilation pipeline for call instruction handling

## Configuration and Debugging

### Compile-Time Configuration

```cpp
// Internal call configuration
static constexpr bool ENABLE_INTERNAL_CALLS = true;
static constexpr bool VALIDATE_CALL_TARGETS = true;
static constexpr bool TRACE_INTERNAL_CALLS = false;
```

### Runtime Debugging

- Optional call tracing for debugging split execution
- Function index validation in debug builds
- Stack state verification at call boundaries
- Performance metrics collection for call overhead

### Error Reporting

- Clear error messages for invalid function indices
- Detailed context information for call generation failures
- Stack trace preservation across internal calls
- Debugging symbols for split function boundaries

## Future Enhancements

### Optimization Opportunities

- **Call Inlining**: Small split functions could be inlined for performance
- **Tail Call Optimization**: Last calls in functions could be optimized
- **Call Caching**: Frequently called functions could be cached
- **Profile-Guided Optimization**: Hot split functions could receive special treatment

### Advanced Features

- **Conditional Splitting**: Runtime decision on whether to use splits
- **Dynamic Split Points**: Adaptive splitting based on execution patterns
- **Cross-Function Optimization**: Optimization passes across split boundaries
- **Parallel Execution**: Independent split functions could execute in parallel
