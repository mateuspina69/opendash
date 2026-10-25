// The loaded-data fingerprint of the ghosts (see loaded_data_fingerprint.hpp).
#include "game/loaded_data_fingerprint.hpp"

namespace od::port {

namespace {
constexpr uint64_t kFnvBasis = 0xcbf29ce484222325ull, kFnvPrime = 0x100000001b3ull; // FNV-1a 64
} // namespace

uint64_t loadedDataFingerprint(const LoadedDataIdentity& id) {
    uint64_t h = kFnvBasis;
    auto byte = [&](uint8_t b) {
        h ^= b;
        h *= kFnvPrime;
    };
    auto add = [&](const std::string& s) {
        for (const char c : s) byte(uint8_t(c));
        byte(0); // the separator
    };
    add(id.stageGeometry);
    add(id.stageTexture);
    for (const std::string& b : id.altBanks) add(b);
    return h;
}

} // namespace od::port
