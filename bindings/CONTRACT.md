# Nexus-LOB — Cross-Domain State Contract (`BookStateView`)

**Contract version:** `1`  ·  **Status:** FROZEN  ·  **Owners:** Person A (C++
`cpp_engine`) defines it, Person B (`python_quant`) mirrors it, `bindings/` bridges it.

This document is the single source of truth for the one data structure that crosses
every language and process boundary in Nexus-LOB. If the C++ struct, the NumPy dtype,
and this document ever disagree, **that is a bug** — the parity tests exist to catch it.

- C++ definition: `cpp_engine/include/nexus/book_state.hpp`
- Python mirror: `python_quant/nexus_quant/book_state.py`
- Pybind bridge: `bindings/pybind_wrapper.cpp`
- Tests: `python_quant/tests/test_contract_smoke.py`,
  `bindings/tests/test_abi_parity.py`, `cpp_engine/tests/abi_check.cpp`

---

## 1. Why a frozen contract exists

The engine (C++) and the research/RL stack (Python) are developed **in parallel by two
people**. Rather than serialize the work behind "finish the engine first," we freeze the
*state snapshot* that flows engine → Python. Both sides then build against the frozen
shape:

- Person B builds the Gymnasium env and RL agent against a pure-Python
  `StubOrderBook` that emits this exact contract **today**.
- Person A later drops the real matching engine behind the same seam **without changing
  a single Python-facing signature**.
- `StubOrderBook` then doubles as the **reference oracle** the C++ engine is
  diff-tested against.

The contract is deliberately a **plain-old-data (POD) struct**, not a serialization
format, because it must be valid in three places at once: a C++ engine member, a
zero-copy NumPy view, and (subsystem 5) a shared-memory ring buffer to the dashboard.

## 2. Core invariants (do not violate)

1. **Prices are integer ticks (`int64`), never floating point.** This keeps the state
   bit-exact with the matching engine and the NASDAQ ITCH 5.0 feed. `0` in a price slot
   means "empty level."
2. **Sizes/counts are unsigned; prices/timestamps are signed.** `size`/`volume` =
   `uint64`, order `count` = `uint32`, `price` = `int64`, `ts_ns` = `int64`.
3. **Fixed depth per side:** `kDepth = DEPTH = 10`. Index `0` is always **best** (top of
   book). Bids descend from best (highest price first); asks ascend from best (lowest
   price first). Unfilled levels are zero-padded.
4. **Trivially copyable, standard-layout, 8-byte aligned.** Enforced by
   `static_assert` in the header. This is what makes `memcpy` / shared-memory / NumPy
   mirroring safe.
5. **Field order is frozen:** all 8-byte members, then all 4-byte, then the 1-byte
   member. This yields deterministic, interior-padding-free layout on LP64
   (MSVC/GCC/Clang). **Do not reorder fields.**

## 3. Field layout (contract v1, `kDepth = 10`)

`sizeof(BookStateView) == 40 * kDepth + 48 == 448 bytes`, `alignof == 8`.
Offsets below are **verified** (compiled with g++ 14, C++20, via `abi_check.cpp`):

| Field | C++ type | NumPy dtype | Shape | Offset (B) | Meaning |
|---|---|---|---|---|---|
| `seq` | `uint64` | `uint64` | scalar | 0 | monotonic engine / ITCH event sequence number |
| `ts_ns` | `int64` | `int64` | scalar | 8 | event timestamp, ns since epoch (ITCH clock) |
| `cum_volume` | `uint64` | `uint64` | scalar | 16 | cumulative shares traded this session |
| `last_trade_sz` | `uint64` | `uint64` | scalar | 24 | size of most recent trade (shares) |
| `last_trade_px` | `int64` | `int64` | scalar | 32 | price of most recent trade (ticks) |
| `bid_px` | `int64[10]` | `int64` | (10,) | 40 | bid prices, ticks; index 0 = best (highest) |
| `bid_sz` | `uint64[10]` | `uint64` | (10,) | 120 | aggregate resting size per bid level |
| `ask_px` | `int64[10]` | `int64` | (10,) | 200 | ask prices, ticks; index 0 = best (lowest) |
| `ask_sz` | `uint64[10]` | `uint64` | (10,) | 280 | aggregate resting size per ask level |
| `version` | `uint32` | `uint32` | scalar | 360 | seqlock publish counter; **even == stable** |
| `bid_ct` | `uint32[10]` | `uint32` | (10,) | 364 | resting order count per bid level |
| `ask_ct` | `uint32[10]` | `uint32` | (10,) | 404 | resting order count per ask level |
| `last_trade_side` | `Side` (`uint8`) | `uint8` | scalar | 444 | aggressor side of most recent trade |
| *(trailing pad)* | — | — | — | 445–447 | implicit padding to 8-byte alignment |

