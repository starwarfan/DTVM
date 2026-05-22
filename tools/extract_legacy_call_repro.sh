#!/usr/bin/env bash
# Copyright (C) 2026 the DTVM authors. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Extract standalone repro fixtures for:
# - block 254277 tx 0 (accident)
# - block 254297 tx 0 (guard)
# - block 433153 tx 0/1 (kWrongBlockGas repro)
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
from typing import Dict, Any, Set, Optional
import requests
import time
import subprocess
import re
import os

out_dir = sys.argv[1]
rpc_url = sys.argv[2]
staged_pipeline_bin = os.environ.get("SILKWORM_STAGED_PIPELINE_BIN", "")

CASES = [
    {
        "block": 254277,
        "tx_index": 0,
        "fixture_name": "block_254277_tx_0.json",
        "case_name": "legacy_call_creation_cost_block_254277_tx0",
        "expected_tx_gas": 57956,
        "silkworm_datadir_env": "SILKWORM_DATADIR_254277",
    },
    {
        "block": 254297,
        "tx_index": 0,
        "fixture_name": "block_254297_tx_0.json",
        "case_name": "legacy_guard_block_254297_tx0",
        "expected_tx_gas": 94849,
        "silkworm_datadir_env": "SILKWORM_DATADIR_254297",
    },
    {
        "block": 433153,
        "tx_index": 0,
        "fixture_name": "block_433153_tx_0.json",
        "case_name": "wrong_block_gas_block_433153_tx0",
        "expected_tx_gas": 21000,
        "silkworm_datadir_env": "SILKWORM_DATADIR_433153",
    },
    {
        "block": 1431916,
        "tx_index": 38,
        "fixture_name": "block_1431916_tx_38.json",
        "case_name": "block_gas_limit_block_1431916_tx38",
        "expected_tx_gas": 845701,
        "silkworm_datadir_env": "SILKWORM_DATADIR_1431916",
    },
    {
        "block": 1431916,
        "tx_index": 38,
        "fixture_name": "block_1431916_prefix_tx_38.json",
        "case_name": "block_gas_limit_block_1431916_prefix_tx38",
        "expected_tx_gas": 845701,
        "silkworm_datadir_env": "SILKWORM_DATADIR_1431916",
        "prefix_replay": True,
        "prefix_tx_count": 38,
    },
    {
        "block": 433153,
        "tx_index": 1,
        "fixture_name": "block_433153_tx_1.json",
        "case_name": "wrong_block_gas_block_433153_tx1",
        "expected_tx_gas": 587578,
        "silkworm_datadir_env": "SILKWORM_DATADIR_433153",
    },
]

extract_only = os.environ.get("EXTRACT_ONLY", "")
if extract_only:
    CASES = [c for c in CASES if c["fixture_name"] == extract_only]


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


def query_account_at(datadir: str, block_number: int, address: str) -> Optional[Dict[str, Any]]:
    if not staged_pipeline_bin or not datadir:
        return None
    cmd = [
        staged_pipeline_bin,
        "--datadir",
        datadir,
        "--exclusive",
        "account_at",
        "--block",
        str(block_number),
        "--address",
        address,
    ]
    try:
        proc = subprocess.run(
            cmd,
            check=True,
            capture_output=True,
            text=True,
        )
    except Exception:
        return None
    line = ""
    merged = proc.stdout + "\n" + proc.stderr
    for l in merged.splitlines():
        if "[account-at]" in l:
            line = l
            break
    if not line:
        return None
    exists_m = re.search(r"exists=(true|false)", line)
    nonce_m = re.search(r"nonce=([0-9]+)", line)
    bal_m = re.search(r"balance=([0-9]+)", line)
    if not exists_m or not nonce_m or not bal_m:
        return None
    exists = exists_m.group(1) == "true"
    nonce = int(nonce_m.group(1)) if exists else 0
    balance = int(bal_m.group(1)) if exists else 0
    return {"exists": exists, "nonce": nonce, "balance": balance}


def h2i(h: str) -> int:
    return int(h, 16)


def to_hex_u256(v: int) -> str:
    return "0x" + v.to_bytes(32, "big").hex()


def normalize_addr(addr: Optional[str]) -> str:
    if not addr:
        raise ValueError("address is missing")
    addr = addr.lower()
    if addr.startswith("0x"):
        return addr
    return "0x" + addr


