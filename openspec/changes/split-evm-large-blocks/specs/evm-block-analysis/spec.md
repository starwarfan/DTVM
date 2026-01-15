# EVM Block Analysis Specification

## Overview

This specification defines the enhanced block analysis capabilities required to support automatic splitting of large EVM bytecode blocks. The analysis extends the existing `EVMAnalyzer` class to track opcode counts and identify optimal split points based on stack height analysis.

## ADDED Requirements

### Requirement: Block Size Tracking

The EVM analyzer MUST track the total number of opcodes in each analyzed block to determine when splitting is required.

#### Scenario: Opcode Counting During Analysis

**Given** an EVM bytecode block with multiple opcodes  
**When** the analyzer processes the bytecode  
**Then** it MUST maintain an accurate count of all opcodes encountered  
**And** the count MUST exclude immediate data bytes following PUSH instructions  
**And** the count MUST be accessible after analysis completion  

**Implementation Details:**
- Counter incremented for each valid opcode
- PUSH instruction data bytes not counted as separate opcodes
- Invalid opcodes still counted for consistency

### Requirement: Split Threshold Configuration

The analyzer MUST support configurable thresholds for determining when block splitting is required.

#### Scenario: Configurable Block Size Threshold

**Given** a configurable block size threshold (default: 1000 opcodes)  
**When** the analyzer completes block analysis  
**Then** it MUST determine if the block exceeds the threshold  
**And** it MUST provide a method to check if splitting is required  
**And** the threshold MUST be configurable at compile time  

**Configuration Interface:**
```cpp
static constexpr uint32_t DEFAULT_BLOCK_SIZE_THRESHOLD = 1000;
bool shouldSplitBlock(uint32_t opcodeCount) const;
```

### Requirement: Split Point Identification

The analyzer MUST identify optimal split points within large blocks based on stack height analysis.

#### Scenario: Optimal Split Point Selection

**Given** a block that exceeds the size threshold  
**When** the analyzer calculates split points  
**Then** it MUST target split points at regular intervals (every N opcodes)  
**And** it MUST search within ±50 opcodes of each target for the optimal point  
**And** the optimal point MUST be where StackHeightDiff is closest to zero  
**And** split points MUST NOT break instruction sequences (e.g., PUSH + data)  

**Algorithm Requirements:**
- Target split every `threshold` opcodes
- Search window of ±50 opcodes around target
- Minimize `abs(StackHeightDiff)` within search window
- Validate instruction boundaries

### Requirement: Split Function Metadata

The analyzer MUST maintain metadata for each identified split function including boundaries and stack information.

#### Scenario: Split Function Information Storage

**Given** identified split points in a large block  
**When** the analyzer processes split boundaries  
**Then** it MUST store split function metadata in a accessible map  
**And** each split function MUST have a unique function index  
**And** metadata MUST include start PC, end PC, and stack height information  
**And** the metadata MUST be queryable by PC address  

**Data Structure:**
```cpp
struct SplitInfo {
  uint64_t StartPC;
  uint64_t EndPC;
  uint32_t FunctionIndex;
  int32_t StackHeightAtStart;
  int32_t StackHeightAtEnd;
};
std::map<uint64_t, SplitInfo> SplitFunctions;
```

## MODIFIED Requirements

### Requirement: Enhanced Block Analysis

The existing block analysis MUST be extended to support split point calculation while maintaining backward compatibility.

#### Scenario: Backward Compatible Analysis Enhancement

**Given** existing EVM bytecode analysis functionality  
**When** the enhanced analyzer processes bytecode  
**Then** it MUST maintain all existing analysis capabilities  
**And** it MUST provide additional split-related information  
**And** existing code using the analyzer MUST continue to work unchanged  
**And** new split functionality MUST be opt-in through method calls  

**Compatibility Requirements:**
- Existing `analyze()` method signature unchanged
- Existing `BlockInfo` structure unchanged
- New functionality accessed through additional methods
- No performance impact when splitting disabled

### Requirement: Stack Height Analysis Integration

The existing stack height analysis MUST be leveraged to determine optimal split points.

#### Scenario: Stack-Aware Split Point Selection

**Given** existing stack height calculation for each opcode  
**When** the analyzer evaluates potential split points  
**Then** it MUST use existing `StackHeightDiff` calculations  
**And** it MUST prefer split points where stack height difference is minimal  
**And** it MUST ensure stack consistency across split boundaries  
**And** existing stack analysis accuracy MUST be maintained  

**Integration Points:**
- Reuse existing stack height calculation logic
- Extend `BlockInfo` usage for split point evaluation
- Maintain stack height tracking accuracy

## Implementation Requirements

### Performance Requirements

- Split point calculation MUST NOT increase analysis time by more than 20%
- Memory overhead for split metadata MUST be minimal (< 1KB per 1000 opcodes)
- Analysis MUST remain single-pass for efficiency

### Error Handling Requirements

- Invalid split points MUST be detected and handled gracefully
- Analysis MUST fallback to no splitting if optimal points cannot be found
- Error conditions MUST be clearly reported through return values or exceptions

### Thread Safety Requirements

- Analysis MUST be thread-safe for concurrent compilation scenarios
- Split metadata MUST be properly isolated between analysis instances
- No global state dependencies for split functionality

## Testing Requirements

### Unit Test Coverage

- Opcode counting accuracy across all instruction types
- Split point selection algorithm correctness
- Stack height integration validation
- Configuration parameter handling
- Error condition handling

### Integration Test Scenarios

- Large blocks with various opcode patterns
- Edge cases: blocks exactly at threshold
- Complex control flow with jumps and jump destinations
- Nested function-like structures in bytecode
- Performance regression testing

### Validation Criteria

- Split functions MUST maintain semantic equivalence to original block
- Stack heights MUST be consistent across split boundaries
- All opcodes MUST be accounted for in split functions
- No instruction sequences MUST be broken by splits

## Cross-References

This specification depends on:
- **evm-function-splitting**: Requires split metadata for function generation
- **evm-internal-calls**: Provides split information for call generation

This specification is used by:
- EVM compiler integration for split-aware compilation
- MIR builder for split point handling during bytecode processing
