/// DPU Fabric: hardware/backend abstraction.
#pragma once
#include <string>
#include <vector>
#include <memory>
#include "dpufabric/device.h"
#include "dpufabric/types.h"

namespace dpufabric {

// A device discovered through a backend.  The backend must never present a
// host process as a DPU, and must never upgrade synthetic evidence.
struct DiscoveredDevice {
    DeviceRecord record;
    bool physical_device_present = false;  // true only for real hardware
    std::string backend_label;
};

// Result of a backend discovery pass.
struct DiscoveryResult {
    bool backend_available = false;
    bool physical_dpu_present = false;      // whether a physical programmable DPU exists
    std::vector<DiscoveredDevice> devices;
    std::string summary;
};

// Explicit backends:
//   SystemBackend     -> what the OS truthfully exposes (NICs, GPU references).
//   SyntheticBackend  -> deterministic programmable test devices/scenarios.
//   UnsupportedBackend-> explicit representation of unavailable functionality.
class DiscoveryBackend {
public:
    virtual ~DiscoveryBackend() = default;
    virtual std::string name() const = 0;
    virtual bool is_vendor_backend() const noexcept { return false; }
    virtual DiscoveryResult discover() = 0;
    // The highest execution class this backend can genuinely provide.  A
    // vendor backend with libraries but no hardware returns UNSUPPORTED.
    virtual ExecutionClass execution_capability() const noexcept { return ExecutionClass::UNSUPPORTED; }
};

// Concrete backend factories.
std::shared_ptr<DiscoveryBackend> make_system_backend();
std::shared_ptr<DiscoveryBackend> make_synthetic_backend();
std::shared_ptr<DiscoveryBackend> make_unsupported_backend();

} // namespace dpufabric
