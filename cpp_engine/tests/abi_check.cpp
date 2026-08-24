// Standalone compile-and-run check of the frozen BookStateView ABI.
// No pybind / Python required:
//   g++ -std=c++20 -I cpp_engine/include cpp_engine/tests/abi_check.cpp -o abi_check
//   ./abi_check
//
// The static_asserts in book_state.hpp fire at COMPILE time; this program also
// prints the concrete size/alignment/offsets, which the shared-memory path and
// the NumPy dtype (python_quant) must match.
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "nexus/book_state.hpp"

using nexus::BookStateView;

int main() {
    std::printf("contract v%u, kDepth = %zu\n",
                nexus::kBookStateContractVersion,
                static_cast<std::size_t>(nexus::kDepth));
    std::printf("sizeof  = %zu (expected %zu)\n",
                sizeof(BookStateView),
                static_cast<std::size_t>(40 * nexus::kDepth + 48));
    std::printf("alignof = %zu\n\n", alignof(BookStateView));

    std::printf("field offsets (for shmem / NumPy dtype parity):\n");
    std::printf("  seq=%zu ts_ns=%zu cum_volume=%zu last_trade_sz=%zu last_trade_px=%zu\n",
                offsetof(BookStateView, seq), offsetof(BookStateView, ts_ns),
                offsetof(BookStateView, cum_volume), offsetof(BookStateView, last_trade_sz),
                offsetof(BookStateView, last_trade_px));
    std::printf("  bid_px=%zu bid_sz=%zu ask_px=%zu ask_sz=%zu\n",
                offsetof(BookStateView, bid_px), offsetof(BookStateView, bid_sz),
                offsetof(BookStateView, ask_px), offsetof(BookStateView, ask_sz));
    std::printf("  version=%zu bid_ct=%zu ask_ct=%zu last_trade_side=%zu\n",
                offsetof(BookStateView, version), offsetof(BookStateView, bid_ct),
                offsetof(BookStateView, ask_ct), offsetof(BookStateView, last_trade_side));
    return 0;
}
