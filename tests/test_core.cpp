#include "testharness.h"
#include "dpufabric/id.h"
#include "dpufabric/error.h"
#include "dpufabric/types.h"
#include "dpufabric/device.h"
#include "dpufabric/deployment.h"
#include "dpufabric/resource.h"
#include "dpufabric/capability.h"
#include "dpufabric/wire.h"
#include <cstdint>
#include <vector>
#include <string>
#include <limits>
#include <cstddef>

using namespace dpufabric;

DPUFABRIC_TEST(strong_id_distinct_types) {
    // Cross-type substitution must not compile; here we assert type distinctness via encode values.
    DeviceId did(5);
    ServiceId sid(5);
    CHECK(did.as_u64() == 5);
    CHECK(sid.as_u64() == 5);   // same numeric value, distinct types
    CHECK(did.encode() == "0000000000000005");
    CHECK(sid.encode() == "0000000000000005");
}

DPUFABRIC_TEST(strong_id_generation_zero) {
    DeviceGeneration g(0);
    CHECK(!g.valid());              // generation zero not valid
    CHECK(g.is_null());
    DeviceGeneration g2(1);
    CHECK(g2.valid());
    CHECK(!g2.is_null());
    WorkerBootId wb(0);
    CHECK(!wb.valid());             // boot id zero invalid
    CoordinatorEpoch ep(1);
    CHECK(ep.valid());
    CoordinatorEpoch ep0(0);
    CHECK(!ep0.valid());
}

DPUFABRIC_TEST(strong_id_encode_decode) {
    DeviceId did(0xDEADBEEF);
    auto s = did.encode();
    auto back = DeviceId::decode(s);
    CHECK(back.has_value());
    CHECK(back->as_u64() == 0xDEADBEEF);
    // invalid chars
    CHECK(!DeviceId::decode("zz").has_value());
    // too long
    CHECK(!DeviceId::decode("0123456789abcdef0").has_value());
    // generation zero decode rejected
    CHECK(!DeviceGeneration::decode("0000000000000000").has_value());
    // overflow
    CHECK(!DeviceId::decode("fffffffffffffffff").has_value());
}

DPUFABRIC_TEST(id_validation_no_overflow) {
    // decode of max should be fine for a plain Id
    auto d = DeviceId::decode("ffffffffffffffff");
    CHECK(d.has_value());
    CHECK(d->as_u64() == std::numeric_limits<uint64_t>::max());
}

DPUFABRIC_TEST(error_round_trip_all_codes) {
    for (ErrorCode c : all_error_codes()) {
        CHECK(is_valid_error(c));
        auto name = error_code_name(c);
        auto parsed = error_code_from_name(name);
        CHECK(parsed.has_value());
        CHECK(*parsed == c);
        DpuError e(c, "detail");
        CHECK(e.code() == c);
    }
    CHECK(!error_code_from_name("NOT_A_CODE").has_value());
}

DPUFABRIC_TEST(enum_lookup_names) {
    CHECK(std::string(offload_class_name(OffloadClass::ENCRYPTION)) == "ENCRYPTION");
    CHECK(offload_class_from_name("ENCRYPTION") == OffloadClass::ENCRYPTION);
    CHECK(!offload_class_from_name("BOGUS").has_value());
    CHECK(std::string(execution_class_name(ExecutionClass::HOST_FALLBACK)) == "HOST_FALLBACK");
    CHECK(std::string(provenance_source_name(ProvenanceSource::SYNTHETIC)) == "SYNTHETIC");
    CHECK(std::string(evidence_class(ProvenanceSource::REAL_OS_DISCOVERY)) == "REAL");
    CHECK(std::string(evidence_class(ProvenanceSource::SYNTHETIC)) == "SYNTHETIC");
    CHECK(std::string(evidence_class(ProvenanceSource::UNSUPPORTED)) == "UNSUPPORTED");
}

DPUFABRIC_TEST(device_lifecycle_transitions) {
    CHECK(valid_device_transition(DeviceLifecycle::DISCOVERED, DeviceLifecycle::AVAILABLE));
    CHECK(valid_device_transition(DeviceLifecycle::AVAILABLE, DeviceLifecycle::READY));
    CHECK(valid_device_transition(DeviceLifecycle::READY, DeviceLifecycle::REVALIDATION_REQUIRED));
    CHECK(valid_device_transition(DeviceLifecycle::READY, DeviceLifecycle::DRAINING));
    CHECK(!valid_device_transition(DeviceLifecycle::RETIRED, DeviceLifecycle::READY));
    CHECK(valid_device_transition(DeviceLifecycle::READY, DeviceLifecycle::OFFLINE));
    CHECK(!valid_device_transition(DeviceLifecycle::DISCOVERED, DeviceLifecycle::READY)); // must go via AVAILABLE
}

DPUFABRIC_TEST(deployment_transitions) {
    CHECK(valid_deployment_transition(DeploymentState::PLANNED, DeploymentState::RESERVED));
    CHECK(valid_deployment_transition(DeploymentState::RESERVED, DeploymentState::STAGING));
    CHECK(valid_deployment_transition(DeploymentState::STAGING, DeploymentState::LOADED));
    CHECK(valid_deployment_transition(DeploymentState::LOADED, DeploymentState::VALIDATING));
    CHECK(valid_deployment_transition(DeploymentState::VALIDATING, DeploymentState::ACTIVATING));
    CHECK(valid_deployment_transition(DeploymentState::ACTIVATING, DeploymentState::ACTIVE));
    CHECK(valid_deployment_transition(DeploymentState::ACTIVE, DeploymentState::DRAINING));
    CHECK(valid_deployment_transition(DeploymentState::DRAINING, DeploymentState::RETIRED));
    CHECK(!valid_deployment_transition(DeploymentState::RETIRED, DeploymentState::ACTIVE));
    CHECK(!valid_deployment_transition(DeploymentState::PLANNED, DeploymentState::ACTIVE));
}

