# Legacy Multipass CALL Gas Repro

This note tracks the legacy CALL gas regression fixed in the multipass frontend
for revisions below Tangerine Whistle.

## Reference Cases

Fixture file:

- `tests/evm/fixtures/legacy_multipass_call/legacy_call_gas_cases.json`

Expected tx gas:

- `block=254277 tx_index=0` -> `tx_gas=57956`
- `block=254297 tx_index=0` -> `tx_gas=94849`

## Repro Commands

From silkworm checkout:

```bash
env SILKWORM_EVM=./libdtvmapi.so,mode=multipass DTVM_EVM_DISABLE_MULTIPASS_GREEDYRA=0 \
  ./build/silkworm/node/cli/staged_pipeline \
  --datadir /mnt/erigon-snapshots/dtvm-repro-254277-b \
  --exclusive run_single_tx --block 254277 --tx-index 0
```

```bash
env SILKWORM_EVM=./libdtvmapi.so,mode=multipass DTVM_EVM_DISABLE_MULTIPASS_GREEDYRA=0 \
  ./build/silkworm/node/cli/staged_pipeline \
  --datadir /mnt/erigon-snapshots/dtvm-repro-254277-20260512 \
  --exclusive run_single_tx --block 254297 --tx-index 0
```

## Root-Cause Direction

In legacy revisions, stack lifting/logical-stack materialization can lose deep
stack operands before CALL-family lowering executes. The frontend fix preserves
CALL operand provenance while keeping logical stack enabled across revisions,
including low revisions.

## Frontend Test Semantics

The regression tests in `src/tests/evm_jit_frontend_tests.cpp` guard operand
provenance rather than implementation details:

- `LegacyRevisionDupSwapPreservesOperandOrder` validates low-revision DUP/SWAP
  stack ordering without requiring a runtime-stack-only fallback.
- `LowRevisionMaterializedMergePreservesCallOperandsAfterDeepDupSwap` asserts
  the deep DUP/SWAP + merge case still lowers CALL with recipient `0xbb` under
  `EVMC_FRONTIER`.

These checks lock the actual root-cause surface: CALL operand provenance at
lowering sites.
