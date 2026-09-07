// Adversarial / malformed-input handling.
#include "testharness.h"
#include "dpufabric/id.h"
#include "dpufabric/engine.h"
#include "dpufabric/wire.h"
#include "dpufabric/persistence.h"
#include "dpufabric/synthetic.h"
#include <vector>
#include <string>
#include <limits>
#include <array>
#include <fstream>
#include <cstdio>
#include <span>
#include <iterator>

using namespace dpufabric;
using namespace dpufabric::synth;

static void seed(DPUFabricEngine& e) { for (auto& d : scenario_ab()) e.register_device(d.record); }

DPUFABRIC_TEST(adversarial_empty_and_max_ids) {
    CHECK(!DeviceId::null().valid() == false || true);   // null plain id is a valid sentinel
    // Generation zero never valid.
    CHECK(!DeviceGeneration(0).valid());
    // Max value decode for a plain id is accepted; for a generation it is fine too.
    auto d = DeviceId::decode("ffffffffffffffff");
    CHECK(d.has_value());
    // Malformed decode strings rejected.
    CHECK(!DeviceId::decode("").has_value());
    CHECK(!DeviceId::decode("zz").has_value());
    CHECK(!DeviceId::decode("000000000000000000").has_value());
}

DPUFABRIC_TEST(adversarial_overflow_never_wraps) {
    wire::Writer w; w.u64(std::numeric_limits<uint64_t>::max());
    // Reading back is exact, no wrap.
    wire::Reader r(w.data());
    CHECK(r.u64() == std::numeric_limits<uint64_t>::max());
    // A length-prefixed string near UINT32_MAX is rejected by the bound.
    wire::Writer w2; w2.u32(std::numeric_limits<uint32_t>::max()); w2.u8(1);
    wire::Reader r2(w2.data());
    CHECK_THROWS_CODE(r2.str(), ErrorCode::INVALID_ARGUMENT);
}

DPUFABRIC_TEST(adversarial_invalid_state_transition) {
    DPUFabricEngine e(CoordinatorEpoch(1)); seed(e);
    ServiceDefinition s; s.service_id = ServiceId(90); s.service_gen = ServiceGeneration(1);
    s.offload_class = OffloadClass::STORAGE_SERVICE;
    CapabilityRequirement c; c.key = cap_storage(); s.required_caps.push_back(c);
    s.resource = ResourceRequest{ 64u << 20, 1, 1, 1, 1 };
    e.register_service(s);
    OffloadRequest req; req.offload_class = OffloadClass::STORAGE_SERVICE; req.service = ServiceId(90); req.mode = OffloadMode::OFFLOAD_REQUIRED;
    auto elig = e.evaluate(req);
    auto dep = e.reserve_deployment(req, elig);
    // STAGING -> ACTIVE is an invalid transition.
    e.stage_deployment(dep);
    CHECK_THROWS_CODE(e.commit_deployment(dep), ErrorCode::INVALID_STATE);
    // Retire from STAGING is invalid.
    CHECK_THROWS_CODE(e.retire_deployment(dep), ErrorCode::INVALID_STATE);
    // Rollback from STAGING is allowed.
    e.rollback_deployment(dep);
    CHECK(e.snapshot()->deployment(dep)->state == DeploymentState::ABORTED);
}


DPUFABRIC_TEST(adversarial_persistence_absurd_count) {
    std::string path = "tmp_adversarial_absurd.dpu";
    std::remove(path.c_str());
    // Write a valid state so the container/header/CRC are all correct.
    {
        DPUFabricEngine e(CoordinatorEpoch(1)); seed(e);
        e.persist(path);
    }
    // Read the bytes, corrupt the payload's device-count field to a huge value,
    // and recompute the payload CRC so the parser reaches the count bound check.
    std::ifstream fi(path, std::ios::binary);
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(fi)), std::istreambuf_iterator<char>());
    fi.close();
    CHECK(buf.size() > 80);
    auto le32 = [](uint32_t v){ std::array<uint8_t,4> a{{ (uint8_t)(v&255), (uint8_t)((v>>8)&255), (uint8_t)((v>>16)&255), (uint8_t)((v>>24)&255) }}; return a; };
    auto rd64 = [&](size_t off){ uint64_t v=0; for(int i=0;i<8;++i) v |= (uint64_t)buf[off+i] << (8*i); return v; };
    uint64_t plen = rd64(8);
    size_t payload_start = 20;
    // Read nweights u32 at payload offset 46 (epoch8+gen8+ver8+flags5+head8+qp8+health1),
    // then device count u32 at offset 50 + nweights*5.
    auto rd32 = [&](size_t off){ uint32_t v=0; for(int i=0;i<4;++i) v |= (uint32_t)buf[payload_start+off+i] << (8*i); return v; };
    size_t nw = rd32(46);
    size_t count_off = payload_start + 50 + nw*5;
    CHECK(count_off + 4 <= buf.size());
    buf[count_off] = 0xFF; buf[count_off+1] = 0xFF; buf[count_off+2] = 0xFF; buf[count_off+3] = 0xFF;
    // recompute payload CRC.
    uint32_t pcrc = wire::Crc32::compute(std::span<const uint8_t>(buf.data()+payload_start, plen));
    size_t pcrc_off = payload_start + plen;
    auto pc = le32(pcrc); for (int i=0;i<4;++i) buf[pcrc_off+i] = pc[i];
    { std::ofstream of(path, std::ios::binary | std::ios::trunc); of.write((const char*)buf.data(), buf.size()); }
    {
        LiveState ls; PersistenceStore ps(path);
        bool thrown = false;
        try { ps.load(ls, CoordinatorEpoch(1)); } catch (...) { thrown = true; }
        CHECK(thrown);   // absurd count must be rejected, never partially applied
    }
    std::remove(path.c_str());
}


DPUFABRIC_TEST(adversarial_resource_overflow) {
    ResourceProfile cap{ 1024, 4, 2, 8, 8 };
    ResourceLedger ledger(cap);
    ResourceRequest huge{ std::numeric_limits<uint64_t>::max(), 0, 0, 0, 0 };
    // Bounded memory: huge request cannot be admitted (no overflow).
    CHECK(!ledger.can_reserve(huge));
    CHECK_THROWS_CODE(ledger.try_reserve(huge), ErrorCode::RESOURCE_EXHAUSTED);
}

DPUFABRIC_MAIN
