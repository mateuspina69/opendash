// The state glyde_base_scripts.cpp keeps (its classes', its script parts, its globals): the members and the exe
// addresses they stand for.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port::glyde_base_scripts_state {

struct TowerFallState {
    StateArray<uint32_t, 0x10> smokes{}; // 0xa59f54: the emitters kept (the fall's end drains them)
    int16_t tick = 0; // 0xa59f94
    uint16_t nextEvent = 0; // 0xa59f96
    uint32_t smokeCount = 0; // 0xa59f98
};
inline TowerFallState& towerFall() {
    static TowerFallState s;
    return s;
}

} // namespace od::port::glyde_base_scripts_state
