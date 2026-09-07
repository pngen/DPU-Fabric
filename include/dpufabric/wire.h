/// DPU Fabric: bounded binary wire writer/reader and CRC32.
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <string_view>
#include <span>
#include "dpufabric/error.h"

namespace dpufabric {
namespace wire {

// Maximum lengths we are willing to accept from any untrusted source.
constexpr size_t kMaxStringBytes = 1u << 20;      // 1 MiB
constexpr size_t kMaxContainerCount = 1u << 20;   // ~1M entries
constexpr size_t kMaxBlobBytes = 1u << 26;        // 64 MiB

// Little-endian fixed writes.
class Writer {
public:
    Writer() { buf_.reserve(256); }

    void u8(uint8_t v) { buf_.push_back(static_cast<char>(v)); }
    void u16(uint16_t v) { u8(static_cast<uint8_t>(v & 0xFF)); u8(static_cast<uint8_t>((v >> 8) & 0xFF)); }
    void u32(uint32_t v) { u16(static_cast<uint16_t>(v & 0xFFFF)); u16(static_cast<uint16_t>((v >> 16) & 0xFFFF)); }
    void u64(uint64_t v) { u32(static_cast<uint32_t>(v & 0xFFFFFFFFu)); u32(static_cast<uint32_t>((v >> 32) & 0xFFFFFFFFu)); }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }

    void raw(std::string_view s) { buf_.insert(buf_.end(), s.begin(), s.end()); }
    void bytes(std::span<const uint8_t> b) { buf_.insert(buf_.end(), b.begin(), b.end()); }

    // length-prefixed string (u32 length then bytes).  Rejects absurd lengths.
    void str(std::string_view s) {
        if (s.size() > kMaxStringBytes) throw_error(ErrorCode::INVALID_ARGUMENT, "string too long to encode");
        u32(static_cast<uint32_t>(s.size()));
        raw(s);
    }
    // length-prefixed blob (u32 length then bytes).
    void blob(std::string_view s) { str(s); }

    const std::vector<char>& data() const noexcept { return buf_; }
    std::vector<char> take() && { return std::move(buf_); }
    size_t size() const noexcept { return buf_.size(); }

private:
    std::vector<char> buf_;
};

// Bounds-checked little-endian reads.  Any over-read or absurd length throws a
// typed error; it never performs one-past-the-end access or wraps counts.
class Reader {
public:
    explicit Reader(std::span<const uint8_t> data) : data_(data) {}
    explicit Reader(const std::vector<char>& v)
        : data_(reinterpret_cast<const uint8_t*>(v.data()), v.size()) {}
    explicit Reader(std::string_view sv)
        : data_(reinterpret_cast<const uint8_t*>(sv.data()), sv.size()) {}

    bool at_end() const noexcept { return pos_ >= data_.size(); }
    size_t remaining() const noexcept { return data_.size() - pos_; }
    size_t position() const noexcept { return pos_; }

    uint8_t u8() {
        require(1);
        return data_[pos_++];
    }
    uint16_t u16() {
        require(2);
        uint16_t lo = data_[pos_], hi = data_[pos_ + 1];
        pos_ += 2;
        return static_cast<uint16_t>(lo | (hi << 8));
    }
    uint32_t u32() {
        require(4);
        uint32_t v = static_cast<uint32_t>(data_[pos_]) |
                     (static_cast<uint32_t>(data_[pos_ + 1]) << 8) |
                     (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
                     (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return v;
    }
    uint64_t u64() {
        uint64_t lo = u32(), hi = u32();
        return lo | (hi << 32);
    }
    int32_t i32() { return static_cast<int32_t>(u32()); }

    // Read a length-prefixed string.  Rejects lengths exceeding the bound, then
    // reads exactly that many bytes.
    std::string str() {
        uint32_t len = u32();
        if (len > kMaxStringBytes) throw_error(ErrorCode::INVALID_ARGUMENT, "string length exceeds bound");
        require(len);
        std::string s(reinterpret_cast<const char*>(data_.data() + pos_), len);
        pos_ += len;
        return s;
    }
    std::vector<uint8_t> blob() {
        uint32_t len = u32();
        if (len > kMaxBlobBytes) throw_error(ErrorCode::INVALID_ARGUMENT, "blob length exceeds bound");
        require(len);
        std::vector<uint8_t> out(len);
        for (uint32_t i = 0; i < len; ++i) out[i] = data_[pos_ + i];
        pos_ += len;
        return out;
    }

    std::span<const uint8_t> raw(size_t n) {
        require(n);
        std::span<const uint8_t> s(data_.data() + pos_, n);
        pos_ += n;
        return s;
    }

    // Validate that the reader is exactly at the end (rejects trailing bytes).
    void expect_end() const {
        if (!at_end()) throw_error(ErrorCode::PROTOCOL_ERROR, "trailing bytes after message");
    }

private:
    void require(size_t n) const {
        if (n > remaining()) throw_error(ErrorCode::PROTOCOL_ERROR, "truncated message");
    }
    std::span<const uint8_t> data_;
    size_t pos_ = 0;
};

// CRC-32 (IEEE 802.3, reflected) used only for integrity (not authentication).
class Crc32 {
public:
    static uint32_t compute(std::span<const uint8_t> data, uint32_t seed = 0xFFFFFFFFu) noexcept {
        uint32_t crc = seed;
        for (uint8_t b : data) {
            crc ^= b;
            for (int k = 0; k < 8; ++k) {
                uint32_t mask = static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1)));
                crc = (crc >> 1) ^ (0xEDB88320u & mask);
            }
        }
        return ~crc;
    }
    static uint32_t compute(std::string_view sv, uint32_t seed = 0xFFFFFFFFu) noexcept {
        return compute(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(sv.data()), sv.size()), seed);
    }
};

} // namespace wire
} // namespace dpufabric
