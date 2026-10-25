// Player goldens: "PLG2", or "PLG3" = PLG2 + the actor collision pass capture (pass ranges after the
// ranges, then static blocks; each record ends with the pass ranges). Tests that only use the PLG2 part
// call plgSkipPass after reading the ranges.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "engine/dat.hpp"

inline bool plgMagic(const std::vector<uint8_t>& f) {
    return f.size() > 16 && (std::memcmp(f.data(), "PLG2", 4) == 0 || std::memcmp(f.data(), "PLG3", 4) == 0);
}
// p = just after the ranges: moves p to the first record and returns the extra bytes per record
inline size_t plgSkipPass(const std::vector<uint8_t>& f, size_t& p) {
    if (std::memcmp(f.data(), "PLG3", 4) != 0) return 0;
    size_t extra = 0;
    const uint32_t np = od::rd32(f.data() + p);
    p += 4;
    for (uint32_t i = 0; i < np; ++i, p += 8) extra += od::rd32(f.data() + p + 4);
    const uint32_t ns = od::rd32(f.data() + p);
    p += 4;
    for (uint32_t i = 0; i < ns; ++i) p += 8 + od::rd32(f.data() + p + 4);
    return extra;
}
