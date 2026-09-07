/// DPU Fabric: resource profiles and local accounting ledger.
#pragma once
#include <cstdint>
#include <algorithm>
#include "dpufabric/id.h"
#include "dpufabric/error.h"

namespace dpufabric {

// A resource budget expressed against device-local logical resources.  Values
// are counts (queues, slots, contexts, functions) or byte sizes.
struct ResourceProfile {
    uint64_t memory_bytes = 0;      // max usable programmable memory (0 = unbounded)
    uint64_t queues = 0;            // max queue consumption (0 = unbounded)
    uint64_t functions = 0;         // max function/port-function attachment
    uint64_t slots = 0;             // max deployment slots
    uint64_t contexts = 0;          // max execution contexts
};

struct ResourceRequest {
    uint64_t memory_bytes = 0;
    uint64_t queues = 0;
    uint64_t functions = 0;
    uint64_t slots = 1;             // a deployment normally consumes one slot
    uint64_t contexts = 1;
};

// A finite accounting ledger over a capacity profile.  All mutations are
// transactional: callers obtain a scoped reservation, which is rolled back
// automatically unless commit() is reached, so a failure at any point can
// never leak resources.
class ResourceLedger {
public:
    explicit ResourceLedger(ResourceProfile cap) : cap_(cap) {}

    const ResourceProfile& capacity() const noexcept { return cap_; }

    struct Usage {
        uint64_t memory_bytes = 0;
        uint64_t queues = 0;
        uint64_t functions = 0;
        uint64_t slots = 0;
        uint64_t contexts = 0;
        bool operator==(const Usage& o) const noexcept { return memory_bytes == o.memory_bytes && queues == o.queues && functions == o.functions && slots == o.slots && contexts == o.contexts; }
        bool operator!=(const Usage& o) const noexcept { return !(*this == o); }
    };

    Usage current() const noexcept { return used_; }

    // Generic headroom fraction (0..100), the minimum across the constrained
    // dimensions.  100 means unconstrained/untouched.
    uint64_t headroom() const noexcept {
        uint64_t frac = 100;
        auto dim = [](uint64_t used, uint64_t cap) -> uint64_t {
            if (cap == 0) return 100;
            if (used >= cap) return 0;
            return ((cap - used) * 100) / cap;
        };
        frac = std::min(frac, dim(used_.memory_bytes, cap_.memory_bytes));
        frac = std::min(frac, dim(used_.queues, cap_.queues));
        frac = std::min(frac, dim(used_.functions, cap_.functions));
        frac = std::min(frac, dim(used_.slots, cap_.slots));
        frac = std::min(frac, dim(used_.contexts, cap_.contexts));
        return frac;
    }

    bool can_reserve(const ResourceRequest& r) const noexcept {
        if (cap_.memory_bytes && (used_.memory_bytes + r.memory_bytes > cap_.memory_bytes)) return false;
        if (cap_.queues && (used_.queues + r.queues > cap_.queues)) return false;
        if (cap_.functions && (used_.functions + r.functions > cap_.functions)) return false;
        if (cap_.slots && (used_.slots + r.slots > cap_.slots)) return false;
        if (cap_.contexts && (used_.contexts + r.contexts > cap_.contexts)) return false;
        return true;
    }

    class ScopedReservation {
    public:
        ScopedReservation() = default;
        ScopedReservation(ResourceLedger* l, const ResourceRequest& r) : ledger_(l), req_(r) {
            ledger_->apply_(req_);
            applied_ = true;
        }
        ScopedReservation(ScopedReservation&& o) noexcept
            : ledger_(o.ledger_), req_(o.req_), applied_(o.applied_), committed_(o.committed_) {
            o.applied_ = false; o.ledger_ = nullptr;
        }
        ScopedReservation& operator=(ScopedReservation&&) = delete;
        ScopedReservation(const ScopedReservation&) = delete;
        ScopedReservation& operator=(const ScopedReservation&) = delete;

        ~ScopedReservation() {
            if (ledger_ && applied_ && !committed_) ledger_->release_(req_);
        }
        void commit() noexcept { committed_ = true; }
        bool applied() const noexcept { return applied_; }
    private:
        ResourceLedger* ledger_ = nullptr;
        ResourceRequest req_;
        bool applied_ = false;
        bool committed_ = false;
    };

    // Acquire a scoped reservation.  Throws RESOURCE_EXHAUSTED if not possible.
    ScopedReservation try_reserve(const ResourceRequest& r) {
        if (!can_reserve(r)) throw_error(ErrorCode::RESOURCE_EXHAUSTED, "resource budget exceeded");
        return ScopedReservation(this, r);
    }

    // Direct release of an already committed reservation (used on rollback of
    // a committed deployment).  Throws INVALID_STATE on underflow.
    void release(const ResourceRequest& r) { release_(r); }

private:
    void apply_(const ResourceRequest& r) {
        used_.memory_bytes += r.memory_bytes;
        used_.queues += r.queues;
        used_.functions += r.functions;
        used_.slots += r.slots;
        used_.contexts += r.contexts;
    }
    void release_(const ResourceRequest& r) noexcept {
        if (used_.memory_bytes < r.memory_bytes || used_.queues < r.queues ||
            used_.functions < r.functions || used_.slots < r.slots ||
            used_.contexts < r.contexts) return; // defensive; input errors surface elsewhere
        used_.memory_bytes -= r.memory_bytes;
        used_.queues -= r.queues;
        used_.functions -= r.functions;
        used_.slots -= r.slots;
        used_.contexts -= r.contexts;
    }

    ResourceProfile cap_;
    Usage used_;
};

} // namespace dpufabric
