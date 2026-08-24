# Nexus-LOB — project context & session handoff

> **Purpose of this file:** persistent memory across Claude Code sessions. Read it
> first every session. When you finish a chunk of work, update **§6 Status** and
> **§7 Next steps** so the next session resumes without re-deriving everything.
> Last updated: **2026-08-24**.

---

## 1. What this project is

**Nexus-LOB** — a hybrid **C++/Python** limit-order-book (LOB) trading &
market-microstructure platform, built as a **finance-placement portfolio project**
(target desks: JPMC Quantitative Research, Nomura Algo Strategies, Goldman Sachs
Systematics). The goal is a project that reads as *institutional-grade* rather than
a yfinance backtester — it signals low-latency systems design, order-book
mechanics, RL for optimal execution, and GPU risk analytics.

**Timeline:** ~2 months / 8 weeks, **2 people**.

Headline resume metrics we intend to produce (targets, not yet achieved):
- C++20 matching engine: **>500k orders/sec, sub-microsecond latency**, zero-alloc.
- PPO/GRPO execution agent: **~14% lower slippage vs VWAP** under simulated
  high-volatility queue dynamics.
- CUDA Monte-Carlo VaR/CVaR: **~40× speedup** vs CPU.

## 2. Architecture (5 subsystems)

1. **Ultra-low-latency matching engine (C++20)** — zero-allocation memory pools,
   intrusive doubly-linked lists + ring buffers, O(1) price-level lookup; Limit /
   Market / FOK / IOC / Cancel / Modify. ITCH 5.0 binary replay.
2. **Microstructure sim + RL execution agent (Python / Pybind11)** — the C++ engine
   exposed as a Gymnasium env; friction/impact modeling (queue drift, latency,
   Almgren–Chriss); PPO/GRPO agent vs TWAP/VWAP/Avellaneda–Stoikov baselines.
3. **GPU risk engine (CUDA / PyTorch C++ extension)** — 100k+ GBM / jump-diffusion
   paths in parallel → real-time VaR/CVaR fed back as a dynamic inventory penalty.
4. **Zero-copy pipeline + dashboard** — async WebSocket / shared-memory IPC → live
   L3 depth, spread heatmaps, fill-latency histograms, PnL.
5. *(numbered "subsystem 5" in code comments — the shared-memory ring → dashboard.)*

**Prices are always INTEGER TICKS**, never floats (bit-exact with the engine and
the ITCH feed).

## 3. Repo layout (monorepo — one GitHub repo)

```
Finance Project-1/            # repo root (branch: main)
├── cpp_engine/               # Person A — C++ engine
│   ├── include/nexus/book_state.hpp   # [DONE] frozen state contract
│   ├── src/                            # [TODO] matching engine impl (not created yet)
│   └── tests/abi_check.cpp            # [DONE] standalone ABI check
├── python_quant/             # Person B — quant / RL
│   └── nexus_quant/
│       ├── __init__.py                # [DONE] package exports
│       └── book_state.py              # [DONE] dtype mirror + StubOrderBook
│   └── tests/test_contract_smoke.py   # [DONE] pure-numpy smoke test
├── bindings/                 # the C++↔Python merge point
│   ├── pybind_wrapper.cpp             # [DONE, placeholder Engine]
│   ├── CONTRACT.md                    # [DONE] full state-contract spec
│   └── tests/test_abi_parity.py       # [DONE] needs compiled module
├── data/                     # tick data — gitignored, never pushed
├── .gitignore                # [DONE]
├── CMakeLists.txt            # [MISSING] root build
├── pyproject.toml            # [MISSING] python packaging
└── CLAUDE.md                 # this file
```

## 4. Two-person split

- **Person A — Systems & Infrastructure Lead:** C++ engine, memory pools, Pybind11,
  CUDA, IPC, profiling.
- **Person B — Quant Research & RL Lead:** ITCH parser, Gymnasium env, RL training,
  baselines, analytics dashboard.

**8-week roadmap (condensed):** W1-2 engine core + ITCH parser/replay · W3-4 pybind
bridge + Gymnasium env + IPC + baseline strategies · W5-6 CUDA risk engine + PPO/GRPO
training · W7-8 profiling, dashboard, benchmarks, write-up.

**Critical integration interfaces (freeze early to work in parallel):**
- **State contract** `nexus::BookStateView` / `BOOK_STATE_DTYPE` — **FROZEN (v1).**
- **Environment contract** `reset()`/`step()` I/O of `OrderBookEnv` — **not yet defined.**

**Git workflow:** `main` only holds code that compiles/runs. Feature branches
(`feature/memory-pool`, `feature/ppo-agent`, …) → PR → review → merge.

## 5. Design decisions already locked (don't silently change)

- **Contract-first seam.** Instead of writing the matching engine first, we froze the
  *cross-language state contract* so both people build in parallel. `StubOrderBook`
  (pure Python) emulates the future C++ `Engine.view()/snapshot()` interface, so the
  RL env can be built/trained before the C++ engine lands — and later becomes the
  **diff-test oracle** the C++ engine is validated against.
