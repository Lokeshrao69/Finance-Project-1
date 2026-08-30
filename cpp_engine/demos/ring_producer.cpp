//
// Nexus-LOB :: subsystem 5 demo — shared-memory ring PRODUCER (cpp_engine)
// Owner: Person A.
//
// Drives a real LimitOrderBook with the synthetic flow generator and publishes
// the resulting BookStateView into a named shared-memory ring after every order,
// so a reader process (ring_probe, the future dashboard) can watch the live book
// across the process boundary. Runs standalone with a bare C++20 compiler:
//
//   g++ -std=c++20 -O2 -I cpp_engine/include
//       cpp_engine/demos/ring_producer.cpp -o ring_producer
//
// Usage:
//   ring_producer <name> [steps] [capacity] [seed] [ms_delay]
//   name      : ring segment name (shares with ring_probe)
//   steps     : number of synthetic orders to generate        (default 2000)
//   capacity  : ring capacity in 448-byte snapshots           (default 16384)
//   seed      : FlowGen LCG seed                               (default 0xC0FFEE)
//   ms_delay  : sleep between publishes, for a watchable cadence (default 1)
//
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "nexus/flow_gen.hpp"
#include "nexus/limit_order_book.hpp"
#include "nexus/shm_ring.hpp"

using namespace nexus;

namespace {

std::int64_t wall_ns() noexcept {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
}

std::uint64_t plum(const char* s, std::uint64_t def) {
    return (s && *s) ? std::strtoull(s, nullptr, 0) : def;   // base 0 -> hex/dec both work
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: ring_producer <name> [steps] [capacity] [seed] [ms_delay]\n");
        return 2;
    }
    const char*     name   = argv[1];
    const std::uint64_t steps    = plum(argc > 2 ? argv[2] : nullptr, 2000);
    const std::size_t    capacity = static_cast<std::size_t>(plum(argc > 3 ? argv[3] : nullptr, 16384));
    const std::uint64_t  seed     = plum(argc > 4 ? argv[4] : nullptr, 0xC0FFEEu);
    const int  delay_ms = argc > 5 ? std::atoi(argv[5]) : 1;

    ShmRing::destroy(name);                     // clear a stale segment from a prior run
    ShmRing ring(name, capacity, ShmRing::Mode::Create);

    // A generous single-instrument band and pool; the flow stays near mid anyway.
    LimitOrderBook book(1, 1'000'000, std::size_t(1) << 20);
    FlowGen gen(seed, /*mid=*/50'000, /*band_lo=*/1, /*band_hi=*/1'000'000);

    std::printf("ring_producer: name=%s steps=%llu capacity=%zu seed=%llu delay=%dms\n",
                name, (unsigned long long)steps, capacity, (unsigned long long)seed, delay_ms);

    const auto t0 = std::chrono::steady_clock::now();
    std::uint64_t published = 0, dropped = 0;
    for (OrderId oid = 1; oid <= steps; ++oid) {
        const OrderSpec o = gen.next(oid);
        if (o.is_market) book.submit_market(o.id, o.side, o.qty, /*fills=*/nullptr, wall_ns());
        else             book.submit_limit (o.id, o.side, o.price, o.qty, o.tif,
                                            /*fills=*/nullptr, wall_ns());

        if (ring.publish(book.view())) ++published; else ++dropped;
        if (delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double secs  = std::chrono::duration<double>(t1 - t0).count();

    const auto& v = book.view();
    std::printf("\nsummary: %llu published, %llu dropped, %.2fs, live_orders=%llu\n",
                (unsigned long long)published, (unsigned long long)dropped, secs,
                (unsigned long long)book.live_orders());
    std::printf("last snapshot: seq=%llu bid=%lld@%lld ask=%lld@%lld cum_vol=%llu\n",
                (unsigned long long)v.seq,
                (long long)v.bid_sz[0], (long long)v.bid_px[0],
                (long long)v.ask_sz[0], (long long)v.ask_px[0],
                (unsigned long long)v.cum_volume);

    ShmRing::destroy(name);
    return 0;
}