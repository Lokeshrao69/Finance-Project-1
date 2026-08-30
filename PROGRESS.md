# Nexus-LOB — Progress Report

**Status date:** 2026-08-30 · **Branch:** `main` · **Milestone:** Phase 1 — C++ matching
engine implemented + pybind `Engine` wired to it. This file is a plain-language
snapshot for anyone (Person A or Person B) picking the project up; the authoritative,
constantly-updated handoff doc is `CLAUDE.md`.

> TL;DR: the cross-language state contract is frozen, the C++ matching engine is
> built and passing its own tests (86/86), the Python bridge drives that real engine,
> and the **shared-memory ring** that will feed the dashboard (subsystem 5's C++ core)
> is built and demoed live. **Not yet built/verified:** the compiled `nexus_engine`
> module (must build in WSL) and everything downstream (ITCH parser, RL env, agent,
> GPU risk, the Python dashboard grain on the ring).

---

## 1. What the project is (30 seconds)

Nexus-LOB is a hybrid **C++/Python** limit-order-book (LOB) trading & market-
microstructure platform built as a **finance-placement portfolio project** (targets:
JPMC Quant Research, Nomura Algo, Goldman Systematics). Two people, ~8 weeks.

Five subsystems (from CLAUDE.md §2):
1. **Ultra-low-latency C++ matching engine** (Person A) — Limit / Market / FOK / IOC /
   Cancel / Modify, zero-allocation, integer-tick prices.
2. **Microstructure sim + RL execution agent** (Person B) — Gymnasium env; PPO/GRPO vs
   TWAP/VWAP/Avellaneda–Stoikov baselines.
3. **GPU risk engine (CUDA)** — Monte-Carlo VaR/CVaR over 100k+ paths (stubbed, not started).
4. **Zero-copy pipeline + dashboard** — shmem/WebSocket → live depth, latency, PnL (not started).
5. *(Subsystem 5 in comments — the shmem ring → dashboard.)*

Headline resume targets (not yet met): >500k orders/sec sub-µs matching; ~14% lower
slippage vs VWAP; ~40× CUDA VaR speedup.

---

## 2. The architecture seam (why things are built in this order)

The two people work in **parallel**, so we froze the *one data structure* that crosses
the C++↔Python boundary before writing the engine — the **state contract**
`BookStateView` (C++) ↔ `BOOK_STATE_DTYPE` (NumPy). Both sides build against that frozen
shape:

- **Person B** can build/train the RL env against a pure-Python `StubOrderBook` that
  emits the exact same `view()`/`snapshot()` interface as the real engine — **today**, no
  C++ build needed.
- **Person A** drops the real `LimitOrderBook` behind the same seam. `StubOrderBook`
  then becomes the **reference oracle** the real engine is diff-tested against.

Rules that must never be broken (they keep the two halves compatible):
- **Prices are integer ticks** (`int64`), never floats. `0` in a price slot = empty level.
- **Fixed depth** `kDepth = DEPTH = 10` per side; index 0 = best level; bids descend,
  asks ascend; empty levels zero-padded.
- **Layout is ABI-frozen:** `sizeof(BookStateView) == 40*10 + 48 == 448` bytes.
  Changing it = ABI break (bump the version, update the Python mirror, re-run parity tests).

See `bindings/CONTRACT.md` for the full spec.

---

## 3. What is DONE (and verified)

### 3a. C++ matching engine — **built & self-tested** ✅
| Piece | File | Notes |
|---|---|---|
| Frozen state contract | `cpp_engine/include/nexus/book_state.hpp` | ABI v1, `sizeof == 448`, `alignof == 8` |
| Value types | `cpp_engine/include/nexus/types.hpp` | `OrderId/Price/Qty`, `Fill`, `Status`, `ExecResult` |
| Zero-alloc order pool | `cpp_engine/include/nexus/order_pool.hpp` | fixed slab + intrusive free-list |
| Matching engine | `cpp_engine/include/nexus/limit_order_book.hpp` | Limit/Market/FOK/IOC/Cancel/Modify, FIFO, O(1) level lookup & id map |
| **Engine tests** | `cpp_engine/tests/lob_test.cpp` | **86/86 checks pass** |
| ABI lock | `cpp_engine/tests/abi_check.cpp` | prints 448 / alignof 8 ✅ |

Verified 2026-08-30 with MSYS2 g++: `lob_test` → `86 checks, 0 failed / ALL PASS`;
`abi_check` → `sizeof = 448 (expected 448)`. Engine behavior covered: resting & L2
ladder order, full/partial crosses, price-time FIFO, multi-level sweeps, IOC / FOK /
market semantics, cancel, modify (priority-keeping reduce vs. priority-losing reprice),
and every reject path (bad qty/price, dup id, pool-full).

### 3b. Build system ✅
- `CMakeLists.txt` — engine lib (header-only today → static lib when `.cpp` land), the
  `nexus_engine` pybind module, `abi_check` + `lob_test` under CTest, and CUDA hooks
  (on-but-stubbed).
- `pyproject.toml` — scikit-build-core packaging; pytest `pythonpath` includes
  `python_quant` and `bindings` so tests import both `nexus_quant` and the compiled
  module without a pip install.

### 3c. Python side — **authored, not yet run** ⚠️
- `python_quant/nexus_quant/book_state.py` — NumPy dtype mirror + `StubOrderBook`
  (the oracle).
- `python_quant/tests/test_contract_smoke.py` — pure-NumPy smoke test.
- `python_quant/nexus_quant/__init__.py` — package exports.

### 3d. Pybind bridge — **wired to the real engine, not yet compiled** ⚠️
`bindings/pybind_wrapper.cpp` no longer a placeholder. `Engine` now owns a real
`LimitOrderBook` and exposes order entry, fills, and the two view flavors (see §5).
`bindings/tests/test_abi_parity.py` updated to drive the real engine.

> **⚠️ Important status nuance:** the C++ engine is verified here. The **compiled
> `nexus_engine` module and the Python tests are NOT built/run yet** — this machine's
> shell has no real Python / CMake / pybind (see §7). They must be built in **WSL**.

### 3e. Shared-memory ring + flow (subsystem 5, C++) — **built & demoed** ✅
| Piece | File | Notes |
|---|---|---|
| Shared-memory SPSC ring | `cpp_engine/include/nexus/shm_ring.hpp` | OS shmem (POSIX + Windows), lock-free SPSC, drop-new-on-full, 448-B slots |
| Synthetic flow generator | `cpp_engine/include/nexus/flow_gen.hpp` | seeded LCG, mid random-walk + passive/aggressive mix |
| Ring tests | `cpp_engine/tests/ring_test.cpp` | **30,011 checks pass** (order + integrity; drop semantics) |
| Publisher + probe demos | `cpp_engine/demos/ring_producer.cpp`, `ring_probe.cpp` | live cross-process book demo — **verified 200 frames, 0 dropped** |

This is the transport the future dashboard consumes: the engine (real or synthetic flow)
publishes its `BookStateView` into the ring after every order; a reader process follows
the live book. Same frozen 448-byte payload end to end.

---

## 4. What is NOT done yet

- ❌ Pybind module built + parity tests green (author one shell, build in WSL).
- ❌ ITCH 5.0 parser + L2 reconstruction.
- ❌ `OrderBookEnv` Gymnasium env + RL agent (PPO/GRPO) + baselines.
- ❌ Diff-test harness `Engine` vs `StubOrderBook` on identical order streams.
- ❌ CUDA VaR/CVaR risk engine.
- ❌ Python dashboard on top of the shmem ring (the ring's C++ publisher/reader core
  ✅ is done — see §3e).

---

## 5. For Person B — the Python API you code against

Once `nexus_engine` is built, the seam surface is exactly the same shape as
`StubOrderBook`, so your env code can target either. The real engine adds order entry:

```python
import nexus_engine as ne

e = ne.Engine()                      # default price band 1..100_000 ticks
# e = ne.Engine(min_price=1, max_price=500_000, pool_capacity=1 << 18)

# Rest a GTC limit: place a bid at price 10_000 for 500 shares.
r = e.submit_limit(1, ne.Side.Bid, 10_000, 500, ne.TimeInForce.GTC)
r  # -> {"id": 1, "status": ne.Status.Accepted, "filled": 0, "resting": 500}

# Aggressive buy lifting the best ask(es) — fills come back per call.
e.submit_limit(10, ne.Side.Ask, 10_050, 100, ne.TimeInForce.GTC)
r2 = e.submit_limit(11, ne.Side.Bid, 10_050, 150, ne.TimeInForce.GTC)
r2  # -> {"id": 11, "status": ne.Status.Filled, "filled": 100, "resting": 0}
e.fills()  # -> [(10, 11, 10_050, 100, ne.Side.Bid)]  # (maker, taker, px, qty, aggressor)

# Quote introspection
e.best_bid(), e.best_ask(), e.spread(), e.live_orders()

# Observation (contract arrays): view() is ZERO-COPY (aliases engine memory —
# normalize/copy it now); snapshot() is a safe owning copy.
obs = e.view()          # {"bid_px","bid_sz","bid_ct","ask_px","ask_sz","ask_ct", ...}
snap = e.snapshot()
```

Key enum values:
- `Side`: `Bid`, `Ask`, `None_` (Python `None` is a keyword, hence `None_`).
- `TimeInForce`: `GTC` (rest residual), `IOC` (fill-then-kill), `FOK` (all-or-nothing).
- `Status`: `Accepted`, `Filled`, `PartiallyFilledResting`, `Canceled`,
  `Rejected_DupId`, `Rejected_BadPrice`, `Rejected_BadQty`, `Rejected_PoolFull`,
  `Rejected_FOK`, `NoOp`.

**You can start NOW against `StubOrderBook`** (no engine build needed) — build the ITCH
parser and `OrderBookEnv` on the frozen contract, then swap `StubOrderBook` for
`Engine` when the WSL build lands.

---

## 6. How to see everything work

```bash
# C++ only (works on Windows + MSYS2 g++, no build system needed):
g++ -std=c++20 -O2 -Wall -Wextra -I cpp_engine/include cpp_engine/tests/abi_check.cpp -o abi_check.exe && ./abi_check.exe
g++ -std=c++20 -O2 -Wall -Wextra -I cpp_engine/include cpp_engine/tests/lob_test.cpp -o lob_test.exe && ./lob_test.exe
g++ -std=c++20 -O2 -Wall -Wextra -I cpp_engine/include cpp_engine/tests/ring_test.cpp -o ring_test.exe && ./ring_test.exe

# Live shared-memory demo (subsystem 5) — run the producer in one terminal, the probe in another:
g++ -std=c++20 -O2 -I cpp_engine/include cpp_engine/demos/ring_producer.cpp -o ring_producer
g++ -std=c++20 -O2 -I cpp_engine/include cpp_engine/demos/ring_probe.cpp -o ring_probe
./ring_producer nex_aapl 4000 16384 0xC0FFEE 1     # terminal 1: book -> ring
./ring_probe nex_aapl 4000 5                          # terminal 2: watch it live

# Full build + Python tests (WSL / Ubuntu + real Python required):
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNEXUS_BUILD_PYBIND=ON
cmake --build build -j && ctest --test-dir build --output-on-failure
pytest bindings/tests/test_abi_parity.py -v      # contract parity + engine seam
pytest python_quant/tests/test_contract_smoke.py -v
```

---

## 7. Environment notes (why some things say "not run here")

This session runs on **Windows 11 + Git Bash / MSYS2**, repo on a **OneDrive** path.
The shell has `g++` (C++20 ✅) but **no real Python, CMake, CUDA, or WSL** (❌). So:

- ✅ C++-only compile/run checks work here (engine tests above).
- ❌ Building the pybind module / running Python tests must happen in **WSL/Ubuntu**
  (the intended dev env) or after installing real Python + CMake + CUDA on Windows.
- ⚠️ Keep `build/`, `data/`, venvs **out of the OneDrive-synced tree** — sync + build
  artifacts is a known breakage source.

See `CLAUDE.md` §8 for the full table and WSL setup guidance.

---

## 8. Suggested next steps

1. **Verify the seam in WSL** (§6 full build) — unblock everything downstream.
2. **Person B:** ITCH 5.0 parser (Add/Execute/Cancel → L2) + `OrderBookEnv`
   (obs = L2 depth + inventory + PnL + time-left; action = offset from mid; adverse-
   selection + inventory penalties). Runs against `StubOrderBook` now.
3. **Both:** diff-test harness — replay one order stream through `Engine` and
   `StubOrderBook`, assert identical `BookStateView`.
4. Later: PPO/GRPO agent vs baselines · CUDA VaR · dashboard.
