"""Smoke test for the Python side of the Nexus-LOB state contract.

Runs with only numpy installed — no build, no pytest required:

    python python_quant/tests/test_contract_smoke.py

Also works under pytest:  pytest python_quant/tests/test_contract_smoke.py
"""
from __future__ import annotations

import sys
from pathlib import Path

# Make `nexus_quant` importable when run as a plain script.
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import numpy as np  # noqa: E402

from nexus_quant.book_state import (  # noqa: E402
    BOOK_STATE_DTYPE,
    CT_DTYPE,
    DEPTH,
    PX_DTYPE,
    SZ_DTYPE,
    Side,
    StubOrderBook,
)

# Must equal the C++ sizeof(nexus::BookStateView) == 40*kDepth + 48.
EXPECTED_ITEMSIZE = 40 * DEPTH + 48


def test_dtype_itemsize_matches_cpp_abi():
    assert BOOK_STATE_DTYPE.itemsize == EXPECTED_ITEMSIZE, (
        f"dtype itemsize {BOOK_STATE_DTYPE.itemsize} != C++ sizeof "
        f"{EXPECTED_ITEMSIZE} — C++/Python layout drift"
    )


def test_view_shapes_and_dtypes():
    v = StubOrderBook().view()
    for name, dt in (
        ("bid_px", PX_DTYPE), ("ask_px", PX_DTYPE),
        ("bid_sz", SZ_DTYPE), ("ask_sz", SZ_DTYPE),
        ("bid_ct", CT_DTYPE), ("ask_ct", CT_DTYPE),
    ):
        arr = v[name]
        assert arr.shape == (DEPTH,), (name, arr.shape)
        assert arr.dtype == np.dtype(dt), (name, arr.dtype)


def test_ladder_ordering_and_values():
    book = StubOrderBook()
    book.add(Side.Ask, price=10_100, size=200, orders=2)
    book.add(Side.Ask, price=10_050, size=100, orders=1)
    book.add(Side.Bid, price=10_000, size=300, orders=3)
    book.add(Side.Bid, price=9_950, size=150, orders=1)
    v = book.view()
    # asks ascend from best (lowest), bids descend from best (highest).
    assert v["ask_px"][0] == 10_050 and v["ask_px"][1] == 10_100
    assert v["bid_px"][0] == 10_000 and v["bid_px"][1] == 9_950
    assert v["ask_sz"][0] == 100 and v["ask_ct"][0] == 1
    assert v["bid_sz"][0] == 300 and v["bid_ct"][0] == 3
    # unfilled levels are zero-padded.
    assert v["bid_px"][2] == 0 and v["ask_px"][2] == 0


def test_cancel_reduces_then_trade_records():
    book = StubOrderBook()
    book.add(Side.Bid, 10_000, 500, orders=5)
    book.cancel(Side.Bid, 10_000, 200, orders=2)
    v = book.view()
    assert v["bid_sz"][0] == 300 and v["bid_ct"][0] == 3
    book.record_trade(Side.Ask, 10_000, 100)
    v = book.view()
    assert v["last_trade_px"] == 10_000
    assert v["last_trade_sz"] == 100
    assert v["last_trade_side"] == Side.Ask
    assert v["cum_volume"] == 100


def test_full_cancel_removes_level():
    book = StubOrderBook()
    book.add(Side.Ask, 10_050, 100)
    book.cancel(Side.Ask, 10_050, 100)
    v = book.view()
    assert v["ask_px"][0] == 0 and v["ask_sz"][0] == 0


def _run_all() -> int:
    tests = {k: v for k, v in sorted(globals().items()) if k.startswith("test_")}
    failed = 0
    for name, fn in tests.items():
        try:
            fn()
            print(f"PASS  {name}")
        except AssertionError as exc:
            failed += 1
            print(f"FAIL  {name}: {exc}")
    print(f"\n{len(tests) - failed}/{len(tests)} passed")
    return failed


if __name__ == "__main__":
    sys.exit(1 if _run_all() else 0)
