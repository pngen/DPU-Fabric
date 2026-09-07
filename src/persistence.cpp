
#include "dpufabric/persistence.h"
#include "dpufabric/wire.h"
#include "dpufabric/device.h"
#include "dpufabric/program.h"
#include <fstream>
#include <cstdio>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <functional>
#include <chrono>

namespace dpufabric {

namespace {

constexpr uint32_t kHeaderCrcLen = 16; // magic+version+reserved+payload_len
constexpr size_t kFixedHead = 20;      // magic(4)+version(2)+reserved(2)+len(8)+header_crc(4)
constexpr size_t kTrailerFixed = 4;    // payload crc
constexpr uint16_t kReserved = 0;

using wire::Writer;
using wire::Reader;

// ---- enum validation helpers (all enums are uint8 on disk) ----
uint8_t read_enum(Reader& r) { return r.u8(); }

bool is_valid_lifecycle(uint8_t v) { return v >= 1 && v <= 10; }
bool is_valid_deploy_state(uint8_t v) { return v >= 1 && v <= 13; }
bool is_valid_cap_state(uint8_t v) { return v >= 1 && v <= 4; }
bool is_valid_provenance(uint8_t v) { return v >= 1 && v <= 7; }
bool is_valid_device_class(uint8_t v) { return v >= 1 && v <= 6; }
bool is_valid_offload_class(uint8_t v) { return v >= 1 && v <= 16; }
bool is_valid_health(uint8_t v) { return v >= 1 && v <= 5; }
bool is_valid_exec_model(uint8_t v) { return v >= 1 && v <= 4; }
bool is_valid_isolation(uint8_t v) { return v >= 1 && v <= 9; }
bool is_valid_security(uint8_t v) { return v >= 1 && v <= 4; }
bool is_valid_statefulness(uint8_t v) { return v >= 1 && v <= 2; }
bool is_valid_artifact_type(uint8_t v) { return v >= 1 && v <= 6; }
bool is_valid_locality(uint8_t v) { return v >= 1 && v <= 6; }
bool is_valid_execution_model(uint8_t v) { return v >= 1 && v <= 4; }
bool is_valid_latency(uint8_t v) { return v >= 1 && v <= 5; }
bool is_valid_exec_class(uint8_t v) { return v >= 1 && v <= 5; }

inline void write_id(Writer& w, uint64_t id) { w.u64(id); }
inline uint64_t read_id(Reader& r) { return r.u64(); }
template <typename IdT> IdT rd_id(Reader& r) { return IdT(read_id(r)); }

void write_string(Writer& w, const std::string& s) { w.str(s); }
std::string read_string(Reader& r) { return r.str(); }

void write_sha(Writer& w, const Sha256& a) { for (auto b : a) w.u8(b); }
Sha256 read_sha(Reader& r) {
    Sha256 a;
    for (auto& b : a) b = r.u8();
    return a;
}

void write_cap(Writer& w, const Capability& c) {
    w.str(c.key.str());
    w.u8(static_cast<uint8_t>(c.state));
    write_id(w, c.evidence_gen.as_u64());
    w.u64(c.version);
    write_id(w, c.min_firmware_gen.as_u64());
    write_id(w, c.min_runtime_gen.as_u64());
    w.u64(c.max_scale);
    w.u8(c.requires_isolation ? 1 : 0);
    w.u8(static_cast<uint8_t>(c.source));
    w.str(c.detail);
}
Capability read_cap(Reader& r) {
    Capability c;
    c.key = CapabilityKey(read_string(r));
    c.state = static_cast<CapabilityState>(read_enum(r));
    if (!is_valid_cap_state(static_cast<uint8_t>(c.state))) throw_error(ErrorCode::CORRUPT_STATE, "bad capability state");
    c.evidence_gen = rd_id<EvidenceGeneration>(r);
    c.version = r.u64();
    c.min_firmware_gen = rd_id<FirmwareGeneration>(r);
    c.min_runtime_gen = rd_id<RuntimeGeneration>(r);
    c.max_scale = r.u64();
    c.requires_isolation = r.u8() != 0;
    c.source = static_cast<ProvenanceSource>(read_enum(r));
    c.detail = read_string(r);
    return c;
}

void write_capreq(Writer& w, const CapabilityRequirement& cr) {
    w.str(cr.key.str());
    w.u8(cr.optional ? 1 : 0);
    w.u64(cr.min_version);
}
CapabilityRequirement read_capreq(Reader& r) {
    CapabilityRequirement cr;
    cr.key = CapabilityKey(read_string(r));
    cr.optional = r.u8() != 0;
    cr.min_version = r.u64();
    return cr;
}

void write_resource_req(Writer& w, const ResourceRequest& rr) {
    w.u64(rr.memory_bytes); w.u64(rr.queues); w.u64(rr.functions); w.u64(rr.slots); w.u64(rr.contexts);
}
ResourceRequest read_resource_req(Reader& r) {
    ResourceRequest rr;
    rr.memory_bytes = r.u64(); rr.queues = r.u64(); rr.functions = r.u64(); rr.slots = r.u64(); rr.contexts = r.u64();
    return rr;
}

void write_resource_cap(Writer& w, const ResourceProfile& p) {
    w.u64(p.memory_bytes); w.u64(p.queues); w.u64(p.functions); w.u64(p.slots); w.u64(p.contexts);
}
ResourceProfile read_resource_cap(Reader& r) {
    ResourceProfile p;
    p.memory_bytes = r.u64(); p.queues = r.u64(); p.functions = r.u64(); p.slots = r.u64(); p.contexts = r.u64();
    return p;
}

void write_isolation(Writer& w, const IsolationSpec& iso) {
    w.u8(static_cast<uint8_t>(iso.level));
    write_id(w, iso.isolation_domain.as_u64());
    write_id(w, iso.tenant.as_u64());
    w.str(iso.trust_domain);
    w.str(iso.security_domain);
    w.u8(static_cast<uint8_t>(iso.security));
}
IsolationSpec read_isolation(Reader& r) {
    IsolationSpec iso;
    iso.level = static_cast<IsolationRequirement>(read_enum(r));
    iso.isolation_domain = rd_id<IsolationDomainId>(r);
    iso.tenant = rd_id<TenantId>(r);
    iso.trust_domain = read_string(r);
    iso.security_domain = read_string(r);
    iso.security = static_cast<SecurityMode>(read_enum(r));
    return iso;
}

void write_device(Writer& w, const DeviceRecord& d) {
    write_id(w, d.device_id.as_u64());
    write_id(w, d.generation.as_u64());
    write_id(w, d.boot_id.as_u64());
    w.str(d.vendor); w.str(d.model);
    w.u8(static_cast<uint8_t>(d.device_class));
    w.str(d.pci_reference); w.str(d.parent_nic_reference);
    write_id(w, d.parent_function.as_u64());
    w.str(d.firmware_version); write_id(w, d.firmware_gen.as_u64());
    w.str(d.runtime_version); write_id(w, d.runtime_gen.as_u64());
    w.str(d.architecture);
    w.u64(d.processing_units); w.u64(d.memory_bytes); w.u64(d.available_programmable_memory);
    write_resource_cap(w, d.resource_capacity);
    w.u32(static_cast<uint32_t>(d.ports.size()));
    for (auto p : d.ports) write_id(w, p.as_u64());
    w.u32(static_cast<uint32_t>(d.functions.size()));
    for (auto f : d.functions) write_id(w, f.as_u64());
    w.u32(static_cast<uint32_t>(d.capabilities.size()));
    for (auto& [k, c] : d.capabilities.all()) (void)k, write_cap(w, c);
    w.u32(static_cast<uint32_t>(d.offload_classes.size()));
    for (auto c : d.offload_classes) w.u8(static_cast<uint8_t>(c));
    w.u32(static_cast<uint32_t>(d.execution_models.size()));
    for (auto m : d.execution_models) w.u8(static_cast<uint8_t>(m));
    w.u32(static_cast<uint32_t>(d.program_formats.size()));
    for (auto& s : d.program_formats) w.str(s);
    w.u8(d.supports_dma ? 1 : 0);
    w.u8(d.rdma_adjacent ? 1 : 0);
    w.str(d.host_comm);
    w.u8(d.hardware_isolation ? 1 : 0);
    write_id(w, d.isolation_domain.as_u64());
    write_id(w, d.tenant.as_u64());
    w.u8(d.isolation_proven ? 1 : 0);
    w.u8(static_cast<uint8_t>(d.locality));
    w.u8(static_cast<uint8_t>(d.health));
    w.u8(static_cast<uint8_t>(d.lifecycle));
    w.u8(static_cast<uint8_t>(d.discovery_source));
    write_id(w, d.evidence_gen.as_u64());
    w.u8(d.evidence_fresh ? 1 : 0);
    w.str(d.discovery_detail);
    write_id(w, d.owning_worker.as_u64());
    write_id(w, d.owner_worker_boot.as_u64());
}

void read_device(Reader& r, DeviceRecord& d) {
    d.device_id = rd_id<DeviceId>(r);
    d.generation = rd_id<DeviceGeneration>(r);
    d.boot_id = rd_id<DeviceBootId>(r);
    d.vendor = read_string(r); d.model = read_string(r);
    d.device_class = static_cast<DeviceClass>(read_enum(r));
    if (!is_valid_device_class(static_cast<uint8_t>(d.device_class))) throw_error(ErrorCode::CORRUPT_STATE, "bad device class");
    d.pci_reference = read_string(r); d.parent_nic_reference = read_string(r);
    d.parent_function = rd_id<FunctionId>(r);
    d.firmware_version = read_string(r); d.firmware_gen = rd_id<FirmwareGeneration>(r);
    d.runtime_version = read_string(r); d.runtime_gen = rd_id<RuntimeGeneration>(r);
    d.architecture = read_string(r);
    d.processing_units = r.u64(); d.memory_bytes = r.u64(); d.available_programmable_memory = r.u64();
    d.resource_capacity = read_resource_cap(r);
    uint32_t nports = r.u32(); if (nports > wire::kMaxContainerCount) throw_error(ErrorCode::CORRUPT_STATE, "port count out of bounds");
    for (uint32_t i = 0; i < nports; ++i) d.ports.push_back(rd_id<PortId>(r));
    uint32_t nfun = r.u32(); if (nfun > wire::kMaxContainerCount) throw_error(ErrorCode::CORRUPT_STATE, "function count out of bounds");
    for (uint32_t i = 0; i < nfun; ++i) d.functions.push_back(rd_id<FunctionId>(r));
    uint32_t ncap = r.u32(); if (ncap > wire::kMaxContainerCount) throw_error(ErrorCode::CORRUPT_STATE, "capability count out of bounds");
    for (uint32_t i = 0; i < ncap; ++i) d.capabilities.add(read_cap(r));
    uint32_t nclass = r.u32(); if (nclass > 64) throw_error(ErrorCode::CORRUPT_STATE, "offload class count out of bounds");
    for (uint32_t i = 0; i < nclass; ++i) d.offload_classes.push_back(static_cast<OffloadClass>(read_enum(r)));
    uint32_t nem = r.u32(); if (nem > 16) throw_error(ErrorCode::CORRUPT_STATE, "execution model count out of bounds");
    for (uint32_t i = 0; i < nem; ++i) d.execution_models.push_back(static_cast<ExecutionModel>(read_enum(r)));
    uint32_t npf = r.u32(); if (npf > wire::kMaxContainerCount) throw_error(ErrorCode::CORRUPT_STATE, "program format count out of bounds");
    for (uint32_t i = 0; i < npf; ++i) d.program_formats.push_back(read_string(r));
    d.supports_dma = r.u8() != 0;
    d.rdma_adjacent = r.u8() != 0;
    d.host_comm = read_string(r);
    d.hardware_isolation = r.u8() != 0;
    d.isolation_domain = rd_id<IsolationDomainId>(r);
    d.tenant = rd_id<TenantId>(r);
    d.isolation_proven = r.u8() != 0;
    d.locality = static_cast<LocalityQuality>(read_enum(r));
    d.health = static_cast<DeviceHealth>(read_enum(r));
    if (!is_valid_health(static_cast<uint8_t>(d.health))) throw_error(ErrorCode::CORRUPT_STATE, "bad device health");
    d.lifecycle = static_cast<DeviceLifecycle>(read_enum(r));
    if (!is_valid_lifecycle(static_cast<uint8_t>(d.lifecycle))) throw_error(ErrorCode::CORRUPT_STATE, "bad device lifecycle");
    d.discovery_source = static_cast<ProvenanceSource>(read_enum(r));
    d.evidence_gen = rd_id<EvidenceGeneration>(r);
    d.evidence_fresh = r.u8() != 0;
    d.discovery_detail = read_string(r);
    d.owning_worker = rd_id<WorkerId>(r);
    d.owner_worker_boot = rd_id<WorkerBootId>(r);
}

void write_service(Writer& w, const ServiceDefinition& s) {
    write_id(w, s.service_id.as_u64());
    write_id(w, s.service_gen.as_u64());
    w.u8(static_cast<uint8_t>(s.offload_class));
    w.u32(static_cast<uint32_t>(s.required_caps.size()));
    for (auto& c : s.required_caps) write_capreq(w, c);
    w.u32(static_cast<uint32_t>(s.optional_caps.size()));
    for (auto& c : s.optional_caps) write_capreq(w, c);
    write_isolation(w, s.isolation);
    write_resource_req(w, s.resource);
    w.u8(static_cast<uint8_t>(s.execution_model));
    write_id(w, s.min_firmware_gen.as_u64());
    write_id(w, s.min_runtime_gen.as_u64());
    write_id(w, s.required_program.as_u64());
    w.str(s.required_abi);
    w.u8(static_cast<uint8_t>(s.statefulness));
    w.u8(s.recoverable ? 1 : 0);
    w.u32(static_cast<uint32_t>(s.allowed_fallback_classes.size()));
    for (auto c : s.allowed_fallback_classes) w.u8(static_cast<uint8_t>(c));
    w.str(s.failure_policy);
    w.u32(static_cast<uint32_t>(s.dependencies.size()));
    for (auto d : s.dependencies) write_id(w, d.as_u64());
}
ServiceDefinition read_service(Reader& r) {
    ServiceDefinition s;
    s.service_id = rd_id<ServiceId>(r);
    s.service_gen = rd_id<ServiceGeneration>(r);
    s.offload_class = static_cast<OffloadClass>(read_enum(r));
    if (!is_valid_offload_class(static_cast<uint8_t>(s.offload_class))) throw_error(ErrorCode::CORRUPT_STATE, "bad offload class");
    uint32_t n = r.u32(); if (n > wire::kMaxContainerCount) throw_error(ErrorCode::CORRUPT_STATE, "required cap count out of bounds");
    for (uint32_t i = 0; i < n; ++i) s.required_caps.push_back(read_capreq(r));
    n = r.u32(); if (n > wire::kMaxContainerCount) throw_error(ErrorCode::CORRUPT_STATE, "optional cap count out of bounds");
    for (uint32_t i = 0; i < n; ++i) s.optional_caps.push_back(read_capreq(r));
    s.isolation = read_isolation(r);
    s.resource = read_resource_req(r);
    s.execution_model = static_cast<ExecutionModel>(read_enum(r));
    s.min_firmware_gen = rd_id<FirmwareGeneration>(r);
    s.min_runtime_gen = rd_id<RuntimeGeneration>(r);
    s.required_program = rd_id<ProgramId>(r);
    s.required_abi = read_string(r);
    s.statefulness = static_cast<Statefulness>(read_enum(r));
    s.recoverable = r.u8() != 0;
    n = r.u32(); if (n > 32) throw_error(ErrorCode::CORRUPT_STATE, "fallback class count out of bounds");
    for (uint32_t i = 0; i < n; ++i) s.allowed_fallback_classes.push_back(static_cast<OffloadClass>(read_enum(r)));
    s.failure_policy = read_string(r);
    n = r.u32(); if (n > wire::kMaxContainerCount) throw_error(ErrorCode::CORRUPT_STATE, "dependency count out of bounds");
    for (uint32_t i = 0; i < n; ++i) s.dependencies.push_back(rd_id<ServiceId>(r));
    return s;
}

void write_program(Writer& w, const ProgramArtifact& p) {
    write_id(w, p.program_id.as_u64());
    write_id(w, p.program_gen.as_u64());
    write_id(w, p.artifact_id.as_u64());
    write_id(w, p.artifact_gen.as_u64());
    w.u8(static_cast<uint8_t>(p.artifact_type));
    w.str(p.version);
    write_sha(w, p.content_hash);
    w.str(p.target_architecture);
    w.str(p.runtime_abi);
    w.u32(static_cast<uint32_t>(p.required_caps.size()));
    for (auto& c : p.required_caps) write_capreq(w, c);
    write_id(w, p.min_firmware_gen.as_u64());
    w.str(p.provenance);
    w.str(p.size_hint);
}
ProgramArtifact read_program(Reader& r) {
    ProgramArtifact p;
    p.program_id = rd_id<ProgramId>(r);
    p.program_gen = rd_id<ProgramGeneration>(r);
    p.artifact_id = rd_id<ArtifactId>(r);
    p.artifact_gen = rd_id<ArtifactGeneration>(r);
    p.artifact_type = static_cast<ArtifactType>(read_enum(r));
    p.version = read_string(r);
    p.content_hash = read_sha(r);
    p.target_architecture = read_string(r);
    p.runtime_abi = read_string(r);
    uint32_t n = r.u32(); if (n > wire::kMaxContainerCount) throw_error(ErrorCode::CORRUPT_STATE, "program cap count out of bounds");
    for (uint32_t i = 0; i < n; ++i) p.required_caps.push_back(read_capreq(r));
    p.min_firmware_gen = rd_id<FirmwareGeneration>(r);
    p.provenance = read_string(r);
    p.size_hint = read_string(r);
    return p;
}

void write_deployment(Writer& w, const Deployment& d) {
    write_id(w, d.deployment_id.as_u64());
    write_id(w, d.generation.as_u64());
    write_id(w, d.attempt_id.as_u64());
    write_id(w, d.service.as_u64());
    write_id(w, d.service_gen.as_u64());
    write_id(w, d.program.as_u64());
    write_id(w, d.program_gen.as_u64());
    write_id(w, d.artifact.as_u64());
    write_id(w, d.artifact_gen.as_u64());
    write_id(w, d.device.as_u64());
    write_id(w, d.device_gen.as_u64());
    write_id(w, d.device_boot.as_u64());
    write_id(w, d.tenant.as_u64());
    write_id(w, d.isolation_domain.as_u64());
    write_resource_req(w, d.resources);
    w.u8(static_cast<uint8_t>(d.state));
    w.u8(d.activation.has_value() ? 1 : 0);
    if (d.activation) write_id(w, d.activation->as_u64());
    write_id(w, d.activation_gen.as_u64());
    write_id(w, d.epoch.as_u64());
    write_id(w, d.worker_boot.as_u64());
    write_id(w, d.evidence_gen.as_u64());
    write_id(w, d.policy_gen.as_u64());
    w.u8(d.authoritative ? 1 : 0);
    w.u64(d.created_ms);
    w.u64(d.updated_ms);
}
Deployment read_deployment(Reader& r) {
    Deployment d;
    d.deployment_id = rd_id<DeploymentId>(r);
    d.generation = rd_id<DeploymentGeneration>(r);
    d.attempt_id = rd_id<DeploymentAttemptId>(r);
    d.service = rd_id<ServiceId>(r);
    d.service_gen = rd_id<ServiceGeneration>(r);
    d.program = rd_id<ProgramId>(r);
    d.program_gen = rd_id<ProgramGeneration>(r);
    d.artifact = rd_id<ArtifactId>(r);
    d.artifact_gen = rd_id<ArtifactGeneration>(r);
    d.device = rd_id<DeviceId>(r);
    d.device_gen = rd_id<DeviceGeneration>(r);
    d.device_boot = rd_id<DeviceBootId>(r);
    d.tenant = rd_id<TenantId>(r);
    d.isolation_domain = rd_id<IsolationDomainId>(r);
    d.resources = read_resource_req(r);
    d.state = static_cast<DeploymentState>(read_enum(r));
    if (!is_valid_deploy_state(static_cast<uint8_t>(d.state))) throw_error(ErrorCode::CORRUPT_STATE, "bad deployment state");
    d.activation = r.u8() != 0 ? std::optional<ActivationId>(rd_id<ActivationId>(r)) : std::nullopt;
    d.activation_gen = rd_id<ActivationGeneration>(r);
    d.epoch = rd_id<CoordinatorEpoch>(r);
    d.worker_boot = rd_id<WorkerBootId>(r);
    d.evidence_gen = rd_id<EvidenceGeneration>(r);
    d.policy_gen = rd_id<PolicyGeneration>(r);
    d.authoritative = r.u8() != 0;
    d.created_ms = r.u64();
    d.updated_ms = r.u64();
    return d;
}

} // namespace

void PersistenceStore::save(const Snapshot& s) {
    Writer w;
    w.u64(s.epoch.as_u64());
    w.u64(s.policy.generation.as_u64());
    w.u64(s.policy.version);
    w.u8(s.policy.allow_host_fallback ? 1 : 0);
    w.u8(s.policy.require_authoritative_evidence ? 1 : 0);
    w.u8(s.policy.prefer_warm_deployments ? 1 : 0);
    w.u8(s.policy.prefer_dedicated_isolation ? 1 : 0);
    w.u8(s.policy.require_offload_for_required ? 1 : 0);
    w.u64(s.policy.min_headroom);
    w.u64(s.policy.max_queue_pressure);
    w.u8(static_cast<uint8_t>(s.policy.min_device_health));
    w.u32(static_cast<uint32_t>(s.policy.weights.size()));
    for (auto& wt : s.policy.weights) {
        w.u8(static_cast<uint8_t>(wt.factor));
        w.i32(static_cast<int32_t>(wt.weight));
    }

    w.u32(static_cast<uint32_t>(s.devices.size()));
    // deterministic order
    for (auto id : s.device_order) {
        auto it = s.devices.find(id);
        if (it == s.devices.end()) continue;
        write_device(w, it->second);
    }
    w.u32(static_cast<uint32_t>(s.services.size()));
    for (auto& [k, v] : s.services) write_service(w, v);
    w.u32(static_cast<uint32_t>(s.programs.size()));
    for (auto& [pid, gens] : s.programs) {
        write_id(w, pid.as_u64());
        w.u32(static_cast<uint32_t>(gens.size()));
        for (auto& [g, art] : gens) {
            write_id(w, g.as_u64());
            write_program(w, art);
            bool cur = (!s.current_program_gen.count(pid)) ? false : (s.current_program_gen.at(pid) == g);
            w.u8(cur ? 1 : 0);
        }
    }
    w.u32(static_cast<uint32_t>(s.deployments.size()));
    for (auto& [k, v] : s.deployments) write_deployment(w, v);
    w.u32(static_cast<uint32_t>(s.device_usage.size()));
    for (auto& [did, u] : s.device_usage) {
        write_id(w, did.as_u64());
        w.u64(u.memory_bytes); w.u64(u.queues); w.u64(u.functions); w.u64(u.slots); w.u64(u.contexts);
    }

    std::vector<uint8_t> payload(w.data().begin(), w.data().end());

    // Build container.
    std::vector<uint8_t> file;
    file.reserve(kFixedHead + payload.size() + kTrailerFixed);
    auto le32 = [](uint32_t v) { std::array<uint8_t,4> a{ static_cast<uint8_t>(v&0xFF), static_cast<uint8_t>((v>>8)&0xFF), static_cast<uint8_t>((v>>16)&0xFF), static_cast<uint8_t>((v>>24)&0xFF) }; return a; };
    auto le16 = [](uint16_t v) { std::array<uint8_t,2> a{ static_cast<uint8_t>(v&0xFF), static_cast<uint8_t>((v>>8)&0xFF) }; return a; };
    auto le64 = [](uint64_t v) { std::array<uint8_t,8> a{}; for (int i=0;i<8;++i) a[i]=static_cast<uint8_t>((v>>(8*i))&0xFF); return a; };
    for (auto b : le32(kMagic)) file.push_back(b);
    for (auto b : le16(kFormatVersion)) file.push_back(b);
    for (auto b : le16(kReserved)) file.push_back(b);
    for (auto b : le64(payload.size())) file.push_back(b);
    uint32_t header_crc = wire::Crc32::compute(std::span<const uint8_t>(file.data(), kHeaderCrcLen));
    for (auto b : le32(header_crc)) file.push_back(b);
    file.insert(file.end(), payload.begin(), payload.end());
    uint32_t payload_crc = wire::Crc32::compute(std::span<const uint8_t>(payload));
    for (auto b : le32(payload_crc)) file.push_back(b);

    // Atomic replace: temp + flush/close + rename.
    std::string tmp = path_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw_error(ErrorCode::IO_ERROR, "cannot open temp state file");
        out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
        out.flush();
        if (!out) throw_error(ErrorCode::IO_ERROR, "cannot write temp state file");
    }
    // Remove destination if present so rename is atomic on Windows semantics.
    std::remove(path_.c_str());
    if (std::rename(tmp.c_str(), path_.c_str()) != 0) {
        std::remove(tmp.c_str());
        throw_error(ErrorCode::IO_ERROR, "cannot atomically replace state file");
    }
}

