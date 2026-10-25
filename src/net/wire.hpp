// Little-endian byte layout of the netplay messages: a writer that appends fields and a bounds-checked reader. A short
// read fails the whole message (ok() turns false and every later read returns 0); it never reads past the end.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace od::net {

class ByteWriter {
public:
    void u8(uint8_t v) { bytes_.push_back(v); }
    void u16(uint16_t v) { put(v, 2); }
    void u32(uint32_t v) { put(v, 4); }
    void u64(uint64_t v) { put(v, 8); }
    void i8(int8_t v) { u8(uint8_t(v)); }
    void i16(int16_t v) { u16(uint16_t(v)); }
    void i32(int32_t v) { u32(uint32_t(v)); }
    void boolean(bool v) { u8(v ? 1 : 0); }
    // a u8 length then the characters (at most kMaxString; longer text is cut there)
    void string(const std::string& s);
    // a u16 length then the bytes (std::length_error over 0xffff)
    void blob(std::span<const uint8_t> b);
    void raw(std::span<const uint8_t> b) { bytes_.insert(bytes_.end(), b.begin(), b.end()); }

    const std::vector<uint8_t>& bytes() const { return bytes_; }
    std::vector<uint8_t> take() { return std::move(bytes_); }

    static constexpr size_t kMaxString = 255;

private:
    void put(uint64_t v, int n) {
        for (int i = 0; i < n; ++i) bytes_.push_back(uint8_t(v >> (8 * i)));
    }
    std::vector<uint8_t> bytes_;
};

class ByteReader {
public:
    explicit ByteReader(std::span<const uint8_t> bytes) : bytes_(bytes) {}

    uint8_t u8() { return uint8_t(get(1)); }
    uint16_t u16() { return uint16_t(get(2)); }
    uint32_t u32() { return uint32_t(get(4)); }
    uint64_t u64() { return get(8); }
    int8_t i8() { return int8_t(u8()); }
    int16_t i16() { return int16_t(u16()); }
    int32_t i32() { return int32_t(u32()); }
    // 0 or 1; any other byte fails the message
    bool boolean();
    std::string string();
    std::vector<uint8_t> blob();

    // every read so far was inside the bytes (and every boolean was 0 / 1)
    bool ok() const { return ok_; }
    // ok and nothing left over: the message had exactly its fields
    bool done() const { return ok_ && pos_ == bytes_.size(); }
    size_t remaining() const { return ok_ ? bytes_.size() - pos_ : 0; }
    void fail() { ok_ = false; }

private:
    uint64_t get(size_t n);
    std::span<const uint8_t> bytes_;
    size_t pos_ = 0;
    bool ok_ = true;
};

} // namespace od::net
