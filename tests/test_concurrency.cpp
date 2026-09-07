// Concurrency stress: mutations racing reads and other mutations.
#include "testharness.h"
#include "dpufabric/engine.h"
#include "dpufabric/synthetic.h"
#include "dpufabric/eligibility.h"
#include <thread>
#include <vector>
#include <atomic>
#include <string>

using namespace dpufabric;
using namespace dpufabric::synth;

DPUFABRIC_TEST(concurrency_mutation_read_race) {
    DPUFabricEngine e(CoordinatorEpoch(1));
    for (auto& d : scenario_ab()) e.register_device(d.record);
    ServiceDefinition s; s.service_id = ServiceId(99); s.service_gen = ServiceGeneration(1);
    s.offload_class = OffloadClass::STORAGE_SERVICE;
    CapabilityRequirement c; c.key = cap_storage(); s.required_caps.push_back(c);
    s.resource = ResourceRequest{ 128u << 20, 1, 1, 1, 1 };
    e.register_service(s);

    std::atomic<bool> stop{ false };
    std::atomic<int> fails{ 0 };
    std::vector<std::thread> threads;

    const int iters = 2000;
    // Writer thread: register/update/fence + deployments.
    threads.emplace_back([&]() {
        for (int i = 0; i < iters; ++i) {
            OffloadRequest r; r.offload_class = OffloadClass::STORAGE_SERVICE; r.service = ServiceId(99); r.mode = OffloadMode::OFFLOAD_REQUIRED;
            try {
                auto elig = e.evaluate(r);
                if (elig.selected) {
                    auto dep = e.reserve_deployment(r, elig);
                    try { e.stage_deployment(dep); e.load_deployment(dep); e.validate_deployment(dep); e.activate_deployment(dep); e.commit_deployment(dep); } catch (const DpuError&) {}
                }
                if ((i % 7) == 0) e.fence_device(DeviceId(1), "churn");
                else e.set_device_state(DeviceId(1), CapabilityState::SUPPORTED, cap_storage());
            } catch (const DpuError&) {}
            if ((i % 5) == 0) std::this_thread::yield();
        }
        stop = true;
    });
    // A few readers/other writers.
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&, t]() {
            while (!stop.load()) {
                auto snap = e.snapshot();               // lock-free read
                (void)snap->devices.size();
                OffloadRequest r; r.offload_class = OffloadClass::STORAGE_SERVICE; r.service = ServiceId(99); r.mode = OffloadMode::OFFLOAD_REQUIRED;
                auto res = e.evaluate(r);               // lock-free eval on snapshot
                (void)res;
                if ((t % 2) == 0) std::this_thread::yield();
            }
        });
    }
    for (auto& th : threads) th.join();
    // After churn, invariant: no active deployment on a fenced/retired device.
    auto snap = e.snapshot();
    for (auto& [id, dep] : snap->deployments) {
        if (dep.state == DeploymentState::ACTIVE) {
            auto d = snap->device(dep.device);
            CHECK(d != nullptr);
            // Threads could leave a fenced device with an active deployment only if a race exists;
            // our engine fences deployments on device fence, so active implies ready.
            if (!d->is_ready_for_offload()) fails += 1;
        }
    }
    CHECK(fails.load() == 0);
}

DPUFABRIC_MAIN
