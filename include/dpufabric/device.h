/// DPU Fabric: vendor-neutral programmable infrastructure device model.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include "dpufabric/id.h"
#include "dpufabric/types.h"
#include "dpufabric/capability.h"
#include "dpufabric/resource.h"
#include "dpufabric/error.h"

namespace dpufabric {

// Explicit health state for a device (kept separate from lifecycle so that a
// device can be HEALTHY yet REVALIDATION_REQUIRED, and vice versa).
enum class DeviceHealth : uint8_t {
    HEALTHY = 1,
    WARN = 2,
    DEGRADED = 3,
    FAILED = 4,
    UNKNOWN = 5,
    END_MARKER = 6
};
constexpr const char* device_health_name(DeviceHealth h) noexcept {
    switch (h) {
        case DeviceHealth::HEALTHY: return "HEALTHY";
        case DeviceHealth::WARN: return "WARN";
        case DeviceHealth::DEGRADED: return "DEGRADED";
        case DeviceHealth::FAILED: return "FAILED";
        case DeviceHealth::UNKNOWN: return "UNKNOWN";
        case DeviceHealth::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}

// Vendor-neutral device record.  Raw vendor strings are preserved for
// provenance; the semantic model is expressed only through the typed fields.
struct DeviceRecord {
    DeviceId device_id;
    DeviceGeneration generation;      // structural model generation
    DeviceBootId boot_id;             // boot/incarnation identity
    std::string vendor;               // raw, provenance only
    std::string model;                // raw, provenance only
    DeviceClass device_class = DeviceClass::UNKNOWN_DEVICE;
    std::string pci_reference;        // e.g. "0000:03:00.0"
    std::string parent_nic_reference; // provenance only
    FunctionId parent_function;       // typed parent function, may be null
    std::string firmware_version;     // raw
    FirmwareGeneration firmware_gen;
    std::string runtime_version;      // raw
    RuntimeGeneration runtime_gen;
    std::string architecture;         // e.g. "arm64", "x86_64"
    uint64_t processing_units = 0;    // cores/pipelines
    uint64_t memory_bytes = 0;        // total on-device memory (0 = unknown)
    uint64_t available_programmable_memory = 0; // (0 = unknown)
    ResourceProfile resource_capacity; // queue/slot/context bounds
    std::vector<PortId> ports;
    std::vector<FunctionId> functions;
    CapabilitySet capabilities;        // typed capability evidence
    std::vector<OffloadClass> offload_classes; // semantic classes (derived from capabilities)
    std::vector<ExecutionModel> execution_models;
    std::vector<std::string> program_formats;
    bool supports_dma = false;
    bool rdma_adjacent = false;
    std::string host_comm;             // host communication mechanism
    bool hardware_isolation = false;   // claims hardware-backed isolation
    IsolationDomainId isolation_domain;  // device's current isolation binding
    TenantId tenant;                     // device's current tenant binding
    bool isolation_proven = false;       // authoritative evidence of correct isolation config
    LocalityQuality locality = LocalityQuality::UNKNOWN; // attachment locality quality
    DeviceHealth health = DeviceHealth::UNKNOWN;
    DeviceLifecycle lifecycle = DeviceLifecycle::DISCOVERED;
    ProvenanceSource discovery_source = ProvenanceSource::CONFIGURED;
    EvidenceGeneration evidence_gen;   // freshness of capability/structural evidence
    bool evidence_fresh = false;       // whether evidence_gen is currently current
    std::string discovery_detail;      // provenance text
    WorkerId owning_worker;            // worker that published live evidence (may be null)
    WorkerBootId owner_worker_boot;    // boot identity of the publishing worker

    // A device can grant NEW authoritative offload only if it is READY, its
    // evidence is fresh, and it is healthy enough.
    bool is_ready_for_offload() const noexcept {
        return lifecycle == DeviceLifecycle::READY && evidence_fresh;
    }
    bool is_authoritative() const noexcept {
        return lifecycle == DeviceLifecycle::READY && evidence_fresh;
    }

    bool supports_class(OffloadClass c) const noexcept {
        for (auto& x : offload_classes) if (x == c) return true;
        return false;
    }

    // Provenance class for reporting.
    const char* evidence_class_label() const noexcept { return evidence_class(discovery_source); }
};

// Device lifecycle transition table.  Transitions not listed are invalid and
// must be rejected by the caller as INVALID_TRANSITION.
inline bool valid_device_transition(DeviceLifecycle from, DeviceLifecycle to) noexcept {
    using L = DeviceLifecycle;
    switch (from) {
        case L::DISCOVERED:
            return to == L::AVAILABLE || to == L::FAILED || to == L::RETIRED || to == L::OFFLINE;
        case L::AVAILABLE:
            return to == L::PROVISIONING || to == L::READY || to == L::REVALIDATION_REQUIRED ||
                   to == L::FAILED || to == L::OFFLINE || to == L::RETIRED || to == L::DEGRADED;
        case L::PROVISIONING:
            return to == L::READY || to == L::AVAILABLE || to == L::FAILED || to == L::OFFLINE ||
                   to == L::RETIRED || to == L::DEGRADED;
        case L::READY:
            return to == L::DEGRADED || to == L::FAILED || to == L::DRAINING ||
                   to == L::REVALIDATION_REQUIRED || to == L::OFFLINE || to == L::RETIRED;
        case L::DEGRADED:
            return to == L::READY || to == L::FAILED || to == L::DRAINING ||
                   to == L::REVALIDATION_REQUIRED || to == L::OFFLINE || to == L::RETIRED;
        case L::DRAINING:
            return to == L::AVAILABLE || to == L::OFFLINE || to == L::RETIRED ||
                   to == L::FAILED || to == L::READY;
        case L::REVALIDATION_REQUIRED:
            return to == L::READY || to == L::AVAILABLE || to == L::FAILED || to == L::OFFLINE ||
                   to == L::RETIRED || to == L::DEGRADED;
        case L::FAILED:
            return to == L::OFFLINE || to == L::RETIRED || to == L::AVAILABLE;
        case L::OFFLINE:
            return to == L::AVAILABLE || to == L::RETIRED || to == L::DISCOVERED;
        case L::RETIRED:
            return false;  // terminal
        case L::END_MARKER:
            return false;
    }
    return false;
}

} // namespace dpufabric
