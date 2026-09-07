// Synthetic scenarios A-R and deterministic ranking.
#include "testharness.h"
#include "dpufabric/engine.h"
#include "dpufabric/synthetic.h"
#include "dpufabric/types.h"
#include "dpufabric/eligibility.h"
#include <string>

using namespace dpufabric;
using namespace dpufabric::synth;

static void seed(DPUFabricEngine& e) { for (auto& d : scenario_ab()) e.register_device(d.record); }


DPUFABRIC_TEST(scenario_multi_device_selection) {
    DPUFabricEngine e(CoordinatorEpoch(1)); seed(e);
    auto sid = DeviceId(1), sidb = DeviceId(2);
    // device A (id1) encryption, device B (id2) storage.
    OffloadRequest req; req.offload_class = OffloadClass::SECURITY_SERVICE; req.mode = OffloadMode::OFFLOAD_REQUIRED;
    CapabilityRequirement c; c.key = cap_encryption(); req.required_caps.push_back(c);
    auto res = e.evaluate(req);
    CHECK(res.selected && *res.selected == DeviceId(1));

    OffloadRequest req2; req2.offload_class = OffloadClass::STORAGE_SERVICE; req2.mode = OffloadMode::OFFLOAD_REQUIRED;
    CapabilityRequirement c2; c2.key = cap_storage(); req2.required_caps.push_back(c2);
    auto res2 = e.evaluate(req2);
    CHECK(res2.selected && *res2.selected == DeviceId(2));
    (void)sid; (void)sidb;
}

DPUFABRIC_TEST(scenario_capability_query) {
    DPUFabricEngine e(CoordinatorEpoch(1)); seed(e);
    auto snap = e.snapshot();
    auto a = snap->device(DeviceId(1));
    CHECK(a != nullptr);
    CHECK(a->capabilities.authoritative(cap_encryption()));
    CHECK(!a->capabilities.authoritative(cap_storage()));
    CHECK(a->supports_class(OffloadClass::NETWORK_SERVICE));
}

DPUFABRIC_TEST(scenario_fallback_explicit) {
    DPUFabricEngine e(CoordinatorEpoch(1)); seed(e);
    OffloadRequest req; req.offload_class = OffloadClass::VIRTUAL_SWITCHING; req.mode = OffloadMode::OFFLOAD_PREFERRED; req.allow_host_fallback = true;
    auto res = e.evaluate(req);
    CHECK(res.fallback_used);
    CHECK(res.execution == ExecutionClass::HOST_FALLBACK);
    CHECK(res.fallback_reason == "DPU_CAPABILITY_UNAVAILABLE");
}

DPUFABRIC_TEST(scenario_isolation_mismatch) {
    DPUFabricEngine e(CoordinatorEpoch(1)); seed(e);
    ServiceDefinition s; s.service_id = ServiceId(70); s.service_gen = ServiceGeneration(1);
    s.offload_class = OffloadClass::STORAGE_SERVICE;
    CapabilityRequirement c; c.key = cap_storage(); s.required_caps.push_back(c);
    s.isolation.level = IsolationRequirement::TENANT_COMPATIBLE_SHARED;
    s.resource = ResourceRequest{ 64u << 20, 1, 0, 1, 1 };
    e.register_service(s);
    OffloadRequest req; req.offload_class = OffloadClass::STORAGE_SERVICE; req.service = ServiceId(70); req.mode = OffloadMode::OFFLOAD_REQUIRED;
    req.tenant = TenantId(999);
    auto res = e.evaluate(req);
    CHECK(!res.hard_pass);
    bool found = false;
    for (auto& cand : res.candidates) for (auto rr : cand.rejected) if (rr == RejectionReason::TENANT_MISMATCH) found = true;
    CHECK(found);
}

