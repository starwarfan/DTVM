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

Automated repro path (recommended):

```bash
bash tools/repro_legacy_call_254277.sh /path/to/silkworm
```

Standalone fixture extraction:

```bash
bash tools/extract_legacy_call_repro.sh
```

This generates:

- `tests/evm/fixtures/legacy_call_repro/block_254277_tx_0.json`
- `tests/evm/fixtures/legacy_call_repro/block_254297_tx_0.json`

Standalone DTVM test entry:

```bash
ctest -R evmLegacyCallReproTests --output-on-failure
```

## Regression Validation Workflow (DTVM-Only)

The standalone repro path is validated inside DTVM tests (no Silkworm runtime
needed at execution time):

```bash
ctest -R evmLegacyCallReproTests --output-on-failure
```

Verification loop for the legacy DUP/SWAP runtime-stack fix:

1. Temporarily disable the legacy runtime-stack branch in
   `src/action/evm_bytecode_visitor.h` for revisions `< EVMC_TANGERINE_WHISTLE`.
2. Rebuild and run:

```bash
cmake --build build --target evmLegacyCallReproTests
ctest -R evmLegacyCallReproTests --output-on-failure
```

Expected result with fix disabled: `evmLegacyCallReproTests` aborts on
`ExecuteFixturesViaDTVMApi` (reproduced accident path `254277:0`).

3. Restore the legacy runtime-stack branch and rerun the same commands.

Expected result with fix restored: `evmLegacyCallReproTests` passes.

## Root-Cause Direction

In legacy revisions, stack lifting/logical-stack materialization can lose deep
stack operands before CALL-family lowering executes. The frontend fix keeps
legacy stack operations on the runtime EVM stack path, preserving CALL argument
order/value at execution sites without interpreter fallback.

## Frontend Test Semantics

The regression test `LegacyRevisionDupSwapUseRuntimeStackPath` in
`src/tests/evm_jit_frontend_tests.cpp` is intentionally behavioral:

- It uses `EVMC_FRONTIER`.
- It asserts `DUP2` and `SWAP1` perform runtime stack access
  (`stackGet`/`stackSet`) in legacy mode.

If legacy runtime-stack branches are disabled in the visitor, the same test
fails with:

- `DupStats.StackGetCount == 0`
- `SwapStats.StackGetCount == 0`
- `SwapStats.StackSetCount == 0`

That failure confirms the test is not cosmetic; it directly guards the legacy
operand provenance needed by CALL-family lowering.
