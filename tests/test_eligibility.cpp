
#include "testharness.h"
#include "dpufabric/engine.h"
#include "dpufabric/synthetic.h"

using namespace dpufabric;
using namespace dpufabric::synth;

static void seed_engine(DPUFabricEngine& eng) {
    for (auto& dd : scenario_ab()) eng.register_device(dd.record);
}

static ServiceDefinition make_enc_svc(ServiceId sid) {
    ServiceDefinition s;
    s.service_id = sid;
    s.service_gen = ServiceGeneration(1);
    s.offload_class = OffloadClass::SECURITY_SERVICE;
    CapabilityRequirement cr; cr.key = cap_encryption(); s.required_caps.push_back(cr);
    s.isolation = IsolationSpec{};
    s.resource = ResourceRequest{ 64u << 20, 1, 0, 1, 1 };
    s.execution_model = ExecutionModel::NATIVE_DATA_PLANE;
    s.statefulness = Statefulness::STATELESS;
    return s;
}

DPUFABRIC_TEST(offload_required_selects_supported_device) {
    DPUFabricEngine eng(CoordinatorEpoch(1));
    seed_engine(eng);
    auto sid = eng.register_service(make_enc_svc(ServiceId(10)));
    OffloadRequest req;
    req.offload_class = OffloadClass::SECURITY_SERVICE;
    req.service = sid;
    req.mode = OffloadMode::OFFLOAD_REQUIRED;
    CapabilityRequirement cr; cr.key = cap_encryption(); req.required_caps.push_back(cr);
    auto res = eng.evaluate(req);
    CHECK(res.hard_pass);
    CHECK(res.execution == ExecutionClass::DPU_OFFLOAD);
    CHECK(res.selected.has_value());
    CHECK(*res.selected == DeviceId(1));
}

DPUFABRIC_TEST(offload_required_selects_second_device) {
    DPUFabricEngine eng(CoordinatorEpoch(1));
    seed_engine(eng);
    OffloadRequest req;
    req.offload_class = OffloadClass::STORAGE_SERVICE;
    req.mode = OffloadMode::OFFLOAD_REQUIRED;
    CapabilityRequirement cr; cr.key = cap_storage(); req.required_caps.push_back(cr);
    auto res = eng.evaluate(req);
    CHECK(res.hard_pass);
    CHECK(*res.selected == DeviceId(2));
}

DPUFABRIC_TEST(offload_required_rejects_when_unsupported) {
    DPUFabricEngine eng(CoordinatorEpoch(1));
    seed_engine(eng);
    OffloadRequest req;
    req.offload_class = OffloadClass::VIRTUAL_SWITCHING;
    req.mode = OffloadMode::OFFLOAD_REQUIRED;
    auto res = eng.evaluate(req);
    CHECK(!res.hard_pass);
    CHECK(res.execution == ExecutionClass::UNSUPPORTED);
    CHECK(!res.selected.has_value());
    bool found = false;
    for (auto r : res.global_rejections) if (r == RejectionReason::OFFLOAD_REQUIRED_UNAVAILABLE) found = true;
    CHECK(found);
}

DPUFABRIC_TEST(offload_preferred_falls_back_explicitly) {
    DPUFabricEngine eng(CoordinatorEpoch(1));
    seed_engine(eng);
    OffloadRequest req;
    req.offload_class = OffloadClass::VIRTUAL_SWITCHING;
    req.mode = OffloadMode::OFFLOAD_PREFERRED;
    req.allow_host_fallback = true;
    auto res = eng.evaluate(req);
    CHECK(!res.hard_pass);
    CHECK(res.fallback_used);
    CHECK(res.execution == ExecutionClass::HOST_FALLBACK);
    CHECK(res.fallback_reason == "DPU_CAPABILITY_UNAVAILABLE");
}

DPUFABRIC_TEST(offload_preferred_no_fallback_unsupported) {
    DPUFabricEngine eng(CoordinatorEpoch(1));
    seed_engine(eng);
    OffloadRequest req;
    req.offload_class = OffloadClass::VIRTUAL_SWITCHING;
    req.mode = OffloadMode::OFFLOAD_PREFERRED;
    req.allow_host_fallback = false;
    auto res = eng.evaluate(req);
    CHECK(!res.fallback_used);
    CHECK(res.execution == ExecutionClass::UNSUPPORTED);
}

DPUFABRIC_TEST(deterministic_equal_result) {
    DPUFabricEngine eng(CoordinatorEpoch(1));
    seed_engine(eng);
    OffloadRequest req;
    req.offload_class = OffloadClass::SECURITY_SERVICE;
    req.mode = OffloadMode::OFFLOAD_REQUIRED;
    CapabilityRequirement cr; cr.key = cap_encryption(); req.required_caps.push_back(cr);
    auto a = eng.evaluate(req);
    auto b = eng.evaluate(req);
    CHECK(a.selected == b.selected);
    CHECK(a.explanation == b.explanation);
}

