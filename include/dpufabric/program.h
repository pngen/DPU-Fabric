/// DPU Fabric: program artifact model.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <span>
#include "dpufabric/id.h"
#include "dpufabric/types.h"
#include "dpufabric/capability.h"
#include "dpufabric/resource.h"
#include "dpufabric/isolation.h"
#include "dpufabric/error.h"

namespace dpufabric {

// Content identity is a 32-byte SHA-256 digest.  Only hash *matching* is
// performed; checksums and hashes are integrity, never authentication.
using Sha256 = std::array<uint8_t, 32>;

inline std::string hex_encode(std::span<const uint8_t> b) {
    constexpr char d[] = "0123456789abcdef";
    std::string s;
    s.reserve(b.size() * 2);
    for (uint8_t x : b) { s.push_back(d[x >> 4]); s.push_back(d[x & 0xF]); }
    return s;
}

// A deployable program/artifact.  Presence of an artifact is not permission
// to run it; compatibility and authority must be proven separately.
struct ProgramArtifact {
    ProgramId program_id;
    ProgramGeneration program_gen;    // may be 0 only for a placeholder? no: must be set
    ArtifactId artifact_id;
    ArtifactGeneration artifact_gen;
    ArtifactType artifact_type = ArtifactType::UNKNOWN;
    std::string version;
    Sha256 content_hash{};
    std::string target_architecture;   // e.g. "arm64", "x86_64"
    std::string runtime_abi;           // e.g. "DOCA_2.x", "host-abi"
    std::vector<CapabilityRequirement> required_caps;
    FirmwareGeneration min_firmware_gen;
    std::string provenance;
    std::string size_hint;             // provenance/debug only
};

// A ServiceDefinition describes what a service requires and how it may fail,
// independent of any particular deployed artifact or device.
struct ServiceDefinition {
    ServiceId service_id;
    ServiceGeneration service_gen;
    OffloadClass offload_class = OffloadClass::CUSTOM_INFRASTRUCTURE_SERVICE;
    std::vector<CapabilityRequirement> required_caps;
    std::vector<CapabilityRequirement> optional_caps;
    IsolationSpec isolation;
    ResourceRequest resource;
    ExecutionModel execution_model = ExecutionModel::UNKNOWN;
    FirmwareGeneration min_firmware_gen;
    RuntimeGeneration min_runtime_gen;
    // Compatibility: program generation pinned for this service generation.
    ProgramId required_program;
    std::string required_abi;
    Statefulness statefulness = Statefulness::STATELESS;
    bool recoverable = false;
    std::vector<OffloadClass> allowed_fallback_classes; // e.g. HOST_FALLBACK
    std::string failure_policy;
    std::vector<ServiceId> dependencies;
};

} // namespace dpufabric