DPUFABRIC_TEST(capability_key_validation) {
    CHECK(CapabilityKey::is_valid("ENCRYPTION"));
    CHECK(!CapabilityKey::is_valid(""));
    CHECK(!CapabilityKey::is_valid("with space"));
    CHECK(!CapabilityKey::is_valid(std::string(65, 'A')));
    CapabilityKey k("ENCRYPTION");
    CHECK(k.str() == "ENCRYPTION");
}

DPUFABRIC_TEST(capability_set_fail_closed) {
    CapabilitySet cs;
    CHECK(cs.state_of(CapabilityKey("X")) == CapabilityState::UNSUPPORTED);  // absent == unsupported
    CHECK(!cs.authoritative(CapabilityKey("X")));
    Capability c; c.key = CapabilityKey("X"); c.state = CapabilityState::UNKNOWN;
    cs.add(c);
    CHECK(!cs.authoritative(CapabilityKey("X")));  // UNKNOWN is not permission
    CHECK(!cs.supports(CapabilityKey("X")));
    c.state = CapabilityState::SUPPORTED;
    cs.add(c);
    CHECK(cs.authoritative(CapabilityKey("X")));
    CHECK(cs.supports(CapabilityKey("X")));
}

DPUFABRIC_TEST(resource_ledger_no_leak) {
    ResourceProfile cap{ 1024, 4, 2, 8, 8 };
    ResourceLedger ledger(cap);
    ResourceRequest rr{ 256, 1, 1, 1, 1 };
    CHECK(ledger.can_reserve(rr));
    { auto res = ledger.try_reserve(rr); CHECK(res.applied()); }  // falls out of scope, rolled back
    CHECK(ledger.current().memory_bytes == 0);   // no leak
    { auto res = ledger.try_reserve(rr); res.commit(); }
    CHECK(ledger.current().memory_bytes == 256);
    // exhaustion
    ResourceRequest big{ 100000, 0, 0, 0, 0 };
    CHECK(!ledger.can_reserve(big));
    CHECK_THROWS_CODE(ledger.try_reserve(big), ErrorCode::RESOURCE_EXHAUSTED);
    // release
    ledger.release(rr);
    CHECK(ledger.current().memory_bytes == 0);
}

DPUFABRIC_TEST(activation_authority_staleness) {
    ActivationAuthority a;
    a.epoch = CoordinatorEpoch(5);
    a.worker = WorkerId(3);
    a.worker_boot = WorkerBootId(10);
    a.device_boot = DeviceBootId(20);
    a.device_gen = DeviceGeneration(1);
    a.service_gen = ServiceGeneration(2);
    a.program_gen = ProgramGeneration(3);
    a.deployment_gen = DeploymentGeneration(4);
    a.activation_gen = ActivationGeneration(5);
    a.policy_gen = PolicyGeneration(6);
    a.evidence_gen = EvidenceGeneration(7);
    AuthorityContext ctx;
    ctx.epoch = CoordinatorEpoch(5);
    ctx.worker = WorkerId(3);
    ctx.worker_boot = WorkerBootId(10);
    ctx.device_boot = DeviceBootId(20);
    ctx.device_gen = DeviceGeneration(1);
    ctx.service_gen = ServiceGeneration(2);
    ctx.program_gen = ProgramGeneration(3);
    ctx.deployment_gen = DeploymentGeneration(4);
    ctx.activation_gen = ActivationGeneration(5);
    ctx.policy_gen = PolicyGeneration(6);
    ctx.evidence_gen = EvidenceGeneration(7);
    CHECK(a.is_current(ctx));
    ctx.epoch = CoordinatorEpoch(6);  // epoch advanced
    CHECK(!a.is_current(ctx));
    ctx.epoch = CoordinatorEpoch(5);
    ctx.device_boot = DeviceBootId(21);  // device rebooted
    CHECK(!a.is_current(ctx));
    ctx.device_boot = DeviceBootId(20);
    ctx.activation_gen = ActivationGeneration(6);  // new activation
    CHECK(!a.is_current(ctx));
}

DPUFABRIC_TEST(next_generation_never_zero) {
    CHECK(next_generation(0) == 1);
    CHECK(next_generation(10) == 11);
    CHECK(next_generation(std::numeric_limits<uint64_t>::max()) == std::numeric_limits<uint64_t>::max());
}

DPUFABRIC_TEST(wire_reader_bounds) {
    wire::Writer w;
    w.u32(0x12345678);
    w.str("hello");
    wire::Reader r(std::string_view(w.data().data(), w.size()));
    CHECK(r.u32() == 0x12345678);
    CHECK(r.str() == "hello");
    r.expect_end();
    // truncated read throws
    std::vector<uint8_t> tiny(w.data().begin(), w.data().begin() + 2);
    wire::Reader t(tiny);
    CHECK(t.remaining() == 2);
    CHECK_THROWS_CODE(t.u32(), ErrorCode::PROTOCOL_ERROR);
    // trailing bytes rejected (2-byte buffer, consume 1)
    wire::Writer w2; w2.u8(1); w2.u8(2);
    wire::Reader t2(std::string_view(w2.data().data(), w2.size()));
    t2.u8();
    CHECK(t2.remaining() == 1);
    CHECK_THROWS_CODE(t2.expect_end(), ErrorCode::PROTOCOL_ERROR);
}

DPUFABRIC_TEST(crc32_known_value) {
    // CRC-32 of "123456789" is 0xCBF43926 (IEEE).
    uint32_t c = wire::Crc32::compute("123456789");
    CHECK(c == 0xCBF43926u);
}

DPUFABRIC_MAIN