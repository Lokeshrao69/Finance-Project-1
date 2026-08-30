# Nexus-LOB — project context & session handoff

> **Purpose of this file:** persistent memory across Claude Code sessions. Read it
> first every session. When you finish a chunk of work, update **§6 Status** and
> **§7 Next steps** so the next session resumes without re-deriving everything.
> Last updated: **2026-08-30**.

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
│   ├── include/nexus/book_state.hpp        # [DONE] frozen state contract (v1, 448 B)
│   ├── include/nexus/types.hpp             # [DONE] OrderId/Price/Qty, Fill, Status, ExecResult
│   ├── include/nexus/order_pool.hpp        # [DONE] zero-alloc intrusive order pool
│   ├── include/nexus/limit_order_book.hpp  # [DONE] matching engine (Limit/Market/FOK/IOC/Cancel/Modify)
│   ├── include/nexus/shm_ring.hpp          # [DONE] shared-memory SPSC ring (subsystem 5)
│   ├── include/nexus/flow_gen.hpp          # [DONE] seeded synthetic order-flow generator
│   ├── src/                                 # (empty — engine is header-only for now)
│   ├── demos/
│   │   ├── ring_producer.cpp               # [DONE] book -> shmem ring publisher
│   │   └── ring_probe.cpp                  # [DONE] live shmem ring reader
│   └── tests/
│       ├── abi_check.cpp                  # [DONE] ABI lock (sizeof == 448)
│       ├── lob_test.cpp                   # [DONE] engine correctness — 86/86 checks ✓
│       └── ring_test.cpp                  # [DONE] ring order/drop semantics — 30k checks ✓
├── python_quant/             # Person B — quant / RL
│   └── nexus_quant/
│       ├── __init__.py                    # [DONE] package exports
│       └── book_state.py                  # [DONE] dtype mirror + StubOrderBook (oracle)
│   └── tests/test_contract_smoke.py       # [DONE] pure-numpy smoke test
├── bindings/                 # the C++↔Python merge point
│   ├── pybind_wrapper.cpp                 # [DONE] real Engine bridge (order entry + views + fills)
│   ├── CONTRACT.md                        # [DONE] full state-contract spec
│   └── tests/test_abi_parity.py           # [DONE] needs compiled module (build in WSL)
├── data/                     # tick data — gitignored, never pushed
├── .gitignore                # [DONE]
├── CMakeLists.txt            # [DONE] root build (engine lib + pybind + CTest + CUDA hooks)
├── pyproject.toml            # [DONE] scikit-build-core packaging
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

## 6. Status — what's DONE (verified 2026-08-30)

**Phase 1 — C++ matching engine implemented & tested; pybind `Engine` wired to it.**

