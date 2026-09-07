/// DPU Fabric: deterministic policy.
#pragma once
#include <cstdint>
#include <vector>
#include "dpufabric/id.h"
#include "dpufabric/capability.h"
#include "dpufabric/types.h"
#include "dpufabric/isolation.h"
#include "dpufabric/device.h"

namespace dpufabric {

// A named, reproducible ranking factor with a weight.  Factors are applied in
// a fixed order so that output is deterministic.
enum class RankFactor : uint8_t {
    WARM_SERVICE = 1,        // existing warm deployment
    LOCALITY = 2,            // attachment/locality quality
    HEADROOM = 3,            // resource headroom
    QUEUE_PRESSURE = 4,      // inverse queue pressure
    DEVICE_LOAD = 5,         // inverse device load
    STATE_TRANSFER = 6,      // inverse state-transfer cost
    HOST_CPU_SAVINGS = 7,    // expected host CPU savings
    OFFLOAD_VALUE = 8,       // expected offload value
    FAILURE_RISK = 9,        // inverse failure risk
    ENERGY = 10,             // energy/cost signal
    POLICY_PREFERENCE = 11,  // configured preference
    TIEBREAK_ID = 12,        // stable tie-break on device identity
    END_MARKER = 13
};
constexpr const char* rank_factor_name(RankFactor f) noexcept {
    switch (f) {
        case RankFactor::WARM_SERVICE: return "WARM_SERVICE";
        case RankFactor::LOCALITY: return "LOCALITY";
        case RankFactor::HEADROOM: return "HEADROOM";
        case RankFactor::QUEUE_PRESSURE: return "QUEUE_PRESSURE";
        case RankFactor::DEVICE_LOAD: return "DEVICE_LOAD";
        case RankFactor::STATE_TRANSFER: return "STATE_TRANSFER";
        case RankFactor::HOST_CPU_SAVINGS: return "HOST_CPU_SAVINGS";
        case RankFactor::OFFLOAD_VALUE: return "OFFLOAD_VALUE";
        case RankFactor::FAILURE_RISK: return "FAILURE_RISK";
        case RankFactor::ENERGY: return "ENERGY";
        case RankFactor::POLICY_PREFERENCE: return "POLICY_PREFERENCE";
        case RankFactor::TIEBREAK_ID: return "TIEBREAK_ID";
        case RankFactor::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}

struct RankFactorWeight {
    RankFactor factor = RankFactor::TIEBREAK_ID;
    int64_t weight = 1;
};

// Deterministic policy.  Every decision is expressed through these fields;
// the engine never fabricates hardware facts and never invents policy.
struct Policy {
    PolicyGeneration generation;   // policy generation (current)
    uint64_t version = 0;

    // Hard required capabilities in addition to the service's own.
    std::vector<CapabilityRequirement> hard_caps;
    // Preferred devices (by identity) — a soft preference, never a hard rule.
    std::vector<DeviceId> preferred_devices;
    bool prefer_warm_deployments = true;
    bool require_authoritative_evidence = true;    // UNKNOWN/REVALIDATION fail closed
    uint64_t min_headroom = 10;                    // 0..100
    uint64_t max_queue_pressure = 90;              // 0..100
    DeviceHealth min_device_health = DeviceHealth::WARN;

    // Fallback control.
    bool allow_host_fallback = false;
    bool require_offload_for_required = true;      // OFFLOAD_REQUIRED must reject on no candidate

    bool prefer_dedicated_isolation = false;
    Statefulness preferred_statefulness = Statefulness::STATELESS;

    // Ranking weights (in fixed factor order).
    std::vector<RankFactorWeight> weights;

    // Evaluation helper: look up a factor weight (defaults to 1).
    int64_t weight_of(RankFactor f) const noexcept {
        for (const auto& w : weights) if (w.factor == f) return w.weight;
        return (f == RankFactor::TIEBREAK_ID) ? 1 : 0;
    }

    static Policy defaults(PolicyGeneration gen) {
        Policy p;
        p.generation = gen;
        p.version = 1;
        p.weights = {
            { RankFactor::WARM_SERVICE, 4 },
            { RankFactor::LOCALITY, 3 },
            { RankFactor::HEADROOM, 2 },
            { RankFactor::QUEUE_PRESSURE, 2 },
            { RankFactor::DEVICE_LOAD, 1 },
            { RankFactor::STATE_TRANSFER, 1 },
            { RankFactor::HOST_CPU_SAVINGS, 2 },
            { RankFactor::OFFLOAD_VALUE, 1 },
            { RankFactor::FAILURE_RISK, -3 },
            { RankFactor::ENERGY, 0 },
            { RankFactor::POLICY_PREFERENCE, 3 },
            { RankFactor::TIEBREAK_ID, 1 }
        };
        for (auto x : p.weights) {
            if (x.factor == RankFactor::FAILURE_RISK || x.factor == RankFactor::QUEUE_PRESSURE ||
                x.factor == RankFactor::DEVICE_LOAD) {
                // inverted factors: we subtract their normalized value*weight
            }
        }
        return p;
    }
};

} // namespace dpufabric
