// Standalone correctness tests for nexus::LimitOrderBook.
// No framework / Python / CMake required:
//   g++ -std=c++20 -O2 -Wall -Wextra -I cpp_engine/include \
//       cpp_engine/tests/lob_test.cpp -o lob_test && ./lob_test
//
// Covers: resting & L2 ladder ordering, full/partial crosses, price-time FIFO
// priority, multi-level sweeps, IOC / FOK / market semantics, cancel, modify
// (priority-keeping reduce vs priority-losing reprice), and every reject path.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "nexus/limit_order_book.hpp"

using namespace nexus;

namespace {

int g_failed = 0;
int g_checks = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_failed;                                                        \
            std::printf("  FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond);\
        }                                                                      \
    } while (0)

// A book with a comfortable band and pool for most tests.
LimitOrderBook make_book(std::size_t cap = 1024) {
    return LimitOrderBook(/*min*/ 1, /*max*/ 1'000'000, cap);
}

// ---------------------------------------------------------------------------
void test_rest_and_ladder() {
    std::printf("test_rest_and_ladder\n");
    auto b = make_book();
    // Insert out of order; the ladder must come back sorted best-first.
    b.submit_limit(1, Side::Ask, 10'100, 200, TimeInForce::GTC);
    b.submit_limit(2, Side::Ask, 10'050, 100, TimeInForce::GTC);
    b.submit_limit(3, Side::Bid, 10'000, 300, TimeInForce::GTC);
    b.submit_limit(4, Side::Bid,  9'950, 150, TimeInForce::GTC);

    const auto& s = b.view();
    CHECK(b.best_bid() == 10'000);
    CHECK(b.best_ask() == 10'050);
    CHECK(b.spread()   == 50);
    // asks ascend from best, bids descend from best.
    CHECK(s.ask_px[0] == 10'050 && s.ask_sz[0] == 100 && s.ask_ct[0] == 1);
    CHECK(s.ask_px[1] == 10'100 && s.ask_sz[1] == 200);
    CHECK(s.bid_px[0] == 10'000 && s.bid_sz[0] == 300 && s.bid_ct[0] == 1);  // one order
    CHECK(s.bid_px[1] ==  9'950 && s.bid_sz[1] == 150);
    // unfilled levels zero-padded.
    CHECK(s.bid_px[2] == 0 && s.ask_px[2] == 0);
    CHECK(b.live_orders() == 4);
}

// Two orders at the same price aggregate into one level (size + count).
void test_level_aggregation() {
    std::printf("test_level_aggregation\n");
    auto b = make_book();
    b.submit_limit(1, Side::Bid, 10'000, 300, TimeInForce::GTC);
    b.submit_limit(2, Side::Bid, 10'000, 200, TimeInForce::GTC);
    const auto& s = b.view();
    CHECK(s.bid_px[0] == 10'000);
    CHECK(s.bid_sz[0] == 500);
    CHECK(s.bid_ct[0] == 2);
    CHECK(b.best_bid() == 10'000 && s.bid_px[1] == 0);
}

// Aggressive bid fully lifts a single resting ask.
void test_full_cross() {
    std::printf("test_full_cross\n");
    auto b = make_book();
    b.submit_limit(1, Side::Ask, 10'050, 100, TimeInForce::GTC);
    std::vector<Fill> fills;
    auto r = b.submit_limit(2, Side::Bid, 10'050, 100, TimeInForce::GTC, &fills);

    CHECK(r.status == Status::Filled);
    CHECK(r.filled == 100 && r.resting == 0);
    CHECK(fills.size() == 1);
    CHECK(fills[0].maker_id == 1 && fills[0].taker_id == 2);
    CHECK(fills[0].px == 10'050 && fills[0].qty == 100);
    CHECK(fills[0].aggressor == Side::Bid);
    // Book is now empty; last-trade telemetry recorded.
    CHECK(!b.has_ask() && !b.has_bid());
    CHECK(b.live_orders() == 0);
    const auto& s = b.view();
    CHECK(s.last_trade_px == 10'050 && s.last_trade_sz == 100);
    CHECK(s.last_trade_side == Side::Bid && s.cum_volume == 100);
}

// Resting order larger than the incoming: maker partially filled, stays on book.
void test_partial_maker() {
    std::printf("test_partial_maker\n");
    auto b = make_book();
    b.submit_limit(1, Side::Ask, 10'050, 500, TimeInForce::GTC);
    auto r = b.submit_limit(2, Side::Bid, 10'050, 200, TimeInForce::GTC);
    CHECK(r.status == Status::Filled && r.filled == 200 && r.resting == 0);
    const auto& s = b.view();
    CHECK(b.best_ask() == 10'050);
    CHECK(s.ask_sz[0] == 300);           // 500 - 200 remaining
    CHECK(s.ask_ct[0] == 1);
    CHECK(b.live_orders() == 1);
}

// Incoming larger than book: crosses everything, rests the residual (GTC).
void test_partial_taker_rests() {
    std::printf("test_partial_taker_rests\n");
    auto b = make_book();
    b.submit_limit(1, Side::Ask, 10'050, 100, TimeInForce::GTC);
    auto r = b.submit_limit(2, Side::Bid, 10'050, 250, TimeInForce::GTC);
    CHECK(r.status == Status::PartiallyFilledResting);
    CHECK(r.filled == 100 && r.resting == 150);
    // Ask gone; 150 now rests as the best bid AT the limit price.
    CHECK(!b.has_ask());
    CHECK(b.best_bid() == 10'050);
    const auto& s = b.view();
    CHECK(s.bid_px[0] == 10'050 && s.bid_sz[0] == 150 && s.bid_ct[0] == 1);
}

// FIFO time priority: earlier order at a price fills before a later one.
void test_price_time_priority() {
    std::printf("test_price_time_priority\n");
    auto b = make_book();
    b.submit_limit(10, Side::Ask, 10'050, 100, TimeInForce::GTC);  // first in
    b.submit_limit(11, Side::Ask, 10'050, 100, TimeInForce::GTC);  // second in
    std::vector<Fill> fills;
    b.submit_limit(12, Side::Bid, 10'050, 150, TimeInForce::GTC, &fills);
    // 150 consumes all of maker 10, then 50 of maker 11.
    CHECK(fills.size() == 2);
    CHECK(fills[0].maker_id == 10 && fills[0].qty == 100);
    CHECK(fills[1].maker_id == 11 && fills[1].qty == 50);
    const auto& s = b.view();
    CHECK(b.best_ask() == 10'050 && s.ask_sz[0] == 50 && s.ask_ct[0] == 1);
}

// A single order sweeps multiple ask price levels (walks the ladder up).
void test_multi_level_sweep() {
    std::printf("test_multi_level_sweep\n");
    auto b = make_book();
    b.submit_limit(1, Side::Ask, 10'050, 100, TimeInForce::GTC);
    b.submit_limit(2, Side::Ask, 10'100, 100, TimeInForce::GTC);
    b.submit_limit(3, Side::Ask, 10'150, 100, TimeInForce::GTC);
    std::vector<Fill> fills;
    auto r = b.submit_limit(4, Side::Bid, 10'100, 250, TimeInForce::GTC, &fills);
    // Crosses 10'050 (100) and 10'100 (100); 10'150 is above the limit.
    CHECK(r.filled == 200);
    CHECK(r.status == Status::PartiallyFilledResting && r.resting == 50);
    CHECK(fills.size() == 2);
    CHECK(fills[0].px == 10'050 && fills[1].px == 10'100);
    // Best ask advanced to the untouched 10'150; residual rests at 10'100 bid.
    CHECK(b.best_ask() == 10'150);
    CHECK(b.best_bid() == 10'100);
    const auto& s = b.view();
    CHECK(s.last_trade_px == 10'100);   // price of the most recent individual fill
}

// IOC: fills what crosses immediately, kills the rest, never rests.
void test_ioc() {
    std::printf("test_ioc\n");
    auto b = make_book();
    b.submit_limit(1, Side::Ask, 10'050, 100, TimeInForce::GTC);
    auto r = b.submit_limit(2, Side::Bid, 10'050, 300, TimeInForce::IOC);
    CHECK(r.status == Status::Filled);      // executed > 0, residual killed
    CHECK(r.filled == 100 && r.resting == 0);
    CHECK(!b.has_bid());                     // nothing rested
    CHECK(b.live_orders() == 0);

    // IOC that crosses nothing is a NoOp and leaves no trace.
    auto r2 = b.submit_limit(3, Side::Bid, 9'000, 100, TimeInForce::IOC);
    CHECK(r2.status == Status::NoOp && r2.filled == 0);
    CHECK(!b.has_bid());
}

// FOK: all-or-nothing. Succeeds only if the whole qty is available.
void test_fok() {
    std::printf("test_fok\n");
    auto b = make_book();
    b.submit_limit(1, Side::Ask, 10'050, 100, TimeInForce::GTC);
    b.submit_limit(2, Side::Ask, 10'100, 100, TimeInForce::GTC);

    // Not enough within the price limit -> rejected, book untouched.
    auto bad = b.submit_limit(3, Side::Bid, 10'050, 150, TimeInForce::FOK);
    CHECK(bad.status == Status::Rejected_FOK);
    CHECK(bad.filled == 0);
    CHECK(b.best_ask() == 10'050 && b.live_orders() == 2);
    CHECK(b.view().cum_volume == 0);        // truly no side effects

    // Enough across two levels within the limit -> fully fills.
    auto ok = b.submit_limit(4, Side::Bid, 10'100, 200, TimeInForce::FOK);
    CHECK(ok.status == Status::Filled && ok.filled == 200 && ok.resting == 0);
    CHECK(!b.has_ask());
}

// Market order: crosses at any price; empty opposite side -> NoOp.
void test_market() {
    std::printf("test_market\n");
    auto b = make_book();
    b.submit_limit(1, Side::Ask, 10'050, 100, TimeInForce::GTC);
    b.submit_limit(2, Side::Ask, 99'999, 100, TimeInForce::GTC);  // far away, still takeable
    auto r = b.submit_market(3, Side::Bid, 150);
    CHECK(r.status == Status::Filled && r.filled == 150 && r.resting == 0);
    CHECK(b.best_ask() == 99'999 && b.view().ask_sz[0] == 50);

    auto empty = b.submit_market(4, Side::Bid, 100);   // sweep the rest
    CHECK(empty.filled == 50 && !b.has_ask());
    auto noop = b.submit_market(5, Side::Bid, 100);     // nothing left
    CHECK(noop.status == Status::NoOp && noop.filled == 0);
}

// Cancel removes a resting order and collapses an emptied level.
void test_cancel() {
    std::printf("test_cancel\n");
    auto b = make_book();
    b.submit_limit(1, Side::Bid, 10'000, 300, TimeInForce::GTC);
    b.submit_limit(2, Side::Bid,  9'950, 100, TimeInForce::GTC);
    auto r = b.cancel(1);
    CHECK(r.status == Status::Canceled);
    CHECK(b.best_bid() == 9'950);            // best moved down to the survivor
    CHECK(b.live_orders() == 1);
    const auto& s = b.view();
    CHECK(s.bid_px[0] == 9'950 && s.bid_px[1] == 0);

    auto nope = b.cancel(999);               // unknown id
    CHECK(nope.status == Status::NoOp);
}

// Modify: in-place shrink keeps priority; reprice/size-up loses it.
void test_modify() {
    std::printf("test_modify\n");
    auto b = make_book();
    b.submit_limit(1, Side::Bid, 10'000, 100, TimeInForce::GTC);  // first at price
    b.submit_limit(2, Side::Bid, 10'000, 100, TimeInForce::GTC);  // behind #1

    // Shrink #1 in place: still first in line.
    auto r = b.modify(1, 10'000, 40);
    CHECK(r.status == Status::Accepted && r.resting == 40);
    CHECK(b.view().bid_sz[0] == 140 && b.view().bid_ct[0] == 2);

    // Prove #1 kept priority: an aggressive ask of 40 hits #1 first.
    std::vector<Fill> fills;
    b.submit_limit(3, Side::Ask, 10'000, 40, TimeInForce::GTC, &fills);
    CHECK(fills.size() == 1 && fills[0].maker_id == 1 && fills[0].qty == 40);
    CHECK(b.view().bid_sz[0] == 100 && b.view().bid_ct[0] == 1);  // only #2 left

    // Reprice #2 up to a new price: cancels + re-adds (loses priority, new level).
    auto r2 = b.modify(2, 10'010, 100);
    CHECK(r2.status == Status::Accepted);
    CHECK(b.best_bid() == 10'010);

    // modify-to-zero behaves as a cancel.
    auto r3 = b.modify(2, 10'010, 0);
    CHECK(r3.status == Status::Canceled);
    CHECK(!b.has_bid());
}

// Every rejection path.
void test_rejects() {
    std::printf("test_rejects\n");
    auto b = make_book();
    CHECK(b.submit_limit(1, Side::Bid, 10'000, 0).status   == Status::Rejected_BadQty);
    CHECK(b.submit_limit(2, Side::Bid, 0, 100).status      == Status::Rejected_BadPrice);
    CHECK(b.submit_limit(3, Side::Bid, 2'000'000, 100).status == Status::Rejected_BadPrice); // out of band

    b.submit_limit(10, Side::Bid, 10'000, 100, TimeInForce::GTC);
    CHECK(b.submit_limit(10, Side::Bid, 10'000, 50).status == Status::Rejected_DupId);

    // Pool exhaustion is a hard reject (no silent growth).
    auto tiny = LimitOrderBook(1, 1000, /*cap*/ 2);
    CHECK(tiny.submit_limit(1, Side::Bid, 100, 10).status == Status::Accepted);
    CHECK(tiny.submit_limit(2, Side::Bid, 101, 10).status == Status::Accepted);
    CHECK(tiny.pool().in_use() == 2 && tiny.pool().capacity() == 2);
    CHECK(tiny.submit_limit(3, Side::Bid, 102, 10).status == Status::Rejected_PoolFull);
    // But an order that fully CROSSES (never needs to rest) still works when full.
    CHECK(tiny.submit_limit(4, Side::Ask, 100, 10).status == Status::Filled);
}

// Cumulative volume and last-trade side survive across many events.
void test_telemetry_accumulates() {
    std::printf("test_telemetry_accumulates\n");
    auto b = make_book();
    b.submit_limit(1, Side::Bid, 10'000, 100, TimeInForce::GTC);
    b.submit_limit(2, Side::Bid, 10'000, 100, TimeInForce::GTC);
    b.submit_limit(3, Side::Ask, 10'000, 50, TimeInForce::GTC);   // taker sells into bids
    CHECK(b.view().cum_volume == 50);
    CHECK(b.view().last_trade_side == Side::Ask);
    b.submit_limit(4, Side::Ask, 10'000, 120, TimeInForce::GTC);
    CHECK(b.view().cum_volume == 170);
    // seq advances once per accepted mutating call (4 submits here).
    CHECK(b.view().seq == 4);
}

}  // namespace

int main() {
    std::printf("=== nexus::LimitOrderBook tests ===\n\n");
    test_rest_and_ladder();
    test_level_aggregation();
    test_full_cross();
    test_partial_maker();
    test_partial_taker_rests();
    test_price_time_priority();
    test_multi_level_sweep();
    test_ioc();
    test_fok();
    test_market();
    test_cancel();
    test_modify();
    test_rejects();
    test_telemetry_accumulates();

    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    if (g_failed == 0) std::printf("ALL PASS\n");
    return g_failed == 0 ? 0 : 1;
}
