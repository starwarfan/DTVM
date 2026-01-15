# EVM Large Block Splitting - Implementation Tasks

## Overview

This document outlines the implementation tasks required to deliver the EVM large block splitting feature. Tasks are organized by priority and dependencies, with clear deliverables and validation criteria.

## Phase 1: Core Analysis Infrastructure

### Task 1.1: Enhance EVMAnalyzer for Opcode Counting ✅ **COMPLETED**
**Priority**: High  
**Dependencies**: None  
**Estimated Effort**: 2-3 days  

**Deliverables**:
- ✅ Add opcode counting functionality to `EVMAnalyzer::analyze()`
- ✅ Implement `shouldSplitBlock()` method with configurable threshold
- ⏳ Add unit tests for opcode counting accuracy across all instruction types
- ✅ Validate counting excludes PUSH instruction data bytes

**Acceptance Criteria**:
- ✅ All EVM opcodes counted correctly during analysis
- ✅ PUSH instruction immediate data not counted as separate opcodes
- ✅ Configurable threshold (default: 1000 opcodes) properly enforced
- ✅ Backward compatibility maintained for existing analyzer usage

**Files Modified**:
- ✅ `src/compiler/evm_frontend/evm_analyzer.h`
- N/A `src/compiler/evm_frontend/evm_analyzer.cpp` (header-only implementation)

### Task 1.2: Implement Split Point Selection Algorithm ✅ **COMPLETED**
**Priority**: High  
**Dependencies**: Task 1.1  
**Estimated Effort**: 3-4 days  

**Deliverables**:
- ✅ Implement `findOptimalSplitPoints()` method
- ✅ Add `findBestSplitPoint()` for stack-height-based optimization
- ✅ Create `SplitInfo` structure and storage map
- ⏳ Add comprehensive unit tests for split point selection

**Acceptance Criteria**:
- ✅ Split points selected at regular intervals (every N opcodes)
- ✅ Optimal points chosen within ±50 opcodes based on minimal StackHeightDiff
- ✅ No instruction sequences broken by split points
- ✅ Split metadata properly stored and queryable

**Files Modified**:
- ✅ `src/compiler/evm_frontend/evm_analyzer.h`
- N/A `src/compiler/evm_frontend/evm_analyzer.cpp` (header-only implementation)

### Task 1.3: Add Split Metadata Management ✅ **COMPLETED**
**Priority**: Medium  
**Dependencies**: Task 1.2  
**Estimated Effort**: 1-2 days  

**Deliverables**:
- ✅ Implement split function metadata storage and retrieval
- ✅ Add methods for querying split information by PC
- ✅ Create function index allocation and management
- ✅ Add validation for split boundary correctness

**Acceptance Criteria**:
- ✅ Split metadata accessible via PC address lookup
- ✅ Function indices properly allocated and tracked
- ✅ Boundary validation ensures complete opcode coverage
- ✅ No gaps or overlaps in split function ranges

**Files Modified**:
- ✅ `src/compiler/evm_frontend/evm_analyzer.h`

## Phase 2: Compilation Pipeline Integration

### Task 2.1: Integrate Analysis into Compilation Pipeline ✅ **COMPLETED**
**Priority**: High  
**Dependencies**: Task 1.3  
**Estimated Effort**: 2-3 days  

**Deliverables**:
- ✅ Modify `EagerEVMJITCompiler::compile()` to call analyzer before function building
- ✅ Add conditional logic for split vs. non-split compilation paths
- ✅ Implement multi-function compilation loop
- ✅ Build different numbers of EVM functions based on analyzer's splitFunctions results
- ⏳ Add integration tests for compilation pipeline

**Acceptance Criteria**:
- ✅ Analyzer called before `buildEVMFunction()` when enabled
- ✅ Single-function path preserved for backward compatibility
- ✅ Multi-function compilation handles all split functions
- ✅ Function count dynamically determined by splitFunctions map size
- ✅ Each split function compiled as separate EVM function with proper indexing
- ✅ Compilation errors properly reported and handled

**Files Modified**:
- ✅ `src/compiler/evm_compiler.cpp`
- ✅ `src/compiler/evm_frontend/evm_mir_compiler.h`

## Phase 3: MIR Builder Enhancement

### Task 3.1: Add Internal Call Interface to EVMMirBuilder ✅ **COMPLETED**
**Priority**: High  
**Dependencies**: Task 2.1  
**Estimated Effort**: 2-3 days  

**Deliverables**:
- ✅ Implement `handleInternalCall(uint32_t funcIdx)` method
- ✅ Add split context management methods
- ✅ Create CallInstruction generation logic
- ⏳ Add unit tests for call instruction generation

**Acceptance Criteria**:
- ✅ Internal calls generate proper CallInstruction objects
- ✅ Function indices correctly targeted in call instructions
- ✅ Split context properly maintained during compilation
- ✅ Call instructions properly inserted into basic blocks

**Files Modified**:
- ✅ `src/compiler/evm_frontend/evm_mir_compiler.h`
- ✅ `src/compiler/evm_frontend/evm_mir_compiler.cpp`

### Task 3.2: Implement Split Context Management ✅ **COMPLETED**
**Priority**: Medium  
**Dependencies**: Task 3.1  
**Estimated Effort**: 1-2 days  

**Deliverables**:
- ✅ Add split information storage and query methods
- ✅ Implement PC-to-function-index mapping
- ✅ Create split point detection utilities
- ✅ Add context initialization and cleanup