| Component | File | State |
|---|---|---|
| C++ state contract | `cpp_engine/include/nexus/book_state.hpp` | ✅ complete, ABI-locked (v1, 448 B) |
| Engine value types | `cpp_engine/include/nexus/types.hpp` | ✅ OrderId/Price/Qty, Fill, Status, ExecResult |
| Zero-alloc order pool | `cpp_engine/include/nexus/order_pool.hpp` | ✅ fixed slab + intrusive free-list |
| Matching engine | `cpp_engine/include/nexus/limit_order_book.hpp` | ✅ Limit/Market/FOK/IOC/Cancel/Modify |
| Engine correctness tests | `cpp_engine/tests/lob_test.cpp` | ✅ **86/86 checks pass** |
| Zero-alloc id→Order map | `cpp_engine/include/nexus/limit_order_book.hpp` (`IdMap`) | ✅ replaced `std::unordered_map` — **hot path is genuinely allocation-free** (bench proves 0 allocs/op) |
| IdMap stress tests | `cpp_engine/tests/id_map_test.cpp` | ✅ **4,676,294 checks pass** (collision/wraparound/backtrack-shift) |
| Shared-memory SPSC ring | `cpp_engine/include/nexus/shm_ring.hpp` | ✅ POSIX+Windows shmem, drop-new-on-full, 448-B slots |
| Synthetic flow generator | `cpp_engine/include/nexus/flow_gen.hpp` | ✅ seeded, deterministic pre-ITCH flow |
| Ring correctness tests | `cpp_engine/tests/ring_test.cpp` | ✅ **30,011 checks pass** |
| Ring producer + probe | `cpp_engine/demos/*.cpp` | ✅ live cross-process book demo (verified on Windows) |
| Benchmark harness | `cpp_engine/bench/bench.cpp` | ✅ throughput + latency + zero-alloc proof — **0 allocs/op on both workloads** (see §7 #8) |
| Python dtype mirror + `StubOrderBook` | `python_quant/nexus_quant/book_state.py` | ✅ complete (diff-test oracle) |
| Package exports | `python_quant/nexus_quant/__init__.py` | ✅ |
| Root build | `CMakeLists.txt` | ✅ engine lib + pybind module + CTest + CUDA hooks |
| Python packaging | `pyproject.toml` | ✅ scikit-build-core |
| Pybind bridge (REAL engine) | `bindings/pybind_wrapper.cpp` | ✅ order entry + zero-copy views + fills |
| Python smoke test (no build) | `python_quant/tests/test_contract_smoke.py` | ✅ |
| ABI parity test (needs build) | `bindings/tests/test_abi_parity.py` | ⚠️ updated for real engine — build & run in WSL |
| Standalone C++ ABI check | `cpp_engine/tests/abi_check.cpp` | ✅ **compiles+runs** |

Verified this session with MSYS2 g++ (all four green):
```
g++ -std=c++20 -O2 -Wall -Wextra -I cpp_engine/include cpp_engine/tests/abi_check.cpp -o abi_check.exe && ./abi_check.exe
g++ -std=c++20 -O2 -Wall -Wextra -I cpp_engine/include cpp_engine/tests/lob_test.cpp -o lob_test.exe && ./lob_test.exe
g++ -std=c++20 -O2 -Wall -Wextra -I cpp_engine/include cpp_engine/tests/ring_test.cpp -o ring_test.exe && ./ring_test.exe
g++ -std=c++20 -O2 -Wall -Wextra -I cpp_engine/include cpp_engine/tests/id_map_test.cpp -o id_map_test.exe && ./id_map_test.exe
```
→ ABI lock prints `contract v1, kDepth=10, sizeof=448 (expected 448), alignof=8`;
`lob_test` prints `86 checks, 0 failed` / `ALL PASS`; `ring_test` prints
`30011 checks, 0 failed` / `ALL PASS` (order+integrity over a writer/reader thread on
real OS shared memory, plus drop-new-on-full); `id_map_test` prints
`4676294 checks, 0 failed` / `ALL PASS` (collision/wraparound/backtrack-shift).
Python tests & the pybind module are **not** run here (no interpreter / no CMake —
see §8); compile `bindings/` in WSL. The benchmark harness proves **0 allocs/op**
on the hot path; throughput/latency re-measured in WSL (§7 #8). Ring demos run here
too: `./ring_producer.exe nex 200 ... & ./ring_probe.exe nex 15 5`.

## 7. Next steps (ordered; low-risk foundations first)

1. ~~**`.gitignore`**~~ — ✅ done 2026-08-24.
2. ~~**`bindings/CONTRACT.md`**~~ — ✅ done 2026-08-24 (full spec, offsets verified).
3. ~~**Build system**~~ — ✅ `CMakeLists.txt` + `pyproject.toml` authored 2026-08-30
   (build/test in WSL — can't build in this shell, §8).
4. ~~**Person A: real `LimitOrderBook`**~~ — ✅ implemented + 86/86 tests, wired into the
   pybind `Engine` (order entry, fills, zero-copy views). Diff-test vs `StubOrderBook`
   pending the WSL build.
5. **Verify the real seam in WSL** (first check when a Linux env is available):
   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNEXUS_BUILD_PYBIND=ON
   cmake --build build -j && ctest --test-dir build --output-on-failure
   pytest bindings/tests/test_abi_parity.py -v
   ```
   → expect `nexus_engine` imports; parity + new order-entry tests green.
6. **Person B — ITCH 5.0 parser + Gymnasium `OrderBookEnv`** (the natural next build;
   runs against `StubOrderBook` with no C++ engine, or against the real `Engine` once
   step 5 lands): ITCH Add/Execute/Cancel → L2 reconstruction; `OrderBookEnv`
   (obs = L2 depth + inventory + PnL + time-left; action = offset-from-mid; `step()`
   with adverse-selection + inventory penalties). Also the **diff-test harness**: replay
   an identical order stream through `Engine` and `StubOrderBook` and assert the published
   `BookStateView` matches.
7. ~~**Subsystem 5 C++ plumbing**~~ — ✅ 2026-08-30: `ShmRing` (SPSC, drop-new-on-full,
   POSIX+Windows) + `FlowGen` + `ring_producer`/`ring_probe` demos verified live on
   Windows. The Python dashboard (subsystem 4/5) will consume this ring later.
8. ~~**Zero-alloc hot path (make the idle claim TRUE)**~~ — ✅ 2026-08-30: replaced
   `id_map_` (`std::unordered_map`, ~1 malloc/resting order) with `nexus::IdMap`, a
   pre-sized open-addressing linear-probe map with backtrack-shift deletion. The
   benchmark (`cpp_engine/bench/bench.cpp`) now proves **0 allocs/op** on both
   workloads; `lob_test` 86/86 and new `id_map_test` 4,676,294 checks pass; ABI lock
   (448 B) intact. Throughput/latency still to be re-measured on real hardware (the
   Windows sandbox throttles memory workloads — §8).
9. **Later:** CUDA risk engine (subsystem 3); Python dashboard reading the shmem ring
   (subsystem 4/5).

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
- **Git behavior on this machine (don't get fooled):** an auto-checkpoint commits
  working-tree changes to `main` as commits titled **"Working Tree Changes"** — so after
  editing files, `git status` may legitimately read *clean* because they're already
  committed (not lost). Given the intended feature-branch → PR workflow, you may want to
  `git reset --soft` those and re-commit deliberately. `core.fsmonitor` was set to
  `false` (was `true`) during debugging — harmless; revert with
  `git config core.fsmonitor true` if desired.

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
