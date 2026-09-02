"""Replay + integrity tests."""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from nexus_quant.book_port import StubBookAdapter
from nexus_quant.book_state import Side, StubOrderBook
from nexus_quant.itch_parser import EventType, NormalizedEvent
from nexus_quant.replay import ReplayEngine, check_integrity, spread_ticks


def ev(**kw) -> NormalizedEvent:
    base = dict(
        kind=EventType.ADD,
        ts_ns=1,
        order_id=1,
        side=Side.Bid,
        price_ticks=100,
        size=50,
    )
    base.update(kw)
    return NormalizedEvent(**base)


def test_add_execute_cancel_delete_replace_trade():
    events = [
        ev(kind=EventType.ADD, side=Side.Bid, price_ticks=100, size=50, order_id=1),
        ev(kind=EventType.ADD, side=Side.Ask, price_ticks=102, size=40, order_id=2),
        ev(kind=EventType.ADD, side=Side.Bid, price_ticks=99, size=30, order_id=3),
        ev(kind=EventType.CANCEL, order_id=1, size=20),
        ev(kind=EventType.EXECUTE, order_id=2, size=10, price_ticks=0),
        ev(kind=EventType.REPLACE, order_id=3, new_order_id=4, price_ticks=101, size=30),
        ev(kind=EventType.TRADE, side=Side.Ask, price_ticks=102, size=5),
        ev(kind=EventType.DELETE, order_id=4),
    ]
    replay = ReplayEngine.from_stub(events)
    last = replay.step_n(len(events))
    assert last is not None
    assert replay.applied == 8
    assert replay.skipped == 0
    s = last.state
    assert int(s["ask_px"][0]) == 102
    assert int(s["ask_sz"][0]) == 30  # 40 - 10 execute
    assert int(s["bid_px"][0]) == 100  # 50-20 cancel; replace-then-delete of 99/101
    assert int(s["last_trade_px"]) == 102
    assert int(s["cum_volume"]) == 15  # execute 10 + print 5


def test_l2_bbo_spread():
    replay = ReplayEngine.from_stub(
        [
            ev(side=Side.Bid, price_ticks=100, size=10, order_id=1),
            ev(side=Side.Ask, price_ticks=101, size=10, order_id=2),
        ]
    )
    f = replay.step_n(2)
    assert spread_ticks(f.state) == 1
    assert int(f.state["bid_px"][0]) == 100
    assert int(f.state["ask_px"][0]) == 101
    assert check_integrity(f.state) == []


def test_crossed_book_detection():
    book = StubOrderBook()
    book.add(Side.Bid, 105, 10)
    book.add(Side.Ask, 100, 10)
    issues = check_integrity(book.view())
    assert any(i.code == "crossed" for i in issues)


def test_injected_stub_is_used():
    stub = StubOrderBook()
    adapter = StubBookAdapter(stub)
    replay = ReplayEngine(
        [ev(side=Side.Bid, price_ticks=50, size=7, order_id=9)],
        book=adapter,
    )
    replay.step()
    assert int(stub.view()["bid_px"][0]) == 50
    assert int(stub.view()["bid_sz"][0]) == 7


def test_snapshot_survives_mutation():
    adapter = StubBookAdapter()
    adapter.rest(Side.Bid, 10, 5, order_id=1)
    snap = adapter.snapshot()
    adapter.rest(Side.Bid, 11, 5, order_id=2)
    assert int(snap["bid_px"][0]) == 10
    assert int(adapter.view()["bid_px"][0]) == 11
