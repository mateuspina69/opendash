// The stage's alternate model banks as a session sends them (see stage_banks.hpp).
#include "app/stage_banks.hpp"

namespace od::app {

namespace {
void put32(std::vector<uint8_t>& o, uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i)));
}
} // namespace

std::vector<uint8_t> encodeStageBanks(const StageBanks& b) {
    std::vector<uint8_t> o{b.stage, b.bankBuffer};
    for (const std::string& n : b.names) {
        o.push_back(uint8_t(n.size()));
        o.insert(o.end(), n.begin(), n.end());
    }
    put32(o, b.modelBank);
    put32(o, b.stageAnims);
    put32(o, b.stageAnimsExtra);
    return o;
}

std::optional<StageBanks> decodeStageBanks(std::span<const uint8_t> bytes) {
    size_t at = 0;
    const auto left = [&] { return bytes.size() - at; };
    if (left() < 2) return std::nullopt;
    StageBanks b;
    b.stage = bytes[at++];
    b.bankBuffer = bytes[at++];
    for (std::string& n : b.names) {
        if (left() < 1) return std::nullopt;
        const size_t len = bytes[at++];
        if (left() < len) return std::nullopt;
        n.assign(bytes.begin() + ptrdiff_t(at), bytes.begin() + ptrdiff_t(at + len));
        at += len;
    }
    if (left() != 12) return std::nullopt;
    const auto get32 = [&] {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= uint32_t(bytes[at++]) << (8 * i);
        return v;
    };
    b.modelBank = get32();
    b.stageAnims = get32();
    b.stageAnimsExtra = get32();
    return b;
}

} // namespace od::app
