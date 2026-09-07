#include "testharness.h"
#include "dpufabric/protocol.h"
#include "dpufabric/wire.h"
#include <vector>
#include <cstring>

using namespace dpufabric;
using namespace dpufabric::proto;

DPUFABRIC_TEST(frame_roundtrip) {
    std::vector<uint8_t> payload{ 1, 2, 3, 4, 5 };
    auto bytes = encode(MsgType::HELLO, payload);
    auto f = decode(bytes);
    CHECK(f.type == MsgType::HELLO);
    CHECK(f.payload == payload);
    CHECK(f.checksum == wire::Crc32::compute(payload));
}

DPUFABRIC_TEST(frame_zero_length_payload) {
    auto bytes = encode(MsgType::PING, {});
    auto f = decode(bytes);
    CHECK(f.type == MsgType::PING);
    CHECK(f.payload.empty());
}

DPUFABRIC_TEST(frame_oversize_rejected) {
    std::vector<uint8_t> big(kMaxPayload + 1, 0);
    CHECK_THROWS_CODE(encode(MsgType::COMMAND, big), ErrorCode::FRAME_TOO_LARGE);
}

DPUFABRIC_TEST(frame_truncated_rejected) {
    auto bytes = encode(MsgType::HELLO, { 1, 2, 3 });
    std::vector<uint8_t> trunc(bytes.begin(), bytes.begin() + 8);
    CHECK_THROWS_CODE(decode(trunc), ErrorCode::PROTOCOL_ERROR);
}

DPUFABRIC_TEST(frame_short_buffer_rejected) {
    std::vector<uint8_t> tiny{ 1, 2, 3 };
    CHECK_THROWS_CODE(decode(tiny), ErrorCode::PROTOCOL_ERROR);
}

DPUFABRIC_TEST(frame_bad_magic_rejected) {
    auto bytes = encode(MsgType::HELLO, { 1 });
    bytes[0] = 0x00; bytes[1] = 0x00; bytes[2] = 0x00; bytes[3] = 0x00;
    CHECK_THROWS_CODE(decode(bytes), ErrorCode::PROTOCOL_ERROR);
}

DPUFABRIC_TEST(frame_bad_version_rejected) {
    auto bytes = encode(MsgType::HELLO, { 1 });
    bytes[4] = 0xFF; bytes[5] = 0xFF;
    CHECK_THROWS_CODE(decode(bytes), ErrorCode::PROTOCOL_ERROR);
}

DPUFABRIC_TEST(frame_checksum_corruption_rejected) {
    auto bytes = encode(MsgType::HELLO, { 1, 2, 3, 4 });
    bytes[bytes.size() - 1] = static_cast<uint8_t>(bytes[bytes.size() - 1] ^ 0x55);   // payload byte
    CHECK_THROWS_CODE(decode(bytes), ErrorCode::CHECKSUM_FAILURE);
}

DPUFABRIC_TEST(frame_trailing_bytes_rejected) {
    auto bytes = encode(MsgType::HELLO, { 1, 2 });
    bytes.push_back(0xAA);   // trailing garbage
    CHECK_THROWS_CODE(decode(bytes), ErrorCode::PROTOCOL_ERROR);
}

DPUFABRIC_TEST(frame_oversize_declared_length_rejected) {
    // Hand-build a header declaring a huge payload length but with a tiny buffer.
    auto bytes = encode(MsgType::HELLO, { 1, 2 });
    // overwrite payload_len (bytes 8..11) with a huge value
    bytes[8] = 0xFF; bytes[9] = 0xFF; bytes[10] = 0xFF; bytes[11] = 0xFF;
    CHECK_THROWS_CODE(decode(bytes), ErrorCode::FRAME_TOO_LARGE);
}

DPUFABRIC_TEST(valid_type_and_unknown) {
    CHECK(valid_type(1));
    CHECK(valid_type(11));
    CHECK(!valid_type(0));
    CHECK(!valid_type(999));
}

DPUFABRIC_MAIN