def revision_for_block(block_number: int) -> str:
    forks = [
        (1_150_000, "EVMC_HOMESTEAD"),
        (2_463_000, "EVMC_TANGERINE_WHISTLE"),
        (2_675_000, "EVMC_SPURIOUS_DRAGON"),
        (4_370_000, "EVMC_BYZANTIUM"),
        (7_280_000, "EVMC_CONSTANTINOPLE"),
        (9_069_000, "EVMC_ISTANBUL"),
        (12_244_000, "EVMC_BERLIN"),
        (12_965_000, "EVMC_LONDON"),
        (15_537_394, "EVMC_PARIS"),
        (17_034_470, "EVMC_SHANGHAI"),
        (19_426_587, "EVMC_CANCUN"),
    ]
    revision = "EVMC_FRONTIER"
    for height, name in forks:
        if block_number >= height:
            revision = name
    return revision


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
            if (
                isinstance(value, str)
                and value.startswith("0x")
                and len(value) == 42
            ):
                addrs.add(normalize_addr(value))
    return addrs


def normalized_slot_key(raw: str) -> str:
    v = raw[2:] if raw.startswith("0x") else raw
    return "0x" + v.rjust(64, "0").lower()


def collect_storage_keys_from_vmtrace(vm_trace: Dict[str, Any]) -> Set[str]:
    if not isinstance(vm_trace, dict):
        return set()
    ops = vm_trace.get("ops", [])
    keys: Set[str] = set()
    for i, op in enumerate(ops):
        name = op.get("op")
        if name not in ("SLOAD", "SSTORE") or i == 0:
            continue
        prev_push = ops[i - 1].get("ex", {}).get("push")
        if not isinstance(prev_push, list) or not prev_push:
            continue
        # vmTrace exposes stack-like values in prev ex.push. The top-of-stack
        # is the last element. For SLOAD/SSTORE, the slot key is stack top.
        candidate = prev_push[-1]
        if isinstance(candidate, str) and candidate.startswith("0x"):
            keys.add(normalized_slot_key(candidate))
    return keys


def collect_frame_storage_keys(vm_trace: Dict[str, Any]) -> Set[str]:
    if not isinstance(vm_trace, dict):
        return set()
    ops = vm_trace.get("ops", [])
    keys: Set[str] = set()
    for i, op in enumerate(ops):
        name = op.get("op")
        if name not in ("SLOAD", "SSTORE") or i == 0:
            continue
        prev_push = ops[i - 1].get("ex", {}).get("push")
        if not isinstance(prev_push, list) or not prev_push:
            continue
        candidate = prev_push[-1]
        if isinstance(candidate, str) and candidate.startswith("0x"):
            keys.add(normalized_slot_key(candidate))
    return keys


def walk_vmtrace_frames(vm_trace: Dict[str, Any]):
    stack = [vm_trace]
    while stack:
        frame = stack.pop()
        if not isinstance(frame, dict):
            continue
        yield frame
        ops = frame.get("ops", [])
        if not isinstance(ops, list):
            continue
        for op in reversed(ops):
            sub = op.get("sub") if isinstance(op, dict) else None
            if isinstance(sub, dict):
                stack.append(sub)


def coerce_field_value(field: str, value: Any) -> Any:
    if field == "nonce":
        if isinstance(value, int):
            return value
        return h2i(value)
    if field == "code":
        if isinstance(value, str):
            return value.lower()
        return "0x"
    if isinstance(value, int):
        return to_hex_u256(value)
    if isinstance(value, str):
        return to_hex_u256(h2i(value))
    return value


def apply_field_forward(current: Any, diff_val: Any, field: str) -> Any:
    if diff_val == "=":
        return current
    if isinstance(diff_val, dict):
        if "+" in diff_val:
            return coerce_field_value(field, diff_val["+"])
        if "*" in diff_val:
            return coerce_field_value(field, diff_val["*"]["to"])
        if "-" in diff_val:
            if field == "nonce":
                return 0
            if field == "code":
                return "0x"
            return to_hex_u256(0)
    return current


