// A digest of a world's state, to compare two runs of the same room tick by tick (a world process against the windowed
// game): the game's mode and place, both random generators, the players in the session
// and every live actor of the pools.
#pragma once

#include <cstdint>

namespace od::port {

struct WorldDigest {
    uint64_t hash = 0;
    uint32_t liveActors = 0;
};
// `detail` (when not null): called with one line per item the hash took in, to find where two runs differ
WorldDigest worldDigest(void (*detail)(const char* line) = nullptr);

} // namespace od::port
