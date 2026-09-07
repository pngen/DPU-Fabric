/// DPU Fabric: core semantic enums and value types.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <array>
#include <optional>

namespace dpufabric {

// == Offload class (semantic, not hardware marketing) =========================
enum class OffloadClass : uint16_t {
    NETWORK_SERVICE = 1,
    DATA_MOVEMENT = 2,
    STORAGE_SERVICE = 3,
    SECURITY_SERVICE = 4,
    TELEMETRY_SERVICE = 5,
    COMPRESSION = 6,
    CHECKSUM = 7,
    ENCRYPTION = 8,
    PACKET_CLASSIFICATION = 9,
    PACKET_STEERING = 10,
    VIRTUAL_SWITCHING = 11,
    FIREWALL_SERVICE = 12,
    STORAGE_PATH_PROCESSING = 13,
    TRANSPORT_ASSIST = 14,
    CONTROL_PLANE_SERVICE = 15,
    CUSTOM_INFRASTRUCTURE_SERVICE = 16,
    END_MARKER = 17
};

constexpr std::array<OffloadClass, static_cast<size_t>(OffloadClass::END_MARKER) - 1> all_offload_classes() noexcept {
    std::array<OffloadClass, static_cast<size_t>(OffloadClass::END_MARKER) - 1> a{};
    size_t i = 0;
    for (uint16_t v = 1; v < static_cast<uint16_t>(OffloadClass::END_MARKER); ++v) a[i++] = static_cast<OffloadClass>(v);
    return a;
}
constexpr const char* offload_class_name(OffloadClass c) noexcept {
    switch (c) {
        case OffloadClass::NETWORK_SERVICE: return "NETWORK_SERVICE";
        case OffloadClass::DATA_MOVEMENT: return "DATA_MOVEMENT";
        case OffloadClass::STORAGE_SERVICE: return "STORAGE_SERVICE";
        case OffloadClass::SECURITY_SERVICE: return "SECURITY_SERVICE";
        case OffloadClass::TELEMETRY_SERVICE: return "TELEMETRY_SERVICE";
        case OffloadClass::COMPRESSION: return "COMPRESSION";
        case OffloadClass::CHECKSUM: return "CHECKSUM";
        case OffloadClass::ENCRYPTION: return "ENCRYPTION";
        case OffloadClass::PACKET_CLASSIFICATION: return "PACKET_CLASSIFICATION";
        case OffloadClass::PACKET_STEERING: return "PACKET_STEERING";
        case OffloadClass::VIRTUAL_SWITCHING: return "VIRTUAL_SWITCHING";
        case OffloadClass::FIREWALL_SERVICE: return "FIREWALL_SERVICE";
        case OffloadClass::STORAGE_PATH_PROCESSING: return "STORAGE_PATH_PROCESSING";
        case OffloadClass::TRANSPORT_ASSIST: return "TRANSPORT_ASSIST";
        case OffloadClass::CONTROL_PLANE_SERVICE: return "CONTROL_PLANE_SERVICE";
        case OffloadClass::CUSTOM_INFRASTRUCTURE_SERVICE: return "CUSTOM_INFRASTRUCTURE_SERVICE";
        case OffloadClass::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}
inline std::optional<OffloadClass> offload_class_from_name(std::string_view s) {
    for (OffloadClass c : all_offload_classes()) if (s == offload_class_name(c)) return c;
    return std::nullopt;
}

// == Capability state ========================================================
enum class CapabilityState : uint8_t {
    SUPPORTED = 1,
    UNSUPPORTED = 2,
    UNKNOWN = 3,
    REVALIDATION_REQUIRED = 4,
    END_MARKER = 5
};
constexpr const char* capability_state_name(CapabilityState s) noexcept {
    switch (s) {
        case CapabilityState::SUPPORTED: return "SUPPORTED";
        case CapabilityState::UNSUPPORTED: return "UNSUPPORTED";
        case CapabilityState::UNKNOWN: return "UNKNOWN";
        case CapabilityState::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
        case CapabilityState::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}
inline std::optional<CapabilityState> capability_state_from_name(std::string_view s) {
    for (uint8_t v = 1; v < static_cast<uint8_t>(CapabilityState::END_MARKER); ++v) {
        auto c = static_cast<CapabilityState>(v);
        if (s == capability_state_name(c)) return c;
    }
    return std::nullopt;
}

// == Evidence / provenance source ============================================
enum class ProvenanceSource : uint8_t {
    REAL_OS_DISCOVERY = 1,
    REAL_VENDOR_API = 2,
    SYNTHETIC = 3,
    CONFIGURED = 4,
    RECOVERED_STALE = 5,
    DERIVED = 6,
    UNSUPPORTED = 7,
    END_MARKER = 8
};
constexpr const char* provenance_source_name(ProvenanceSource s) noexcept {
    switch (s) {
        case ProvenanceSource::REAL_OS_DISCOVERY: return "REAL_OS_DISCOVERY";
        case ProvenanceSource::REAL_VENDOR_API: return "REAL_VENDOR_API";
        case ProvenanceSource::SYNTHETIC: return "SYNTHETIC";
        case ProvenanceSource::CONFIGURED: return "CONFIGURED";
        case ProvenanceSource::RECOVERED_STALE: return "RECOVERED_STALE";
        case ProvenanceSource::DERIVED: return "DERIVED";
        case ProvenanceSource::UNSUPPORTED: return "UNSUPPORTED";
        case ProvenanceSource::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}
inline std::optional<ProvenanceSource> provenance_source_from_name(std::string_view s) {
    for (uint8_t v = 1; v < static_cast<uint8_t>(ProvenanceSource::END_MARKER); ++v) {
        auto c = static_cast<ProvenanceSource>(v);
        if (s == provenance_source_name(c)) return c;
    }
    return std::nullopt;
}
// Compact labels used for the REAL / SYNTHETIC / UNSUPPORTED evidence class.
inline const char* evidence_class(ProvenanceSource s) noexcept {
    switch (s) {
        case ProvenanceSource::REAL_OS_DISCOVERY:
        case ProvenanceSource::REAL_VENDOR_API: return "REAL";
        case ProvenanceSource::SYNTHETIC: return "SYNTHETIC";
        case ProvenanceSource::UNSUPPORTED: return "UNSUPPORTED";
        default: return "DERIVED";
    }
}

// == Execution class (fallback semantics) ====================================
enum class ExecutionClass : uint8_t {
    DPU_OFFLOAD = 1,
    SMARTNIC_OFFLOAD = 2,
    INFRA_PROCESSOR_OFFLOAD = 3,
    HOST_FALLBACK = 4,
    UNSUPPORTED = 5,
    END_MARKER = 6
};
constexpr const char* execution_class_name(ExecutionClass e) noexcept {
    switch (e) {
        case ExecutionClass::DPU_OFFLOAD: return "DPU_OFFLOAD";
        case ExecutionClass::SMARTNIC_OFFLOAD: return "SMARTNIC_OFFLOAD";
        case ExecutionClass::INFRA_PROCESSOR_OFFLOAD: return "INFRA_PROCESSOR_OFFLOAD";
        case ExecutionClass::HOST_FALLBACK: return "HOST_FALLBACK";
        case ExecutionClass::UNSUPPORTED: return "UNSUPPORTED";
        case ExecutionClass::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}
inline std::optional<ExecutionClass> execution_class_from_name(std::string_view s) {
    for (uint8_t v = 1; v < static_cast<uint8_t>(ExecutionClass::END_MARKER); ++v) {
        auto c = static_cast<ExecutionClass>(v);
        if (s == execution_class_name(c)) return c;
    }
    return std::nullopt;
}

// == Device class ============================================================
enum class DeviceClass : uint8_t {
    DPU = 1,
    SMARTNIC = 2,
    INFRA_PROCESSOR = 3,
    HOST_PROCESSOR = 4,
    NIC_ADJACENT = 5,
    UNKNOWN_DEVICE = 6,
    END_MARKER = 7
};
constexpr const char* device_class_name(DeviceClass c) noexcept {
    switch (c) {
        case DeviceClass::DPU: return "DPU";
        case DeviceClass::SMARTNIC: return "SMARTNIC";
        case DeviceClass::INFRA_PROCESSOR: return "INFRA_PROCESSOR";
        case DeviceClass::HOST_PROCESSOR: return "HOST_PROCESSOR";
        case DeviceClass::NIC_ADJACENT: return "NIC_ADJACENT";
        case DeviceClass::UNKNOWN_DEVICE: return "UNKNOWN_DEVICE";
        case DeviceClass::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}
inline std::optional<DeviceClass> device_class_from_name(std::string_view s) {
    for (uint8_t v = 1; v < static_cast<uint8_t>(DeviceClass::END_MARKER); ++v) {
        auto c = static_cast<DeviceClass>(v);
        if (s == device_class_name(c)) return c;
    }
    return std::nullopt;
}

// == Device lifecycle ========================================================
enum class DeviceLifecycle : uint8_t {
    DISCOVERED = 1,
    AVAILABLE = 2,
    PROVISIONING = 3,
    READY = 4,
    DEGRADED = 5,
    DRAINING = 6,
    REVALIDATION_REQUIRED = 7,
    FAILED = 8,
    OFFLINE = 9,
    RETIRED = 10,
    END_MARKER = 11
};
constexpr const char* device_lifecycle_name(DeviceLifecycle s) noexcept {
    switch (s) {
        case DeviceLifecycle::DISCOVERED: return "DISCOVERED";
        case DeviceLifecycle::AVAILABLE: return "AVAILABLE";
        case DeviceLifecycle::PROVISIONING: return "PROVISIONING";
        case DeviceLifecycle::READY: return "READY";
        case DeviceLifecycle::DEGRADED: return "DEGRADED";
        case DeviceLifecycle::DRAINING: return "DRAINING";
        case DeviceLifecycle::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
        case DeviceLifecycle::FAILED: return "FAILED";
        case DeviceLifecycle::OFFLINE: return "OFFLINE";
        case DeviceLifecycle::RETIRED: return "RETIRED";
        case DeviceLifecycle::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}
inline std::optional<DeviceLifecycle> device_lifecycle_from_name(std::string_view s) {
    for (uint8_t v = 1; v < static_cast<uint8_t>(DeviceLifecycle::END_MARKER); ++v) {
        auto c = static_cast<DeviceLifecycle>(v);
        if (s == device_lifecycle_name(c)) return c;
    }
    return std::nullopt;
}

// == Deployment lifecycle ====================================================
enum class DeploymentState : uint8_t {
    PLANNED = 1,
    RESERVED = 2,
    STAGING = 3,
    LOADED = 4,
    VALIDATING = 5,
    ACTIVATING = 6,
    ACTIVE = 7,
    DEGRADED = 8,
    DRAINING = 9,
    FAILED = 10,
    ABORTED = 11,
    RETIRED = 12,
    REVALIDATION_REQUIRED = 13,
    END_MARKER = 14
};
constexpr const char* deployment_state_name(DeploymentState s) noexcept {
    switch (s) {
        case DeploymentState::PLANNED: return "PLANNED";
        case DeploymentState::RESERVED: return "RESERVED";
        case DeploymentState::STAGING: return "STAGING";
        case DeploymentState::LOADED: return "LOADED";
        case DeploymentState::VALIDATING: return "VALIDATING";
        case DeploymentState::ACTIVATING: return "ACTIVATING";
        case DeploymentState::ACTIVE: return "ACTIVE";
        case DeploymentState::DEGRADED: return "DEGRADED";
        case DeploymentState::DRAINING: return "DRAINING";
        case DeploymentState::FAILED: return "FAILED";
        case DeploymentState::ABORTED: return "ABORTED";
        case DeploymentState::RETIRED: return "RETIRED";
        case DeploymentState::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
        case DeploymentState::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}
inline std::optional<DeploymentState> deployment_state_from_name(std::string_view s) {
    for (uint8_t v = 1; v < static_cast<uint8_t>(DeploymentState::END_MARKER); ++v) {
        auto c = static_cast<DeploymentState>(v);
        if (s == deployment_state_name(c)) return c;
    }
    return std::nullopt;
}

// == Isolation requirement ===================================================
enum class IsolationRequirement : uint8_t {
    DEDICATED_DEVICE = 1,
    DEDICATED_FUNCTION = 2,
    DEDICATED_QUEUE = 3,
    DEDICATED_EXECUTION_CONTEXT = 4,
    SHARED_ISOLATED_CONTEXT = 5,
    TENANT_COMPATIBLE_SHARED = 6,
    SECURITY_DOMAIN = 7,
    TRUST_DOMAIN = 8,
    NONE = 9,
    END_MARKER = 10
};
constexpr const char* isolation_requirement_name(IsolationRequirement r) noexcept {
    switch (r) {
        case IsolationRequirement::DEDICATED_DEVICE: return "DEDICATED_DEVICE";
        case IsolationRequirement::DEDICATED_FUNCTION: return "DEDICATED_FUNCTION";
        case IsolationRequirement::DEDICATED_QUEUE: return "DEDICATED_QUEUE";
        case IsolationRequirement::DEDICATED_EXECUTION_CONTEXT: return "DEDICATED_EXECUTION_CONTEXT";
        case IsolationRequirement::SHARED_ISOLATED_CONTEXT: return "SHARED_ISOLATED_CONTEXT";
        case IsolationRequirement::TENANT_COMPATIBLE_SHARED: return "TENANT_COMPATIBLE_SHARED";
        case IsolationRequirement::SECURITY_DOMAIN: return "SECURITY_DOMAIN";
        case IsolationRequirement::TRUST_DOMAIN: return "TRUST_DOMAIN";
        case IsolationRequirement::NONE: return "NONE";
        case IsolationRequirement::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}

// == Offload mode ============================================================
enum class OffloadMode : uint8_t {
    OFFLOAD_REQUIRED = 1,
    OFFLOAD_PREFERRED = 2,
    END_MARKER = 3
};
constexpr const char* offload_mode_name(OffloadMode m) noexcept {
    switch (m) {
        case OffloadMode::OFFLOAD_REQUIRED: return "OFFLOAD_REQUIRED";
        case OffloadMode::OFFLOAD_PREFERRED: return "OFFLOAD_PREFERRED";
        case OffloadMode::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}

// == Execution model =========================================================
enum class ExecutionModel : uint8_t {
    NATIVE_DATA_PLANE = 1,
    FIRMWARE_ACCELERATED = 2,
    HOST_AUXILIARY = 3,
    UNKNOWN = 4,
    END_MARKER = 5
};
constexpr const char* execution_model_name(ExecutionModel m) noexcept {
    switch (m) {
        case ExecutionModel::NATIVE_DATA_PLANE: return "NATIVE_DATA_PLANE";
        case ExecutionModel::FIRMWARE_ACCELERATED: return "FIRMWARE_ACCELERATED";
        case ExecutionModel::HOST_AUXILIARY: return "HOST_AUXILIARY";
        case ExecutionModel::UNKNOWN: return "UNKNOWN";
        case ExecutionModel::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}

// == Program / artifact type =================================================
enum class ArtifactType : uint8_t {
    FIRMWARE_IMAGE = 1,
    RTE_PROGRAM = 2,
    CONFIGURATION = 3,
    RUNTIME_EXTENSION = 4,
    SYNTHETIC = 5,
    UNKNOWN = 6,
    END_MARKER = 7
};
constexpr const char* artifact_type_name(ArtifactType t) noexcept {
    switch (t) {
        case ArtifactType::FIRMWARE_IMAGE: return "FIRMWARE_IMAGE";
        case ArtifactType::RTE_PROGRAM: return "RTE_PROGRAM";
        case ArtifactType::CONFIGURATION: return "CONFIGURATION";
        case ArtifactType::RUNTIME_EXTENSION: return "RUNTIME_EXTENSION";
        case ArtifactType::SYNTHETIC: return "SYNTHETIC";
        case ArtifactType::UNKNOWN: return "UNKNOWN";
        case ArtifactType::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}

// == Stateful service ========================================================
enum class Statefulness : uint8_t {
    STATELESS = 1,
    STATEFUL = 2,
    END_MARKER = 3
};
constexpr const char* statefulness_name(Statefulness s) noexcept {
    switch (s) {
        case Statefulness::STATELESS: return "STATELESS";
        case Statefulness::STATEFUL: return "STATEFUL";
        case Statefulness::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}

// == Security mode ===========================================================
enum class SecurityMode : uint8_t {
    SECURE = 1,
    ISOLATED = 2,
    HYBRID = 3,
    UNVERIFIED = 4,
    END_MARKER = 5
};
constexpr const char* security_mode_name(SecurityMode s) noexcept {
    switch (s) {
        case SecurityMode::SECURE: return "SECURE";
        case SecurityMode::ISOLATED: return "ISOLATED";
        case SecurityMode::HYBRID: return "HYBRID";
        case SecurityMode::UNVERIFIED: return "UNVERIFIED";
        case SecurityMode::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}

// == Locality quality ========================================================
enum class LocalityQuality : uint8_t {
    SAME_DEVICE = 1,
    SAME_FUNCTION = 2,
    SAME_NIC = 3,
    SAME_ROOT = 4,
    REMOTE = 5,
    UNKNOWN = 6,
    END_MARKER = 7
};
constexpr const char* locality_quality_name(LocalityQuality q) noexcept {
    switch (q) {
        case LocalityQuality::SAME_DEVICE: return "SAME_DEVICE";
        case LocalityQuality::SAME_FUNCTION: return "SAME_FUNCTION";
        case LocalityQuality::SAME_NIC: return "SAME_NIC";
        case LocalityQuality::SAME_ROOT: return "SAME_ROOT";
        case LocalityQuality::REMOTE: return "REMOTE";
        case LocalityQuality::UNKNOWN: return "UNKNOWN";
        case LocalityQuality::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}

} // namespace dpufabric