bool PersistenceStore::load(LiveState& out, CoordinatorEpoch) {
    std::ifstream in(path_, std::ios::binary);
    if (!in) return false; // fresh coordinator

    in.seekg(0, std::ios::end);
    std::streamoff end = in.tellg();
    in.seekg(0, std::ios::beg);
    if (end < 0) throw_error(ErrorCode::IO_ERROR, "cannot stat state file");
    uint64_t filesz = static_cast<uint64_t>(end);
    if (filesz == 0) throw_error(ErrorCode::CORRUPT_STATE, "empty state file");
    if (filesz > kMaxFileBytes) throw_error(ErrorCode::CORRUPT_STATE, "state file exceeds bound");

    std::vector<uint8_t> buf(filesz);
    in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(filesz));
    if (!in && !in.eof()) throw_error(ErrorCode::IO_ERROR, "cannot read state file");

    if (buf.size() < kFixedHead + kTrailerFixed) throw_error(ErrorCode::CORRUPT_STATE, "state file too small");
    Reader h(buf);
    uint32_t magic = h.u32();
    if (magic != kMagic) throw_error(ErrorCode::CORRUPT_STATE, "bad state magic");
    uint16_t ver = h.u16();
    if (ver != kFormatVersion) throw_error(ErrorCode::CORRUPT_STATE, "unsupported state version");
    uint16_t reserved = h.u16();
    if (reserved != kReserved) throw_error(ErrorCode::CORRUPT_STATE, "bad reserved field");
    uint64_t payload_len = h.u64();
    if (payload_len > kMaxFileBytes) throw_error(ErrorCode::CORRUPT_STATE, "payload length exceeds bound");
    uint32_t header_crc = h.u32();
    if (wire::Crc32::compute(std::span<const uint8_t>(buf.data(), kHeaderCrcLen)) != header_crc)
        throw_error(ErrorCode::CHECKSUM_FAILURE, "state header checksum mismatch");
    // Exact size check: no trailing garbage, exactly one frame.
    if (buf.size() != kFixedHead + payload_len + kTrailerFixed)
        throw_error(ErrorCode::CORRUPT_STATE, "state file has trailing or missing bytes");
    std::span<const uint8_t> payload = std::span<const uint8_t>(buf.data() + kFixedHead, payload_len);
    uint32_t payload_crc = 0;
    {
        Reader tail(std::span<const uint8_t>(buf.data() + kFixedHead + payload_len, kTrailerFixed));
        payload_crc = tail.u32();
    }
    if (wire::Crc32::compute(payload) != payload_crc)
        throw_error(ErrorCode::CHECKSUM_FAILURE, "state payload checksum mismatch");

    Reader r(payload);
    uint64_t persisted_epoch = r.u64();
    out.epoch = CoordinatorEpoch(std::max(persisted_epoch, (uint64_t)1));
    out.policy.generation = PolicyGeneration(r.u64());
    out.policy.version = r.u64();
    out.policy.allow_host_fallback = r.u8() != 0;
    out.policy.require_authoritative_evidence = r.u8() != 0;
    out.policy.prefer_warm_deployments = r.u8() != 0;
    out.policy.prefer_dedicated_isolation = r.u8() != 0;
    out.policy.require_offload_for_required = r.u8() != 0;
    out.policy.min_headroom = r.u64();
    out.policy.max_queue_pressure = r.u64();
    out.policy.min_device_health = static_cast<DeviceHealth>(read_enum(r));
    uint32_t nweights = r.u32(); if (nweights > 64) throw_error(ErrorCode::CORRUPT_STATE, "policy weight count out of bounds");
    for (uint32_t i = 0; i < nweights; ++i) {
        RankFactorWeight wt;
        wt.factor = static_cast<RankFactor>(read_enum(r));
        wt.weight = r.i32();
        out.policy.weights.push_back(wt);
    }

    uint32_t ndev = r.u32(); if (ndev > wire::kMaxContainerCount) throw_error(ErrorCode::CORRUPT_STATE, "device count out of bounds");
    for (uint32_t i = 0; i < ndev; ++i) {
        DeviceRecord d; read_device(r, d);
        out.devices[d.device_id] = d;
        out.device_order.push_back(d.device_id);
        out.ledgers.emplace(d.device_id, ResourceLedger(d.resource_capacity));
    }
    uint32_t nsvc = r.u32(); if (nsvc > wire::kMaxContainerCount) throw_error(ErrorCode::CORRUPT_STATE, "service count out of bounds");
    for (uint32_t i = 0; i < nsvc; ++i) {
        ServiceDefinition s = read_service(r);
        out.services[s.service_id] = std::move(s);
    }
    uint32_t nprog = r.u32(); if (nprog > wire::kMaxContainerCount) throw_error(ErrorCode::CORRUPT_STATE, "program count out of bounds");
    for (uint32_t i = 0; i < nprog; ++i) {
        ProgramId pid = rd_id<ProgramId>(r);
        uint32_t ng = r.u32(); if (ng > 256) throw_error(ErrorCode::CORRUPT_STATE, "program generation count out of bounds");
        for (uint32_t gi = 0; gi < ng; ++gi) {
            ProgramGeneration g = rd_id<ProgramGeneration>(r);
            ProgramArtifact art = read_program(r);
            bool cur = r.u8() != 0;
            out.programs[pid][g] = std::move(art);
            if (cur) out.current_program_gen[pid] = g;
        }
    }
    uint32_t ndep = r.u32(); if (ndep > wire::kMaxContainerCount) throw_error(ErrorCode::CORRUPT_STATE, "deployment count out of bounds");
    for (uint32_t i = 0; i < ndep; ++i) {
        Deployment d = read_deployment(r);
        out.deployments[d.deployment_id] = std::move(d);
    }
    uint32_t nusage = r.u32(); if (nusage > wire::kMaxContainerCount) throw_error(ErrorCode::CORRUPT_STATE, "usage count out of bounds");
    for (uint32_t i = 0; i < nusage; ++i) {
        DeviceId did = rd_id<DeviceId>(r);
        ResourceLedger::Usage u;
        u.memory_bytes = r.u64(); u.queues = r.u64(); u.functions = r.u64(); u.slots = r.u64(); u.contexts = r.u64();
        out.usage[did] = u;
    }
    r.expect_end();

    // Conservative recovery: no live authority survives a restart as fresh.
    for (auto& [did, d] : out.devices) {
        d.evidence_fresh = false;
        if (d.lifecycle != DeviceLifecycle::RETIRED && d.lifecycle != DeviceLifecycle::FAILED &&
            d.lifecycle != DeviceLifecycle::OFFLINE)
            d.lifecycle = DeviceLifecycle::REVALIDATION_REQUIRED;
    }
    for (auto& [dep_id, dep] : out.deployments) {
        if (dep.state == DeploymentState::ACTIVE || dep.state == DeploymentState::DEGRADED ||
            dep.state == DeploymentState::DRAINING) {
            dep.state = DeploymentState::REVALIDATION_REQUIRED;
        }
        dep.authoritative = false;
        dep.activation.reset();
    }
    out.activations.clear();  // live authority is never restored from disk

    return true;
}

} // namespace dpufabric
