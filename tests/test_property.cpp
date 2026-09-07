// Seeded deterministic property / invariant test.
#include "testharness.h"
#include "dpufabric/engine.h"
#include "dpufabric/synthetic.h"
#include "dpufabric/eligibility.h"
#include <random>
#include <stdexcept>
#include <string>

using namespace dpufabric;
using namespace dpufabric::synth;

static bool check_invariants(const DPUFabricEngine& e) {
    auto snap = e.snapshot();
    // 1. No active deployment without a current, ready device.
    for (auto& [id, dep] : snap->deployments) {
        if (dep.state == DeploymentState::ACTIVE) {
            auto d = snap->device(dep.device);
            if (!d) return false;
            if (!d->is_ready_for_offload()) return false;
            if (!dep.authoritative) return false;
        }
        if (dep.authoritative != (dep.state == DeploymentState::ACTIVE)) return false;
    }
    // 2. No REVALIDATION_REQUIRED / FAILED / RETIRED device has an active deployment.
    for (auto& [did, d] : snap->devices) {
        if (d.lifecycle == DeviceLifecycle::REVALIDATION_REQUIRED || d.lifecycle == DeviceLifecycle::FAILED ||
            d.lifecycle == DeviceLifecycle::RETIRED || d.lifecycle == DeviceLifecycle::DRAINING) {
            for (auto& [id, dep] : snap->deployments) {
                if (dep.device == did && dep.state == DeploymentState::ACTIVE) return false;
            }
        }
    }
    // 3. Resource usage never below zero and never above capacity.
    for (auto& [did, u] : snap->device_usage) {
        auto d = snap->device(did);
        if (!d) continue;
        auto cap = d->resource_capacity;
        if (cap.memory_bytes && u.memory_bytes > cap.memory_bytes) return false;
        if (cap.queues && u.queues > cap.queues) return false;
        if (cap.slots && u.slots > cap.slots) return false;
        if (cap.contexts && u.contexts > cap.contexts) return false;
    }
    // 4. No retired deployment active.
    for (auto& [id, dep] : snap->deployments) {
        if (dep.state == DeploymentState::RETIRED && dep.authoritative) return false;
    }
    return true;
}

DPUFABRIC_TEST(property_random_invariants) {
    const uint32_t seed = 0x5EED1234u;
    std::mt19937 rng(seed);
    try {
        DPUFabricEngine e(CoordinatorEpoch(1));
        for (auto& d : scenario_ab()) e.register_device(d.record);

        // A service we will deploy/activate/drain.
        ServiceDefinition s; s.service_id = ServiceId(88); s.service_gen = ServiceGeneration(1);
        s.offload_class = OffloadClass::STORAGE_SERVICE;
        CapabilityRequirement c; c.key = cap_storage(); s.required_caps.push_back(c);
        s.resource = ResourceRequest{ 128u << 20, 1, 1, 1, 1 };
        e.register_service(s);

        std::vector<DeploymentId> deps;
        const int ops = 3000;
        for (int i = 0; i < ops; ++i) {
            int choice = static_cast<int>(rng() % 6);
            auto reqfor = [&]() {
                OffloadRequest r; r.offload_class = OffloadClass::STORAGE_SERVICE; r.service = ServiceId(88); r.mode = OffloadMode::OFFLOAD_REQUIRED;
                return r;
            };
            if (choice == 0) {
                // evaluate (read-only)
                (void)e.evaluate(reqfor());
            } else if (choice == 1) {
                // plan + reserve + maybe advance
                try {
                    auto req = reqfor();
                    auto elig = e.evaluate(req);
                    if (elig.selected) {
                        auto dep = e.reserve_deployment(req, elig);
                        deps.push_back(dep);
                    }
                } catch (const DpuError&) {}
            } else if (choice == 2 && !deps.empty()) {
                // advance a deployment one or two steps
                auto dep = deps[rng() % deps.size()];
                try { e.stage_deployment(dep); e.load_deployment(dep); } catch (const DpuError&) {}
            } else if (choice == 3 && !deps.empty()) {
                auto dep = deps[rng() % deps.size()];
                try { e.validate_deployment(dep); e.activate_deployment(dep); e.commit_deployment(dep); } catch (const DpuError&) {}
            } else if (choice == 4 && !deps.empty()) {
                auto dep = deps[rng() % deps.size()];
                try { e.drain_deployment(dep); } catch (const DpuError&) {}
                try { e.retire_deployment(dep); } catch (const DpuError&) {}
            } else if (choice == 5 || deps.empty()) {
                // random evidence churn / device fence or reset
                DeviceId did = (rng() & 1) ? DeviceId(1) : DeviceId(2);
                try {
                    if ((rng() & 1) == 0) e.fence_device(did, "churn");
                    else e.set_device_state(did, CapabilityState::SUPPORTED, cap_storage());
                } catch (const DpuError&) {}
            }
            if (!check_invariants(e)) {
                throw std::runtime_error("invariant violated at op " + std::to_string(i));
            }
        }
        // Determinism: identical request at the end yields the identical result.
        OffloadRequest r; r.offload_class = OffloadClass::STORAGE_SERVICE; r.service = ServiceId(88); r.mode = OffloadMode::OFFLOAD_REQUIRED;
        auto a = e.evaluate(r); auto b = e.evaluate(r);
        CHECK(a.explanation == b.explanation);
        CHECK(a.selected == b.selected);
    } catch (...) {
        std::printf("PROPERTY FAILURE seed=0x%08x\n", seed);
        throw;
    }
}

DPUFABRIC_MAIN
