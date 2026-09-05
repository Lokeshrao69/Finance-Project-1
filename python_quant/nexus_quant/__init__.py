"""Nexus-LOB quant package (python_quant). Owner: Person B."""
from __future__ import annotations

from .book_state import (
    BOOK_STATE_DTYPE,
    CONTRACT_VERSION,
    CT_DTYPE,
    DEPTH,
    PX_DTYPE,
    SZ_DTYPE,
    Side,
    StubOrderBook,
    empty_state,
)
from .itch_parser import EventType, ItchParseStats, NormalizedEvent, iter_itch_events
from .replay import ReplayEngine, check_integrity
from .envs import OBS_DIM, OrderBookEnv

# PPO execution agent is exposed via nexus_quant.agents — pulled in lazily by
# the top-level convenience re-exports below (kept additive, no import order
# coupling with .envs).
from .agents import (
    PPOConfig,
    PPOPolicy,
    evaluate_policy,
    format_table,
    strategy_table,
    train_ppo,
)

__all__ = [
    "BOOK_STATE_DTYPE",
    "CONTRACT_VERSION",
    "CT_DTYPE",
    "DEPTH",
    "OBS_DIM",
    "PX_DTYPE",
    "SZ_DTYPE",
    "EventType",
    "ItchParseStats",
    "NormalizedEvent",
    "OrderBookEnv",
    "PPOConfig",
    "PPOPolicy",
    "ReplayEngine",
    "Side",
    "StubOrderBook",
    "check_integrity",
    "empty_state",
    "evaluate_policy",
    "format_table",
    "iter_itch_events",
    "strategy_table",
    "train_ppo",
]
