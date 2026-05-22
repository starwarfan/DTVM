#!/usr/bin/env python3
"""Compare legacy_call_repro fixture prestate vs Silkworm datadir (if available)."""
from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path


def load_fixture(path: Path) -> dict:
    return json.loads(path.read_text())


def query_account_at(datadir: str, block: int, addr: str) -> dict | None:
    sp = os.environ.get(
        "STAGED_PIPELINE",
        "/mnt/data/zhonghao/silkworm/build/silkworm/node/cli/staged_pipeline",
    )
    if not Path(sp).is_file():
        return None
    cmd = [
        sp,
        "--datadir",
        datadir,
        "--exclusive",
        "account_at",
        "--block",
        str(block),
        "--address",
        addr,
    ]
    try:
        out = subprocess.check_output(cmd, stderr=subprocess.STDOUT, text=True, timeout=120)
    except subprocess.CalledProcessError as e:
        print(e.output, file=sys.stderr)
        return None
    nonce = balance = None
    for line in out.splitlines():
        if "nonce=" in line:
            for part in line.split():
                if part.startswith("nonce="):
                    nonce = int(part.split("=", 1)[1])
                if part.startswith("balance="):
                    balance = int(part.split("=", 1)[1])
    if nonce is None:
        return None
    return {"nonce": nonce, "balance": balance}


def main() -> int:
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} <fixture.json> [datadir]", file=sys.stderr)
        return 2

    fixture_path = Path(sys.argv[1])
    datadir = sys.argv[2] if len(sys.argv) > 2 else os.environ.get(
        "SILKWORM_DATADIR_1431916",
        "/mnt/erigon-snapshots/silkworm-dtvm-1431915-preflight-20260522-100324",
    )

    fx = load_fixture(fixture_path)
    block = int(fx["block_number"])
    prestate = fx["prestate"]
    tx = fx["tx"]
    sender = tx["from"].lower()

    print(f"fixture: {fixture_path.name}")
    print(f"block: {block} tx_index: {fx.get('tx_index')} replay_mode: {fx.get('replay_mode', 'single')}")
    print(f"datadir: {datadir}")
    print(f"prestate accounts: {len(prestate)}")
    print()

    # Sender nonce must match tx for valid replay.
    s = prestate.get(sender, {})
    print(f"sender {sender}")
    print(f"  fixture nonce: {s.get('nonce')}  tx.nonce: {tx['nonce']}  match: {s.get('nonce') == tx['nonce']}")

    db = query_account_at(datadir, block - 1, sender)
    if db:
        print(f"  datadir @{block - 1} nonce: {db['nonce']}  balance: {db['balance']}")
        print(
            f"  datadir vs tx.nonce: {'OK' if db['nonce'] == tx['nonce'] else 'MISMATCH (datadir corrupt or wrong block)'}"
        )
    else:
        print("  datadir: unavailable")

    target = (tx.get("to") or "").lower()
    if target and target in prestate:
        t = prestate[target]
        print(f"\ntarget {target}")
        print(f"  fixture balance: {t.get('balance')}")
        print(f"  fixture storage slots: {len(t.get('storage', {}))}")
        db_t = query_account_at(datadir, block - 1, target)
        if db_t:
            print(f"  datadir @{block - 1} balance: {db_t['balance']}")

    zero_nonce = [a for a, acc in prestate.items() if acc.get("nonce") == 0]
    print(f"\naccounts with nonce=0 in fixture: {len(zero_nonce)} / {len(prestate)}")
    if zero_nonce:
        print("  (prefix fixtures extracted without datadir often have RPC fallback 0)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
