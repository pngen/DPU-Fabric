/// DPU Fabric: coordinator state structures and immutable snapshots.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <vector>
#include <atomic>
#include "dpufabric/id.h"
#include "dpufabric/device.h"
#include "dpufabric/program.h"
#include "dpufabric/deployment.h"
#include "dpufabric/context.h"
#include "dpufabric/policy.h"
#include "dpufabric/resource.h"
#include "dpufabric/error.h"

namespace dpufabric {

// A fully immutable snapshot of all coordinator state.  Readers obtain a
// shared_ptr to this and evaluate without holding any lock; writers rebuild it
// after each mutation.  This keeps policy evaluation and backend calls free of
// registry locks, satisfying the lock-ordering audit.
struct Snapshot {
    CoordinatorEpoch epoch;
    Policy policy;
    std::map<DeviceId, DeviceRecord> devices;
    std::map<ServiceId, ServiceDefinition> services;
    // Program history: program_id -> generation -> artifact.
    std::map<ProgramId, std::map<ProgramGeneration, ProgramArtifact>> programs;
    std::map<ProgramId, ProgramGeneration> current_program_gen;   // current generation per program
    std::map<DeploymentId, Deployment> deployments;
    std::map<ActivationId, ActivationRecord> activations;
    std::map<ExecutionContextId, ExecutionContext> contexts;
    std::map<DeviceId, ResourceLedger::Usage> device_usage;
    std::vector<DeviceId> device_order;

    const DeviceRecord* device(DeviceId id) const noexcept {
        auto it = devices.find(id);
        return it == devices.end() ? nullptr : &it->second;
    }
    const ResourceLedger::Usage* usage(DeviceId id) const noexcept {
        auto it = device_usage.find(id);
        return it == device_usage.end() ? nullptr : &it->second;
    }
    const Deployment* deployment(DeploymentId id) const noexcept {
        auto it = deployments.find(id);
        return it == deployments.end() ? nullptr : &it->second;
    }
    const ServiceDefinition* service(ServiceId id) const noexcept {
        auto it = services.find(id);
        return it == services.end() ? nullptr : &it->second;
    }
    ProgramGeneration current_gen(ProgramId id) const noexcept {
        auto it = current_program_gen.find(id);
        return it == current_program_gen.end() ? ProgramGeneration::null() : it->second;
    }
};

// The live (mutable) coordinator state.  Mutations are serialized by the
// engine's single mutation mutex; a Snapshot is rebuilt and published after
// every mutation, so readers never observe a partially-updated view.
struct LiveState {
    CoordinatorEpoch epoch;
    Policy policy;
    std::map<DeviceId, DeviceRecord> devices;
    std::map<ServiceId, ServiceDefinition> services;
    std::map<ProgramId, std::map<ProgramGeneration, ProgramArtifact>> programs;
    std::map<ProgramId, ProgramGeneration> current_program_gen;
    std::map<DeploymentId, Deployment> deployments;
    std::map<ActivationId, ActivationRecord> activations;
    std::map<ExecutionContextId, ExecutionContext> contexts;
    // Per-device resource ledger (capacity + usage).  Only usage is persisted
    // as part of durable state.
    std::map<DeviceId, ResourceLedger> ledgers;
    std::map<DeviceId, ResourceLedger::Usage> usage;
    std::vector<DeviceId> device_order;
    std::map<FunctionId, DeviceId> function_owner;   // function attachment owner
    std::map<PortId, DeviceId> port_owner;           // port attachment owner
    std::map<WorkerId, WorkerBootId> workers;        // known workers + current boot id
    std::map<WorkerId, uint64_t> worker_seq;         // registration sequence (deterministic)
};

// Publish an immutable snapshot from live state.
inline std::shared_ptr<const Snapshot> make_snapshot(const LiveState& s) {
    auto out = std::make_shared<Snapshot>();
    out->epoch = s.epoch;
    out->policy = s.policy;
    out->devices = s.devices;
    out->services = s.services;
    out->programs = s.programs;
    out->current_program_gen = s.current_program_gen;
    out->deployments = s.deployments;
    out->activations = s.activations;
    out->contexts = s.contexts;
    out->device_usage = s.usage;
    out->device_order = s.device_order;
    return out;
}

} // namespace dpufabric
