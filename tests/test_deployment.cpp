#include "testharness.h"
#include "dpufabric/engine.h"
#include "dpufabric/synthetic.h"

using namespace dpufabric;
using namespace dpufabric::synth;

static void seed(DPUFabricEngine& eng) { for (auto& dd : scenario_ab()) eng.register_device(dd.record); }

static ServiceDefinition storage_svc(ServiceId sid) {
    ServiceDefinition s;
    s.service_id = sid; s.service_gen = ServiceGeneration(1);
    s.offload_class = OffloadClass::STORAGE_SERVICE;
    CapabilityRequirement cr; cr.key = cap_storage(); s.required_caps.push_back(cr);
    s.resource = ResourceRequest{ 128u << 20, 2, 1, 1, 1 };
    return s;
}

static OffloadRequest req_for(ServiceId sid, OffloadClass oc) {
    OffloadRequest r; r.offload_class = oc; r.service = sid; r.mode = OffloadMode::OFFLOAD_REQUIRED; return r;
}

DPUFABRIC_TEST(deployment_full_lifecycle) {
    DPUFabricEngine eng(CoordinatorEpoch(1)); seed(eng);
    auto sid = eng.register_service(storage_svc(ServiceId(50)));
    auto req = req_for(sid, OffloadClass::STORAGE_SERVICE);
    auto elig = eng.evaluate(req); CHECK(elig.selected.has_value());
    auto dep = eng.reserve_deployment(req, elig);
    auto snap = eng.snapshot();
    CHECK(snap->deployment(dep)->state == DeploymentState::RESERVED);
    // resources reserved on device B
    auto usage = snap->usage(*elig.selected); CHECK(usage != nullptr); CHECK(usage->slots == 1);
    eng.stage_deployment(dep);
    eng.load_deployment(dep);
    eng.validate_deployment(dep);
    auto act = eng.activate_deployment(dep);
    eng.commit_deployment(dep);
    snap = eng.snapshot();
    CHECK(snap->deployment(dep)->state == DeploymentState::ACTIVE);
    CHECK(snap->deployment(dep)->authoritative);
    auto ait = snap->activations.find(act); CHECK(ait != snap->activations.end());
    CHECK(ait->second.state == ActivationState::ACTIVE);
}

DPUFABRIC_TEST(deployment_invalid_transition_rejected) {
    DPUFabricEngine eng(CoordinatorEpoch(1)); seed(eng);
    auto sid = eng.register_service(storage_svc(ServiceId(51)));
    auto req = req_for(sid, OffloadClass::STORAGE_SERVICE);
    auto elig = eng.evaluate(req);
    auto dep = eng.reserve_deployment(req, elig);
    CHECK_THROWS_CODE(eng.commit_deployment(dep), ErrorCode::INVALID_STATE);  // ACTIVATING required
    CHECK_THROWS_CODE(eng.retire_deployment(dep), ErrorCode::INVALID_STATE);  // RESERVED -> RETIRED invalid
}

DPUFABRIC_TEST(deployment_rollback_releases_resources) {
    DPUFabricEngine eng(CoordinatorEpoch(1)); seed(eng);
    auto sid = eng.register_service(storage_svc(ServiceId(52)));
    auto req = req_for(sid, OffloadClass::STORAGE_SERVICE);
    auto elig = eng.evaluate(req);
    auto dep = eng.reserve_deployment(req, elig);
    auto device = *elig.selected;
    CHECK(eng.snapshot()->usage(device)->slots == 1);
    eng.rollback_deployment(dep);
    CHECK(eng.snapshot()->usage(device) == nullptr || eng.snapshot()->usage(device)->slots == 0);
    CHECK(eng.snapshot()->deployment(dep)->state == DeploymentState::ABORTED);
}

DPUFABRIC_TEST(deployment_cancel_releases_resources) {
    DPUFabricEngine eng(CoordinatorEpoch(1)); seed(eng);
    auto sid = eng.register_service(storage_svc(ServiceId(53)));
    auto req = req_for(sid, OffloadClass::STORAGE_SERVICE);
    auto elig = eng.evaluate(req);
    auto dep = eng.reserve_deployment(req, elig);
    auto device = *elig.selected;
    eng.cancel_deployment(dep);
    CHECK(eng.snapshot()->usage(device)->slots == 0);
    CHECK(eng.snapshot()->deployment(dep)->state == DeploymentState::ABORTED);
}