`Side` enum: `Bid = 0`, `Ask = 1`, `None = 2` (Python: `Side.NONE`; pybind exposes it as
`Side.None_` because `None` is a Python keyword).

## 4. The two access flavors across the pybind seam

The engine exposes the same payload two ways. **Choosing the wrong one is the most
likely correctness bug at this seam**, so it is spelled out here.

### `view()` — zero-copy live window (RL hot path)
- Returns a dict of NumPy arrays that **alias engine memory directly** (no copy).
- Valid **only until the next mutating engine call.** After the engine steps, the arrays
  reflect the *new* state — they are a live window, not a frozen picture.
- Use it in the env's observation path where you immediately normalize/consume the data.
  If you need to keep it, `.copy()` it or use `snapshot()`.

### `snapshot()` — owning copy (telemetry / tests / cross-thread)
- Returns a dict of NumPy arrays that **own their memory** (a `memcpy` at call time).
- Safe to retain across engine mutations, hand to another thread, log, or diff.

The parity test `test_zero_copy_vs_snapshot_semantics` locks this behavior: after a
mutation, a previously-taken `view()` reflects the change while a previously-taken
`snapshot()` does not.

## 5. Lifetime & concurrency

- The `BookStateView` is a **stable member of the engine** (never reallocated), so
  zero-copy views never dangle *while the engine object is alive*. Keeping a `view()`
  array alive after the `Engine` is destroyed is undefined behavior — don't.
- **`version` is a seqlock counter** for the (future) lock-free shared-memory path: the
  writer bumps `version` to odd before mutating and to even after. A reader takes a
  snapshot only when `version` is even and unchanged across the read. Even = stable.
- The current placeholder `Engine` is single-threaded; the seqlock discipline is
  specified now so the shared-memory ring (subsystem 5) and the async IPC broadcaster
  can adopt it without a contract change.

## 6. Versioning & change protocol

`BookStateView` is ABI-frozen at **v1**. Any change to field set, order, type, or
`kDepth` is an **ABI break**. To make one:

1. Bump `kBookStateContractVersion` in `book_state.hpp` **and** `CONTRACT_VERSION` in
   `book_state.py` (keep them equal — a test asserts it).
2. Update the NumPy mirror `BOOK_STATE_DTYPE` (and `DEPTH`) in `book_state.py` so field
   names/formats/order match exactly.
3. Update the size lock `static_assert(sizeof(...) == 40*kDepth + 48)` if the shape
   math changed, and this document's §3 table.
4. Re-run all three test entry points in §7 and confirm green.

`kDepth` is the **primary modeling knob** for the RL observation. Changing it is an ABI
change — keep C++ `kDepth` and Python `DEPTH` in lock-step.

## 7. How to verify parity

```bash
# 1. C++-only: compile-time static_asserts + prints concrete size/offsets.
g++ -std=c++20 -O2 -I cpp_engine/include cpp_engine/tests/abi_check.cpp -o abi_check
./abi_check          # expect: sizeof = 448 (expected 448), alignof = 8

# 2. Python-only (needs numpy, no build): dtype itemsize == 448, stub behavior.
python python_quant/tests/test_contract_smoke.py

# 3. Full ABI parity (needs the compiled `nexus_engine` pybind module):
pytest bindings/tests/test_abi_parity.py -v
#    asserts nexus_engine.STATE_NBYTES == BOOK_STATE_DTYPE.itemsize (== 448),
#    DEPTH/CONTRACT_VERSION agree, view() shapes/dtypes match, live-vs-snapshot.
```

The core cross-language lock is one equation: **C++ `sizeof(BookStateView)` ==
NumPy `BOOK_STATE_DTYPE.itemsize` == `40*DEPTH + 48`**. If that holds and field order is
unchanged, the zero-copy view and the shared-memory path are byte-safe.

## 8. Build note (environment)

The intended dev environment is **WSL/Ubuntu + NVIDIA GPU** (per the project plan).
`abi_check.cpp` compiles with any C++20 compiler (incl. Windows MSYS2 g++). The pybind
module and the Python tests require a real Python + pybind11 + a compiler toolchain —
build them in WSL, not under a OneDrive path. See root `CLAUDE.md` §8.
