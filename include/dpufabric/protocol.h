/// DPU Fabric: framed wire transport (versioned, checksummed, bounded).
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>
#include <string>
#include <string_view>
#include <initializer_list>
#include "dpufabric/wire.h"
#include "dpufabric/error.h"

namespace dpufabric {
namespace proto {

// Fixed header: magic(4) version(2) type(2) payload_len(4) crc(4) = 16 bytes.
constexpr uint32_t kMagic = 0x44504657;        // "DPFW" little-endian
constexpr uint16_t kVersion = 1;
constexpr size_t kHeaderSize = 16;
constexpr uint32_t kMaxPayload = 4u * 1024u * 1024u;  // 4 MiB
constexpr uint32_t kMaxFrameSize = kHeaderSize + kMaxPayload;

// Message types.  UNKNOWN is used only for hostile-input tests.
enum class MsgType : uint16_t {
    HELLO = 1,
    PING = 2,
    PONG = 3,
    REGISTER_DEVICE = 4,
    PUBLISH_EVIDENCE = 5,
    COMMAND = 6,
    RESULT = 7,
    ERROR_MSG = 8,
    ACK = 9,
    NOTIFY = 10,
    BYE = 11,
    UNKNOWN_TYPE = 999,
    END_MARKER = 1000
};
constexpr const char* msg_type_name(MsgType t) noexcept {
    switch (t) {
        case MsgType::HELLO: return "HELLO";
        case MsgType::PING: return "PING";
        case MsgType::PONG: return "PONG";
        case MsgType::REGISTER_DEVICE: return "REGISTER_DEVICE";
        case MsgType::PUBLISH_EVIDENCE: return "PUBLISH_EVIDENCE";
        case MsgType::COMMAND: return "COMMAND";
        case MsgType::RESULT: return "RESULT";
        case MsgType::ERROR_MSG: return "ERROR";
        case MsgType::ACK: return "ACK";
        case MsgType::NOTIFY: return "NOTIFY";
        case MsgType::BYE: return "BYE";
        case MsgType::UNKNOWN_TYPE: return "UNKNOWN_TYPE";
        case MsgType::END_MARKER: return "END_MARKER";
    }
    return "UNKNOWN";
}

// A decoded frame.  payload is exactly the message body.
struct Frame {
    MsgType type = MsgType::UNKNOWN_TYPE;
    std::vector<uint8_t> payload;
    uint32_t checksum = 0;
};

// Encode a frame into bytes.  Throws on oversize payload.
inline std::vector<uint8_t> encode(MsgType type, std::span<const uint8_t> payload) {
    if (payload.size() > kMaxPayload) throw_error(ErrorCode::FRAME_TOO_LARGE, "payload exceeds frame bound");
    wire::Writer w;
    w.u32(kMagic);
    w.u16(kVersion);
    w.u16(static_cast<uint16_t>(type));
    w.u32(static_cast<uint32_t>(payload.size()));
    w.u32(wire::Crc32::compute(payload));
    w.raw(std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size()));
    return std::vector<uint8_t>(w.data().begin(), w.data().end());
}

// Convenience overload for initializer-list payloads.
inline std::vector<uint8_t> encode(MsgType type, std::initializer_list<uint8_t> il) {
    std::vector<uint8_t> v(il);
    return encode(type, v);
}

// Decode a single frame.  Requires that the buffer is exactly one frame
// (trailing bytes are rejected).  Throws a typed error on any malformed,
// truncated, oversize, bad-magic, bad-version, or bad-checksum input.
inline Frame decode(std::span<const uint8_t> buf) {
    if (buf.size() < kHeaderSize) throw_error(ErrorCode::PROTOCOL_ERROR, "frame shorter than header");
    if (buf.size() > kMaxFrameSize) throw_error(ErrorCode::FRAME_TOO_LARGE, "frame exceeds bound");
    wire::Reader r(buf);
    uint32_t magic = r.u32();
    if (magic != kMagic) throw_error(ErrorCode::PROTOCOL_ERROR, "bad frame magic");
    uint16_t ver = r.u16();
    if (ver != kVersion) throw_error(ErrorCode::PROTOCOL_ERROR, "unsupported frame version");
    uint16_t raw_type = r.u16();
    uint32_t plen = r.u32();
    if (plen > kMaxPayload) throw_error(ErrorCode::FRAME_TOO_LARGE, "payload length exceeds bound");
    if (plen > r.remaining() - 4u) throw_error(ErrorCode::PROTOCOL_ERROR, "truncated payload");
    uint32_t declared_crc = r.u32();
    std::span<const uint8_t> payload = r.raw(plen);
    if (payload.size() != plen) throw_error(ErrorCode::PROTOCOL_ERROR, "payload length mismatch");
    if (wire::Crc32::compute(payload) != declared_crc) throw_error(ErrorCode::CHECKSUM_FAILURE, "frame checksum mismatch");
    r.expect_end();  // trailing bytes rejected
    Frame f;
    f.type = static_cast<MsgType>(raw_type);
    f.checksum = declared_crc;
    f.payload.assign(payload.begin(), payload.end());
    return f;
}

// Message type validation from the wire: reject unknown enum values.
inline bool valid_type(uint16_t t) noexcept {
    return t == 1 || t == 2 || t == 3 || t == 4 || t == 5 || t == 6 || t == 7 ||
           t == 8 || t == 9 || t == 10 || t == 11;
}

} // namespace proto
} // namespace dpufabric
