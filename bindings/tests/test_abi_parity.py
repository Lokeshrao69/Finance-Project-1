"""C++ <-> Python ABI parity test for the Nexus-LOB state contract.

Requires the compiled bridge module `nexus_engine` (build `bindings/` first) and
pytest. Skips cleanly if the module isn't built yet:

    pytest bindings/tests/test_abi_parity.py -v
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python_quant"))

import numpy as np  # noqa: E402
import pytest  # noqa: E402

from nexus_quant import book_state as bs  # noqa: E402

nexus_engine = pytest.importorskip(
    "nexus_engine",
    reason="C++ bridge not built yet — build bindings/ then re-run (see CONTRACT.md).",
)


def test_constants_match():
    assert nexus_engine.DEPTH == bs.DEPTH
    assert nexus_engine.CONTRACT_VERSION == bs.CONTRACT_VERSION


def test_struct_size_matches_dtype():
    # The core ABI lock: C++ sizeof(BookStateView) == NumPy dtype itemsize.
    assert nexus_engine.STATE_NBYTES == bs.BOOK_STATE_DTYPE.itemsize


def test_view_matches_python_contract():
    v = nexus_engine.Engine().view()
    for name, dt in (
        ("bid_px", bs.PX_DTYPE), ("bid_sz", bs.SZ_DTYPE), ("bid_ct", bs.CT_DTYPE),
        ("ask_px", bs.PX_DTYPE), ("ask_sz", bs.SZ_DTYPE), ("ask_ct", bs.CT_DTYPE),
    ):
        assert v[name].shape == (bs.DEPTH,)
        assert v[name].dtype == np.dtype(dt)


def test_zero_copy_vs_snapshot_semantics():
    e = nexus_engine.Engine()
    live = e.view()        # aliases engine memory (live window)
    frozen = e.snapshot()  # owning copy
    e._debug_set_level(nexus_engine.Side.Bid, 0, 10_000, 500, 3)
    assert live["bid_px"][0] == 10_000  # live view reflects the mutation
    assert frozen["bid_px"][0] == 0     # earlier snapshot is unaffected


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__, "-v"]))
