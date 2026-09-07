// Activation authority generation fencing.
#include "testharness.h"
#include "dpufabric/deployment.h"
#include "dpufabric/engine.h"
#include "dpufabric/synthetic.h"

using namespace dpufabric;
using namespace dpufabric::synth;

static void seed(DPUFabricEngine& e) { for (auto& d : scenario_ab()) e.register_device(d.record); }

DPUFABRIC_TEST(authority_epoch_change) {
    DPUFabricEngine e(CoordinatorEpoch(1)); seed(e);
    CHECK(e.epoch().as_u64() == 1);
    auto snap = e.snapshot();
    CHECK(snap->epoch.as_u64() == 1);
    // Advance epoch via coordinator restart simulation.
    e.advance_epoch();
    CHECK(e.epoch().as_u64() == 2);
    // The snapshot epoch advances; old activation authorizations under epoch 1 are stale.
    auto snap2 = e.snapshot();
    CHECK(snap2->epoch.as_u64() == 2);
    // Manager: a 1-epoch token is not current under 2.
    AuthorityContext ctx; ctx.epoch = CoordinatorEpoch(2);
    ActivationAuthority a1; a1.epoch = CoordinatorEpoch(1);
    CHECK(!a1.is_current(ctx));
}

DPUFABRIC_TEST(authority_stale_boot_and_generations) {
    // Use the engine's deployment authority fencing directly after a device boot change.
    DPUFabricEngine e(CoordinatorEpoch(1)); seed(e);
    ServiceDefinition s; s.service_id = ServiceId(80); s.service_gen = ServiceGeneration(1);
    s.offload_class = OffloadClass::STORAGE_SERVICE;
    CapabilityRequirement c; c.key = cap_storage(); s.required_caps.push_back(c);
    s.resource = ResourceRequest{ 64u << 20, 1, 0, 1, 1 };
    e.register_service(s);
    OffloadRequest req; req.offload_class = OffloadClass::STORAGE_SERVICE; req.service = ServiceId(80); req.mode = OffloadMode::OFFLOAD_REQUIRED;
    auto elig = e.evaluate(req); CHECK(elig.selected.has_value());
    auto dep = e.reserve_deployment(req, elig);
    e.stage_deployment(dep); e.load_deployment(dep); e.validate_deployment(dep); e.activate_deployment(dep); e.commit_deployment(dep);
    CHECK(e.snapshot()->deployment(dep)->authoritative);
    CHECK(e.snapshot()->deployment(dep)->epoch.as_u64() == 1);
    // Device boot changes -> fencing.
    e.fence_device(*elig.selected, "boot change");
    CHECK(!e.snapshot()->deployment(dep)->authoritative);
    CHECK(e.snapshot()->deployment(dep)->state == DeploymentState::REVALIDATION_REQUIRED);
}

DPUFABRIC_TEST(authority_generation_chain_current) {
    // Build a complete current authority and verify is_current; mutate each field.
    ActivationAuthority a;
    a.epoch = CoordinatorEpoch(3); a.worker = WorkerId(9); a.worker_boot = WorkerBootId(11);
    a.device = DeviceId(2); a.device_boot = DeviceBootId(22); a.device_gen = DeviceGeneration(1);
    a.service_gen = ServiceGeneration(2); a.program_gen = ProgramGeneration(3);
    a.deployment_gen = DeploymentGeneration(4); a.activation_gen = ActivationGeneration(5);
    a.policy_gen = PolicyGeneration(6); a.evidence_gen = EvidenceGeneration(7);
    AuthorityContext c;
    c.epoch = CoordinatorEpoch(3); c.worker = WorkerId(9); c.worker_boot = WorkerBootId(11);
    c.device_boot = DeviceBootId(22); c.device_gen = DeviceGeneration(1);
    c.service_gen = ServiceGeneration(2); c.program_gen = ProgramGeneration(3);
    c.deployment_gen = DeploymentGeneration(4); c.activation_gen = ActivationGeneration(5);
    c.policy_gen = PolicyGeneration(6); c.evidence_gen = EvidenceGeneration(7);
    CHECK(a.is_current(c));
    auto mutate = [&](auto&& fn){ fn(c); CHECK(!a.is_current(c)); };
    mutate([&](AuthorityContext& x){ x.epoch = CoordinatorEpoch(4); });
    c.epoch = CoordinatorEpoch(3);
    mutate([&](AuthorityContext& x){ x.worker_boot = WorkerBootId(12); });
    c.worker_boot = WorkerBootId(11);
    mutate([&](AuthorityContext& x){ x.device_boot = DeviceBootId(23); });
    c.device_boot = DeviceBootId(22);
    mutate([&](AuthorityContext& x){ x.program_gen = ProgramGeneration(4); });
    c.program_gen = ProgramGeneration(3);
    mutate([&](AuthorityContext& x){ x.activation_gen = ActivationGeneration(6); });
    c.activation_gen = ActivationGeneration(5);
    mutate([&](AuthorityContext& x){ x.policy_gen = PolicyGeneration(7); });
    c.policy_gen = PolicyGeneration(6);
    CHECK(a.is_current(c));
}

DPUFABRIC_MAIN
