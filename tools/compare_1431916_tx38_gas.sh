#!/usr/bin/env bash
# Compare block 1431916 tx38 gas: canonical (receipt) vs DTVM standalone.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD:-${ROOT}/build}"
TX_HASH="0x0c10fafe0cdbfff32abfe53d57ec861d09986cc1050c850481f79b1a862bb10a"
CANONICAL=845701
RPC_URL="${RPC_URL:-https://ethereum.publicnode.com}"

echo "=== block 1431916 tx38 gas ==="
echo "canonical (mainnet receipt): ${CANONICAL}"

RECEIPT_GAS="$(python3 - <<PY
import requests, os, sys
rpc = os.environ.get("RPC_URL", "https://ethereum.publicnode.com")
tx = "${TX_HASH}"
r = requests.post(rpc, json={"jsonrpc":"2.0","id":1,"method":"eth_getTransactionReceipt","params":[tx]}, timeout=60).json()
if "result" not in r or not r["result"]:
    sys.exit("receipt fetch failed")
print(int(r["result"]["gasUsed"], 16))
PY
)"
echo "rpc receipt gasUsed:         ${RECEIPT_GAS}"

if [[ "${RECEIPT_GAS}" != "${CANONICAL}" ]]; then
  echo "WARN: receipt gas != expected ${CANONICAL}" >&2
fi

export LD_LIBRARY_PATH="${BUILD}/lib:${LD_LIBRARY_PATH:-}"
TEST_BIN="${BUILD}/evmLegacyCallReproTests"
if [[ ! -x "${TEST_BIN}" ]]; then
  echo "SKIP: build evmLegacyCallReproTests first (cmake --build build --target evmLegacyCallReproTests)"
  exit 0
fi

echo ""
echo "--- DTVM standalone (prefix prestate, tx38 only) ---"
echo "  (expected wrong until DTVM gas fix; interp/multipass should match each other)"
DTVM_LOG="$(mktemp)"
"${TEST_BIN}" --gtest_filter='EVMLegacyCallReproTest.DtvmPrefixReplayCanonicalGasOn1431916Tx38' 2>&1 | tee "${DTVM_LOG}" || true
DTVM_GAS="$(grep -oP 'Multi\.TxGas\s+Which is: \K[0-9]+' "${DTVM_LOG}" | head -1 || true)"
if [[ -z "${DTVM_GAS}" ]]; then
  DTVM_GAS="$(grep -oP 'Which is: \K[0-9]+' "${DTVM_LOG}" | head -1 || true)"
fi
echo "  dtvm multipass tx_gas:     ${DTVM_GAS:-unknown}"
if [[ "${DTVM_GAS}" == "${CANONICAL}" ]]; then
  echo "  dtvm: matches canonical"
else
  echo "  dtvm: differs from canonical ${CANONICAL} (known bug)"
fi
rm -f "${DTVM_LOG}"

echo ""
echo "--- evmone standalone ---"
if "${TEST_BIN}" --gtest_filter='EVMLegacyCallReproTest.EvmoneMatchesCanonicalOn1431916Tx38' 2>&1 | tee /tmp/evmone_1431916.log; then
  grep -E 'PASSED|FAILED|Failure|Which is' /tmp/evmone_1431916.log || true
else
  ec=$?
  if grep -q 'Segmentation fault\|SIGSEGV\|core dumped' /tmp/evmone_1431916.log 2>/dev/null; then
    echo "evmone standalone: SIGSEGV (use Silkworm builtin evmone = ${CANONICAL} as reference)"
  else
    echo "evmone standalone: exit ${ec} (see /tmp/evmone_1431916.log)"
  fi
fi

echo ""
echo "Silkworm reference (datadir @ 1431915, builtin evmone): tx38 gas = ${CANONICAL}"
echo "  evmone block cumulative after tx38 = 1808953, header gas_used = 4685953"
