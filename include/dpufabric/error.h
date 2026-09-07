/// DPU Fabric: typed error codes and exception type.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <stdexcept>
#include <optional>
#include <array>

namespace dpufabric {

// Severity is intentionally kept out of public error codes so that the same
// condition maps to the same code regardless of caller policy.
enum class ErrorCode : uint16_t {
    OK = 0,
    NOT_FOUND = 1,
    ALREADY_EXISTS = 2,
    INVALID_ARGUMENT = 3,
    INVALID_STATE = 4,
    INVALID_TRANSITION = 5,
    UNSUPPORTED = 6,
    UNKNOWN_CAPABILITY = 7,
    CAPABILITY_STALE = 8,
    CAPABILITY_MISMATCH = 9,
    DEVICE_NOT_READY = 10,
    DEVICE_FAILED = 11,
    DEVICE_DRAINING = 12,
    DEVICE_REVALIDATION_REQUIRED = 13,
    FIRMWARE_INCOMPATIBLE = 14,
    RUNTIME_INCOMPATIBLE = 15,
    ARTIFACT_INCOMPATIBLE = 16,
    ISOLATION_MISMATCH = 17,
    TENANT_MISMATCH = 18,
    RESOURCE_EXHAUSTED = 19,
    NO_ELIGIBLE_DEVICE = 20,
    OFFLOAD_REQUIRED_UNAVAILABLE = 21,
    STALE_EPOCH = 22,
    STALE_WORKER = 23,
    STALE_DEVICE_BOOT = 24,
    STALE_DEVICE_GENERATION = 25,
    STALE_SERVICE_GENERATION = 26,
    STALE_PROGRAM_GENERATION = 27,
    STALE_DEPLOYMENT = 28,
    STALE_ACTIVATION = 29,
    AUTHORITY_REJECTED = 30,
    CHECKSUM_FAILURE = 31,
    CORRUPT_STATE = 32,
    PROTOCOL_ERROR = 33,
    FRAME_TOO_LARGE = 34,
    IO_ERROR = 35,
    BACKEND_ERROR = 36,
    CANCELLED = 37,
    END_MARKER = 38   // must stay last; used for bounds and round-trip tests
};

constexpr std::array<ErrorCode, static_cast<size_t>(ErrorCode::END_MARKER)> all_error_codes() noexcept {
    std::array<ErrorCode, static_cast<size_t>(ErrorCode::END_MARKER)> a{};
    size_t i = 0;
    for (uint16_t v = 0; v < static_cast<uint16_t>(ErrorCode::END_MARKER); ++v) {
        a[i++] = static_cast<ErrorCode>(v);
    }
    return a;
}

inline bool is_valid_error(ErrorCode c) noexcept {
    uint16_t v = static_cast<uint16_t>(c);
    return v < static_cast<uint16_t>(ErrorCode::END_MARKER);
}

constexpr const char* error_code_name(ErrorCode c) noexcept {
    switch (c) {
        case ErrorCode::OK: return "OK";
        case ErrorCode::NOT_FOUND: return "NOT_FOUND";
        case ErrorCode::ALREADY_EXISTS: return "ALREADY_EXISTS";
        case ErrorCode::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case ErrorCode::INVALID_STATE: return "INVALID_STATE";
        case ErrorCode::INVALID_TRANSITION: return "INVALID_TRANSITION";
        case ErrorCode::UNSUPPORTED: return "UNSUPPORTED";
        case ErrorCode::UNKNOWN_CAPABILITY: return "UNKNOWN_CAPABILITY";
        case ErrorCode::CAPABILITY_STALE: return "CAPABILITY_STALE";
        case ErrorCode::CAPABILITY_MISMATCH: return "CAPABILITY_MISMATCH";
        case ErrorCode::DEVICE_NOT_READY: return "DEVICE_NOT_READY";
        case ErrorCode::DEVICE_FAILED: return "DEVICE_FAILED";
        case ErrorCode::DEVICE_DRAINING: return "DEVICE_DRAINING";
        case ErrorCode::DEVICE_REVALIDATION_REQUIRED: return "DEVICE_REVALIDATION_REQUIRED";
        case ErrorCode::FIRMWARE_INCOMPATIBLE: return "FIRMWARE_INCOMPATIBLE";
        case ErrorCode::RUNTIME_INCOMPATIBLE: return "RUNTIME_INCOMPATIBLE";
        case ErrorCode::ARTIFACT_INCOMPATIBLE: return "ARTIFACT_INCOMPATIBLE";
        case ErrorCode::ISOLATION_MISMATCH: return "ISOLATION_MISMATCH";
        case ErrorCode::TENANT_MISMATCH: return "TENANT_MISMATCH";
        case ErrorCode::RESOURCE_EXHAUSTED: return "RESOURCE_EXHAUSTED";
        case ErrorCode::NO_ELIGIBLE_DEVICE: return "NO_ELIGIBLE_DEVICE";
        case ErrorCode::OFFLOAD_REQUIRED_UNAVAILABLE: return "OFFLOAD_REQUIRED_UNAVAILABLE";
        case ErrorCode::STALE_EPOCH: return "STALE_EPOCH";
        case ErrorCode::STALE_WORKER: return "STALE_WORKER";
        case ErrorCode::STALE_DEVICE_BOOT: return "STALE_DEVICE_BOOT";
        case ErrorCode::STALE_DEVICE_GENERATION: return "STALE_DEVICE_GENERATION";
        case ErrorCode::STALE_SERVICE_GENERATION: return "STALE_SERVICE_GENERATION";
        case ErrorCode::STALE_PROGRAM_GENERATION: return "STALE_PROGRAM_GENERATION";
        case ErrorCode::STALE_DEPLOYMENT: return "STALE_DEPLOYMENT";
        case ErrorCode::STALE_ACTIVATION: return "STALE_ACTIVATION";
        case ErrorCode::AUTHORITY_REJECTED: return "AUTHORITY_REJECTED";
        case ErrorCode::CHECKSUM_FAILURE: return "CHECKSUM_FAILURE";
        case ErrorCode::CORRUPT_STATE: return "CORRUPT_STATE";
        case ErrorCode::PROTOCOL_ERROR: return "PROTOCOL_ERROR";
        case ErrorCode::FRAME_TOO_LARGE: return "FRAME_TOO_LARGE";
        case ErrorCode::IO_ERROR: return "IO_ERROR";
        case ErrorCode::BACKEND_ERROR: return "BACKEND_ERROR";
        case ErrorCode::CANCELLED: return "CANCELLED";
        case ErrorCode::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}

// Parse an ErrorCode from its stable text name.  Returns nullopt on unknown.
inline std::optional<ErrorCode> error_code_from_name(std::string_view s) {
    for (ErrorCode c : all_error_codes()) {
        if (s == error_code_name(c)) return c;
    }
    return std::nullopt;
}

// Typed error carrying a stable code plus a human-readable detail.
class DpuError final : public std::runtime_error {
public:
    explicit DpuError(ErrorCode code, std::string message = {})
        : std::runtime_error(message.empty() ? error_code_name(code) : message),
          code_(code), message_(message.empty() ? error_code_name(code) : std::move(message)) {}

    ErrorCode code() const noexcept { return code_; }
    const std::string& detail() const noexcept { return message_; }
    bool is(ErrorCode c) const noexcept { return code_ == c; }

private:
    ErrorCode code_;
    std::string message_;
};

// Convenience throw helpers.
[[noreturn]] inline void throw_error(ErrorCode code, std::string msg = {}) {
    throw DpuError(code, std::move(msg));
}

} // namespace dpufabric