def apply_state_diff_forward(prestate: Dict[str, Any], state_diff: Dict[str, Any]) -> None:
    for raw_addr, account_diff in state_diff.items():
        addr = normalize_addr(raw_addr)
        if addr not in prestate:
            prestate[addr] = {
                "balance": to_hex_u256(0),
                "nonce": 0,
                "code": "0x",
                "storage": {},
            }
        account = prestate[addr]
        for field in ("balance", "nonce", "code"):
            if field not in account_diff:
                continue
            account[field] = apply_field_forward(account[field], account_diff[field], field)
            if field == "balance":
                account[field] = coerce_field_value(field, account[field])
            elif field == "nonce":
                account[field] = coerce_field_value(field, account[field])
            elif field == "code":
                account[field] = coerce_field_value(field, account[field])
        storage_diff = account_diff.get("storage", {})
        for slot, value_diff in storage_diff.items():
            slot = normalized_slot_key(slot)
            if isinstance(value_diff, dict):
                if "+" in value_diff:
                    account["storage"][slot] = normalized_slot_key(value_diff["+"])
                elif "*" in value_diff:
                    account["storage"][slot] = normalized_slot_key(value_diff["*"]["to"])
                elif "-" in value_diff:
                    account["storage"].pop(slot, None)
            elif value_diff == "=":
                continue


