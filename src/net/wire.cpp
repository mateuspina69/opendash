// The netplay messages' byte layout (see wire.hpp).
#include "net/wire.hpp"

#include <algorithm>
#include <stdexcept>

namespace od::net {

void ByteWriter::string(const std::string& s) {
    const size_t n = std::min(s.size(), kMaxString);
    u8(uint8_t(n));
    bytes_.insert(bytes_.end(), s.begin(), s.begin() + std::ptrdiff_t(n));
}

void ByteWriter::blob(std::span<const uint8_t> b) {
    if (b.size() > 0xffff) throw std::length_error("net: a blob over 65535 bytes");
    u16(uint16_t(b.size()));
    bytes_.insert(bytes_.end(), b.begin(), b.end());
}

uint64_t ByteReader::get(size_t n) {
    if (!ok_ || bytes_.size() - pos_ < n) {
        ok_ = false;
        return 0;
    }
    uint64_t v = 0;
    for (size_t i = 0; i < n; ++i) v |= uint64_t(bytes_[pos_ + i]) << (8 * i);
    pos_ += n;
    return v;
}

bool ByteReader::boolean() {
    const uint8_t v = u8();
    if (v > 1) ok_ = false;
    return v == 1;
}

std::string ByteReader::string() {
    const size_t n = u8();
    if (!ok_ || bytes_.size() - pos_ < n) {
        ok_ = false;
        return {};
    }
    std::string s(reinterpret_cast<const char*>(bytes_.data() + pos_), n);
    pos_ += n;
    return s;
}

std::vector<uint8_t> ByteReader::blob() {
    const size_t n = u16();
    if (!ok_ || bytes_.size() - pos_ < n) {
        ok_ = false;
        return {};
    }
    std::vector<uint8_t> b(bytes_.begin() + std::ptrdiff_t(pos_), bytes_.begin() + std::ptrdiff_t(pos_ + n));
    pos_ += n;
    return b;
}

} // namespace od::net
