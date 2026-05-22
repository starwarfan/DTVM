# Legacy CALL Repro Fixture

This directory stores standalone repro fixtures for the historical legacy CALL
gas divergence:

- `block 254277 tx 0` (accident path)
- `block 254297 tx 0` (guard path)
- `block 433153 tx 0/1` (`kWrongBlockGas` repro)

Each case file is self-contained and includes:

- `revision`
- `tx`
- `env`
- `prestate` (touched accounts with nonce/balance/code/storage)
- `expected` (`status` and `tx_gas`)

Use `tools/extract_legacy_call_repro.sh` to regenerate fixtures from live chain
data.

For best historical nonce/balance fidelity during extraction, provide:

- `SILKWORM_STAGED_PIPELINE_BIN`
- `SILKWORM_DATADIR_254277`
- `SILKWORM_DATADIR_254297`
- `SILKWORM_DATADIR_433153` (optional, for nonce/balance fidelity)
