#pragma once
//
// Nexus-LOB :: matching-engine value types (cpp_engine)
// Owner: Person A. These are the order-entry vocabulary types used by
// nexus::LimitOrderBook. They are deliberately small PODs — the ONE type that
// crosses the language/process seam is nexus::BookStateView (book_state.hpp),
// not these; these stay engine-side (and, for OrderType/TIF/ExecResult, are what
// the pybind order-entry methods marshal).
//
#include <cstdint>

#include "nexus/book_state.hpp"  // Side, kDepth, BookStateView

namespace nexus {

// Engine-wide scalar aliases. Chosen to match the frozen BookStateView fields:
//   price/timestamp are signed int64 (ticks), size/volume unsigned uint64.
// Prices are INTEGER TICKS — never floating point (see bindings/CONTRACT.md §2).
using OrderId = std::uint64_t;
using Price   = std::int64_t;   // ticks; 0 == "empty level", never a valid resting price
using Qty     = std::uint64_t;  // shares

// How an incoming order is priced.
enum class OrderType : std::uint8_t { Limit = 0, Market = 1 };

// What happens to the unfilled remainder of an aggressing order.
//   GTC = rest the residual as passive liquidity (default for a plain limit).
//   IOC = fill what crosses now, discard the residual (never rests).
//   FOK = fill the ENTIRE quantity immediately or do nothing at all.
enum class TimeInForce : std::uint8_t { GTC = 0, IOC = 1, FOK = 2 };

// One execution (a maker consumed by a taker). Trade price is the RESTING
// (maker/passive) price by convention; `aggressor` is the incoming order's side.
struct Fill {
    OrderId maker_id;
    OrderId taker_id;
    Price   px;
    Qty     qty;
    Side    aggressor;
};

// Outcome of a single order-entry call. `filled` = shares executed this call;
// `resting` = shares left resting on the book afterwards (0 for market/IOC/FOK
// and for fully-filled or rejected orders).
enum class Status : std::uint8_t {
    Accepted = 0,               // resting order added, no cross
    Filled,                     // fully executed, nothing left resting
    PartiallyFilledResting,     // partially executed, remainder rests (limit GTC)
    Canceled,                   // cancel / modify-to-zero succeeded
    Rejected_DupId,             // submit with an id already live on the book
    Rejected_BadPrice,          // price <= 0 or outside the configured band
    Rejected_BadQty,            // quantity == 0
    Rejected_PoolFull,          // order pool exhausted (no zero-alloc growth)
    Rejected_FOK,               // fill-or-kill could not fill in full
    NoOp,                       // nothing to do (unknown id, empty-book market, ...)
};

struct ExecResult {
    OrderId id;
    Status  status;
    Qty     filled;
    Qty     resting;
};

// Human-readable status name for logs / test output. Header-only, no allocation.
inline const char* to_string(Status s) noexcept {
    switch (s) {
        case Status::Accepted:               return "Accepted";
        case Status::Filled:                 return "Filled";
        case Status::PartiallyFilledResting: return "PartiallyFilledResting";
        case Status::Canceled:               return "Canceled";
        case Status::Rejected_DupId:         return "Rejected_DupId";
        case Status::Rejected_BadPrice:      return "Rejected_BadPrice";
        case Status::Rejected_BadQty:        return "Rejected_BadQty";
        case Status::Rejected_PoolFull:      return "Rejected_PoolFull";
        case Status::Rejected_FOK:           return "Rejected_FOK";
        case Status::NoOp:                   return "NoOp";
    }
    return "?";
}

}  // namespace nexus
