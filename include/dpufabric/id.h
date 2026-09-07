/// DPU Fabric: strong typed identities.
#pragma once
#include <cstdint>
#include <compare>
#include <string>
#include <string_view>
#include <optional>
#include <functional>
#include <limits>

namespace dpufabric {

// Identity tags.  Each tag is an empty type used only to make Id<Tag> a
// distinct type so that a DeviceId can never silently collide with a
// ServiceId, a generation with a boot-id, and so on.
struct DeviceIdTag {};
struct ServiceIdTag {};
struct ProgramIdTag {};
struct ArtifactIdTag {};
struct DeploymentIdTag {};
struct ActivationIdTag {};
struct ExecutionContextIdTag {};
struct ResourceContextIdTag {};
struct PortIdTag {};
struct FunctionIdTag {};
struct QueueIdTag {};
struct TenantIdTag {};
struct IsolationDomainIdTag {};
struct WorkerIdTag {};
struct LeaseIdTag {};
struct DeploymentAttemptIdTag {};
struct CapabilityIdTag {};

struct DeviceGenerationTag {};
struct WorkerBootIdTag {};
struct CoordinatorEpochTag {};
struct EvidenceGenerationTag {};
struct PolicyGenerationTag {};
struct ServiceGenerationTag {};
struct ProgramGenerationTag {};
struct ArtifactGenerationTag {};
struct DeploymentGenerationTag {};
struct ActivationGenerationTag {};
struct FirmwareGenerationTag {};
struct RuntimeGenerationTag {};
struct BootGenerationTag {};
struct DeviceBootIdTag {};

// Traits: per-tag rules governing valid values.
template <typename Tag>
struct IdTraits {
    static constexpr bool allow_zero = true;
    static constexpr uint64_t max_value = std::numeric_limits<uint64_t>::max();
    static constexpr const char* name() { return "Id"; }
};

template <> struct IdTraits<DeviceGenerationTag> {
    static constexpr bool allow_zero = false; static constexpr uint64_t max_value = std::numeric_limits<uint64_t>::max();
    static constexpr const char* name() { return "DeviceGeneration"; } };
template <> struct IdTraits<ServiceGenerationTag> {
    static constexpr bool allow_zero = false; static constexpr uint64_t max_value = std::numeric_limits<uint64_t>::max();
    static constexpr const char* name() { return "ServiceGeneration"; } };
template <> struct IdTraits<ProgramGenerationTag> {
    static constexpr bool allow_zero = false; static constexpr uint64_t max_value = std::numeric_limits<uint64_t>::max();
    static constexpr const char* name() { return "ProgramGeneration"; } };
template <> struct IdTraits<ArtifactGenerationTag> {
    static constexpr bool allow_zero = false; static constexpr uint64_t max_value = std::numeric_limits<uint64_t>::max();
    static constexpr const char* name() { return "ArtifactGeneration"; } };
template <> struct IdTraits<DeploymentGenerationTag> {
    static constexpr bool allow_zero = false; static constexpr uint64_t max_value = std::numeric_limits<uint64_t>::max();
    static constexpr const char* name() { return "DeploymentGeneration"; } };
template <> struct IdTraits<ActivationGenerationTag> {
    static constexpr bool allow_zero = false; static constexpr uint64_t max_value = std::numeric_limits<uint64_t>::max();
    static constexpr const char* name() { return "ActivationGeneration"; } };
template <> struct IdTraits<EvidenceGenerationTag> {
    static constexpr bool allow_zero = false; static constexpr uint64_t max_value = std::numeric_limits<uint64_t>::max();
    static constexpr const char* name() { return "EvidenceGeneration"; } };
template <> struct IdTraits<PolicyGenerationTag> {
    static constexpr bool allow_zero = false; static constexpr uint64_t max_value = std::numeric_limits<uint64_t>::max();
    static constexpr const char* name() { return "PolicyGeneration"; } };
template <> struct IdTraits<CoordinatorEpochTag> {
    static constexpr bool allow_zero = false; static constexpr uint64_t max_value = std::numeric_limits<uint64_t>::max();
    static constexpr const char* name() { return "CoordinatorEpoch"; } };
template <> struct IdTraits<WorkerBootIdTag> {
    static constexpr bool allow_zero = false; static constexpr uint64_t max_value = std::numeric_limits<uint64_t>::max();
    static constexpr const char* name() { return "WorkerBootId"; } };
template <> struct IdTraits<DeviceBootIdTag> {
    static constexpr bool allow_zero = false; static constexpr uint64_t max_value = std::numeric_limits<uint64_t>::max();
    static constexpr const char* name() { return "DeviceBootId"; } };
template <> struct IdTraits<FirmwareGenerationTag> {
    static constexpr bool allow_zero = false; static constexpr uint64_t max_value = std::numeric_limits<uint64_t>::max();
    static constexpr const char* name() { return "FirmwareGeneration"; } };
template <> struct IdTraits<RuntimeGenerationTag> {
    static constexpr bool allow_zero = false; static constexpr uint64_t max_value = std::numeric_limits<uint64_t>::max();
    static constexpr const char* name() { return "RuntimeGeneration"; } };
template <> struct IdTraits<BootGenerationTag> {
    static constexpr bool allow_zero = false; static constexpr uint64_t max_value = std::numeric_limits<uint64_t>::max();
    static constexpr const char* name() { return "BootGeneration"; } };
template <> struct IdTraits<DeploymentAttemptIdTag> {
    static constexpr bool allow_zero = false; static constexpr uint64_t max_value = std::numeric_limits<uint64_t>::max();
    static constexpr const char* name() { return "DeploymentAttemptId"; } };
template <> struct IdTraits<CapabilityIdTag> {
    static constexpr bool allow_zero = false; static constexpr uint64_t max_value = std::numeric_limits<uint64_t>::max();
    static constexpr const char* name() { return "CapabilityId"; } };

template <typename Tag>
class Id {
public:
    using value_type = uint64_t;
    constexpr Id() noexcept : value_(0) {}
    constexpr explicit Id(uint64_t v) noexcept : value_(v) {}

