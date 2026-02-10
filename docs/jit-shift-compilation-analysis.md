# JIT Compilation Performance Analysis: RA-Expensive Opcodes

## Problem Summary

EVM shift opcodes (SHL/SHR/SAR) generate long `SelectInstruction` chains in MIR
(~15 MIR instructions per shift opcode). When hundreds or thousands of shift
operations appear in a single EVM function, the greedy register allocator's
complexity becomes superlinear (approaching O(n^2)), causing compilation times to
explode from milliseconds to minutes.

## Root Cause

Each shift opcode expands to a U256 shift implemented as 4 x i64 component
shifts with cross-component carry propagation. This generates per-component
`SelectInstruction` chains:

```
SrcValue = Select(IsMatch, Value[0], SrcValue)
SrcValue = Select(IsMatch, Value[1], SrcValue)
SrcValue = Select(IsMatch, Value[2], SrcValue)
SrcValue = Select(IsMatch, Value[3], SrcValue)
SrcValue = Select(IsInBounds, SrcValue, Zero)
// ... similar chain for CarryValue ...
```

The register allocator (greedy RA) struggles when:
1. Many such chains exist in a single basic block
2. Virtual register live ranges overlap extensively
3. Eviction/splitting cascades compound the cost

## Two Distinct Patterns

### Pattern b0: min_stack (DUP-same-operand)

**Bytecode**: `DUP1 SHL DUP1 SHL DUP1 SHL ...` (interleaved)

**Key characteristic**: Each SHL's two operands are identical (`Shift == Value`)
because DUP1 duplicates the top-of-stack, and SHL pops both from the same
duplicated value.

**Root cause**: The shift result feeds back as BOTH operands of the next shift
via DUP. This creates a serial feedback loop where each Select chain's live
ranges overlap with all subsequent chains. The same value cycles through
`handleShift` repeatedly, creating exponentially overlapping live ranges.

**Compilation time** (Release, codeSize=2087, ~1023 SHL ops):
- Without fix: ~78 seconds
- With fix (protectUnsafeValue on intermediates): **~6 seconds** (13x improvement)

### Pattern b1: full_stack (DUP-then-shift)

**Bytecode**: `DUP1 x1023` then `SHL x1022` then `POP` (batched)

**Key characteristic**: All 1023 DUPs push the SAME `counter` value onto the
stack. Each SHL consumes the previous SHL result (top) and an original `counter`
copy (second). So `Shift != Value` for all SHLs after the first.

**Root cause**: `counter[0..3]` (4 MInstruction*) are each used by ~1022
different SHL calls spread across the entire function. Their live ranges span the
entire function, creating massive interference with all Select chain
intermediates. The problem is fundamentally about **large fan-out** of a single
value, not about dependency chains.

**Compilation time** (Release, codeSize=2087, ~1023 SHL ops):
- Without any fix: ~57-132 seconds (varies by opcode)
- Input-level protectUnsafeValue: ~67-145 seconds (no improvement, sometimes worse)

## Implemented Fix: DUP Pattern Detection (b0)

**Location**: `src/compiler/evm_frontend/evm_mir_compiler.h` (`handleShift`)
and `src/compiler/evm_frontend/evm_mir_compiler.cpp` (handleLeftShift,
handleLogicalRightShift, handleArithmeticRightShift)

**Detection**: In `handleShift`, after `extractU256Operand`:
```cpp
bool BreakLiveRanges = (Shift == Value);
```
`std::array::operator==` compares all 4 `MInstruction*` pointers. When both
operands come from the same DUP'd stack value, the pointers are identical.

**Mitigation**: When `BreakLiveRanges == true`, insert `protectUnsafeValue`
(Dassign + Dread pair) after the Select chain outputs for `SrcValue` and
`CarryValue` inside each handler. This forces a spill/reload that breaks the
long live ranges of the Select chain outputs, preventing the RA from building
up massive interference graphs.

**Result**: b0 compilation reduced from ~78s to ~6s with no b1 regression.

## Unresolved: b1 Pattern

### Why protectUnsafeValue doesn't help b1

**Intermediate protection** (SrcValue/CarryValue after Select chains):
Adds extra VRs inside the Select chain, extending chains and making RA worse.
Result: b1 regressed from ~132s to ~151s.

**Input protection** (Value components before Select chains):
Creates fresh copies via Dassign/Dread, but `counter[i]` is still USED by
~1022 Dassign instructions. Its live range still spans the entire function.
The RA complexity is dominated by the sheer VR count (~19000) in a single BB,
not just live range lengths. Result: mixed, no consistent improvement.

### Potential Solutions (not yet implemented)

1. **Non-linear MIR estimate penalty**: When RA-expensive opcodes (SHL, SHR,
   SAR, MUL, SIGNEXTEND, BYTE) exceed a count threshold (e.g., 64), add a
   quadratic penalty to the MIR estimate. This pushes extreme patterns past
   `MAX_JIT_MIR_ESTIMATE` while leaving normal contracts unaffected.

2. **RA budget/timeout**: Add a compilation time or iteration budget to the
   greedy RA. If exceeded, bail out and fallback to interpreter. This handles
   ALL pathological patterns regardless of opcode type.

3. **Function splitting**: Break the single large basic block into smaller
   functions or compilation units at the MIR level, reducing per-unit RA cost.

4. **DUP-level optimization**: In `handleDup`, when the same value has been
   duplicated many times (e.g., >16), insert `protectUnsafeValue` to create
   fresh copies. This wouldn't help b1's counter fan-out but might help
   intermediate patterns.

