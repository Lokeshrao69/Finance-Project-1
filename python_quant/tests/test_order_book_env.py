"""OrderBookEnv + baseline tests."""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import numpy as np

from nexus_quant.baselines import compare, run_episode
from nexus_quant.book_port import StubBookAdapter
from nexus_quant.book_state import StubOrderBook
from nexus_quant.envs.order_book_env import (
    MAX_OFFSET,
    OBS_DIM,
    OrderBookEnv,
)


def test_reset_and_obs_shape():
    env = OrderBookEnv(inventory=400, horizon=8, seed=99)
    obs, info = env.reset()
    assert obs.shape == (OBS_DIM,)
    assert OBS_DIM == 44
    assert obs.dtype == np.float32
    assert env.inventory == 400
    assert env.t == 0
    assert info["arrival_mid"] > 0
    obs2, r, term, trunc, _ = env.step(0.0)
    assert obs2.shape == (44,)
    assert isinstance(r, float)
    assert term is False


def test_action_bounds_and_semantics():
    env = OrderBookEnv(inventory=500, horizon=6, seed=3)
    env.reset()
    _o, _r, _t, _u, info = env.step(np.array([-2.0], dtype=np.float32))
    assert info["mode"] == "market"
    assert info["action_ticks"] == -MAX_OFFSET

    env = OrderBookEnv(inventory=500, horizon=6, seed=3)
    env.reset()
    _o, _r, _t, _u, info = env.step(1.0)
    assert info["action_ticks"] == MAX_OFFSET


def test_inventory_conservation():
    Q = 600
    env = OrderBookEnv(inventory=Q, horizon=12, child_max=80, seed=21)
    env.reset()
    while True:
        _o, _r, term, trunc, _i = env.step(-1.0)
        if term or trunc:
            break
    filled = sum(sz for _, _, sz in env.fills)
    assert filled + env.inventory == Q
    assert env.inventory >= 0


def test_termination_and_truncation():
    env = OrderBookEnv(inventory=80, horizon=3, child_max=20, seed=8)
    env.reset()
    ended = False
    saw_trunc = False
    for _ in range(20):
        _o, _r, term, trunc, _i = env.step(1.0)  # passive → likely leftover
        if term or trunc:
            ended = True
            saw_trunc = saw_trunc or trunc
            assert term or env.t >= env.horizon
            break
    assert ended


def test_reset_restores_inventory():
    env = OrderBookEnv(inventory=300, horizon=5, seed=8)
    env.reset()
    env.step(-1.0)
    assert env.t == 1
    env.reset(seed=8)
    assert env.t == 0
    assert env.inventory == 300


def test_obs_finite_and_inv_feature():
    env = OrderBookEnv(inventory=200, horizon=4, seed=1)
    obs, _ = env.reset()
    assert np.all(np.isfinite(obs))
    assert abs(obs[40] - 1.0) < 1e-6
    assert abs(obs[41] - 1.0) < 1e-6


def test_injected_stub_book():
    stub = StubOrderBook()
    env = OrderBookEnv(inventory=100, horizon=3, seed=4, book=stub)
    env.reset()
    assert isinstance(env.book, StubBookAdapter)
    env.step(-1.0)
    assert stub.view()["bid_px"][0] != 0 or stub.view()["ask_px"][0] != 0


def test_reward_and_shortfall_are_finite():
    env = OrderBookEnv(inventory=200, horizon=5, seed=2)
    env.reset()
    _o, r, _t, _u, info = env.step(-1.0)
    assert np.isfinite(r)
    assert np.isfinite(info["shortfall_bps"])
    assert "pnl_ticks" in info


def test_baselines_run():
    rows = compare(seed=11, inventory=300, horizon=8, child_max=80)
    assert {r.name for r in rows} == {"twap", "vwap", "pov", "passive"}
    for r in rows:
        assert r.steps > 0
        assert r.filled + r.leftover == 300
        assert np.isfinite(r.reward)


def test_twap_episode_identity():
    env = OrderBookEnv(inventory=240, horizon=6, seed=5)
    res = run_episode(env, "twap", seed=5)
    assert res.filled + res.leftover == 240
