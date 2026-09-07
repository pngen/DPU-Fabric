/// DPU Fabric: typed offload request.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include "dpufabric/id.h"
#include "dpufabric/types.h"
#include "dpufabric/capability.h"
#include "dpufabric/isolation.h"
#include "dpufabric/resource.h"

namespace dpufabric {

// Latency sensitivity: a coarse classification used only for ranking, never as
// an authority override.
enum class LatencyClass : uint8_t {
    ULTRA_LOW = 1,
    LOW = 2,
    NORMAL = 3,
    BULK = 4,
    UNSPECIFIED = 5,
    END_MARKER = 6
};
constexpr const char* latency_class_name(LatencyClass l) noexcept {
    switch (l) {
        case LatencyClass::ULTRA_LOW: return "ULTRA_LOW";
        case LatencyClass::LOW: return "LOW";
        case LatencyClass::NORMAL: return "NORMAL";
        case LatencyClass::BULK: return "BULK";
        case LatencyClass::UNSPECIFIED: return "UNSPECIFIED";
        case LatencyClass::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}

// The complete typed request DPU Fabric evaluates against authoritative
// device evidence.  Hard requirements must never silently weaken.
struct OffloadRequest {
    // A caller-supplied request id for correlation in explanations/logs.
    uint64_t sequence = 0;

    OffloadClass offload_class = OffloadClass::CUSTOM_INFRASTRUCTURE_SERVICE;
    std::vector<CapabilityRequirement> required_caps;
    std::vector<CapabilityRequirement> optional_caps;

    std::optional<DeviceClass> required_device_class;
    std::optional<DeviceId> pinned_device;        // explicit device pin (hard)
    std::optional<PortId> required_port;
    std::optional<FunctionId> required_function;
    LocalityQuality min_locality = LocalityQuality::UNKNOWN;

    TenantId tenant;
    IsolationDomainId isolation_domain;
    IsolationSpec isolation;                       // explicit isolation spec
    Statefulness statefulness = Statefulness::STATELESS;

    ResourceRequest resource;
    SecurityMode security = SecurityMode::UNVERIFIED;

    LatencyClass latency = LatencyClass::UNSPECIFIED;
    uint64_t expected_throughput = 0;              // ops/s or Gbps (advisory)
    uint64_t expected_duration_ms = 0;             // 0 = unspecified

    // Existing service/program that this request should reuse or replace.
    std::optional<ServiceId> existing_service;
    std::optional<ServiceId> service;
    std::optional<ProgramId> required_program;
    std::string required_abi;                       // e.g. "DOCA_2.x"

    // Fallback policy.
    OffloadMode mode = OffloadMode::OFFLOAD_REQUIRED;
    bool allow_host_fallback = false;               // whether HOST_FALLBACK permitted
    std::vector<OffloadClass> allowed_fallback_classes;

    std::string provenance;                         // provenance/debug text

    // Policy generation pin: only the policy at or above this generation may be
    // applied.  0 = current policy.
    PolicyGeneration min_policy_gen;

    // A requested minimum headroom (0..100).
    uint64_t min_headroom = 0;
    uint64_t max_queue_pressure = 0;                // 0 = no constraint

    bool is_required() const noexcept { return mode == OffloadMode::OFFLOAD_REQUIRED; }
};

} // namespace dpufabric
