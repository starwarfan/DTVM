#!/usr/bin/env bash
# Copyright (C) 2026 the DTVM authors. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Extract standalone repro fixtures for:
# - block 254277 tx 0 (accident)
# - block 254297 tx 0 (guard)
#
# The generated fixtures are self-contained and can be consumed by DTVM tests
# without silkworm runtime / snapshot dependencies.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${1:-${ROOT_DIR}/tests/evm/fixtures/legacy_call_repro}"
RPC_URL="${RPC_URL:-https://ethereum.publicnode.com}"

mkdir -p "${OUT_DIR}"

python3 - "${OUT_DIR}" "${RPC_URL}" <<'PY'
import json
import sys
from typing import Dict, Any, Set
import requests
import time

out_dir = sys.argv[1]
rpc_url = sys.argv[2]

CASES = [
    {
        "block": 254277,
        "tx_index": 0,
        "fixture_name": "block_254277_tx_0.json",
        "case_name": "legacy_call_creation_cost_block_254277_tx0",
        "expected_tx_gas": 57956,
    },
    {
        "block": 254297,
        "tx_index": 0,
        "fixture_name": "block_254297_tx_0.json",
        "case_name": "legacy_guard_block_254297_tx0",
        "expected_tx_gas": 94849,
    },
]


def rpc_call(method: str, params):
    payload = {"jsonrpc": "2.0", "id": 1, "method": method, "params": params}
    last_err = None
    for _ in range(4):
        try:
            resp = requests.post(
                rpc_url,
                json=payload,
                timeout=60,
                headers={"User-Agent": "dtvm-legacy-repro-extractor/1.0"},
            )
            resp.raise_for_status()
            body = resp.json()
            if "error" in body:
                raise RuntimeError(f"{method} failed: {body['error']}")
            return body["result"]
        except Exception as err:
            last_err = err
            time.sleep(0.8)
    raise RuntimeError(str(last_err))


def rpc_call_or_default(method: str, params, default):
    try:
        return rpc_call(method, params)
    except Exception:
        return default


def h2i(h: str) -> int:
    return int(h, 16)


def to_hex_u256(v: int) -> str:
    return "0x" + v.to_bytes(32, "big").hex()


def normalize_addr(addr: str) -> str:
    addr = addr.lower()
    if addr.startswith("0x"):
        return addr
    return "0x" + addr


def revision_for_block(block_number: int) -> str:
    # Mainnet Tangerine Whistle starts at block 2,463,000.
    if block_number < 2_463_000:
        return "EVMC_FRONTIER"
    return "EVMC_TANGERINE_WHISTLE"


def status_from_receipt(receipt: Dict[str, Any]) -> str:
    if "status" in receipt and receipt["status"] is not None:
        return "success" if h2i(receipt["status"]) == 1 else "failure"
    return "success"


def extract_storage_value_from_diff(diff_obj: Any) -> str:
    if isinstance(diff_obj, str):
        # "=" style can appear at account-level but not meaningful for prestate slot.
        return to_hex_u256(0)
    if "*" in diff_obj:
        return to_hex_u256(h2i(diff_obj["*"]["from"]))
    if "+" in diff_obj:
        return to_hex_u256(0)
    if "-" in diff_obj:
        return to_hex_u256(h2i(diff_obj["-"]))
    return to_hex_u256(0)


def account_existed_before(account_diff: Dict[str, Any]) -> bool:
    for field in ("balance", "nonce", "code"):
        value = account_diff.get(field)
        if isinstance(value, dict) and ("*" in value or "-" in value):
            return True
        if value == "=":
            return True
    return False


def collect_trace_addresses(trace_entries) -> Set[str]:
    addrs: Set[str] = set()
    for entry in trace_entries:
        action = entry.get("action", {})
        for key in ("from", "to", "address", "refundAddress"):
            value = action.get(key)
            if isinstance(value, str) and value.startswith("0x") and len(value) == 42:
                addrs.add(normalize_addr(value))
    return addrs


