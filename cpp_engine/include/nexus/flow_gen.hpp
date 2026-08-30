#pragma once
//
// Nexus-LOB :: deterministic synthetic order-flow generator (cpp_engine)
// Owner: Person A.
//
// Drives the matching engine for demos and pre-ITCH tests. Once the ITCH 5.0
// parser lands, the real feed replaces this — but a seeded, reproducible
// generator stays useful for RL training loops and the dashboard demo.
//
// Behaviour: a mid-price random walk confined to a tick band, plus a small
// realistic mix each step — mostly passive limit orders a few ticks away from
// mid, with an occasional marketable aggressor. All prices integer ticks.
//
// Determinism: a 64-bit LCG (SplitMix-style finalizer on the step). The same
// seed + mid always yields the identical order stream.
//
#include <cstdint>

#include "nexus/types.hpp"  // OrderId / Side / Price / Qty / TimeInForce

namespace nexus {

struct OrderSpec {
    OrderId     id;
    Side        side;
    Price       price;        // ticks; 0 for a market order
    Qty         qty;
    TimeInForce tif;
    bool        is_market;
};

class FlowGen {
public:
    FlowGen(std::uint64_t seed, Price mid, Price band_lo, Price band_hi)
        : state_(seed), mid_(mid), band_lo_(band_lo), band_hi_(band_hi) {}

    // Advance one step and emit one order (ids supplied by the caller so the
    // book can hand them out monontonically across generators if desired).
    OrderSpec next(OrderId id) {
        // Drift the mid price ~30% of steps; keep it inside the band.
        const std::uint64_t r = rnd();
        if ((r & 3u) == 0) {
            mid_ += ((r >> 2) & 1u) ? 1 : -1;
            if (mid_ < band_lo_ + 8) mid_ = band_lo_ + 8;
            if (mid_ > band_hi_ - 8) mid_ = band_hi_ - 8;
        }

        const Qty qty = 10 + static_cast<Qty>(rnd() % 491);           // 10..500
        const bool aggressive = (rnd() % 10) < 2;                     // ~20%

        if (aggressive) {
            const Side s = ((rnd() & 1u) == 0) ? Side::Bid : Side::Ask;
            return OrderSpec{id, s, 0, qty, TimeInForce::IOC, /*market=*/true};
        }

        const bool  bid   = (rnd() % 100) < 55;                       // skew passive bids
        const Price off   = 1 + static_cast<Price>(rnd() % 4);        // 1..4 ticks
        const Price price = bid ? mid_ - off : mid_ + off;
        const TimeInForce tif = (rnd() % 20 == 0) ? TimeInForce::IOC : TimeInForce::GTC;
        return OrderSpec{id, bid ? Side::Bid : Side::Ask, price, qty, tif, /*market=*/false};
    }

    Price mid() const noexcept { return mid_; }

private:
    // SplitMix64-style step: good enough mixing for reproducible pseudo-flow.
    std::uint64_t rnd() noexcept {
        state_ += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    std::uint64_t state_;
    Price         mid_;
    Price         band_lo_;
    Price         band_hi_;
};

} // namespace nexus