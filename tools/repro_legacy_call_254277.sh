#!/bin/bash
# Copyright (C) 2025 the DTVM authors. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Reproduce the historical legacy CALL gas divergence on block 254277 tx0 and
# verify the fix against a guard case (block 254297 tx0).
#
# This script runs Silkworm's run_single_tx using the DTVM lib built from the
# current checkout, then checks:
#   - multipass and interpreter agree on 254277:0 gas (57956)
#   - multipass and interpreter agree on 254297:0 gas (94849)
#
# Usage:
#   tools/repro_legacy_call_254277.sh /path/to/silkworm
#
# Optional env vars:
#   DTVM_BUILD_DIR        (default: /mnt/data/zhonghao/DTVM/build)
#   DATADIR_254277        (default: /mnt/erigon-snapshots/dtvm-repro-254277-b)
#   DATADIR_254297        (default: /mnt/erigon-snapshots/dtvm-repro-254277-20260512)
#   STAGED_PIPELINE_BIN   (default: <silkworm>/build/silkworm/node/cli/staged_pipeline)
#
set -euo pipefail

if [ $# -ne 1 ]; then
  echo "Usage: $0 /path/to/silkworm"
  exit 1
fi

SILKWORM_DIR="$1"
DTVM_BUILD_DIR="${DTVM_BUILD_DIR:-/mnt/data/zhonghao/DTVM/build}"
DATADIR_254277="${DATADIR_254277:-/mnt/erigon-snapshots/dtvm-repro-254277-b}"
DATADIR_254297="${DATADIR_254297:-/mnt/erigon-snapshots/dtvm-repro-254277-20260512}"
STAGED_PIPELINE_BIN="${STAGED_PIPELINE_BIN:-$SILKWORM_DIR/build/silkworm/node/cli/staged_pipeline}"

if [ ! -f "$DTVM_BUILD_DIR/lib/libdtvmapi.so" ]; then
  echo "Error: $DTVM_BUILD_DIR/lib/libdtvmapi.so not found"
  echo "Build it first: cmake --build \"$DTVM_BUILD_DIR\" --target dtvmapi"
  exit 1
fi
if [ ! -x "$STAGED_PIPELINE_BIN" ]; then
  echo "Error: staged_pipeline not found or not executable: $STAGED_PIPELINE_BIN"
  exit 1
fi
if [ ! -d "$DATADIR_254277" ]; then
  echo "Error: datadir missing: $DATADIR_254277"
  exit 1
fi
if [ ! -d "$DATADIR_254297" ]; then
  echo "Error: datadir missing: $DATADIR_254297"
  exit 1
fi

cp "$DTVM_BUILD_DIR/lib/libdtvmapi.so" "$SILKWORM_DIR/libdtvmapi.so"

run_single_tx() {
  local mode="$1"
  local datadir="$2"
  local block="$3"
  local tx_index="$4"
  local log_file="$5"
  (
    cd "$SILKWORM_DIR"
    env SILKWORM_EVM="./libdtvmapi.so,mode=${mode}" \
      DTVM_EVM_DISABLE_MULTIPASS_GREEDYRA=0 \
      "$STAGED_PIPELINE_BIN" \
      --datadir "$datadir" \
      --exclusive run_single_tx --block "$block" --tx-index "$tx_index" \
      >"$log_file" 2>&1
  )
}

extract_tx_gas() {
  local log_file="$1"
  local gas
  gas=$(awk 'match($0, /tx_gas=[0-9]+/) {print substr($0, RSTART+7, RLENGTH-7); exit}' "$log_file")
  if [ -z "${gas:-}" ]; then
    echo "Error: failed to parse tx_gas from $log_file" >&2
    return 1
  fi
  echo "$gas"
}

check_case() {
  local datadir="$1"
  local block="$2"
  local tx_index="$3"
  local expected="$4"
  local tag="$5"
  local interp_log="/tmp/repro_legacy_call_${tag}_interp.log"
  local mp_log="/tmp/repro_legacy_call_${tag}_mp.log"

  echo "== Running $tag (block=$block tx=$tx_index expected=$expected) =="
  run_single_tx "interpreter" "$datadir" "$block" "$tx_index" "$interp_log"
  run_single_tx "multipass" "$datadir" "$block" "$tx_index" "$mp_log"

  local interp_gas
  local mp_gas
  interp_gas=$(extract_tx_gas "$interp_log")
  mp_gas=$(extract_tx_gas "$mp_log")

  echo "  interpreter tx_gas=$interp_gas"
  echo "  multipass   tx_gas=$mp_gas"

  if [ "$interp_gas" != "$expected" ]; then
    echo "FAIL: interpreter gas mismatch for $tag (expected $expected)"
    exit 1
  fi
  if [ "$mp_gas" != "$expected" ]; then
    echo "FAIL: multipass gas mismatch for $tag (expected $expected)"
    exit 1
  fi
  if [ "$interp_gas" != "$mp_gas" ]; then
    echo "FAIL: interpreter/multipass mismatch for $tag"
    exit 1
  fi
}

check_case "$DATADIR_254277" 254277 0 57956 "254277_tx0"
check_case "$DATADIR_254297" 254297 0 94849 "254297_tx0"

echo "PASS: legacy CALL repro checks are green."