for case in CASES:
    block_number = case["block"]
    tx_index = case["tx_index"]
    block_tag = hex(block_number)
    prev_block_tag = hex(block_number - 1)

    block = rpc_call("eth_getBlockByNumber", [block_tag, True])
    if block is None:
        raise RuntimeError(f"missing block {block_number}")
    txs = block["transactions"]
    if tx_index >= len(txs):
        raise RuntimeError(f"block {block_number} tx_index {tx_index} out of range")

    tx = txs[tx_index]
    tx_hash = tx["hash"]
    receipt = rpc_call("eth_getTransactionReceipt", [tx_hash])
    replay = rpc_call(
        "trace_replayBlockTransactions",
        [block_tag, ["stateDiff", "trace"]],
    )
    tx_replay = replay[tx_index]
    state_diff = tx_replay.get("stateDiff", {})
    trace_entries = tx_replay.get("trace", [])

    addresses: Set[str] = set()
    addresses.add(normalize_addr(tx["from"]))
    if tx.get("to"):
        addresses.add(normalize_addr(tx["to"]))
    addresses.add(normalize_addr(block["miner"]))
    created_addrs: Set[str] = set()
    for addr, account_diff in state_diff.items():
        if account_existed_before(account_diff):
            addresses.add(normalize_addr(addr))
        else:
            created_addrs.add(normalize_addr(addr))
    addresses.update(collect_trace_addresses(trace_entries))
    # Exclude addresses created during this tx from prestate materialization.
    addresses.difference_update(created_addrs)

    prestate: Dict[str, Any] = {}
    for addr in sorted(addresses):
        balance = rpc_call_or_default("eth_getBalance", [addr, prev_block_tag], "0x0")
        nonce = rpc_call_or_default(
            "eth_getTransactionCount", [addr, prev_block_tag], "0x0"
        )
        code = rpc_call_or_default("eth_getCode", [addr, prev_block_tag], "0x")
        prestate[addr] = {
            "balance": to_hex_u256(h2i(balance)),
            "nonce": h2i(nonce),
            "code": code.lower(),
            "storage": {},
        }

    sender_addr = normalize_addr(tx["from"])
    prestate[sender_addr]["nonce"] = h2i(tx["nonce"])

    # Add changed storage slots as touched prestate storage.
    for addr, account_diff in state_diff.items():
        if not account_existed_before(account_diff):
            continue
        addr = normalize_addr(addr)
        storage_diff = account_diff.get("storage", {})
        if addr not in prestate:
            prestate[addr] = {
                "balance": to_hex_u256(0),
                "nonce": 0,
                "code": "0x",
                "storage": {},
            }
        for key, value_diff in storage_diff.items():
            slot = key.lower()
            if not slot.startswith("0x"):
                slot = "0x" + slot
            prestate[addr]["storage"][slot] = extract_storage_value_from_diff(value_diff)

    block_base_fee = block.get("baseFeePerGas", "0x0")
    block_prev_randao = block.get("mixHash", "0x0")
    block_prev_randao = to_hex_u256(h2i(block_prev_randao))

    tx_gas_used = h2i(receipt["gasUsed"])
    if tx_gas_used != case["expected_tx_gas"]:
        raise RuntimeError(
            f"unexpected gas for {case['case_name']}: got {tx_gas_used}, "
            f"expected {case['expected_tx_gas']}"
        )

    fixture = {
        "case_name": case["case_name"],
        "chain_id": h2i(tx["chainId"]) if "chainId" in tx and tx["chainId"] else 1,
        "block_number": block_number,
        "tx_index": tx_index,
        "tx_hash": tx_hash.lower(),
        "revision": revision_for_block(block_number),
        "tx": {
            "kind": "call",
            "from": normalize_addr(tx["from"]),
            "to": normalize_addr(tx["to"]),
            "nonce": h2i(tx["nonce"]),
            "gas_limit": h2i(tx["gas"]),
            "gas_price": tx["gasPrice"].lower(),
            "value": tx["value"].lower(),
            "input": tx["input"].lower(),
        },
        "env": {
            "block_number": block_number,
            "block_timestamp": h2i(block["timestamp"]),
            "block_coinbase": normalize_addr(block["miner"]),
            "block_prev_randao": block_prev_randao,
            "block_gas_limit": h2i(block["gasLimit"]),
            "block_base_fee": block_base_fee.lower(),
            "tx_origin": normalize_addr(tx["from"]),
        },
        "prestate": prestate,
        "expected": {
            "status": status_from_receipt(receipt),
            "tx_gas": tx_gas_used,
        },
        "meta": {
            "source": "rpc+trace_replayBlockTransactions",
            "rpc_url": rpc_url,
            "trace_includes": ["stateDiff", "trace"],
        },
    }

    out_path = f"{out_dir}/{case['fixture_name']}"
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(fixture, f, indent=2, sort_keys=False)
        f.write("\n")
    print(f"generated {out_path}")
PY

echo "Extraction complete: ${OUT_DIR}"
