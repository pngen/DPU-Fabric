// Internal message codec shared by the coordinator and worker tools.
// This is NOT part of the public installed API; it is an implementation detail
// of the multiprocess control-plane proof.
#pragma once
#include "dpufabric/wire.h"
#include "dpufabric/device.h"
#include "dpufabric/id.h"
#include "dpufabric/types.h"
#include "dpufabric/error.h"
#include <vector>
#include <string>

namespace dpufabric {
namespace mpproto {

using wire::Writer;
using wire::Reader;

inline void put_id(Writer& w, uint64_t id) { w.u64(id); }
inline uint64_t get_id(Reader& r) { return r.u64(); }

inline void put_str(Writer& w, const std::string& s) { w.str(s); }
inline std::string get_str(Reader& r) { return r.str(); }

// Serialize a Capability record into a writer.
inline void put_cap(Writer& w, const Capability& c) {
    w.str(c.key.str());
    w.u8(static_cast<uint8_t>(c.state));
    w.u64(c.version);
    w.u8(c.requires_isolation ? 1 : 0);
    w.u8(static_cast<uint8_t>(c.source));
}
inline Capability get_cap(Reader& r) {
    Capability c;
    c.key = CapabilityKey(r.str());
    c.state = static_cast<CapabilityState>(r.u8());
    c.version = r.u64();
    c.requires_isolation = r.u8() != 0;
    c.source = static_cast<ProvenanceSource>(r.u8());
    return c;
}

inline void put_device(Writer& w, const DeviceRecord& d) {
    w.u64(d.device_id.as_u64());
    w.u64(d.generation.as_u64());
    w.u64(d.boot_id.as_u64());
    w.str(d.vendor); w.str(d.model);
    w.u8(static_cast<uint8_t>(d.device_class));
    w.str(d.pci_reference);
    w.u64(d.firmware_gen.as_u64());
    w.u64(d.runtime_gen.as_u64());
    w.str(d.architecture);
    w.u64(d.memory_bytes);
    w.u64(d.resource_capacity.memory_bytes); w.u64(d.resource_capacity.queues);
    w.u64(d.resource_capacity.functions); w.u64(d.resource_capacity.slots);
    w.u64(d.resource_capacity.contexts);
    w.u32(static_cast<uint32_t>(d.ports.size()));
    for (auto p : d.ports) w.u64(p.as_u64());
    w.u32(static_cast<uint32_t>(d.functions.size()));
    for (auto f : d.functions) w.u64(f.as_u64());
    w.u32(static_cast<uint32_t>(d.capabilities.size()));
    for (auto& [k, c] : d.capabilities.all()) (void)k, put_cap(w, c);
    w.u32(static_cast<uint32_t>(d.offload_classes.size()));
    for (auto c : d.offload_classes) w.u8(static_cast<uint8_t>(c));
    w.u8(static_cast<uint8_t>(d.health));
    w.u8(static_cast<uint8_t>(d.lifecycle));
    w.u8(d.evidence_fresh ? 1 : 0);
    w.u8(static_cast<uint8_t>(d.discovery_source));
    w.u64(d.evidence_gen.as_u64());
    w.u64(d.isolation_domain.as_u64());
    w.u64(d.tenant.as_u64());
    w.u8(d.isolation_proven ? 1 : 0);
    w.u8(static_cast<uint8_t>(d.locality));
    w.str(d.discovery_detail);
}

inline DeviceRecord get_device(Reader& r) {
    DeviceRecord d;
    d.device_id = DeviceId(r.u64());
    d.generation = DeviceGeneration(r.u64());
    d.boot_id = DeviceBootId(r.u64());
    d.vendor = r.str(); d.model = r.str();
    d.device_class = static_cast<DeviceClass>(r.u8());
    d.pci_reference = r.str();
    d.firmware_gen = FirmwareGeneration(r.u64());
    d.runtime_gen = RuntimeGeneration(r.u64());
    d.architecture = r.str();
    d.memory_bytes = r.u64();
    d.resource_capacity.memory_bytes = r.u64(); d.resource_capacity.queues = r.u64();
    d.resource_capacity.functions = r.u64(); d.resource_capacity.slots = r.u64();
    d.resource_capacity.contexts = r.u64();
    uint32_t np = r.u32(); if (np > wire::kMaxContainerCount) throw_error(ErrorCode::CORRUPT_STATE, "port count");
    for (uint32_t i = 0; i < np; ++i) d.ports.push_back(PortId(r.u64()));
    uint32_t nf = r.u32(); if (nf > wire::kMaxContainerCount) throw_error(ErrorCode::CORRUPT_STATE, "function count");
    for (uint32_t i = 0; i < nf; ++i) d.functions.push_back(FunctionId(r.u64()));
    uint32_t nc = r.u32(); if (nc > wire::kMaxContainerCount) throw_error(ErrorCode::CORRUPT_STATE, "capability count");
    for (uint32_t i = 0; i < nc; ++i) d.capabilities.add(get_cap(r));
    uint32_t nocl = r.u32(); if (nocl > 64) throw_error(ErrorCode::CORRUPT_STATE, "offload class count");
    for (uint32_t i = 0; i < nocl; ++i) d.offload_classes.push_back(static_cast<OffloadClass>(r.u8()));
    d.health = static_cast<DeviceHealth>(r.u8());
    d.lifecycle = static_cast<DeviceLifecycle>(r.u8());
    d.evidence_fresh = r.u8() != 0;
    d.discovery_source = static_cast<ProvenanceSource>(r.u8());
    d.evidence_gen = EvidenceGeneration(r.u64());
    d.isolation_domain = IsolationDomainId(r.u64());
    d.tenant = TenantId(r.u64());
    d.isolation_proven = r.u8() != 0;
    d.locality = static_cast<LocalityQuality>(r.u8());
    d.discovery_detail = r.str();
    return d;
}

// Command types addressed from a controlling client to the coordinator.
enum class Cmd : uint8_t {
    CREATE_DEPLOYMENT = 1,
    STATUS = 2,
    DRAIN = 3,
    SHUTDOWN = 4,
    END = 5
};

inline std::vector<uint8_t> encode_hello(uint64_t worker, uint64_t boot, const std::string& label) {
    Writer w; w.u64(worker); w.u64(boot); w.str(label);
    return std::vector<uint8_t>(w.data().begin(), w.data().end());
}
inline void decode_hello(Reader& r, uint64_t& worker, uint64_t& boot, std::string& label) {
    worker = r.u64(); boot = r.u64(); label = r.str();
}

// A modest helper to toggle a data-plane "governed command" sent to a worker.
inline std::vector<uint8_t> encode_command(uint64_t cmd_id, const std::string& action) {
    Writer w; w.u64(cmd_id); w.str(action);
    return std::vector<uint8_t>(w.data().begin(), w.data().end());
}
inline void decode_command(Reader& r, uint64_t& cmd_id, std::string& action) {
    cmd_id = r.u64(); action = r.str();
}

} // namespace mpproto
} // namespace dpufabric
