//
// Nexus-LOB :: IdMap unit test (cpp_engine)
// Owner: Person A.
//
// IdMap is the zero-allocation id -> Order* open-addressing map that replaced
// std::unordered_map on the engine's hot path (limit_order_book.hpp). lob_test
// covers the integrated behaviour; this test drives IdMap directly at tiny
// capacities so linear probes hit collisions and wrap the ring, forcing the
// backtrack-shift deletion path — not just the low-load happy path.
//
// Header-only, standalone, mirrors the check-count style of lob_test.cpp:
//   g++ -std=c++20 -O2 -Wall -Wextra -I cpp_engine/include
//       cpp_engine/tests/id_map_test.cpp -o id_map_test && ./id_map_test
//
#include <cstdint>
#include <cstdio>
#include <unordered_set>

#include "nexus/limit_order_book.hpp"   // nexus::IdMap, nexus::OrderId, nexus::Order

using namespace nexus;

static long g_checks = 0;

static int fail(int line, const char* expr) {
    std::printf("FAIL line %d: %s\n", line, expr);
    return 1;
}
#define CHECK(cond)                              \
    do {                                         \
        if (!(cond)) return fail(__LINE__, #cond); \
        ++g_checks;                              \
    } while (0)

// A non-heap token standing in for a real Order*. IdMap only stores/returns the
// pointer; correctness here means non-null iff the id is live.
static Order* token(OrderId id) {
    return reinterpret_cast<Order*>((std::uintptr_t)(0x1000 + (id & 0xfffu)));
}

struct Lcg {
    std::uint64_t s;
    explicit Lcg(std::uint64_t seed) : s(seed) {}
    std::uint64_t next() noexcept {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return s >> 33;
    }
};

// Deterministic stress: a tiny table (collisions + probe wraparound, load kept at
// ~capacity so it never fills) doing inserts / erases / re-inserts, cross-checked
// against a reference set after every step.
static int stress(std::uint64_t seed, std::size_t capacity, int iters) {
    IdMap m(capacity);                       // table is next_pow2(2*capacity) slots
    std::unordered_set<OrderId> ref;
    Lcg rng(seed);

    for (int i = 0; i < iters; ++i) {
        // 25% "hot" ids (few distinct -> collisions), 75% fresh ids.
        const OrderId id = (rng.next() & 3u) == 0u
                               ? (OrderId)(rng.next() % 64u)
                               : (OrderId)(1u << 20) + (OrderId)(rng.next() % (1u << 20));

        switch (rng.next() & 3u) {
            case 0:
            case 1:  // insert while absent AND under the load cap (never fill the table)
                if (!ref.count(id) && ref.size() < capacity) {
                    ref.insert(id);
                    m.insert(id, token(id));
                }
                break;
            case 2:  // erase (may be a no-op for an unknown id, like cancel)
                m.erase(id);
                ref.erase(id);
                break;
            default: // read-only probe
                break;
        }

        // The touched key must agree with the reference immediately.
        CHECK((m.find(id) != nullptr) == (ref.count(id) != 0));
        // No live key may ever be lost (backtrack-shift must keep chains intact).
        for (auto k : ref) { CHECK(m.find(k) != nullptr); }
    }

    // Final consistency across a wide id range + size parity.
    for (std::uint64_t id = 0; id < 2048; ++id)
        CHECK((m.find(id) != nullptr) == (ref.count(id) != 0));
    CHECK(m.size() == ref.size());
    return 0;
}

int main() {
    std::printf("=== nexus::IdMap tests ===\n");

    if (stress(0xC0FFEEULL, 8, 20000))  return 1;   // 8 -> 16 slots, load ~0.5
    if (stress(0x1234ULL, 2, 40000))    return 1;   // 2 -> 8  slots: tight, wrap-heavy
    if (stress(0xDEADBEEFULL, 16, 30000)) return 1; // 16->32 slots
    if (stress(0xBEEFULL, 1024, 5000)) return 1;    // larger table, more live keys

    std::printf("%ld checks, 0 failed\n", g_checks);
    std::printf("ALL PASS\n");
    return 0;
}