    static constexpr Id null() noexcept { return Id{}; }
    constexpr bool is_null() const noexcept { return value_ == 0; }
    constexpr uint64_t as_u64() const noexcept { return value_; }

    constexpr bool operator==(const Id& o) const noexcept { return value_ == o.value_; }
    constexpr bool operator!=(const Id& o) const noexcept { return value_ != o.value_; }
    constexpr std::strong_ordering operator<=>(const Id& o) const noexcept { return value_ <=> o.value_; }

    std::string encode() const {
        constexpr char digits[] = "0123456789abcdef";
        std::string out(16, '0');
        uint64_t v = value_;
        for (int i = 15; i >= 0; --i) { out[static_cast<size_t>(i)] = digits[v & 0xF]; v >>= 4; }
        return out;
    }

    static std::optional<Id> decode(std::string_view sv) {
        if (sv.size() > 16) return std::nullopt;
        if (sv.empty()) return std::nullopt;
        uint64_t v = 0;
        for (char c : sv) {
            uint64_t d;
            if (c >= '0' && c <= '9') d = static_cast<uint64_t>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<uint64_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = static_cast<uint64_t>(c - 'A' + 10);
            else return std::nullopt;
            if (v > (std::numeric_limits<uint64_t>::max() - d) / 16) return std::nullopt;
            v = v * 16 + d;
        }
        Id id{ v };
        if (!id.valid()) return std::nullopt;
        return id;
    }

    constexpr bool valid() const noexcept {
        if constexpr (!IdTraits<Tag>::allow_zero) {
            if (value_ == 0) return false;
        }
        if constexpr (IdTraits<Tag>::max_value != std::numeric_limits<uint64_t>::max()) {
            if (value_ > IdTraits<Tag>::max_value) return false;
        }
        return true;
    }

    static constexpr const char* type_name() noexcept { return IdTraits<Tag>::name(); }

private:
    uint64_t value_;
};

using DeviceId = Id<DeviceIdTag>;
using ServiceId = Id<ServiceIdTag>;
using ProgramId = Id<ProgramIdTag>;
using ArtifactId = Id<ArtifactIdTag>;
using DeploymentId = Id<DeploymentIdTag>;
using DeploymentAttemptId = Id<DeploymentAttemptIdTag>;
using ActivationId = Id<ActivationIdTag>;
using ExecutionContextId = Id<ExecutionContextIdTag>;
using ResourceContextId = Id<ResourceContextIdTag>;
using PortId = Id<PortIdTag>;
using FunctionId = Id<FunctionIdTag>;
using QueueId = Id<QueueIdTag>;
using TenantId = Id<TenantIdTag>;
using IsolationDomainId = Id<IsolationDomainIdTag>;
using WorkerId = Id<WorkerIdTag>;
using LeaseId = Id<LeaseIdTag>;
using CapabilityId = Id<CapabilityIdTag>;

using DeviceGeneration = Id<DeviceGenerationTag>;
using ServiceGeneration = Id<ServiceGenerationTag>;
using ProgramGeneration = Id<ProgramGenerationTag>;
using ArtifactGeneration = Id<ArtifactGenerationTag>;
using DeploymentGeneration = Id<DeploymentGenerationTag>;
using ActivationGeneration = Id<ActivationGenerationTag>;
using EvidenceGeneration = Id<EvidenceGenerationTag>;
using PolicyGeneration = Id<PolicyGenerationTag>;
using CoordinatorEpoch = Id<CoordinatorEpochTag>;
using WorkerBootId = Id<WorkerBootIdTag>;
using DeviceBootId = Id<DeviceBootIdTag>;
using FirmwareGeneration = Id<FirmwareGenerationTag>;
using RuntimeGeneration = Id<RuntimeGenerationTag>;
using BootGeneration = Id<BootGenerationTag>;

constexpr uint64_t next_generation(uint64_t cur) noexcept {
    return (cur == std::numeric_limits<uint64_t>::max()) ? cur : cur + 1;
}

} // namespace dpufabric

namespace std {
template <typename Tag>
struct hash<dpufabric::Id<Tag>> {
    std::size_t operator()(const dpufabric::Id<Tag>& id) const noexcept {
        return std::hash<uint64_t>{}(id.as_u64());
    }
};
} // namespace std