def serialize_tx_for_fixture(tx: Dict[str, Any], receipt: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    is_create = tx.get("to") is None
    tx_to = (
        normalize_addr(receipt.get("contractAddress"))
        if is_create and receipt and receipt.get("contractAddress")
        else normalize_addr(tx["to"])
        if tx.get("to")
        else "0x0000000000000000000000000000000000000000"
    )
    return {
        "kind": "create" if is_create else "call",
        "from": normalize_addr(tx["from"]),
        "to": tx_to,
        "nonce": h2i(tx["nonce"]),
        "gas_limit": h2i(tx["gas"]),
        "gas_price": tx["gasPrice"].lower(),
        "value": tx["value"].lower(),
        "input": tx["input"].lower(),
    }


for case in CASES:
    block_number = case["block"]
    tx_index = case["tx_index"]
    block_tag = hex(block_number)
    prev_block_tag = hex(block_number - 1)
    silkworm_datadir = os.environ.get(case["silkworm_datadir_env"], "")
    prefix_replay = bool(case.get("prefix_replay"))
    prefix_tx_count = int(case.get("prefix_tx_count", 0))

    block = rpc_call("eth_getBlockByNumber", [block_tag, True])
    if block is None:
        raise RuntimeError(f"missing block {block_number}")
    txs = block["transactions"]
    if tx_index >= len(txs):
        raise RuntimeError(f"block {block_number} tx_index {tx_index} out of range")

    tx = txs[tx_index]
    tx_hash = tx["hash"]
    receipt = rpc_call("eth_getTransactionReceipt", [tx_hash])
    block_replay = rpc_call(
        "trace_replayBlockTransactions",
        [block_tag, ["stateDiff", "trace", "vmTrace"]],
    )
    tx_replay = block_replay[tx_index]
    state_diff = tx_replay.get("stateDiff", {})
    trace_entries = tx_replay.get("trace", [])
    vm_trace = tx_replay.get("vmTrace", {})

    if prefix_replay:
        if prefix_tx_count != tx_index:
            raise RuntimeError(
                f"prefix_tx_count {prefix_tx_count} must equal tx_index {tx_index}"
            )

    addresses: Set[str] = set()
    addresses.add(normalize_addr(tx["from"]))
    if tx.get("to"):
        addresses.add(normalize_addr(tx["to"]))
    contract_address = receipt.get("contractAddress")
    addresses.add(normalize_addr(block["miner"]))
    created_addrs: Set[str] = set()
    prior_limit = prefix_tx_count if prefix_replay else tx_index
    for prior_idx in range(prior_limit):
        prior_tx = txs[prior_idx]
        addresses.add(normalize_addr(prior_tx["from"]))
        if prior_tx.get("to"):
            addresses.add(normalize_addr(prior_tx["to"]))
        prior_diff = block_replay[prior_idx].get("stateDiff", {})
        prior_trace = block_replay[prior_idx].get("trace", [])
        addresses.update(collect_trace_addresses(prior_trace))
        for addr, account_diff in prior_diff.items():
            if account_existed_before(account_diff):
                addresses.add(normalize_addr(addr))
    if prefix_replay:
        target_diff = block_replay[tx_index].get("stateDiff", {})
        target_trace = block_replay[tx_index].get("trace", [])
        addresses.update(collect_trace_addresses(target_trace))
        for addr, account_diff in target_diff.items():
            if account_existed_before(account_diff):
                addresses.add(normalize_addr(addr))
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
        account_at = (
            query_account_at(silkworm_datadir, block_number - 1, addr)
            if silkworm_datadir
            else None
        )
        if account_at is not None:
            balance_int = account_at["balance"]
            nonce_int = account_at["nonce"]
            balance = hex(balance_int)
            nonce = hex(nonce_int)
        else:
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

    if not prefix_replay:
        sender_addr = normalize_addr(tx["from"])
        prestate[sender_addr]["nonce"] = h2i(tx["nonce"])

        for prior_idx in range(tx_index):
            apply_state_diff_forward(
                prestate, block_replay[prior_idx].get("stateDiff", {})
            )

    if not prefix_replay:
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
                slot = normalized_slot_key(slot)
                prestate[addr]["storage"][slot] = extract_storage_value_from_diff(
                    value_diff
                )

        # Add read-set storage slots observed in vmTrace for the top-level recipient.
        if tx.get("to"):
            to_addr = normalize_addr(tx["to"])
            read_keys = collect_storage_keys_from_vmtrace(vm_trace)
            for slot in sorted(read_keys):
                if slot in prestate[to_addr]["storage"]:
                    continue
                value = rpc_call_or_default(
                    "eth_getStorageAt", [to_addr, slot, prev_block_tag], "0x0"
                )
                prestate[to_addr]["storage"][slot] = normalized_slot_key(value)

        # Improve slot ownership by mapping vmTrace frame code -> account address.
        # This captures storage reads/writes of internal call frames.
        code_to_addrs: Dict[str, Set[str]] = {}
        for addr in sorted(prestate.keys()):
            code_hex = prestate[addr]["code"]
            code_to_addrs.setdefault(code_hex, set()).add(addr)

        for frame in walk_vmtrace_frames(vm_trace):
            frame_code = frame.get("code")
            if not isinstance(frame_code, str) or frame_code == "0x":
                continue
            owners = code_to_addrs.get(frame_code.lower(), set())
            if not owners:
                continue
            frame_keys = collect_frame_storage_keys(frame)
            if not frame_keys:
                continue
            for owner_addr in owners:
                for slot in sorted(frame_keys):
                    if slot in prestate[owner_addr]["storage"]:
                        continue
                    value = rpc_call_or_default(
                        "eth_getStorageAt", [owner_addr, slot, prev_block_tag], "0x0"
                    )
                    prestate[owner_addr]["storage"][slot] = normalized_slot_key(
                        value
                    )

    block_base_fee = block.get("baseFeePerGas", "0x0")
    block_prev_randao = block.get("mixHash", "0x0")
    block_prev_randao = to_hex_u256(h2i(block_prev_randao))
    parent_block = rpc_call("eth_getBlockByNumber", [hex(block_number - 1), False])
    parent_hash = parent_block["hash"].lower()
    block_hashes: Dict[str, str] = {}
    start = max(0, block_number - 256)
    for bn in range(start, block_number):
        b = rpc_call_or_default("eth_getBlockByNumber", [hex(bn), False], None)
        if isinstance(b, dict) and b.get("hash"):
            block_hashes[str(bn)] = b["hash"].lower()

    tx_gas_used = h2i(receipt["gasUsed"])
    if tx_gas_used != case["expected_tx_gas"]:
        raise RuntimeError(
            f"unexpected gas for {case['case_name']}: got {tx_gas_used}, "
            f"expected {case['expected_tx_gas']}"
        )

    if prefix_replay:
        for prior_idx in range(prefix_tx_count):
            apply_state_diff_forward(
                prestate, block_replay[prior_idx].get("stateDiff", {})
            )

        # Materialize storage slots read/written in the target tx using pre-tx values.
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
                slot = normalized_slot_key(key)
                prestate[addr]["storage"][slot] = extract_storage_value_from_diff(
                    value_diff
                )

        if tx.get("to"):
            to_addr = normalize_addr(tx["to"])
            read_keys = collect_storage_keys_from_vmtrace(vm_trace)
            for slot in sorted(read_keys):
                if to_addr not in prestate:
                    prestate[to_addr] = {
                        "balance": to_hex_u256(0),
                        "nonce": 0,
                        "code": "0x",
                        "storage": {},
                    }
                if slot in prestate[to_addr]["storage"]:
                    continue
                value = rpc_call_or_default(
                    "eth_getStorageAt", [to_addr, slot, prev_block_tag], "0x0"
                )
                prestate[to_addr]["storage"][slot] = normalized_slot_key(value)

        code_to_addrs: Dict[str, Set[str]] = {}
        for addr in sorted(prestate.keys()):
            code_hex = prestate[addr]["code"]
            code_to_addrs.setdefault(code_hex, set()).add(addr)

        for frame in walk_vmtrace_frames(vm_trace):
            frame_code = frame.get("code")
            if not isinstance(frame_code, str) or frame_code == "0x":
                continue
            owners = code_to_addrs.get(frame_code.lower(), set())
            if not owners:
                continue
            frame_keys = collect_frame_storage_keys(frame)
            if not frame_keys:
                continue
            for owner_addr in owners:
                for slot in sorted(frame_keys):
                    if slot in prestate[owner_addr]["storage"]:
                        continue
                    value = rpc_call_or_default(
                        "eth_getStorageAt", [owner_addr, slot, prev_block_tag], "0x0"
                    )
                    prestate[owner_addr]["storage"][slot] = normalized_slot_key(
                        value
                    )

        sender_addr = normalize_addr(tx["from"])
        if sender_addr in prestate:
            prestate[sender_addr]["nonce"] = h2i(tx["nonce"])

        prefix_transactions = []
        for prior_idx in range(prefix_tx_count):
            prior_tx = txs[prior_idx]
            prior_receipt = rpc_call(
                "eth_getTransactionReceipt", [prior_tx["hash"]]
            )
            prefix_transactions.append(
                serialize_tx_for_fixture(prior_tx, prior_receipt)
            )
        target_tx = serialize_tx_for_fixture(tx, receipt)
        fixture = {
            "case_name": case["case_name"],
            "replay_mode": "prefix",
            "chain_id": h2i(tx["chainId"]) if "chainId" in tx and tx["chainId"] else 1,
            "block_number": block_number,
            "tx_index": tx_index,
            "prefix_tx_count": prefix_tx_count,
            "tx_hash": tx_hash.lower(),
            "revision": revision_for_block(block_number),
            "prefix_transactions": prefix_transactions,
            "tx": target_tx,
            "env": {
                "block_number": block_number,
                "block_timestamp": h2i(block["timestamp"]),
                "block_coinbase": normalize_addr(block["miner"]),
                "block_prev_randao": block_prev_randao,
                "block_gas_limit": h2i(block["gasLimit"]),
                "block_base_fee": block_base_fee.lower(),
                "tx_origin": normalize_addr(tx["from"]),
                "block_hash": parent_hash,
                "block_hashes": block_hashes,
            },
            "prestate": prestate,
            "expected": {
                "status": status_from_receipt(receipt),
                "tx_gas": tx_gas_used,
            },
            "meta": {
                "source": "rpc+trace_replayBlockTransactions+prefix_replay",
                "rpc_url": rpc_url,
                "trace_includes": ["stateDiff", "trace"],
            },
        }
        out_path = f"{out_dir}/{case['fixture_name']}"
        with open(out_path, "w", encoding="utf-8") as f:
            json.dump(fixture, f, indent=2, sort_keys=False)
            f.write("\n")
        print(f"generated {out_path}")
        continue

    is_create = tx.get("to") is None
    tx_to = (
        normalize_addr(contract_address)
        if is_create
        else normalize_addr(tx["to"])
    )
    fixture = {
        "case_name": case["case_name"],
        "chain_id": h2i(tx["chainId"]) if "chainId" in tx and tx["chainId"] else 1,
        "block_number": block_number,
        "tx_index": tx_index,
        "tx_hash": tx_hash.lower(),
        "revision": revision_for_block(block_number),
        "tx": {
            "kind": "create" if is_create else "call",
            "from": normalize_addr(tx["from"]),
            "to": tx_to,
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
            "block_hash": parent_hash,
            "block_hashes": block_hashes,
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
