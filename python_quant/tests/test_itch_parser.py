"""ITCH 5.0 parser tests."""
from __future__ import annotations

import io
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from nexus_quant.book_state import Side
from nexus_quant.itch_parser import (
    EventType,
    ItchParseStats,
    NormalizedEvent,
    encode_event,
    iter_itch_events,
)


def _ev(**kw) -> NormalizedEvent:
    base = dict(
        kind=EventType.ADD,
        ts_ns=34_200_000_000_001,
        order_id=1,
        side=Side.Bid,
        price_ticks=15000,
        size=100,
    )
    base.update(kw)
    return NormalizedEvent(**base)


def test_roundtrip_all_types():
    cases = [
        _ev(kind=EventType.ADD, side=Side.Bid, price_ticks=14990, size=250, order_id=11),
        _ev(kind=EventType.ADD_MPID, side=Side.Ask, price_ticks=15010, size=80, order_id=12),
        _ev(kind=EventType.EXECUTE, side=Side.NONE, size=40, order_id=11, price_ticks=0),
        _ev(kind=EventType.EXECUTE_PX, side=Side.NONE, size=10, order_id=11, price_ticks=14990),
        _ev(kind=EventType.CANCEL, side=Side.NONE, size=20, order_id=11, price_ticks=0),
        _ev(kind=EventType.DELETE, side=Side.NONE, size=0, order_id=12, price_ticks=0),
        _ev(kind=EventType.REPLACE, side=Side.NONE, size=60, order_id=11, new_order_id=99, price_ticks=14985),
        _ev(kind=EventType.TRADE, side=Side.Ask, size=15, order_id=0, price_ticks=15000),
    ]
    blob = b"".join(encode_event(e) for e in cases)
    parsed = list(iter_itch_events(blob))
    assert len(parsed) == 8
    assert [p.kind for p in parsed] == [c.kind for c in cases]
    assert parsed[0].order_id == 11 and parsed[0].price_ticks == 14990
    assert parsed[1].kind == EventType.ADD_MPID and parsed[1].side == Side.Ask
    assert parsed[6].new_order_id == 99
    assert parsed[7].kind == EventType.TRADE and parsed[7].price_ticks == 15000


def test_big_endian_price_and_id():
    # Framed Add: length 36, raw fields BE.
    buf = bytearray(38)
    buf[0:2] = (36).to_bytes(2, "big")
    buf[2] = ord("A")
    buf[3:5] = (1).to_bytes(2, "big")
    buf[5:7] = (1).to_bytes(2, "big")
    buf[7:13] = (0x010203040506).to_bytes(6, "big")
    buf[13:21] = (0x100000002).to_bytes(8, "big")
    buf[21] = ord("B")
    buf[22:26] = (1000).to_bytes(4, "big")
    buf[26:34] = b"AAPL    "
    buf[34:38] = (15000).to_bytes(4, "big")
    got = list(iter_itch_events(bytes(buf)))
    assert len(got) == 1
    ev = got[0]
    assert ev.order_id == 0x100000002
    assert ev.size == 1000
    assert ev.price_ticks == 15000
    assert ev.ts_ns == 0x010203040506
    le_price = int.from_bytes(buf[34:38], "little")
    assert le_price != 15000


def test_unframed_and_message_boundaries():
    a = encode_event(_ev(order_id=1), framed=False)
    b = encode_event(_ev(kind=EventType.DELETE, order_id=1), framed=False)
    parsed = list(iter_itch_events(a + b))
    assert [p.kind for p in parsed] == [EventType.ADD, EventType.DELETE]


def test_truncated_and_malformed():
    stats = ItchParseStats()
    good = encode_event(_ev(order_id=5))
    junk = b"\x00\x03ZZZ" + good[:8]  # short framed garbage + truncated good
    evs = list(iter_itch_events(junk, stats=stats))
    assert stats.truncated >= 1 or stats.skipped_type >= 1
    # A lone type byte with no payload is truncated, not raised.
    stats2 = ItchParseStats()
    assert list(iter_itch_events(b"A", stats=stats2)) == []
    assert stats2.truncated == 1


def test_lazy_streaming_does_not_materialize():
    events = [_ev(order_id=i, size=10 + i) for i in range(1, 81)]
    blob = b"".join(encode_event(e) for e in events)
    gen = iter_itch_events(io.BytesIO(blob), chunk_size=64)
    first = next(gen)
    assert first.order_id == 1
    rest = list(gen)
    assert len(rest) == 79
    assert rest[-1].order_id == 80


def test_large_stream_chunked():
    n = 500
    blob = b"".join(encode_event(_ev(order_id=i + 1, size=1)) for i in range(n))
    stats = ItchParseStats()
    count = sum(1 for _ in iter_itch_events(io.BytesIO(blob), chunk_size=128, stats=stats))
    assert count == n
    assert stats.emitted == n
    assert stats.bytes_read == len(blob)