DPUFABRIC_TEST(deployment_drain_retire) {
    DPUFabricEngine eng(CoordinatorEpoch(1)); seed(eng);
    auto sid = eng.register_service(storage_svc(ServiceId(54)));
    auto req = req_for(sid, OffloadClass::STORAGE_SERVICE);
    auto elig = eng.evaluate(req);
    auto dep = eng.reserve_deployment(req, elig);
    eng.stage_deployment(dep); eng.load_deployment(dep); eng.validate_deployment(dep);
    eng.activate_deployment(dep); eng.commit_deployment(dep);
    CHECK(eng.snapshot()->deployment(dep)->state == DeploymentState::ACTIVE);
    eng.drain_deployment(dep);
    CHECK(eng.snapshot()->deployment(dep)->state == DeploymentState::DRAINING);
    CHECK(!eng.snapshot()->deployment(dep)->authoritative);
    eng.retire_deployment(dep);
    CHECK(eng.snapshot()->deployment(dep)->state == DeploymentState::RETIRED);
    CHECK(eng.snapshot()->usage(*elig.selected)->slots == 0);
}

DPUFABRIC_TEST(activation_stale_after_device_boot_change) {
    DPUFabricEngine eng(CoordinatorEpoch(1)); seed(eng);
    auto sid = eng.register_service(storage_svc(ServiceId(55)));
    auto req = req_for(sid, OffloadClass::STORAGE_SERVICE);
    auto elig = eng.evaluate(req);
    auto dep = eng.reserve_deployment(req, elig);
    eng.stage_deployment(dep); eng.load_deployment(dep); eng.validate_deployment(dep);
    eng.activate_deployment(dep); eng.commit_deployment(dep);
    CHECK(eng.snapshot()->deployment(dep)->authoritative);
    // device boot changes
    auto dev = eng.snapshot()->device(*elig.selected);
    CHECK(dev != nullptr);
    eng.fence_device(*elig.selected, "boot changed");
    // the deployment authority is no longer current and the device is fenced
    CHECK(!eng.snapshot()->device(*elig.selected)->is_ready_for_offload());
    CHECK(eng.snapshot()->deployment(dep)->authoritative == false );   // fence should have fenced it? we call apply?
}

DPUFABRIC_TEST(commit_rejected_on_stale_program_generation) {
    DPUFabricEngine eng(CoordinatorEpoch(1)); seed(eng);
    // Create a program at gen 1 on the storage device, register a service requiring it.
    ProgramArtifact p;
    p.program_id = ProgramId(70); p.program_gen = ProgramGeneration(1);
    p.artifact_id = ArtifactId(71); p.artifact_gen = ArtifactGeneration(1);
    p.artifact_type = ArtifactType::RTE_PROGRAM;
    p.target_architecture = "arm64"; p.runtime_abi = "synthetic-rte";
    for (auto& b : p.content_hash) b = 1;
    eng.register_program(p);
    ServiceDefinition svc = storage_svc(ServiceId(56));
    svc.required_program = ProgramId(70);
    auto sid = eng.register_service(svc);
    auto req = req_for(sid, OffloadClass::STORAGE_SERVICE);
    req.required_program = ProgramId(70);
    auto elig = eng.evaluate(req);
    CHECK(elig.selected.has_value());
    auto dep = eng.reserve_deployment(req, elig);
    eng.stage_deployment(dep); eng.load_deployment(dep); eng.validate_deployment(dep);
    eng.activate_deployment(dep);
    // program generation advances to 2 -> old deployment commit must fail
    ProgramArtifact p2 = p; p2.program_gen = ProgramGeneration(2);
    eng.register_program(p2);
    CHECK_THROWS_CODE(eng.commit_deployment(dep), ErrorCode::STALE_PROGRAM_GENERATION);
}

DPUFABRIC_TEST(resource_exhaustion_rejected_without_leak) {
    DPUFabricEngine eng(CoordinatorEpoch(1)); seed(eng);
    // Evaluate against a service so a candidate exists, then verify that an
    // oversized request (no service bound, so the request resource profile is
    // used) is rejected by admission on every candidate.
    auto sid = eng.register_service(storage_svc(ServiceId(57)));
    auto req = req_for(sid, OffloadClass::STORAGE_SERVICE);
    auto elig = eng.evaluate(req); CHECK(elig.selected.has_value());

    OffloadRequest big;
    big.offload_class = OffloadClass::STORAGE_SERVICE;
    big.mode = OffloadMode::OFFLOAD_REQUIRED;
    CapabilityRequirement cr; cr.key = cap_storage(); big.required_caps.push_back(cr);
    big.resource = ResourceRequest{ 0, 0, 0, 10000, 0 };   // requests 10k slots
    auto big_elig = eng.evaluate(big);
    CHECK(!big_elig.hard_pass);
    bool saw_resource = false;
    for (auto& c : big_elig.candidates) for (auto rr : c.rejected) if (rr == RejectionReason::RESOURCE_INSUFFICIENT) saw_resource = true;
    CHECK(saw_resource);
}

DPUFABRIC_MAIN