DPUFABRIC_TEST(warm_deployment_beats_cold) {
    DPUFabricEngine eng(CoordinatorEpoch(1));
    seed_engine(eng);
    auto rec_c = make_dpu(DeviceId(3), "DPU-C", { cap_network(), cap_encryption() },
                          ResourceProfile{ 256u << 20, 8, 2, 16, 16 }, DeviceBootId(0xC3),
                          { OffloadClass::NETWORK_SERVICE, OffloadClass::SECURITY_SERVICE });
    rec_c.locality = LocalityQuality::SAME_FUNCTION;
    eng.register_device(rec_c);

    ServiceDefinition svc;
    svc.service_id = ServiceId(20);
    svc.service_gen = ServiceGeneration(1);
    svc.offload_class = OffloadClass::SECURITY_SERVICE;
    CapabilityRequirement e; e.key = cap_encryption(); svc.required_caps.push_back(e);
    CapabilityRequirement n; n.key = cap_network(); svc.required_caps.push_back(n);
    svc.resource = ResourceRequest{ 64u << 20, 1, 0, 1, 1 };
    auto sid = eng.register_service(svc);

    OffloadRequest deploy_req;
    deploy_req.offload_class = OffloadClass::SECURITY_SERVICE;
    deploy_req.service = sid;
    deploy_req.mode = OffloadMode::OFFLOAD_REQUIRED;
    deploy_req.resource = svc.resource;
    auto elig = eng.evaluate(deploy_req);
    CHECK(elig.selected.has_value());
    auto dep = eng.reserve_deployment(deploy_req, elig);
    eng.stage_deployment(dep);
    eng.load_deployment(dep);
    eng.validate_deployment(dep);
    eng.activate_deployment(dep);
    eng.commit_deployment(dep);

    auto res = eng.evaluate(deploy_req);
    CHECK(res.selected.has_value());
    bool warm_on_result = false;
    for (auto& c : res.candidates) {
        if (c.device == *res.selected) {
            for (auto& fs : c.scores) { if (fs.factor == RankFactor::WARM_SERVICE && fs.normalized > 0.5) warm_on_result = true; }
        }
    }
    CHECK(warm_on_result);
}

DPUFABRIC_TEST(tenant_mismatch_rejected) {
    DPUFabricEngine eng(CoordinatorEpoch(1));
    seed_engine(eng);
    ServiceDefinition svc;
    svc.service_id = ServiceId(30);
    svc.service_gen = ServiceGeneration(1);
    svc.offload_class = OffloadClass::STORAGE_SERVICE;
    CapabilityRequirement cr; cr.key = cap_storage(); svc.required_caps.push_back(cr);
    svc.isolation.level = IsolationRequirement::TENANT_COMPATIBLE_SHARED;
    svc.resource = ResourceRequest{ 64u << 20, 1, 0, 1, 1 };
    auto sid = eng.register_service(svc);
    OffloadRequest req;
    req.offload_class = OffloadClass::STORAGE_SERVICE;
    req.service = sid;
    req.mode = OffloadMode::OFFLOAD_REQUIRED;
    req.tenant = TenantId(999);
    auto res = eng.evaluate(req);
    CHECK(!res.hard_pass);
    bool found_tm = false;
    for (auto& c : res.candidates) for (auto rr : c.rejected) if (rr == RejectionReason::TENANT_MISMATCH) found_tm = true;
    CHECK(found_tm);
}

DPUFABRIC_TEST(firmware_incompatible_rejected) {
    DPUFabricEngine eng(CoordinatorEpoch(1));
    seed_engine(eng);
    ServiceDefinition svc;
    svc.service_id = ServiceId(40);
    svc.service_gen = ServiceGeneration(1);
    svc.offload_class = OffloadClass::SECURITY_SERVICE;
    CapabilityRequirement cr; cr.key = cap_encryption(); svc.required_caps.push_back(cr);
    svc.min_firmware_gen = FirmwareGeneration(100);
    svc.resource = ResourceRequest{ 1, 0, 0, 1, 1 };
    auto sid = eng.register_service(svc);
    OffloadRequest req;
    req.offload_class = OffloadClass::SECURITY_SERVICE;
    req.service = sid;
    req.mode = OffloadMode::OFFLOAD_REQUIRED;
    auto res = eng.evaluate(req);
    CHECK(!res.hard_pass);
    bool found_fw = false;
    for (auto& c : res.candidates) for (auto rr : c.rejected) if (rr == RejectionReason::FIRMWARE_INCOMPATIBLE) found_fw = true;
    CHECK(found_fw);
}

DPUFABRIC_TEST(duplicate_registration_conflict_rejected) {
    DPUFabricEngine eng(CoordinatorEpoch(1));
    seed_engine(eng);
    auto d = make_dpu(DeviceId(1), "DPU-A", {}, ResourceProfile{}, DeviceBootId(0xA1), { OffloadClass::NETWORK_SERVICE });
    d.pci_reference = "0000:99:00.0";
    d.vendor = "something-else";
    CHECK_THROWS_CODE(eng.register_device(d), ErrorCode::ALREADY_EXISTS);
}

DPUFABRIC_MAIN
