# Split EVM Large Blocks

## Summary

This proposal introduces automatic splitting of large EVM bytecode blocks during compilation to improve performance and reduce memory pressure. When an EVM block exceeds a configurable opcode threshold (default: 1000 opcodes), the compiler will automatically split it into multiple functions with optimized split points based on stack height analysis.

## Motivation

Large EVM bytecode blocks can cause several performance issues:

1. **Memory Pressure**: Large blocks require significant memory allocation during compilation
2. **JIT Compilation Time**: Oversized blocks take longer to compile and optimize
3. **Cache Performance**: Large functions may not fit efficiently in instruction caches
4. **Stack Management**: Complex stack operations in large blocks are harder to optimize

By automatically splitting large blocks at optimal points, we can:
- Reduce compilation memory usage
- Improve JIT compilation performance  
- Enable better code cache utilization
- Maintain stack consistency across function boundaries

## Design Overview

The implementation involves four key components:

1. **Block Analysis**: Enhanced `EVMAnalyzer` to count opcodes and identify optimal split points
2. **Function Splitting**: Logic to determine split boundaries based on stack height analysis
3. **Internal Call Generation**: New `handleInternalCall` interface in `EVMMirBuilder`
4. **Bytecode Visitor Integration**: Modified visitor to handle function calls at split points

### Split Point Selection

Split points are chosen using the following algorithm:
- Target split every N opcodes (configurable, default 1000)
- Within ±50 opcodes of target, find point with `StackHeightDiff` closest to 0
- Ensure splits don't break critical instruction sequences
- Maintain function signature compatibility

## Implementation Plan

The implementation is divided into three main capabilities:

1. **EVM Block Analysis**: Extend analyzer to track opcode counts and identify split candidates
2. **EVM Function Splitting**: Implement splitting logic and split point optimization  
3. **EVM Internal Calls**: Add internal function call generation and management

Each capability has detailed requirements and scenarios defined in separate specification documents.

## Configuration

- `EVM_BLOCK_SIZE_THRESHOLD`: Maximum opcodes per block (default: 1000)
- `EVM_SPLIT_SEARCH_WINDOW`: Search window for optimal split points (default: ±50 opcodes)
- `EVM_ENABLE_BLOCK_SPLITTING`: Feature toggle (default: enabled)

## Compatibility

This change is backward compatible:
- Existing bytecode continues to work unchanged
- Small blocks (< threshold) are not affected
- Split functions maintain identical semantics to original blocks
- No changes to external APIs or ABIs

## Testing Strategy

- Unit tests for split point selection algorithm
- Integration tests with various bytecode patterns
- Performance benchmarks comparing split vs non-split compilation
- Stress tests with extremely large blocks
- Compatibility tests with existing EVM test suites
