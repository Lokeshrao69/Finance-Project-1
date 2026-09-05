#!/usr/bin/env python
"""Train the PPO execution agent and compare it against the baselines.

Usage
-----
    PYTHONPATH=python_quant python python_quant/scripts/train_eval_agent.py \
        [--iters 1500] [--episodes 16] [--epochs 4] [--eval-every 250] \
        [--eval-episodes 40] [--seed 2756] [--unit-seed 10015] \
        [--out policy_ppo.npz] [--table-episodes 100] [--table-seed 48879]

The policy is written as a plain ``.npz`` of NumPy arrays and can be reloaded
later with ``PPOPolicy.load`` — no torch, no checkpoints.
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(_ROOT / "python_quant"))

from nexus_quant import (  # noqa: E402
    OrderBookEnv,
    PPOConfig,
    format_table,
    strategy_table,
    train_ppo,
)


def main() -> None:
    ap = argparse.ArgumentParser(description="Tune + evaluate NEXUS-LOB agent")
    ap.add_argument("--iters", type=int, default=1500)
    ap.add_argument("--episodes", type=int, default=16)
    ap.add_argument("--epochs", type=int, default=4)
    ap.add_argument("--eval-every", type=int, default=250)
    ap.add_argument("--eval-episodes", type=int, default=40)
    ap.add_argument("--seed", type=int, default=0xACE)
    ap.add_argument("--unit-seed", type=int, default=0x2717)
    ap.add_argument("--out", type=str, default="python_quant/artifacts/policy_ppo.npz")
    ap.add_argument("--table-episodes", type=int, default=100)
    ap.add_argument("--table-seed", type=int, default=0xBEEF)
    args = ap.parse_args()

    cfg = PPOConfig(
        iterations=args.iters,
        episodes=args.episodes,
        epochs=args.epochs,
        eval_every=args.eval_every,
        eval_episodes=args.eval_episodes,
        seed=args.seed,
        unit_seed=args.unit_seed,
    )
    t0 = time.time()
    policy, history = train_ppo(OrderBookEnv, cfg)
    print(f"trained {args.iters} iterations in {time.time() - t0:.1f}s")

    print(f"{'it':>4} {'reward':>9} {'shortfall_bps':>13} {'pg_loss':>8} {'v_loss':>7} {'entropy':>8}")
    for h in history:
        it, rw, sf, pg, vl, en = h.as_row()
        print(f"{it:>4} {rw:>9.3f} {sf:>13.4f} {pg:>8.4f} {vl:>7.3f} {en:>8.4f}")

    policy.save(args.out)
    print("saved policy ->", args.out)

    rows = strategy_table(
        policy, agent_name="ppo", n_episodes=args.table_episodes, seed=args.table_seed
    )
    print(f"\n=== execution comparison on {args.table_episodes} seeded episodes ===")
    print(format_table(rows))

    ppo = next(r for r in rows if r["name"] == "ppo")
    vwap = next(r for r in rows if r["name"] == "vwap")
    print(
        f"\nagent shortfall {ppo['shortfall_bps_mean']:.3f} bps vs "
        f"VWAP {vwap['shortfall_bps_mean']:.3f} bps "
        f"-> {ppo['vs_vwap_pct']:+6.1f}%"
    )


if __name__ == "__main__":
    main()