**Acceptance Criteria**:
- ✅ Split information properly stored in builder context
- ✅ PC lookup returns correct function indices
- ✅ Split point detection works efficiently during compilation
- ✅ Context properly cleaned up after compilation

**Files Modified**:
- ✅ `src/compiler/evm_frontend/evm_mir_compiler.h`
- ✅ `src/compiler/evm_frontend/evm_mir_compiler.cpp`

## Phase 4: Bytecode Visitor Integration

### Task 4.1: Enhance Bytecode Visitor for Split Awareness ✅ **COMPLETED**
**Priority**: High  
**Dependencies**: Task 3.2  
**Estimated Effort**: 2-3 days  

**Deliverables**:
- ✅ Modify `EVMByteCodeVisitor::decode()` to check for split points
- ✅ Add split point handling and PC skipping logic
- ✅ Integrate internal call generation
- ⏳ Create comprehensive integration tests

**Acceptance Criteria**:
- ✅ Split points detected during bytecode processing
- ✅ Internal calls generated at appropriate split boundaries
- ✅ Bytecode processing skips opcodes within split functions
- ✅ Existing functionality preserved for non-split code

**Files Modified**:
- ✅ `src/action/evm_bytecode_visitor.h`

### Task 4.2: Implement PC Range Skipping Logic ✅ **COMPLETED**
**Priority**: Medium  
**Dependencies**: Task 4.1  
**Estimated Effort**: 1-2 days  

**Deliverables**:
- ✅ Add logic to skip processing opcodes within split function ranges
- ✅ Implement proper instruction pointer advancement
- ✅ Add validation for complete opcode coverage
- ⏳ Create edge case tests for boundary conditions

**Acceptance Criteria**:
- ✅ Opcodes within split functions not processed by main visitor
- ✅ Instruction pointer correctly advanced to split function end
- ✅ No opcodes duplicated or omitted during processing
- ✅ Boundary conditions properly handled

**Files Modified**:
- ✅ `src/action/evm_bytecode_visitor.h`

## Phase 5: Testing and Validation

### Task 5.1: Comprehensive Unit Testing
**Priority**: High  
**Dependencies**: All previous tasks  
**Estimated Effort**: 3-4 days  

**Deliverables**:
- Unit tests for all new analyzer functionality
- Unit tests for split point selection algorithm
- Unit tests for internal call generation
- Unit tests for bytecode visitor enhancements

**Acceptance Criteria**:
- >95% code coverage for new functionality
- All edge cases covered by tests
- Performance regression tests pass
- Memory leak tests pass

**Test Files**:
- `tests/unit/evm_analyzer_test.cpp`
- `tests/unit/evm_mir_builder_test.cpp`
- `tests/unit/evm_bytecode_visitor_test.cpp`

### Task 5.2: Integration Testing
**Priority**: High  
**Dependencies**: Task 5.1  
**Estimated Effort**: 2-3 days  

**Deliverables**:
- End-to-end tests with various bytecode patterns
- Performance benchmarks comparing split vs. non-split
- Stress tests with extremely large blocks
- Compatibility tests with existing EVM test suites

**Acceptance Criteria**:
- Semantic equivalence verified for split vs. non-split execution
- Performance overhead within acceptable limits (<30% compilation time)
- Large block handling (10K+ opcodes) works correctly
- All existing EVM tests continue to pass

**Test Files**:
- `tests/integration/evm_splitting_test.cpp`
- `tests/performance/evm_compilation_benchmark.cpp`

### Task 5.3: Documentation and Configuration
**Priority**: Medium  
**Dependencies**: Task 5.2  
**Estimated Effort**: 1-2 days  

**Deliverables**:
- Update build system configuration options
- Add runtime configuration documentation
- Create debugging and troubleshooting guide
- Update performance tuning recommendations

**Acceptance Criteria**:
- Configuration options properly documented
- Feature can be enabled/disabled at compile time
- Runtime parameters clearly explained
- Debugging information available for split analysis

**Files Modified**:
- Build system configuration files
- Documentation files

## Parallelizable Work

The following tasks can be worked on in parallel after their dependencies are met:

**Parallel Group 1** (after Task 1.3):
- Task 2.1: Compilation pipeline integration
- Task 3.1: MIR builder enhancement

**Parallel Group 2** (after Tasks 2.2 and 3.2):
- Task 4.1: Bytecode visitor integration
- Task 5.1: Unit testing (for completed components)

**Parallel Group 3** (after Task 4.2):
- Task 5.2: Integration testing
- Task 5.3: Documentation

## Risk Mitigation

### High-Risk Areas
1. **Stack Consistency**: Ensure split points maintain proper stack state
2. **Performance Impact**: Monitor compilation time overhead
3. **Semantic Correctness**: Verify split execution matches non-split results

### Mitigation Strategies
1. Extensive testing with stack height validation
2. Performance benchmarking at each milestone
3. Comprehensive semantic equivalence testing
4. Fallback to non-split compilation on errors

## Success Criteria

### Functional Requirements
- Large blocks (>1000 opcodes) automatically split during compilation
- Split functions execute with identical semantics to original blocks
- Backward compatibility maintained for existing code
- Configuration options available for tuning

### Performance Requirements
- Compilation time increase <30% for split blocks
- Runtime performance impact <5%
- Memory overhead minimal (<1KB per 1000 opcodes)
- No memory leaks or resource issues

### Quality Requirements
- >95% test coverage for new functionality
- All existing tests continue to pass
- No regressions in compilation or runtime performance
- Clear error messages and debugging support
