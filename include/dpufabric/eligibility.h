/// DPU Fabric: eligibility evaluation and structured explanations.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include "dpufabric/id.h"
#include "dpufabric/types.h"
#include "dpufabric/request.h"
#include "dpufabric/policy.h"

namespace dpufabric {

// Named, machine-readable rejection reasons.  Each maps to a distinct cause so
// that the explanation is never an opaque score.
enum class RejectionReason : uint16_t {
    NONE = 0,
    DEVICE_NOT_FOUND = 1,
    DEVICE_NOT_READY = 2,
    DEVICE_DRAINING = 3,
    DEVICE_FAILED = 4,
    DEVICE_EVIDENCE_STALE = 5,
    DEVICE_GENERATION_STALE = 6,
    DEVICE_BOOT_STALE = 7,
    DEVICE_HEALTH_THRESHOLD = 8,
    REQUIRED_CAPABILITY_UNSUPPORTED = 9,
    REQUIRED_CAPABILITY_UNKNOWN = 10,
    REQUIRED_CAPABILITY_STALE = 11,
    CAPABILITY_VERSION_MISMATCH = 12,
    FIRMWARE_INCOMPATIBLE = 13,
    RUNTIME_INCOMPATIBLE = 14,
    ARTIFACT_INCOMPATIBLE = 15,
    ARCHITECTURE_INCOMPATIBLE = 16,
    ABI_INCOMPATIBLE = 17,
    ISOLATION_DOMAIN_MISMATCH = 18,
    TENANT_MISMATCH = 19,
    DEDICATED_ISOLATION_UNAVAILABLE = 20,
    ISOLATION_UNPROVEN = 21,
    PORT_MISMATCH = 22,
    FUNCTION_MISMATCH = 23,
    DEVICE_CLASS_MISMATCH = 24,
    RESOURCE_INSUFFICIENT = 25,
    QUEUE_PRESSURE_HIGH = 26,
    HEADROOM_LOW = 27,
    DEPENDENCY_UNAVAILABLE = 28,
    POLICY_DISALLOWED = 29,
    SECURITY_MODE_INCOMPATIBLE = 30,
    LOCALITY_INSUFFICIENT = 31,
    PINNED_DEVICE_MISMATCH = 32,
    PROGRAM_GENERATION_STALE = 33,
    SERVICE_GENERATION_STALE = 34,
    DEPLOYMENT_GENERATION_STALE = 35,
    OFFLOAD_REQUIRED_UNAVAILABLE = 36,
    END_MARKER = 37
};
constexpr const char* rejection_reason_name(RejectionReason r) noexcept {
    switch (r) {
        case RejectionReason::NONE: return "NONE";
        case RejectionReason::DEVICE_NOT_FOUND: return "DEVICE_NOT_FOUND";
        case RejectionReason::DEVICE_NOT_READY: return "DEVICE_NOT_READY";
        case RejectionReason::DEVICE_DRAINING: return "DEVICE_DRAINING";
        case RejectionReason::DEVICE_FAILED: return "DEVICE_FAILED";
        case RejectionReason::DEVICE_EVIDENCE_STALE: return "DEVICE_EVIDENCE_STALE";
        case RejectionReason::DEVICE_GENERATION_STALE: return "DEVICE_GENERATION_STALE";
        case RejectionReason::DEVICE_BOOT_STALE: return "DEVICE_BOOT_STALE";
        case RejectionReason::DEVICE_HEALTH_THRESHOLD: return "DEVICE_HEALTH_THRESHOLD";
        case RejectionReason::REQUIRED_CAPABILITY_UNSUPPORTED: return "REQUIRED_CAPABILITY_UNSUPPORTED";
        case RejectionReason::REQUIRED_CAPABILITY_UNKNOWN: return "REQUIRED_CAPABILITY_UNKNOWN";
        case RejectionReason::REQUIRED_CAPABILITY_STALE: return "REQUIRED_CAPABILITY_STALE";
        case RejectionReason::CAPABILITY_VERSION_MISMATCH: return "CAPABILITY_VERSION_MISMATCH";
        case RejectionReason::FIRMWARE_INCOMPATIBLE: return "FIRMWARE_INCOMPATIBLE";
        case RejectionReason::RUNTIME_INCOMPATIBLE: return "RUNTIME_INCOMPATIBLE";
        case RejectionReason::ARTIFACT_INCOMPATIBLE: return "ARTIFACT_INCOMPATIBLE";
        case RejectionReason::ARCHITECTURE_INCOMPATIBLE: return "ARCHITECTURE_INCOMPATIBLE";
        case RejectionReason::ABI_INCOMPATIBLE: return "ABI_INCOMPATIBLE";
        case RejectionReason::ISOLATION_DOMAIN_MISMATCH: return "ISOLATION_DOMAIN_MISMATCH";
        case RejectionReason::TENANT_MISMATCH: return "TENANT_MISMATCH";
        case RejectionReason::DEDICATED_ISOLATION_UNAVAILABLE: return "DEDICATED_ISOLATION_UNAVAILABLE";
        case RejectionReason::ISOLATION_UNPROVEN: return "ISOLATION_UNPROVEN";
        case RejectionReason::PORT_MISMATCH: return "PORT_MISMATCH";
        case RejectionReason::FUNCTION_MISMATCH: return "FUNCTION_MISMATCH";
        case RejectionReason::DEVICE_CLASS_MISMATCH: return "DEVICE_CLASS_MISMATCH";
        case RejectionReason::RESOURCE_INSUFFICIENT: return "RESOURCE_INSUFFICIENT";
        case RejectionReason::QUEUE_PRESSURE_HIGH: return "QUEUE_PRESSURE_HIGH";
        case RejectionReason::HEADROOM_LOW: return "HEADROOM_LOW";
        case RejectionReason::DEPENDENCY_UNAVAILABLE: return "DEPENDENCY_UNAVAILABLE";
        case RejectionReason::POLICY_DISALLOWED: return "POLICY_DISALLOWED";
        case RejectionReason::SECURITY_MODE_INCOMPATIBLE: return "SECURITY_MODE_INCOMPATIBLE";
        case RejectionReason::LOCALITY_INSUFFICIENT: return "LOCALITY_INSUFFICIENT";
        case RejectionReason::PINNED_DEVICE_MISMATCH: return "PINNED_DEVICE_MISMATCH";
        case RejectionReason::PROGRAM_GENERATION_STALE: return "PROGRAM_GENERATION_STALE";
        case RejectionReason::SERVICE_GENERATION_STALE: return "SERVICE_GENERATION_STALE";
        case RejectionReason::DEPLOYMENT_GENERATION_STALE: return "DEPLOYMENT_GENERATION_STALE";
        case RejectionReason::OFFLOAD_REQUIRED_UNAVAILABLE: return "OFFLOAD_REQUIRED_UNAVAILABLE";
        case RejectionReason::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}

// One per-candidate ranking record with named, reproducible factors.
struct FactorScore {
    RankFactor factor = RankFactor::TIEBREAK_ID;
    double normalized = 0.0;   // in [0,1]
    double weighted = 0.0;     // normalized * weight
    std::string note;
};

struct CandidateReport {
    DeviceId device;
    bool passed_hard = false;
    std::vector<RejectionReason> rejected;   // empty when passed_hard
    std::vector<FactorScore> scores;
    double total = 0.0;
    std::string detail;
};

struct EligibilityResult {
    OffloadMode mode = OffloadMode::OFFLOAD_REQUIRED;
    ExecutionClass execution = ExecutionClass::UNSUPPORTED;
    std::optional<DeviceId> selected;
    bool hard_pass = false;                  // at least one candidate passed hard
    bool fallback_used = false;
    std::vector<RejectionReason> global_rejections;  // request-level rejections
    std::vector<CandidateReport> candidates;
    std::string fallback_reason;
    std::string explanation;

    // Deterministic, human-readable explanation from the recorded (already
    // deterministic) fields.
    std::string summarize() const;
};

struct Snapshot;

// Evaluate an offload request against an immutable snapshot.  Deterministic:
// the same snapshot + request + policy yields the same result.
EligibilityResult evaluate_offload(const Snapshot& snap, const OffloadRequest& req);

} // namespace dpufabric
