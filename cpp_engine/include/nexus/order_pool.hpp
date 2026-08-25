#pragma once
//
// Nexus-LOB :: zero-allocation order pool + intrusive order node (cpp_engine)
// Owner: Person A.
//
// The pool pre-allocates a fixed slab of Order nodes ONCE at construction and
// hands them out / reclaims them via an intrusive free-list threaded through
// Order::next. After construction there is NO heap allocation on the order path
// (the "zero-alloc hot path" the engine advertises).
//
// CRITICAL INVARIANT: the slab is sized once and NEVER resized or push_back'd
// again. Every live Order* is aliased from the slab into the book's intrusive
// price-level lists and its id->Order* map; a std::vector reallocation would
// invalidate ALL of them at once (silent corruption). Exhaustion is therefore a
// hard reject, not a grow — see LimitOrderBook and the plan's pool-exhaustion
// decision.
//
#include <cstddef>
#include <vector>

#include "nexus/types.hpp"

namespace nexus {

// Intrusive book node. Lives in the pool's slab; linked into a LimitLevel's FIFO
// list while resting, and into the pool's free-list (via `next`) while free.
struct Order {
    OrderId id;      // client/ITCH order id
    Price   price;   // resting price in ticks (kept so cancel/modify find the slot in O(1))
    Qty     qty;     // REMAINING size (decremented on partial fills), not original size
    Side    side;    // Bid / Ask
    Order*  prev;    // intrusive FIFO predecessor (nullptr at level head)
    Order*  next;    // intrusive FIFO successor; doubles as free-list link when free
};

class OrderPool {
public:
    explicit OrderPool(std::size_t capacity) : slab_(capacity) {
        // Thread every slot onto the free-list. Build back-to-front so the list
        // hands out slot 0 first (nicer locality when the book warms up).
        free_head_ = nullptr;
        for (std::size_t i = capacity; i-- > 0;) {
            slab_[i].next = free_head_;
            free_head_ = &slab_[i];
        }
    }

    // Non-copyable AND non-movable: live Order* alias into slab_, and moving the
    // pool (or the book that owns it by value) must not be allowed to dangle them.
    // Declaring the deleted copy ops also suppresses the implicit move ops.
    OrderPool(const OrderPool&) = delete;
    OrderPool& operator=(const OrderPool&) = delete;

    // O(1). Returns an UNINITIALIZED node (caller sets every field) or nullptr
    // when the slab is exhausted.
    Order* allocate() noexcept {
        Order* o = free_head_;
        if (o == nullptr) return nullptr;
        free_head_ = o->next;
        ++in_use_;
        return o;
    }

    // O(1). The node must already be unlinked from any price-level list.
    void free(Order* o) noexcept {
        o->next = free_head_;
        free_head_ = o;
        --in_use_;
    }

    std::size_t capacity() const noexcept { return slab_.size(); }
    std::size_t in_use() const noexcept { return in_use_; }

private:
    std::vector<Order> slab_;         // sized ONCE in the ctor; never grown again
    Order*             free_head_ = nullptr;
    std::size_t        in_use_ = 0;
};

}  // namespace nexus
