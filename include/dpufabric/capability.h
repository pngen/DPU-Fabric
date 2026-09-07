/// DPU Fabric: typed capability model.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <map>
#include "dpufabric/id.h"
#include "dpufabric/types.h"
#include "dpufabric/error.h"

namespace dpufabric {

// A capability key is a validated vendor-neutral name (for example
// "ENCRYPTION", "PACKET_STEERING", "COMPRESSION").  It is a distinct type so
// that a capability key cannot be silently used where a capability record id
// is expected, and so that garbage strings are rejected at construction.
class CapabilityKey {
public:
    static constexpr size_t kMaxLen = 64;

    CapabilityKey() = default;
    explicit CapabilityKey(std::string s) : value_(validate(std::move(s))) {}
    CapabilityKey(const char* s) : value_(validate(std::string(s))) {}

    const std::string& str() const noexcept { return value_; }
    bool empty() const noexcept { return value_.empty(); }

    bool operator==(const CapabilityKey& o) const noexcept { return value_ == o.value_; }
    bool operator!=(const CapabilityKey& o) const noexcept { return value_ != o.value_; }
    bool operator<(const CapabilityKey& o) const noexcept { return value_ < o.value_; }
    bool operator<(std::string_view o) const noexcept { return value_ < o; }

private:
    static std::string validate(std::string s) {
        if (!is_valid(s)) throw_error(ErrorCode::INVALID_ARGUMENT, "invalid capability key");
        return s;
    }
public:
    static bool is_valid(std::string_view s) noexcept {
        if (s.empty() || s.size() > kMaxLen) return false;
        for (char c : s) {
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
        }
        return true;
    }
    std::string value_;
};

// A single capability record as reported by evidence.
struct Capability {
    CapabilityKey key;
    CapabilityState state = CapabilityState::UNKNOWN;
    // Generation/freshness of the evidence that established this capability.
    EvidenceGeneration evidence_gen;
    uint64_t version = 0;              // capability semantic version
    // Required firmware/runtime level (0 = not specified).
    FirmwareGeneration min_firmware_gen;
    RuntimeGeneration min_runtime_gen;
    // Scale bound: maximum supported representation the capability can cover.
    uint64_t max_scale = 0;            // 0 = unspecified
    // Isolation properties the capability itself requires.
    bool requires_isolation = false;
    // Estimated acquisition cost across devices; used only as a hint, never
    // as an authority override.
    ProvenanceSource source = ProvenanceSource::CONFIGURED;
    std::string detail;                // provenance/debug text

    // A hard requirement is satisfied only when state == SUPPORTED and the
    // evidence is fresh (not REVALIDATION_REQUIRED) and, if firmware/runtime
    // floors were recorded, the device meets them (checked by the caller).
    bool satisfies_hard() const noexcept {
        return state == CapabilityState::SUPPORTED;
    }
};

// A capability requirement with a required/optional flag.
struct CapabilityRequirement {
    CapabilityKey key;
    // Optional capabilities do not block: a device lacking one is merely
    // ranked lower, never rejected for its absence.
    bool optional = false;
    uint64_t min_version = 0;
};

// Ordered set of capability records keyed by capability key.
class CapabilitySet {
public:
    CapabilityState state_of(const CapabilityKey& k) const noexcept {
        auto it = map_.find(k);
        return it == map_.end() ? CapabilityState::UNSUPPORTED : it->second.state;
    }
    bool supports(const CapabilityKey& k) const noexcept {
        return state_of(k) == CapabilityState::SUPPORTED;
    }
    const Capability* find(const CapabilityKey& k) const noexcept {
        auto it = map_.find(k);
        return it == map_.end() ? nullptr : &it->second;
    }
    void add(Capability c) { map_[c.key] = std::move(c); }
    void remove(const CapabilityKey& k) { map_.erase(k); }
    void set_state(const CapabilityKey& k, CapabilityState s) {
        auto it = map_.find(k);
        if (it == map_.end()) {
            if (s != CapabilityState::SUPPORTED) return; // absence is already UNSUPPORTED
            Capability c; c.key = k; c.state = s; map_[k] = std::move(c);
        } else {
            it->second.state = s;
        }
    }
    size_t size() const noexcept { return map_.size(); }
    bool empty() const noexcept { return map_.empty(); }
    const std::map<CapabilityKey, Capability>& all() const noexcept { return map_; }
    std::map<CapabilityKey, Capability>& mutable_all() noexcept { return map_; }

    // A capability that is present but not SUPPORTED must fail closed for hard
    // requirements.  UNKNOWN or REVALIDATION_REQUIRED are not permission.
    bool authoritative(const CapabilityKey& k) const noexcept {
        auto it = map_.find(k);
        return it != map_.end() && it->second.state == CapabilityState::SUPPORTED;
    }

private:
    std::map<CapabilityKey, Capability> map_;
};

} // namespace dpufabric
