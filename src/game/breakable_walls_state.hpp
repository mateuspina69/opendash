// The state breakable_walls.cpp keeps (its classes', its script parts, its globals): the members and the exe
// addresses they stand for.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port::breakable_walls_state {

// The hits queued this frame and the damage counters (the segments' own, by segment; the shared ones, by index)
struct BreakableWallState {
    uint16_t queuedHits = 0; // 0xa5a77c: the queue's length (the high half of the dword is kept)
    StateArray<uint32_t, 0x40> queue{}; // 0xa5a780: attack << 16 | the segment
    StateArray<uint16_t, 0x40> damage{}; // 0xa5a6fc: the damage summed this frame per segment
    StateArray<uint16_t, 0x40> sharedDamage{}; // 0xa5a880: ... per shared counter (kBreakSharedCounter)
};
inline BreakableWallState& state() {
    static BreakableWallState s;
    return s;
}

} // namespace od::port::breakable_walls_state
