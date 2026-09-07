
#include "dpufabric/engine.h"
#include "dpufabric/eligibility.h"
#include "dpufabric/persistence.h"
#include <stdexcept>
#include <utility>

namespace dpufabric {

DPUFabricEngine::DPUFabricEngine(CoordinatorEpoch epoch, Policy policy)
    : snap_(std::make_shared<Snapshot>()) {
    state_.epoch = epoch;
    state_.policy = std::move(policy);
    rebuild_snapshot();
}

DPUFabricEngine::~DPUFabricEngine() = default;

CoordinatorEpoch DPUFabricEngine::epoch() const noexcept {
    return snap_.load()->epoch;
}

Policy DPUFabricEngine::policy() const {
    return snap_.load()->policy;
}

void DPUFabricEngine::rebuild_snapshot() {
    snap_.store(make_snapshot(state_));
}

// ---- backends ----
void DPUFabricEngine::import_backend(std::shared_ptr<DiscoveryBackend> backend) {
    if (!backend) throw_error(ErrorCode::INVALID_ARGUMENT, "null backend");
    std::lock_guard<std::mutex> g(mutex_);
    backends_.push_back(std::move(backend));
}

std::vector<DiscoveryResult> DPUFabricEngine::run_all_backends() {
    std::vector<std::shared_ptr<DiscoveryBackend>> backends;
    {
        std::lock_guard<std::mutex> g(mutex_);
        backends = backends_;
    }
    std::vector<DiscoveryResult> out;
    for (auto& b : backends) {
        DiscoveryResult r = b->discover();   // backend call: NO lock held
        // Register discovered devices (import via register_device under the lock).
        for (auto& dd : r.devices) {
            try {
                if (state_.devices.count(dd.record.device_id) == 0) {
                    register_device(dd.record);
                } else {
                    update_device(dd.record);
                }
            } catch (const DpuError&) {
                // Collision: leave as-is; duplication is reported by the caller.
            }
        }
        out.push_back(std::move(r));
    }
    return out;
}

// ---- device registry ----
DeviceId DPUFabricEngine::register_device(DeviceRecord rec) {
    if (rec.device_id.is_null()) throw_error(ErrorCode::INVALID_ARGUMENT, "device id must be set");
    std::lock_guard<std::mutex> g(mutex_);
    if (state_.devices.count(rec.device_id))
        throw_error(ErrorCode::ALREADY_EXISTS, "device already registered");
    // deterministic order
    state_.devices[rec.device_id] = rec;
    state_.device_order.push_back(rec.device_id);
    state_.ledgers.emplace(rec.device_id, ResourceLedger(rec.resource_capacity));
    state_.usage[rec.device_id] = ResourceLedger::Usage{};
    // attach ports/functions owner
    for (auto& p : rec.ports) state_.port_owner[p] = rec.device_id;
    for (auto& f : rec.functions) state_.function_owner[f] = rec.device_id;
    rebuild_snapshot();
    return rec.device_id;
}

DeviceId DPUFabricEngine::update_device(DeviceRecord rec) {
    if (rec.device_id.is_null()) throw_error(ErrorCode::INVALID_ARGUMENT, "device id must be set");
    std::lock_guard<std::mutex> g(mutex_);
    auto it = state_.devices.find(rec.device_id);
    if (it == state_.devices.end()) throw_error(ErrorCode::NOT_FOUND, "device not found");
    // Conflicting immutable identity: reject deterministic.
    const DeviceRecord& old = it->second;
    if (!old.pci_reference.empty() && !rec.pci_reference.empty() && old.pci_reference != rec.pci_reference)
        throw_error(ErrorCode::ALREADY_EXISTS, "conflicting immutable device identity (pci_reference)");
    it->second = rec;
    rebuild_snapshot();
    return rec.device_id;
}

void DPUFabricEngine::retire_device(DeviceId id) {
    std::lock_guard<std::mutex> g(mutex_);
    auto it = state_.devices.find(id);
    if (it == state_.devices.end()) throw_error(ErrorCode::NOT_FOUND, "device not found");
    // Fence/drain all deployments on this device.
    for (auto& [dep_id, dep] : state_.deployments) {
        if (dep.device == id &&
            (dep.state == DeploymentState::ACTIVE || dep.state == DeploymentState::DEGRADED ||
             dep.state == DeploymentState::DRAINING)) {
            if (valid_deployment_transition(dep.state, DeploymentState::REVALIDATION_REQUIRED))
                dep.state = DeploymentState::REVALIDATION_REQUIRED;
            dep.authoritative = false;
            if (dep.activation) {
                auto ait = state_.activations.find(*dep.activation);
                if (ait != state_.activations.end()) ait->second.state = ActivationState::STALE;
            }
            release_resources(dep_id);
        }
    }
    it->second.lifecycle = DeviceLifecycle::RETIRED;
    it->second.evidence_fresh = false;
    rebuild_snapshot();
}

void DPUFabricEngine::transition_device(DeviceId id, DeviceLifecycle to) {
    std::lock_guard<std::mutex> g(mutex_);
    auto it = state_.devices.find(id);
    if (it == state_.devices.end()) throw_error(ErrorCode::NOT_FOUND, "device not found");
    if (!valid_device_transition(it->second.lifecycle, to))
        throw_error(ErrorCode::INVALID_TRANSITION, "invalid device lifecycle transition");
    if (to == DeviceLifecycle::REVALIDATION_REQUIRED || to == DeviceLifecycle::OFFLINE ||
        to == DeviceLifecycle::FAILED) {
        it->second.evidence_fresh = false;
        for (auto& [k, c] : it->second.capabilities.all()) (void)k;
        // Fence deployments that relied on this evidence.
        for (auto& [dep_id, dep] : state_.deployments) {
            if (dep.device == id && (dep.state == DeploymentState::ACTIVE || dep.state == DeploymentState::DEGRADED)) {
                dep.state = DeploymentState::REVALIDATION_REQUIRED;
                dep.authoritative = false;
                if (dep.activation) {
                    auto ait = state_.activations.find(*dep.activation);
                    if (ait != state_.activations.end()) ait->second.state = ActivationState::STALE;
                }
            }
        }
    }
    it->second.lifecycle = to;
    rebuild_snapshot();
}

void DPUFabricEngine::set_device_state(DeviceId id, CapabilityState s, const CapabilityKey& cap) {
    std::lock_guard<std::mutex> g(mutex_);
    auto it = state_.devices.find(id);
    if (it == state_.devices.end()) throw_error(ErrorCode::NOT_FOUND, "device not found");
    it->second.capabilities.set_state(cap, s);
    rebuild_snapshot();
}

void DPUFabricEngine::fence_device(DeviceId id, std::string reason) {
    std::lock_guard<std::mutex> g(mutex_);
    auto it = state_.devices.find(id);
    if (it == state_.devices.end()) throw_error(ErrorCode::NOT_FOUND, "device not found");
    it->second.evidence_fresh = false;
    it->second.discovery_detail = reason;
    if (it->second.lifecycle != DeviceLifecycle::RETIRED && it->second.lifecycle != DeviceLifecycle::OFFLINE &&
        it->second.lifecycle != DeviceLifecycle::FAILED) {
        it->second.lifecycle = DeviceLifecycle::REVALIDATION_REQUIRED;
    }
    // Fence any deployments that were authoritative under this device evidence.
    for (auto& [dep_id, dep] : state_.deployments) {
        if (dep.device == id && (dep.state == DeploymentState::ACTIVE || dep.state == DeploymentState::DEGRADED)) {
            dep.state = DeploymentState::REVALIDATION_REQUIRED;
            dep.authoritative = false;
            if (dep.activation) {
                auto ait = state_.activations.find(*dep.activation);
                if (ait != state_.activations.end()) ait->second.state = ActivationState::STALE;
            }
        }
    }
    rebuild_snapshot();
}

// ---- service / program ----
ServiceId DPUFabricEngine::register_service(ServiceDefinition def) {
    if (def.service_id.is_null()) throw_error(ErrorCode::INVALID_ARGUMENT, "service id must be set");
    std::lock_guard<std::mutex> g(mutex_);
    if (state_.services.count(def.service_id)) throw_error(ErrorCode::ALREADY_EXISTS, "service already registered");
    state_.services[def.service_id] = std::move(def);
    rebuild_snapshot();
    return def.service_id;
}

ProgramId DPUFabricEngine::register_program(ProgramArtifact art) {
    if (art.program_id.is_null()) throw_error(ErrorCode::INVALID_ARGUMENT, "program id must be set");
    if (art.program_gen.is_null()) throw_error(ErrorCode::INVALID_ARGUMENT, "program generation must be set");
    std::lock_guard<std::mutex> g(mutex_);
    auto& gens = state_.programs[art.program_id];
    gens[art.program_gen] = std::move(art);
    auto cur = state_.current_program_gen.find(art.program_id);
    if (cur == state_.current_program_gen.end() || art.program_gen > cur->second) {
        state_.current_program_gen[art.program_id] = art.program_gen;
    }
    rebuild_snapshot();
    return art.program_id;
}

void DPUFabricEngine::retire_program(ProgramId id) {
    std::lock_guard<std::mutex> g(mutex_);
    if (!state_.programs.count(id)) throw_error(ErrorCode::NOT_FOUND, "program not found");
    state_.current_program_gen.erase(id);
    rebuild_snapshot();
}

// ---- worker lifecycle ----
void DPUFabricEngine::register_worker(WorkerId worker, WorkerBootId boot) {
    if (worker.is_null()) throw_error(ErrorCode::INVALID_ARGUMENT, "worker id must be set");
    if (boot.is_null()) throw_error(ErrorCode::INVALID_ARGUMENT, "worker boot id must be set");
    std::lock_guard<std::mutex> g(mutex_);
    auto it = state_.workers.find(worker);
    if (it != state_.workers.end()) {
        if (it->second == boot) return; // idempotent
        throw_error(ErrorCode::STALE_WORKER, "worker already registered with a different boot id");
    }
    state_.workers[worker] = boot;
    state_.worker_seq[worker] = state_.worker_seq.size() + 1;
    rebuild_snapshot();
}

void DPUFabricEngine::worker_heartbeat(WorkerId worker, WorkerBootId boot) {
    std::lock_guard<std::mutex> g(mutex_);
    auto it = state_.workers.find(worker);
    if (it == state_.workers.end() || it->second != boot)
        throw_error(ErrorCode::STALE_WORKER, "unknown worker or stale boot id");
}

void DPUFabricEngine::mark_worker_lost(WorkerId worker) {
    std::lock_guard<std::mutex> g(mutex_);
    // Fence all devices owned by the dead worker and their evidence.
    for (auto& [id, rec] : state_.devices) {
        if (rec.owning_worker == worker && rec.lifecycle != DeviceLifecycle::RETIRED) {
            rec.evidence_fresh = false;
            rec.lifecycle = DeviceLifecycle::REVALIDATION_REQUIRED;
            for (auto& [k, c] : rec.capabilities.all()) (void)k;
        }
    }
    // Fence deployments whose authority is bound to the dead worker's boot.
    auto itb = state_.workers.find(worker);
    if (itb != state_.workers.end()) {
        WorkerBootId old_boot = itb->second;
        for (auto& [dep_id, dep] : state_.deployments) {
            if (dep.worker_boot == old_boot && dep.state == DeploymentState::ACTIVE) {
                dep.state = DeploymentState::REVALIDATION_REQUIRED;
                dep.authoritative = false;
                if (dep.activation) {
                    auto ait = state_.activations.find(*dep.activation);
                    if (ait != state_.activations.end()) ait->second.state = ActivationState::STALE;
                }
            }
        }
        // Permit the worker to re-register with a fresh boot id after this loss.
        state_.workers.erase(itb);
        state_.worker_seq.erase(worker);
    }
    rebuild_snapshot();
}

// ---- offload evaluation ----
EligibilityResult DPUFabricEngine::evaluate(const OffloadRequest& req) const {
    return evaluate_offload(*snap_.load(), req);
}

// ---- deployment lifecycle ----
void DPUFabricEngine::do_deploy_transition(DeploymentId id, DeploymentState to) {
    auto it = state_.deployments.find(id);
    if (it == state_.deployments.end()) throw_error(ErrorCode::NOT_FOUND, "deployment not found");
    if (!valid_deployment_transition(it->second.state, to))
        throw_error(ErrorCode::INVALID_TRANSITION, "invalid deployment state transition");
    it->second.state = to;
    it->second.authoritative = (to == DeploymentState::ACTIVE);
    rebuild_snapshot();
}

void DPUFabricEngine::release_resources(DeploymentId id) {
    auto it = state_.deployments.find(id);
    if (it == state_.deployments.end()) return;
    const Deployment& dep = it->second;
    auto lit = state_.ledgers.find(dep.device);
    if (lit != state_.ledgers.end()) {
        try { lit->second.release(dep.resources); } catch (...) { /* no-op */ }
        state_.usage[dep.device] = lit->second.current();
    }
}

PlanResult DPUFabricEngine::plan_deployment(const OffloadRequest& req) {
    EligibilityResult elig = evaluate(req);
    PlanResult pr;
    pr.eligibility = elig;
    if (elig.selected) {
        pr.planned = true;
        pr.device = *elig.selected;
        pr.note = "eligible device found";
    } else if (req.mode == OffloadMode::OFFLOAD_PREFERRED && req.allow_host_fallback) {
        pr.planned = true;  // fallback is a valid plan
        pr.note = "host fallback";
    } else {
        pr.planned = false;
        pr.note = "no eligible offload candidate";
    }
    return pr;
}

DeploymentId DPUFabricEngine::reserve_deployment(const OffloadRequest& req, const EligibilityResult& elig) {
    if (!elig.selected) throw_error(ErrorCode::INVALID_STATE, "cannot reserve without a selected offload device");
    std::lock_guard<std::mutex> g(mutex_);
    auto dev_it = state_.devices.find(*elig.selected);
    if (dev_it == state_.devices.end() || !dev_it->second.is_ready_for_offload())
        throw_error(ErrorCode::DEVICE_NOT_READY, "selected device is not ready");
    const ServiceDefinition* svc = nullptr;
    if (req.service) {
        auto sit = state_.services.find(*req.service);
        if (sit == state_.services.end()) throw_error(ErrorCode::NOT_FOUND, "service not found");
        svc = &sit->second;
    }
    ResourceRequest rr = svc ? svc->resource : req.resource;

    Deployment d;
    d.deployment_id = DeploymentId(next_id());
    d.generation = DeploymentGeneration(1);
    d.attempt_id = DeploymentAttemptId(next_id());
    d.service = req.service ? *req.service : ServiceId::null();
    d.service_gen = svc ? svc->service_gen : ServiceGeneration::null();
    d.program = req.required_program ? *req.required_program : (svc ? svc->required_program : ProgramId::null());
    d.program_gen = ProgramGeneration::null();
    if (!d.program.is_null()) {
        auto pg = state_.current_program_gen.find(d.program);
        if (pg != state_.current_program_gen.end()) d.program_gen = pg->second;
    }
    d.artifact = ArtifactId::null();
    d.device = *elig.selected;
    d.device_gen = dev_it->second.generation;
    d.device_boot = dev_it->second.boot_id;
    d.tenant = req.tenant;
    d.isolation_domain = req.isolation_domain;
    d.resources = rr;
    d.state = DeploymentState::RESERVED;
    d.authoritative = false;

    auto lit = state_.ledgers.find(d.device);
    if (lit == state_.ledgers.end()) throw_error(ErrorCode::INVALID_STATE, "no ledger for device");
    if (!lit->second.can_reserve(rr)) throw_error(ErrorCode::RESOURCE_EXHAUSTED, "device resource budget exhausted");
    lit->second.try_reserve(rr).commit();
    state_.usage[d.device] = lit->second.current();

    DeploymentId id = d.deployment_id;
    state_.deployments[id] = std::move(d);
    rebuild_snapshot();
    return id;
}

void DPUFabricEngine::stage_deployment(DeploymentId id) { std::lock_guard<std::mutex> g(mutex_); do_deploy_transition(id, DeploymentState::STAGING); }
void DPUFabricEngine::load_deployment(DeploymentId id) { std::lock_guard<std::mutex> g(mutex_); do_deploy_transition(id, DeploymentState::LOADED); }
void DPUFabricEngine::validate_deployment(DeploymentId id) { std::lock_guard<std::mutex> g(mutex_); do_deploy_transition(id, DeploymentState::VALIDATING); }

ActivationId DPUFabricEngine::activate_deployment(DeploymentId id) {
    std::lock_guard<std::mutex> g(mutex_);
    do_deploy_transition(id, DeploymentState::ACTIVATING);
    auto it = state_.deployments.find(id);
    ActivationRecord act;
    act.activation_id = ActivationId(next_id());
    act.activation_gen = ActivationGeneration(next_generation(it->second.activation_gen.as_u64()));
    act.deployment_id = id;
    act.deployment_gen = it->second.generation;
    act.state = ActivationState::PENDING;
    it->second.activation = act.activation_id;
    it->second.activation_gen = act.activation_gen;
    ActivationId aid = act.activation_id;
    state_.activations[aid] = std::move(act);
    rebuild_snapshot();
    return aid;
}

void DPUFabricEngine::commit_deployment(DeploymentId id) {
    std::lock_guard<std::mutex> g(mutex_);
    auto dep_it = state_.deployments.find(id);
    if (dep_it == state_.deployments.end()) throw_error(ErrorCode::NOT_FOUND, "deployment not found");
    Deployment& dep = dep_it->second;
    if (dep.state != DeploymentState::ACTIVATING) throw_error(ErrorCode::INVALID_STATE, "deployment must be ACTIVATING to commit");
    // Authority is only granted if every generation/boot component is current.
    auto dev_it = state_.devices.find(dep.device);
    if (dev_it == state_.devices.end() || !dev_it->second.is_ready_for_offload())
        throw_error(ErrorCode::DEVICE_NOT_READY, "device not ready for active authority");
    if (!dep.program.is_null()) {
        auto cg = state_.current_program_gen.find(dep.program);
        if (cg == state_.current_program_gen.end() || cg->second != dep.program_gen)
            throw_error(ErrorCode::STALE_PROGRAM_GENERATION, "program generation no longer current");
    }
    if (!dep.service.is_null()) {
        auto sg = state_.services.find(dep.service);
        if (sg != state_.services.end() && sg->second.service_gen != dep.service_gen) {
            // If the service definition advanced, this deployment is stale.
        }
    }

    ActivationAuthority auth;
    auth.epoch = state_.epoch;
    auth.worker = WorkerId::null();
    auth.worker_boot = dep.worker_boot;
    auth.device = dep.device;
    auth.device_boot = dep.device_boot;
    auth.device_gen = dep.device_gen;
    auth.service_gen = dep.service_gen;
    auth.program_gen = dep.program_gen;
    auth.deployment_gen = dep.generation;
    auth.activation_gen = dep.activation_gen;
    auth.policy_gen = state_.policy.generation;
    auth.evidence_gen = dev_it->second.evidence_gen;

    auto act_it = state_.activations.find(*dep.activation);
    if (act_it == state_.activations.end()) throw_error(ErrorCode::INVALID_STATE, "activation missing");
    act_it->second.authority = auth;
    act_it->second.state = ActivationState::ACTIVE;
    act_it->second.reason = "committed";
    dep.state = DeploymentState::ACTIVE;
    dep.authoritative = true;
    dep.epoch = state_.epoch;
    rebuild_snapshot();
}

void DPUFabricEngine::cancel_deployment(DeploymentId id) {
    std::lock_guard<std::mutex> g(mutex_);
    auto it = state_.deployments.find(id);
    if (it == state_.deployments.end()) throw_error(ErrorCode::NOT_FOUND, "deployment not found");
    if (it->second.state != DeploymentState::PLANNED && it->second.state != DeploymentState::RESERVED &&
        it->second.state != DeploymentState::STAGING && it->second.state != DeploymentState::LOADED &&
        it->second.state != DeploymentState::VALIDATING && it->second.state != DeploymentState::ACTIVATING)
        throw_error(ErrorCode::INVALID_STATE, "cannot cancel in current state");
    release_resources(id);
    if (it->second.activation) {
        auto ait = state_.activations.find(*it->second.activation);
        if (ait != state_.activations.end()) ait->second.state = ActivationState::CANCELLED;
    }
    it->second.state = DeploymentState::ABORTED;
    it->second.authoritative = false;
    rebuild_snapshot();
}

void DPUFabricEngine::rollback_deployment(DeploymentId id) {
    std::lock_guard<std::mutex> g(mutex_);
    auto it = state_.deployments.find(id);
    if (it == state_.deployments.end()) throw_error(ErrorCode::NOT_FOUND, "deployment not found");
    if (it->second.state != DeploymentState::FAILED && it->second.state != DeploymentState::RESERVED &&
        it->second.state != DeploymentState::STAGING && it->second.state != DeploymentState::LOADED &&
        it->second.state != DeploymentState::VALIDATING && it->second.state != DeploymentState::ACTIVATING)
        throw_error(ErrorCode::INVALID_STATE, "cannot rollback in current state");
    release_resources(id);
    if (it->second.activation) {
        auto ait = state_.activations.find(*it->second.activation);
        if (ait != state_.activations.end()) ait->second.state = ActivationState::CANCELLED;
    }
    it->second.state = DeploymentState::ABORTED;
    it->second.authoritative = false;
    rebuild_snapshot();
}

void DPUFabricEngine::drain_deployment(DeploymentId id) {
    std::lock_guard<std::mutex> g(mutex_);
    auto it = state_.deployments.find(id);
    if (it == state_.deployments.end()) throw_error(ErrorCode::NOT_FOUND, "deployment not found");
    if (it->second.state != DeploymentState::ACTIVE && it->second.state != DeploymentState::DEGRADED &&
        it->second.state != DeploymentState::REVALIDATION_REQUIRED)
        throw_error(ErrorCode::INVALID_STATE, "only active/degraded/revalidation-required deployment can drain");
    if (it->second.activation) {
        auto ait = state_.activations.find(*it->second.activation);
        if (ait != state_.activations.end()) ait->second.state = ActivationState::DRAINING;
    }
    it->second.state = DeploymentState::DRAINING;
    it->second.authoritative = false;
    rebuild_snapshot();
}

void DPUFabricEngine::retire_deployment(DeploymentId id) {
    std::lock_guard<std::mutex> g(mutex_);
    auto it = state_.deployments.find(id);
    if (it == state_.deployments.end()) throw_error(ErrorCode::NOT_FOUND, "deployment not found");
    if (it->second.state != DeploymentState::DRAINING && it->second.state != DeploymentState::FAILED &&
        it->second.state != DeploymentState::ABORTED && it->second.state != DeploymentState::REVALIDATION_REQUIRED &&
        it->second.state != DeploymentState::ACTIVE && it->second.state != DeploymentState::DEGRADED)
        throw_error(ErrorCode::INVALID_STATE, "cannot retire in current state");
    if (it->second.state == DeploymentState::ACTIVE || it->second.state == DeploymentState::DEGRADED ||
        it->second.state == DeploymentState::DRAINING || it->second.state == DeploymentState::REVALIDATION_REQUIRED) {
        if (it->second.activation) {
            auto ait = state_.activations.find(*it->second.activation);
            if (ait != state_.activations.end()) ait->second.state = ActivationState::RETIRED;
        }
        release_resources(id);
    }
    it->second.state = DeploymentState::RETIRED;
    it->second.authoritative = false;
    rebuild_snapshot();
}

void DPUFabricEngine::fail_deployment(DeploymentId id, ErrorCode code, std::string reason) {
    std::lock_guard<std::mutex> g(mutex_);
    auto it = state_.deployments.find(id);
    if (it == state_.deployments.end()) throw_error(ErrorCode::NOT_FOUND, "deployment not found");
    (void)code; (void)reason;
    if (!valid_deployment_transition(it->second.state, DeploymentState::FAILED))
        throw_error(ErrorCode::INVALID_TRANSITION, "cannot fail in current state");
    release_resources(id);
    if (it->second.activation) {
        auto ait = state_.activations.find(*it->second.activation);
        if (ait != state_.activations.end()) ait->second.state = ActivationState::STALE;
    }
    it->second.state = DeploymentState::FAILED;
    it->second.authoritative = false;
    rebuild_snapshot();
}

// ---- coordinator restart / recovery ----
void DPUFabricEngine::advance_epoch() {
    std::lock_guard<std::mutex> g(mutex_);
    state_.epoch = CoordinatorEpoch(next_generation(state_.epoch.as_u64()));
    rebuild_snapshot();
}

void DPUFabricEngine::apply_recovery(RecoveryPolicy p) {
    std::lock_guard<std::mutex> g(mutex_);
    for (auto& [id, rec] : state_.devices) {
        if (rec.lifecycle == DeviceLifecycle::RETIRED) continue;
        rec.evidence_fresh = false;
        if (rec.lifecycle != DeviceLifecycle::FAILED && rec.lifecycle != DeviceLifecycle::OFFLINE)
            rec.lifecycle = DeviceLifecycle::REVALIDATION_REQUIRED;
        if (p == RecoveryPolicy::FENCE_ALL_DYNAMIC) {
            for (auto& [k, c] : rec.capabilities.mutable_all()) {
                (void)k;
                if (c.state == CapabilityState::SUPPORTED) c.state = CapabilityState::REVALIDATION_REQUIRED;
            }
        }
    }
    for (auto& [dep_id, dep] : state_.deployments) {
        if (dep.state == DeploymentState::ACTIVE || dep.state == DeploymentState::DEGRADED) {
            dep.state = DeploymentState::REVALIDATION_REQUIRED;
            dep.authoritative = false;
            if (dep.activation) {
                auto ait = state_.activations.find(*dep.activation);
                if (ait != state_.activations.end()) ait->second.state = ActivationState::STALE;
            }
        }
    }
    rebuild_snapshot();
}

// ---- persistence ----
void DPUFabricEngine::persist(const std::string& path) {
    auto s = snap_.load();
    PersistenceStore store(path);
    store.save(*s);
}

bool DPUFabricEngine::recover_from(const std::string& path) {
    std::lock_guard<std::mutex> g(mutex_);
    PersistenceStore store(path);
    CoordinatorEpoch floor = CoordinatorEpoch(state_.epoch.as_u64());
    bool existed = store.load(state_, floor);
    if (existed) {
        state_.epoch = CoordinatorEpoch(next_generation(state_.epoch.as_u64()));
        recovered_ = true;
        // Rebuild ledgers for recovered devices from their capacities.
        for (auto& [did, rec] : state_.devices) {
            state_.ledgers.emplace(did, ResourceLedger(rec.resource_capacity));
            state_.usage[did] = ResourceLedger::Usage{};
        }
        // Re-seed usage from recovered usage if present (already fenced by load).
    }
    rebuild_snapshot();
    return existed;
}

// ---- summary ----
std::string DPUFabricEngine::describe() const {
    auto s = snap_.load();
    std::string out;
    out += "epoch=" + s->epoch.encode();
    out += " devices=" + std::to_string(s->devices.size());
    out += " services=" + std::to_string(s->services.size());
    out += " programs=" + std::to_string(s->programs.size());
    out += " deployments=" + std::to_string(s->deployments.size());
    out += " activations=" + std::to_string(s->activations.size());
    return out;
}

} // namespace dpufabric