5. **Linear-scan RA for large functions**: Switch to a simpler O(n) register
   allocator when the MIR instruction count exceeds a threshold.

### Practical Consideration

The b1 pattern (1023 consecutive DUPs followed by 1023 consecutive SHLs) is a
**synthetic benchmark** pattern. Real EVM contracts are unlikely to have such
extreme opcode concentration. The b0 pattern (interleaved DUP+SHL) is somewhat
more realistic and is already handled by the DUP detection fix.

## Benchmark Evidence

All measurements on Release build, codeSize=2087, mirEstimate=19485:

| Case    | Pattern | No fix | DUP detect (current) |
|---------|---------|--------|---------------------|
| SHL/b0  | DUP     | ~78s   | **6.0s**            |
| SHL/b1  | full    | ~132s  | 132s (unchanged)    |
| SHR/b0  | DUP     | ~78s*  | **5.3s**            |
| SHR/b1  | full    | ~114s  | 114s (unchanged)    |
| SAR/b0  | DUP     | ~78s*  | **2.8s**            |
| SAR/b1  | full    | ~57s   | 57s (unchanged)     |

*Estimated from SHL/b0 baseline; exact measurements for SHR/SAR b0 without fix
were not captured separately.

## All RA-Expensive Opcodes Analysis

Beyond shift opcodes, other handlers also generate Select chains or heavy MIR
that could cause similar RA slowdowns at high density.

### Select Chain Density per Handler

| Handler | Select/call | Total MIR/call | Opcode | Weight | Risk |
|---------|-------------|----------------|--------|--------|------|
| handleLogicalRightShift | **96** | ~160-190 | SHR (0x1c) | 15 | **High** |
| handleLeftShift | **92** | ~150-180 | SHL (0x1b) | 15 | **High** |
| handleArithmeticRightShift | **52** | ~100-130 | SAR (0x1d) | 15 | **High** |
| handleSignextend | **21** | ~80-100 | SIGNEXTEND (0x0b) | 20 | **Medium** |
| handleExp (computeExpByteSize) | 7 | ~25-30 | EXP (0x0a) | 5 | Low |
| handleByte | 4 | ~25-35 | BYTE (0x1a) | 8 | Low |
| handleCompareGT_LT | 3 | ~25-30 | GT/LT/SGT/SLT | 12 | Low |
| handleMul | **0** | ~50-60 | MUL (0x02) | 80 | **Special** |

### Key Observations

1. **SHL/SHR/SAR (High risk)**: 52-96 Selects per call with nested dependency
   chains (J loop + K loop over 4 components). The b0 DUP pattern is handled
   by the implemented fix. Weight of 15 severely underestimates actual MIR
   output (~150-190 instructions).

2. **SIGNEXTEND (Medium risk)**: 21 Selects per call with two dependency chain
   loops (SignBit chain + result component chain). Already has
   `protectUnsafeValue` on result components, which partially mitigates the
   issue. Could still be problematic with 500+ consecutive SIGNEXTEND ops.
   Weight of 20 underestimates actual MIR (~80-100).

3. **MUL (Special case)**: Zero Select chains, but generates heavy inline U256
   multiplication (~50-60 MIR via partial products, EvmUmul128, carry
   propagation). The original `synth/MUL/b0` hanging case proved that **large
   intermediate value fan-out causes RA explosion even without Select chains**.
   Weight of 80 is the most accurate relative to actual MIR count.

4. **BYTE, Compare, EXP (Low risk)**: Few Selects per call, unlikely to cause
   issues even at moderate density.

### Weight Accuracy

| Opcode | Current Weight | Actual MIR/call | Ratio (actual/weight) |
|--------|---------------|-----------------|----------------------|
| SHL | 15 | ~150-180 | **10-12x** underestimated |
| SHR | 15 | ~160-190 | **10-13x** underestimated |
| SAR | 15 | ~100-130 | **7-9x** underestimated |
| SIGNEXTEND | 20 | ~80-100 | **4-5x** underestimated |
| MUL | 80 | ~50-60 | ~0.7x (slightly overestimated) |
| BYTE | 8 | ~25-35 | ~3-4x underestimated |

Note: Weight underestimation alone doesn't cause problems — the RA cost is
superlinear, so the real issue is **opcode density** (hundreds of the same
expensive opcode in one function), not individual weight inaccuracy.

### Generalizable Fix: DUP Detection

The `Shift == Value` check (comparing `std::array<MInstruction*, 4>` pointers)
can be generalized to any binary operation handler. When `OpA == OpB`, it means
both operands come from the same DUP'd stack value, creating a feedback loop
where the result cycles back as both inputs. This pattern is the primary cause
of RA explosion in the b0 (min_stack) benchmark variant.

Candidates for generalization (if needed):
- `handleBinaryArithmetic<BO_MUL>` — already the most expensive; DUP pattern
  would compound the cost
- `handleSignextend` — medium Select density, DUP pattern possible
- `handleBitwiseOp` — low individual cost but DUP pattern could amplify

## Current State

- **DUP pattern detection**: Implemented and verified for shift opcodes.
  Handles b0 effectively (78s → 6s).
- **MIR weight**: SHL/SHR/SAR kept at 15 (linear estimate; underestimates
  actual MIR by ~10x but weight accuracy is not the core issue).
- **MAX_JIT_MIR_ESTIMATE**: 50000 (b1's mirEstimate=19485 is below threshold).
- **b1 compilation**: Still slow (~57-132s) but completes; not addressed yet.
- **Other opcodes**: SIGNEXTEND has partial mitigation (existing
  protectUnsafeValue). MUL is known problematic at high density.
