//
// Nexus-LOB :: subsystem 5 demo — shared-memory ring PROBE (cpp_engine)
// Owner: Person A.
//
// The consumer half of the ring demo: attach to a named ring that ring_producer
// is filling and print the live top-of-book, so you can SEE the reconstructed
// book moving. Standalone C++20:
//
//   g++ -std=c++20 -O2 -I cpp_engine/include
//       cpp_engine/demos/ring_probe.cpp -o ring_probe
//
// Usage:
//   ring_probe <name> [count] [poll_ms]
//   count   : frames to print before the summary   (default 25)
//   poll_ms : how long to sleep when the ring is empty (default 10)
//
// Run in a second terminal while ring_producer is writing:
//   ./ring_producer nex_aapl 4000 16384 0xC0FFEE 1
//   ./ring_probe nex_aapl 4000 5
//
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "nexus/book_state.hpp"
#include "nexus/shm_ring.hpp"

using namespace nexus;

namespace {

std::uint64_t plum(const char* s, std::uint64_t def) {
    return (s && *s) ? std::strtoull(s, nullptr, 0) : def;
}

void print_frame(const BookStateView& v) {
    std::printf("seq=%-8llu ts=%-12lld bid %8lld @ %-7lld ask %8lld @ %-7lld "
                "spread=%-5lld ltp=%-7lld cum_vol=%-10llu\n",
                (unsigned long long)v.seq,
                (long long)v.ts_ns,
                (unsigned long long)v.bid_sz[0], (long long)v.bid_px[0],
                (unsigned long long)v.ask_sz[0], (long long)v.ask_px[0],
                (long long)((v.ask_px[0] && v.bid_px[0]) ? v.ask_px[0] - v.bid_px[0] : 0),
                (long long)v.last_trade_px,
                (unsigned long long)v.cum_volume);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: ring_probe <name> [count] [poll_ms]\n");
        return 2;
    }
    const char*          name    = argv[1];
    const std::uint64_t  count   = plum(argc > 2 ? argv[2] : nullptr, 25);
    const int            poll_ms = argc > 3 ? std::atoi(argv[3]) : 10;

    std::printf("ring_probe: attaching to '%s' ...\n", name);
    ShmRing ring(name, /*capacity (must match producer)*/ 16384, ShmRing::Mode::Attach);
    std::printf("attached. ring capacity=%llu slot_bytes=%zu (448 == contract)\n",
                (unsigned long long)ring.capacity(), ring.slot_bytes());

    const auto t0 = std::chrono::steady_clock::now();
    BookStateView v;
    std::uint64_t read = 0, first_seq = 0;
    while (read < count) {
        if (ring.try_read(v)) {
            if (read == 0) first_seq = v.seq;
            print_frame(v);
            ++read;
        } else if (poll_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double secs = std::chrono::duration<double>(t1 - t0).count();

    std::printf("\nsummary: read=%llu (seq %llu..%llu) in %.2fs, dropped=%llu\n",
                (unsigned long long)read, (unsigned long long)first_seq,
                (unsigned long long)v.seq, secs, (unsigned long long)ring.dropped());
    return 0;
}