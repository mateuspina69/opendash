// The state visited_rooms.cpp keeps (its classes', its script parts, its globals): the members and the exe
// addresses they stand for.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port::visited_rooms_state {

// The route search's scratch (exe globals): the path from the start (its depth), the branches still to try (a stack,
// its top), the shortest path to the target found so far (its depth). Entries: depth << 24 | room key; kNoEntry free
struct RouteSearch {
    StateArray<uint32_t, 0x20> path{}; // 0xa5a5c0
    uint32_t depth = 0; // 0xa5a5b8
    StateArray<uint32_t, 0x20> branches{}; // 0xa5a538
    int32_t branchTop = 0; // 0xa5a6c0: -1: none
    StateArray<uint32_t, 0x20> best{}; // 0xa5a640
    uint32_t bestDepth = 0; // 0xa5a5bc
};
inline RouteSearch& routeSearch() {
    static RouteSearch s;
    return s;
}

} // namespace od::port::visited_rooms_state
