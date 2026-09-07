#include "testharness.h"
#include "dpufabric/engine.h"
#include "dpufabric/synthetic.h"
#include "dpufabric/persistence.h"
#include <fstream>
#include <vector>
#include <cstdio>
#include <string>

using namespace dpufabric;
using namespace dpufabric::synth;

static std::string path_for(const char* n) { return std::string("test_") + n + ".dpu"; }
static void remove_file(const char* n) { std::remove(path_for(n).c_str()); }

static void seed_for_persist(DPUFabricEngine& eng, ServiceId sid) {
    for (auto& dd : scenario_ab()) eng.register_device(dd.record);
    ServiceDefinition svc;
    svc.service_id = sid; svc.service_gen = ServiceGeneration(1);
    svc.offload_class = OffloadClass::STORAGE_SERVICE;
    CapabilityRequirement cr; cr.key = cap_storage(); svc.required_caps.push_back(cr);
    svc.resource = ResourceRequest{ 128u << 20, 2, 1, 1, 1 };
    eng.register_service(svc);
}

DPUFABRIC_TEST(persistence_roundtrip_conservative_recovery) {
    remove_file("rt");
    {
        DPUFabricEngine eng(CoordinatorEpoch(1));
        auto sid = ServiceId(60);
        seed_for_persist(eng, sid);
        OffloadRequest req;
        req.offload_class = OffloadClass::STORAGE_SERVICE; req.service = sid; req.mode = OffloadMode::OFFLOAD_REQUIRED;
        auto elig = eng.evaluate(req); CHECK(elig.selected.has_value());
        auto dep = eng.reserve_deployment(req, elig);
        eng.stage_deployment(dep); eng.load_deployment(dep); eng.validate_deployment(dep);
        eng.activate_deployment(dep);
        try { eng.commit_deployment(dep); } catch (...) { /* program not pinned; still fine */ }
        eng.persist(path_for("rt"));
        CHECK(std::ifstream(path_for("rt")).good());
    }
    {
        // Fresh coordinator recovers structural state; epoch advances and dynamic evidence is fenced.
        DPUFabricEngine eng(CoordinatorEpoch(5));
        bool existed = eng.recover_from(path_for("rt"));
        CHECK(existed);
        auto snap = eng.snapshot();
        CHECK(snap->epoch.as_u64() > 0);
        CHECK(snap->devices.size() == 2);          // structural devices recovered
        CHECK(snap->services.size() == 1);
        CHECK(snap->deployments.size() >= 1);      // deployment intent recovered
        // Dynamic evidence must NOT be fresh.
        for (auto& [id, d] : snap->devices) CHECK(!d.evidence_fresh);
        // The recovered deployment must be conservatively fenced.
        for (auto& [id, dep] : snap->deployments) {
            CHECK(!dep.authoritative);
            CHECK(dep.state == DeploymentState::REVALIDATION_REQUIRED || dep.state == DeploymentState::RETIRED);
        }
    }
}

DPUFABRIC_TEST(persistence_corrupt_checksum_rejected) {
    remove_file("corrupt");
    {
        DPUFabricEngine eng(CoordinatorEpoch(1));
        seed_for_persist(eng, ServiceId(61));
        eng.persist(path_for("corrupt"));
    }
    // Read, corrupt a payload byte, rewrite.
    std::fstream f(path_for("corrupt"), std::ios::in | std::ios::binary);
    std::vector<char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); f.close();
    CHECK(buf.size() > 30);
    buf[25] = static_cast<char>(buf[25] ^ 0xFF);   // corrupt payload
    std::ofstream of(path_for("corrupt"), std::ios::binary | std::ios::trunc);
    of.write(buf.data(), static_cast<std::streamsize>(buf.size())); of.close();
    {
        DPUFabricEngine eng(CoordinatorEpoch(1));
        LiveState ls; PersistenceStore ps(path_for("corrupt"));
        CHECK_THROWS_CODE(ps.load(ls, CoordinatorEpoch(1)), ErrorCode::CHECKSUM_FAILURE);
    }
}

DPUFABRIC_TEST(persistence_truncated_rejected) {
    remove_file("trunc");
    {
        DPUFabricEngine eng(CoordinatorEpoch(1));
        seed_for_persist(eng, ServiceId(62));
        eng.persist(path_for("trunc"));
    }
    std::fstream f(path_for("trunc"), std::ios::in | std::ios::binary);
    std::vector<char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); f.close();
    buf.resize(buf.size() - 3);   // truncate tail
    std::ofstream of(path_for("trunc"), std::ios::binary | std::ios::trunc);
    of.write(buf.data(), static_cast<std::streamsize>(buf.size())); of.close();
    {
        DPUFabricEngine eng(CoordinatorEpoch(1));
        LiveState ls; PersistenceStore ps(path_for("trunc"));
        CHECK_THROWS_CODE(ps.load(ls, CoordinatorEpoch(1)), ErrorCode::CORRUPT_STATE);
    }
}

DPUFABRIC_TEST(persistence_bad_magic_rejected) {
    remove_file("magic");
    {
        DPUFabricEngine eng(CoordinatorEpoch(1)); seed_for_persist(eng, ServiceId(63));
        eng.persist(path_for("magic"));
    }
    std::fstream f(path_for("magic"), std::ios::in | std::ios::binary);
    std::vector<char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); f.close();
    if (buf.size() >= 4) { buf[0] = 0x00; buf[1] = 0x00; buf[2] = 0x00; buf[3] = 0x00; }
    std::ofstream of(path_for("magic"), std::ios::binary | std::ios::trunc);
    of.write(buf.data(), static_cast<std::streamsize>(buf.size())); of.close();
    {
        DPUFabricEngine eng(CoordinatorEpoch(1));
        LiveState ls; PersistenceStore ps(path_for("magic"));
        CHECK_THROWS_CODE(ps.load(ls, CoordinatorEpoch(1)), ErrorCode::CORRUPT_STATE);
    }
}

DPUFABRIC_TEST(persistence_trailing_garbage_rejected) {
    remove_file("trail");
    {
        DPUFabricEngine eng(CoordinatorEpoch(1)); seed_for_persist(eng, ServiceId(64));
        eng.persist(path_for("trail"));
    }
    std::fstream f(path_for("trail"), std::ios::in | std::ios::binary);
    std::vector<char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); f.close();
    buf.push_back(static_cast<char>(0xEE));   // one trailing byte
    std::ofstream of(path_for("trail"), std::ios::binary | std::ios::trunc);
    of.write(buf.data(), static_cast<std::streamsize>(buf.size())); of.close();
    {
        DPUFabricEngine eng(CoordinatorEpoch(1));
        LiveState ls; PersistenceStore ps(path_for("trail"));
        CHECK_THROWS_CODE(ps.load(ls, CoordinatorEpoch(1)), ErrorCode::CORRUPT_STATE);
    }
}

DPUFABRIC_TEST(persistence_fresh_when_missing) {
    remove_file("missing");
    {
        DPUFabricEngine eng(CoordinatorEpoch(1));
        LiveState ls; PersistenceStore ps(path_for("missing"));
        CHECK(!ps.load(ls, CoordinatorEpoch(1)));   // fresh coordinator
    }
}

DPUFABRIC_MAIN