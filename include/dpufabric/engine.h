/// DPU Fabric: the coordinator engine.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <atomic>
#include <mutex>
#include <map>
#include "dpufabric/id.h"
#include "dpufabric/types.h"
#include "dpufabric/device.h"
#include "dpufabric/program.h"
#include "dpufabric/deployment.h"
#include "dpufabric/request.h"
#include "dpufabric/eligibility.h"
#include "dpufabric/policy.h"
#include "dpufabric/registry.h"
#include "dpufabric/backend.h"
#include "dpufabric/resource.h"
#include "dpufabric/error.h"
#include "dpufabric/persistence.h"

namespace dpufabric {

// Result of a deployment plan step.
struct PlanResult {
    DeploymentId deployment_id;
    DeviceId device;
    bool planned = false;
    EligibilityResult eligibility;
    std::string note;
};

// How conservative a coordinator restart is: how much dynamic evidence is
// fenced back to REVALIDATION_REQUIRED.
enum class RecoveryPolicy : uint8_t {
    FENCE_ALL_DYNAMIC = 1,   // every dynamic field requires revalidation
    FENCE_AUTHORITY_ONLY = 2 // fence activations/authority, keep structural
};

class DPUFabricEngine {
public:
    explicit DPUFabricEngine(CoordinatorEpoch epoch, Policy policy = Policy::defaults(PolicyGeneration(1)));
    ~DPUFabricEngine();
    DPUFabricEngine(const DPUFabricEngine&) = delete;
    DPUFabricEngine& operator=(const DPUFabricEngine&) = delete;

    // ---- read-only snapshot (lock-free) ----
    std::shared_ptr<const Snapshot> snapshot() const noexcept { return snap_.load(); }
    CoordinatorEpoch epoch() const noexcept;
    Policy policy() const;

    // ---- backends ----
    void import_backend(std::shared_ptr<DiscoveryBackend> backend);
    std::vector<DiscoveryResult> run_all_backends();

    // ---- device registry ----
    DeviceId register_device(DeviceRecord rec);
    DeviceId update_device(DeviceRecord rec);
    void retire_device(DeviceId id);
    void transition_device(DeviceId id, DeviceLifecycle to);
    void set_device_state(DeviceId id, CapabilityState s, const CapabilityKey& cap);
    void fence_device(DeviceId id, std::string reason);

    // ---- service / program registry ----
    ServiceId register_service(ServiceDefinition def);
    ProgramId register_program(ProgramArtifact art);
    void retire_program(ProgramId id);

    // ---- worker evidence lifecycle ----
    void register_worker(WorkerId worker, WorkerBootId boot);
    void worker_heartbeat(WorkerId worker, WorkerBootId boot);
    void mark_worker_lost(WorkerId worker);

    // ---- offload evaluation ----
    EligibilityResult evaluate(const OffloadRequest& req) const;

    // ---- deployment lifecycle ----
    PlanResult plan_deployment(const OffloadRequest& req);
    DeploymentId reserve_deployment(const OffloadRequest& req, const EligibilityResult& elig);
    void stage_deployment(DeploymentId id);
    void load_deployment(DeploymentId id);
    void validate_deployment(DeploymentId id);
    ActivationId activate_deployment(DeploymentId id);
    void commit_deployment(DeploymentId id);
    void cancel_deployment(DeploymentId id);
    void rollback_deployment(DeploymentId id);
    void drain_deployment(DeploymentId id);
    void retire_deployment(DeploymentId id);
    void fail_deployment(DeploymentId id, ErrorCode code, std::string reason);

    // ---- coordinator restart / recovery ----
    void advance_epoch();
    void apply_recovery(RecoveryPolicy p = RecoveryPolicy::FENCE_ALL_DYNAMIC);

    // ---- persistence ----
    void persist(const std::string& path);
    bool recover_from(const std::string& path);

    // Summary for CLI/reporting.
    std::string describe() const;

private:
    // Apply a transition to a deployment with validation.
    void do_deploy_transition(DeploymentId id, DeploymentState to);
    void release_resources(DeploymentId id);
    void rebuild_snapshot();

    mutable std::mutex mutex_;
    LiveState state_;
    std::atomic<std::shared_ptr<const Snapshot>> snap_;
    std::vector<std::shared_ptr<DiscoveryBackend>> backends_;
    uint64_t id_seq_ = 1;   // monotonic id generator for deployments/activations
    bool recovered_ = false;

    uint64_t next_id() noexcept { return id_seq_++; }
};

} // namespace dpufabric
