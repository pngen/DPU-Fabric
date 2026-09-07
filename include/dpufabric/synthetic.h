
/// DPU Fabric: synthetic deterministic backend + device builders.
#pragma once
#include <string>
#include <vector>
#include <memory>
#include "dpufabric/device.h"
#include "dpufabric/backend.h"
#include "dpufabric/id.h"

namespace dpufabric {
namespace synth {

// Build a DPU-class device record with a deterministic capability set.
DeviceRecord make_dpu(DeviceId id, const std::string& model,
                      std::vector<CapabilityKey> caps,
                      ResourceProfile resource_capacity,
                      DeviceBootId boot,
                      std::vector<OffloadClass> classes);

// Enable a capability on a device record (SUPPORTED, fresh evidence).
void add_capability(DeviceRecord& d, const CapabilityKey& key, uint64_t version = 1,
                    ProvenanceSource src = ProvenanceSource::SYNTHETIC);

// The canonical two-device scenario used by the synthetic backend and tests:
//  Device A (DPU): network service, encryption, packet steering, limited memory.
//  Device B (DPU): storage service, compression, higher memory, different isolation.
std::vector<DiscoveredDevice> scenario_ab();

class SyntheticBackend final : public DiscoveryBackend {
public:
    std::string name() const override { return "synthetic"; }
    bool is_vendor_backend() const noexcept override { return false; }
    DiscoveryResult discover() override;
    ExecutionClass execution_capability() const noexcept override { return ExecutionClass::DPU_OFFLOAD; }
};

// Convenient capability keys used across tests and examples.
inline CapabilityKey cap_network()   { return CapabilityKey("NETWORK"); }
inline CapabilityKey cap_encryption(){ return CapabilityKey("ENCRYPTION"); }
inline CapabilityKey cap_steering()  { return CapabilityKey("PACKET_STEERING"); }
inline CapabilityKey cap_storage()   { return CapabilityKey("STORAGE"); }
inline CapabilityKey cap_compression(){ return CapabilityKey("COMPRESSION"); }
inline CapabilityKey cap_checksum()  { return CapabilityKey("CHECKSUM"); }

} // namespace synth
} // namespace dpufabric
