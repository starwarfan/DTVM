# Add EVM JIT Fallback to Interpreter

## Summary
Add fallback mechanism from EVM JIT compilation to interpreter execution, enabling seamless transition when JIT compilation encounters unsupported operations or runtime conditions.

## Motivation
Currently, EVM JIT compilation is an all-or-nothing approach. When the JIT compiler encounters unsupported opcodes, complex control flow, or runtime conditions that cannot be efficiently compiled, the entire execution must fall back to interpreter mode from the beginning. This results in:

1. **Performance degradation**: Losing all JIT optimization benefits for the entire execution
2. **Complexity**: Requiring complete re-execution from the start
3. **Resource waste**: Discarding partially compiled code and optimization work

A mid-execution fallback mechanism would allow:
- Preserving JIT performance benefits for successfully compiled portions
- Graceful degradation only for problematic code sections
- Better overall performance for mixed workloads

## Goals
- Enable EVMMirBuilder to generate fallback calls to interpreter
- Preserve complete EVM execution state (PC, stack, memory) during transition
- Allow interpreter to resume execution from arbitrary EVM state
- Maintain deterministic execution semantics across JIT/interpreter boundary

## Non-Goals
- Fallback from interpreter to JIT (one-way transition only)
- Automatic re-compilation after fallback
- Cross-function fallback (limited to single function scope)

## Success Criteria
- JIT-compiled EVM code can fallback to interpreter at any instruction boundary
- All EVM execution state is correctly preserved and transferred
- Interpreter can resume execution from transferred state
- Execution results are identical to pure interpreter or pure JIT execution
- Performance degradation is minimal for fallback transition overhead
