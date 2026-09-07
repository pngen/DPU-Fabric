/// DPU Fabric: execution and resource context model.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "dpufabric/id.h"
#include "dpufabric/resource.h"

namespace dpufabric {

enum class ExecutionContextState : uint8_t {
    CREATED = 1,
    BOUND = 2,
    ACTIVE = 3,
    INVALIDATED = 4,
    REVALIDATION_REQUIRED = 5,
    END_MARKER = 6
};
constexpr const char* execution_context_state_name(ExecutionContextState s) noexcept {
    switch (s) {
        case ExecutionContextState::CREATED: return "CREATED";
        case ExecutionContextState::BOUND: return "BOUND";
        case ExecutionContextState::ACTIVE: return "ACTIVE";
        case ExecutionContextState::INVALIDATED: return "INVALIDATED";
        case ExecutionContextState::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
        case ExecutionContextState::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}

// A context of execution for a service (per-service, per-tenant, per-port,
// per-queue, or per-isolation-domain).  It is invalidated after any event that
// makes the underlying hardware/runtime state uncertain.
struct ExecutionContext {
    ExecutionContextId id;
    uint64_t generation = 1;
    DeviceId device;
    ServiceId service;
    TenantId tenant;
    IsolationDomainId isolation_domain;
    ExecutionContextState state = ExecutionContextState::CREATED;
    ResourceRequest resources;
    bool authoritative = false;
    EvidenceGeneration evidence_gen;
    std::vector<ExecutionContextId> dependencies;
};

// A resource context captures a discrete resource attachment (port/function/
// queue) bound into a larger execution context.
struct ResourceContext {
    ResourceContextId id;
    PortId port;
    FunctionId function;
    QueueId queue;
    DeviceId device;
    TenantId tenant;
    ResourceRequest resources;
    bool leased = false;
};

} // namespace dpufabric
