
#include "dpufabric/synthetic.h"

namespace dpufabric {
namespace synth {

void add_capability(DeviceRecord& d, const CapabilityKey& key, uint64_t version, ProvenanceSource src) {
    Capability c;
    c.key = key;
    c.state = CapabilityState::SUPPORTED;
    c.version = version;
    c.evidence_gen = EvidenceGeneration(1);
    c.source = src;
    d.capabilities.add(std::move(c));
}

DeviceRecord make_dpu(DeviceId id, const std::string& model,
                      std::vector<CapabilityKey> caps,
                      ResourceProfile cap, DeviceBootId boot,
                      std::vector<OffloadClass> classes) {
    DeviceRecord d;
    d.device_id = id;
    d.generation = DeviceGeneration(1);
    d.boot_id = boot;
    d.vendor = "synthetic";
    d.model = model;
    d.device_class = DeviceClass::DPU;
    d.pci_reference = "0000:" + std::string(model.size(), '0') + ":00.0";
    d.firmware_version = "1.0";
    d.firmware_gen = FirmwareGeneration(2);
    d.runtime_version = "1.2";
    d.runtime_gen = RuntimeGeneration(3);
    d.architecture = "arm64";
    d.processing_units = 8;
    d.memory_bytes = cap.memory_bytes;
    d.available_programmable_memory = cap.memory_bytes;
    d.resource_capacity = cap;
    d.locality = LocalityQuality::SAME_FUNCTION;
    d.health = DeviceHealth::HEALTHY;
    d.lifecycle = DeviceLifecycle::READY;
    d.discovery_source = ProvenanceSource::SYNTHETIC;
    d.evidence_gen = EvidenceGeneration(42);
    d.evidence_fresh = true;
    d.offload_classes = std::move(classes);
    d.execution_models = { ExecutionModel::NATIVE_DATA_PLANE };
    d.program_formats = { "synthetic-rte" };
    d.hardware_isolation = true;
    d.isolation_proven = true;
    for (auto& k : caps) add_capability(d, k);
    return d;
}

std::vector<DiscoveredDevice> scenario_ab() {
    std::vector<DiscoveredDevice> out;

    // Device A: network DPU, limited programmable memory.
    DeviceRecord a = make_dpu(DeviceId(1), "DPU-A",
        { cap_network(), cap_encryption(), cap_steering() },
        ResourceProfile{ 256u << 20, 8, 2, 16, 16 },
        DeviceBootId(0xA1), { OffloadClass::NETWORK_SERVICE, OffloadClass::SECURITY_SERVICE, OffloadClass::PACKET_STEERING });
    a.ports = { PortId(1), PortId(2) };
    a.functions = { FunctionId(11) };
    a.hardware_isolation = true;
    a.isolation_proven = true;
    a.isolation_domain = IsolationDomainId(7);
    a.tenant = TenantId(100);
    out.push_back({ std::move(a), false, "synthetic" });

    // Device B: storage DPU, higher memory, different isolation.
    DeviceRecord b = make_dpu(DeviceId(2), "DPU-B",
        { cap_storage(), cap_compression(), cap_checksum() },
        ResourceProfile{ 2u << 30, 16, 4, 32, 32 },
        DeviceBootId(0xB2), { OffloadClass::STORAGE_SERVICE, OffloadClass::COMPRESSION, OffloadClass::CHECKSUM });
    b.ports = { PortId(10), PortId(11) };
    b.functions = { FunctionId(21) };
    b.hardware_isolation = true;
    b.isolation_proven = true;
    b.isolation_domain = IsolationDomainId(8);
    b.tenant = TenantId(200);
    out.push_back({ std::move(b), false, "synthetic" });

    return out;
}

DiscoveryResult SyntheticBackend::discover() {
    DiscoveryResult r;
    r.backend_available = true;
    r.physical_dpu_present = false;
    r.devices = scenario_ab();
    r.summary = "synthetic backend: 2 modeled DPU devices (SYNTHETIC evidence)";
    return r;
}

} // namespace synth

std::shared_ptr<DiscoveryBackend> make_synthetic_backend() { return std::make_shared<synth::SyntheticBackend>(); }

} // namespace dpufabric
