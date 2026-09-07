/// DPU Fabric: deployment + activation authority model.
#pragma once
#include <cstdint>
#include <string>
#include <optional>
#include "dpufabric/id.h"
#include "dpufabric/types.h"
#include "dpufabric/resource.h"
#include "dpufabric/error.h"

namespace dpufabric {

// The current authoritative "world state" that a deployment/activation must be
// bound to in order to be authoritative now.
struct AuthorityContext {
    CoordinatorEpoch epoch;
    WorkerBootId worker_boot;
    WorkerId worker;
    DeviceBootId device_boot;
    DeviceGeneration device_gen;
    ServiceGeneration service_gen;
    ProgramGeneration program_gen;
    DeploymentGeneration deployment_gen;
    ActivationGeneration activation_gen;
    PolicyGeneration policy_gen;
    EvidenceGeneration evidence_gen;
};

// An authority token captured at activation time.  A token is authoritative
// only if every generation/boot/epoch component matches the current context.
// Generations are compared for equality: holding an older generation is stale;
// holding a newer one is impossible/forged and therefore also rejected.
struct ActivationAuthority {
    CoordinatorEpoch epoch;
    WorkerId worker;
    WorkerBootId worker_boot;
    DeviceId device;
    DeviceBootId device_boot;
    DeviceGeneration device_gen;
    ServiceGeneration service_gen;
    ProgramGeneration program_gen;
    DeploymentGeneration deployment_gen;
    ActivationGeneration activation_gen;
    PolicyGeneration policy_gen;
    EvidenceGeneration evidence_gen;

    bool is_current(const AuthorityContext& cur) const noexcept {
        if (epoch != cur.epoch) return false;
        if (worker != cur.worker) return false;
        if (worker_boot != cur.worker_boot) return false;
        if (device_boot != cur.device_boot) return false;
        if (device_gen != cur.device_gen) return false;
        if (service_gen != cur.service_gen) return false;
        if (program_gen != cur.program_gen) return false;
        if (deployment_gen != cur.deployment_gen) return false;
        if (activation_gen != cur.activation_gen) return false;
        if (policy_gen != cur.policy_gen) return false;
        if (evidence_gen != cur.evidence_gen) return false;
        return true;
    }
};

enum class ActivationState : uint8_t {
    PENDING = 1,
    ACTIVE = 2,
    DRAINING = 3,
    RETIRED = 4,
    STALE = 5,
    CANCELLED = 6,
    END_MARKER = 7
};
constexpr const char* activation_state_name(ActivationState s) noexcept {
    switch (s) {
        case ActivationState::PENDING: return "PENDING";
        case ActivationState::ACTIVE: return "ACTIVE";
        case ActivationState::DRAINING: return "DRAINING";
        case ActivationState::RETIRED: return "RETIRED";
        case ActivationState::STALE: return "STALE";
        case ActivationState::CANCELLED: return "CANCELLED";
        case ActivationState::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}

// A single activation of a deployment.
struct ActivationRecord {
    ActivationId activation_id;
    ActivationGeneration activation_gen;
    DeploymentId deployment_id;
    DeploymentGeneration deployment_gen;
    ActivationAuthority authority;
    ActivationState state = ActivationState::PENDING;
    std::string reason = "created";
};

// A deployment records the installation of a service artifact onto a device.
// It separates durable logical identity from the attempt that produced it, and
// it is never authoritative unless its binding generations are current.
struct Deployment {
    DeploymentId deployment_id;
    DeploymentGeneration generation;
    DeploymentAttemptId attempt_id;    // the specific attempt that produced this
    ServiceId service;
    ServiceGeneration service_gen;
    ProgramId program;
    ProgramGeneration program_gen;
    ArtifactId artifact;
    ArtifactGeneration artifact_gen;
    DeviceId device;
    DeviceGeneration device_gen;
    DeviceBootId device_boot;
    TenantId tenant;
    IsolationDomainId isolation_domain;
    ResourceRequest resources;
    DeploymentState state = DeploymentState::PLANNED;
    std::optional<ActivationId> activation;
    ActivationGeneration activation_gen;
    CoordinatorEpoch epoch;             // coordinator epoch under last authority
    WorkerBootId worker_boot;           // worker that last asserted authority
    EvidenceGeneration evidence_gen;
    PolicyGeneration policy_gen;
    bool authoritative = false;         // true only when activation is current
    uint64_t created_ms = 0;
    uint64_t updated_ms = 0;

    bool active() const noexcept { return state == DeploymentState::ACTIVE; }
};

// Deployment lifecycle transition table.
inline bool valid_deployment_transition(DeploymentState from, DeploymentState to) noexcept {
    using D = DeploymentState;
    switch (from) {
        case D::PLANNED:
            return to == D::RESERVED || to == D::ABORTED || to == D::FAILED;
        case D::RESERVED:
            return to == D::STAGING || to == D::PLANNED || to == D::ABORTED || to == D::FAILED;
        case D::STAGING:
            return to == D::LOADED || to == D::RESERVED || to == D::ABORTED || to == D::FAILED;
        case D::LOADED:
            return to == D::VALIDATING || to == D::STAGING || to == D::ABORTED || to == D::FAILED;
        case D::VALIDATING:
            return to == D::ACTIVATING || to == D::LOADED || to == D::FAILED || to == D::ABORTED || to == D::REVALIDATION_REQUIRED;
        case D::ACTIVATING:
            return to == D::ACTIVE || to == D::FAILED || to == D::ABORTED || to == D::REVALIDATION_REQUIRED || to == D::DEGRADED;
        case D::ACTIVE:
            return to == D::DEGRADED || to == D::DRAINING || to == D::REVALIDATION_REQUIRED || to == D::FAILED || to == D::RETIRED;
        case D::DEGRADED:
            return to == D::ACTIVE || to == D::DRAINING || to == D::REVALIDATION_REQUIRED || to == D::FAILED || to == D::RETIRED;
        case D::DRAINING:
            return to == D::RETIRED || to == D::ACTIVE || to == D::REVALIDATION_REQUIRED || to == D::FAILED;
        case D::FAILED:
            return to == D::PLANNED || to == D::ABORTED || to == D::RETIRED || to == D::REVALIDATION_REQUIRED;
        case D::ABORTED:
            return to == D::PLANNED || to == D::RETIRED;
        case D::RETIRED:
            return false; // terminal
        case D::REVALIDATION_REQUIRED:
            return to == D::VALIDATING || to == D::ACTIVE || to == D::FAILED || to == D::RETIRED || to == D::DRAINING;
        case D::END_MARKER:
            return false;
    }
    return false;
}

} // namespace dpufabric
