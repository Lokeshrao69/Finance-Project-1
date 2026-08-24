//
// Nexus-LOB :: Pybind11 bridge (SHARED SEAM — bindings/)
// Owner: interface architect. Wires the C++ matching engine's BookStateView to
// Python as (a) zero-copy live views for the RL hot path and (b) owning copies
// (snapshot) for telemetry / tests. See bindings/CONTRACT.md.
//
// NOTE: `Engine` below is a THIN PLACEHOLDER exposing only the state seam, so
// Person B can integrate against the frozen interface today. Person A replaces
// its body with the real matching engine (cpp_engine/) WITHOUT changing any of
// these Python-facing signatures.
//
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include <cstring>
#include <stdexcept>
#include <string>

#include "nexus/book_state.hpp"

namespace py = pybind11;
using nexus::BookStateView;
using nexus::Side;

namespace {

// Placeholder owner of the contract state. The real engine lives in cpp_engine/.
class Engine {
public:
    Engine() noexcept { std::memset(&state_, 0, sizeof(state_)); }

    // Non-owning access to the live state. The struct is a stable engine member
    // (never reallocated), so zero-copy views over it never dangle while the
    // engine is alive — but they are a LIVE WINDOW, not a frozen snapshot.
    const BookStateView& state() const noexcept { return state_; }
    BookStateView&       mutable_state() noexcept { return state_; }

private:
    BookStateView state_;
};

// Zero-copy, 1-D array over `ptr` of length `n`, keeping `owner` (the engine)
// alive for the array's lifetime. A non-null `base` => pybind does NOT copy.
template <typename T>
py::array_t<T> view_1d(const T* ptr, std::size_t n, py::handle owner) {
    return py::array_t<T>({static_cast<py::ssize_t>(n)},
                          {static_cast<py::ssize_t>(sizeof(T))},
                          ptr, owner);
}

// Owning (copied) 1-D array — safe to retain across engine mutations.
template <typename T>
py::array_t<T> copy_1d(const T* ptr, std::size_t n) {
    auto a = py::array_t<T>(static_cast<py::ssize_t>(n));
    std::memcpy(a.mutable_data(), ptr, n * sizeof(T));
    return a;
}

template <bool ZeroCopy>
py::dict make_payload(py::object self, const BookStateView& s) {
    py::dict d;
    if constexpr (ZeroCopy) {
        d["bid_px"] = view_1d(s.bid_px, nexus::kDepth, self);
        d["bid_sz"] = view_1d(s.bid_sz, nexus::kDepth, self);
        d["bid_ct"] = view_1d(s.bid_ct, nexus::kDepth, self);
        d["ask_px"] = view_1d(s.ask_px, nexus::kDepth, self);
        d["ask_sz"] = view_1d(s.ask_sz, nexus::kDepth, self);
        d["ask_ct"] = view_1d(s.ask_ct, nexus::kDepth, self);
    } else {
        d["bid_px"] = copy_1d(s.bid_px, nexus::kDepth);
        d["bid_sz"] = copy_1d(s.bid_sz, nexus::kDepth);
        d["bid_ct"] = copy_1d(s.bid_ct, nexus::kDepth);
        d["ask_px"] = copy_1d(s.ask_px, nexus::kDepth);
        d["ask_sz"] = copy_1d(s.ask_sz, nexus::kDepth);
        d["ask_ct"] = copy_1d(s.ask_ct, nexus::kDepth);
    }
    d["seq"]             = s.seq;
    d["ts_ns"]           = s.ts_ns;
    d["cum_volume"]      = s.cum_volume;
    d["last_trade_px"]   = s.last_trade_px;
    d["last_trade_sz"]   = s.last_trade_sz;
    d["last_trade_side"] = s.last_trade_side;
    return d;
}

} // namespace

PYBIND11_MODULE(nexus_engine, m) {
    m.doc() = "Nexus-LOB C++ matching-engine bridge (state contract v"
              + std::to_string(nexus::kBookStateContractVersion) + ")";
    m.attr("CONTRACT_VERSION") = nexus::kBookStateContractVersion;
    m.attr("DEPTH")            = static_cast<std::size_t>(nexus::kDepth);
    // Exposed so tests/test_abi_parity.py can assert C++ sizeof == NumPy itemsize.
    m.attr("STATE_NBYTES")     = static_cast<std::size_t>(sizeof(BookStateView));

    py::enum_<Side>(m, "Side")
        .value("Bid", Side::Bid)
        .value("Ask", Side::Ask)
        .value("None_", Side::None);

    py::class_<Engine>(m, "Engine")
        .def(py::init<>())

        // ---- ZERO-COPY LIVE VIEW (RL hot path) --------------------------------
        // Dict of numpy arrays that alias engine memory. Valid ONLY until the
        // next mutating engine call; the normalization step in the env (or an
        // explicit .copy()) is what makes the observation stable. See CONTRACT.md
        // "Lifetime & concurrency".
        .def("view",
             [](py::object self) {
                 return make_payload<true>(self, self.cast<Engine&>().state());
             },
             "Zero-copy live view of the book (mind the lifetime caveat).")

        // ---- OWNING SNAPSHOT (telemetry / tests / cross-thread) ---------------
        .def("snapshot",
             [](py::object self) {
                 return make_payload<false>(self, self.cast<Engine&>().state());
             },
             "Owning copy of the book state — safe to retain.")

        // ---- TEST HOOK: inject a synthetic ladder (placeholder only) ----------
        // Lets Person B exercise the seam before the real engine exists. Person A
        // removes this once matching drives the state.
        .def("_debug_set_level",
             [](Engine& e, Side side, std::size_t level, std::int64_t px,
                std::uint64_t sz, std::uint32_t ct) {
                 if (level >= nexus::kDepth)
                     throw std::out_of_range("level >= DEPTH");
                 BookStateView& s = e.mutable_state();
                 if (side == Side::Bid) {
                     s.bid_px[level] = px; s.bid_sz[level] = sz; s.bid_ct[level] = ct;
                 } else if (side == Side::Ask) {
                     s.ask_px[level] = px; s.ask_sz[level] = sz; s.ask_ct[level] = ct;
                 }
                 ++s.version;
             });
}