DPUFABRIC_TEST(scenario_resource_pressure_and_program_update) {
    DPUFabricEngine e(CoordinatorEpoch(1)); seed(e);
    // Register a program at gen 1 on arm64 for device B (storage).
    ProgramArtifact p; p.program_id = ProgramId(200); p.program_gen = ProgramGeneration(1);
    p.artifact_id = ArtifactId(201); p.artifact_gen = ArtifactGeneration(1);
    p.artifact_type = ArtifactType::RTE_PROGRAM; p.target_architecture = "arm64";
    p.runtime_abi = "synthetic-rte";
    e.register_program(p);
    ServiceDefinition s; s.service_id = ServiceId(71); s.service_gen = ServiceGeneration(1);
    s.offload_class = OffloadClass::STORAGE_SERVICE;
    CapabilityRequirement c; c.key = cap_storage(); s.required_caps.push_back(c);
    s.required_program = ProgramId(200);
    s.resource = ResourceRequest{ 1024u << 20, 1, 0, 1, 1 };
    e.register_service(s);
    OffloadRequest req; req.offload_class = OffloadClass::STORAGE_SERVICE; req.service = ServiceId(71); req.mode = OffloadMode::OFFLOAD_REQUIRED;
    req.required_program = ProgramId(200);
    // device B has 2GB memory; 1GB request fits. Deploy and commit.
    auto elig = e.evaluate(req);
    CHECK(elig.selected && *elig.selected == DeviceId(2));
    auto dep = e.reserve_deployment(req, elig);
    e.stage_deployment(dep); e.load_deployment(dep); e.validate_deployment(dep);
    e.activate_deployment(dep);
    // Advance the program to gen 2 BEFORE commit -> commit must fail (stale program gen).
    ProgramArtifact p2 = p; p2.program_gen = ProgramGeneration(2);
    e.register_program(p2);
    CHECK_THROWS_CODE(e.commit_deployment(dep), ErrorCode::STALE_PROGRAM_GENERATION);
}

DPUFABRIC_TEST(scenario_deviate_authority_and_drain) {
    DPUFabricEngine e(CoordinatorEpoch(1)); seed(e);
    auto sid = ServiceId(72);
    ServiceDefinition s; s.service_id = sid; s.service_gen = ServiceGeneration(1);
    s.offload_class = OffloadClass::STORAGE_SERVICE;
    CapabilityRequirement c; c.key = cap_storage(); s.required_caps.push_back(c);
    s.resource = ResourceRequest{ 64u << 20, 1, 0, 1, 1 };
    e.register_service(s);
    OffloadRequest req; req.offload_class = OffloadClass::STORAGE_SERVICE; req.service = sid; req.mode = OffloadMode::OFFLOAD_REQUIRED;
    auto elig = e.evaluate(req); CHECK(elig.selected.has_value());
    auto dep = e.reserve_deployment(req, elig);
    e.stage_deployment(dep); e.load_deployment(dep); e.validate_deployment(dep); e.activate_deployment(dep); e.commit_deployment(dep);
    CHECK(e.snapshot()->deployment(dep)->state == DeploymentState::ACTIVE);
    // Device degrades -> deployment fenced.
    e.fence_device(*elig.selected, "degraded");
    CHECK(e.snapshot()->deployment(dep)->state == DeploymentState::REVALIDATION_REQUIRED);
    CHECK(!e.snapshot()->deployment(dep)->authoritative);
    // Drain + retire allows clean release.
    e.drain_deployment(dep);
    e.retire_deployment(dep);
    CHECK(e.snapshot()->usage(*elig.selected)->slots == 0);
}

DPUFABRIC_TEST(scenario_unknown_capability_fails_closed) {
    DPUFabricEngine e(CoordinatorEpoch(1)); seed(e);
    // Device A has ENCRYPTION SUPPORTED. Set it to UNKNOWN and verify it fails closed.
    e.set_device_state(DeviceId(1), CapabilityState::UNKNOWN, cap_encryption());
    ServiceDefinition s; s.service_id = ServiceId(73); s.service_gen = ServiceGeneration(1);
    s.offload_class = OffloadClass::SECURITY_SERVICE;
    CapabilityRequirement c; c.key = cap_encryption(); s.required_caps.push_back(c);
    s.resource = ResourceRequest{ 64u << 20, 1, 0, 1, 1 };
    e.register_service(s);
    OffloadRequest req; req.offload_class = OffloadClass::SECURITY_SERVICE; req.service = ServiceId(73); req.mode = OffloadMode::OFFLOAD_REQUIRED;
    auto res = e.evaluate(req);
    CHECK(!res.hard_pass);   // UNKNOWN is not permission
}

DPUFABRIC_MAIN