- **`BookStateView` layout is ABI-frozen (v1).** Depth `kDepth = 10` per side. Field
  order: all 8-byte members, then 4-byte, then 1-byte → padding-free on LP64.
  `sizeof == 40*kDepth + 48 == 448`. Any change = ABI break → bump
  `kBookStateContractVersion`, update `book_state.py` (`BOOK_STATE_DTYPE` + `DEPTH`),
  re-run parity tests.
- **Two view flavors across the seam:** `view()` = zero-copy live window (RL hot path,
  valid only until the next mutating engine call); `snapshot()` = owning copy (safe to
  retain, telemetry/tests/cross-thread).

## 6. Status — what's DONE (verified 2026-08-24)

**Phase 0 — integration seam frozen & stubbed. Nothing substantive built yet.**

| Component | File | State |
|---|---|---|
| C++ state contract | `cpp_engine/include/nexus/book_state.hpp` | ✅ complete, ABI-locked |
| Python dtype mirror + `StubOrderBook` | `python_quant/nexus_quant/book_state.py` | ✅ complete |
| Package exports | `python_quant/nexus_quant/__init__.py` | ✅ |
| Pybind bridge (placeholder `Engine`) | `bindings/pybind_wrapper.cpp` | ⚠️ seam only, no matching |
| Python smoke test (no build) | `python_quant/tests/test_contract_smoke.py` | ✅ |
| ABI parity test (needs build) | `bindings/tests/test_abi_parity.py` | ✅ (skips if unbuilt) |
| Standalone C++ ABI check | `cpp_engine/tests/abi_check.cpp` | ✅ **compiles+runs** |

Verified this session with MSYS2 g++:
`g++ -std=c++20 -O2 -I cpp_engine/include cpp_engine/tests/abi_check.cpp -o abi_check.exe`
→ prints `contract v1, kDepth=10, sizeof=448 (expected 448), alignof=8`, contiguous
offsets. Python tests **not** run (no interpreter — see §8).

## 7. Next steps (ordered; low-risk foundations first)

1. ~~**`.gitignore`**~~ — ✅ done 2026-08-24.
2. ~~**`bindings/CONTRACT.md`**~~ — ✅ done 2026-08-24 (full spec, offsets verified).
3. **Build system** — root `CMakeLists.txt` (engine lib + pybind module via
   FetchContent + `abi_check`) and `pyproject.toml`. **Note: cannot build in this
   Windows/Git-Bash shell — see §8; author the files, build in WSL/Ubuntu.**
4. **First real subsystem — pick one and branch:**
   - **A (Person A):** real C++ `LimitOrderBook` (Order/LimitLevel structs, zero-alloc
     pool, intrusive lists, O(1) ID map, matching) → wire into the pybind `Engine` so
     it drives `BookStateView`. Diff-test vs `StubOrderBook`.
   - **B (Person B):** ITCH 5.0 parser (Add/Execute/Cancel) + L2 reconstruction +
     `OrderBookEnv` Gymnasium env (obs = L2 depth + inventory + PnL + time-left;
     continuous action = order placement distance from mid; `step()` with
     adverse-selection + inventory penalties). Can run against `StubOrderBook` with no
     C++ engine present — **the natural next thing to build.**

## 8. Environment reality (IMPORTANT — read before running anything)

This Claude session runs on **Windows 11 + Git Bash / MSYS2** (NOT WSL). The repo
lives on a **OneDrive** path (`C:\Users\pekka\OneDrive\Documents\Finance Project-1`).

| Tool | Status in this shell |
|---|---|
| `g++` | ✅ `/c/msys64/ucrt64/bin/g++` — C++20 OK (can compile/run C++-only checks) |
| `python`/`python3` | ❌ only the WindowsApps stub — **no real interpreter** |
| `cmake`, `make`, `ninja` | ❌ not installed |
| `clang++`, `cl` (MSVC) | ❌ not found |
| `nvcc` / CUDA | ❌ not found |
| `wsl` | ❌ not available in this shell |

**Consequences:**
- ✅ Can do **C++-only** compile/run checks here (e.g. `abi_check.cpp`).
- ❌ Cannot run Python tests, build the pybind module, or compile CUDA here.
- The **project plan assumes WSL/Ubuntu + NVIDIA GPU (Dell G15)** as the real dev env.
  Anything needing Python / pybind / CUDA should be built and run **in WSL**, not here.
- **To unblock full builds:** either set up WSL (`wsl --install`, then clone/build in
  the Linux filesystem — not under `/mnt/c/OneDrive`, which is slow and OneDrive can
  corrupt build artifacts), or install real Python + CMake + a CUDA toolkit on Windows.
- **OneDrive caveat:** keep `build/`, `data/`, venvs out of the synced tree (or move the
  repo off OneDrive) — OneDrive sync + build artifacts is a known source of breakage.

## 9. How to verify current work

```bash
# C++ contract (works in this shell with MSYS2 g++):
g++ -std=c++20 -O2 -Wall -Wextra -I cpp_engine/include \
    cpp_engine/tests/abi_check.cpp -o abi_check.exe && ./abi_check.exe

# Python smoke test (needs a real Python + numpy — run in WSL/venv):
python python_quant/tests/test_contract_smoke.py

# ABI parity (needs the compiled pybind module — build bindings/ first):
pytest bindings/tests/test_abi_parity.py -v
```
