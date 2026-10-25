// The ST54 take-off arc (pool 0x20 type 0x72 sub 1, exe 0x50dfc0), content/late_game/st54_takeoff.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x50dfc0 (ST54 type 0x72 sub 1; record 0x8ac2ec, table 0x8ac328 of content/late_game/st53_story): level until its
// step is set to 1 from outside, then flies off along a pitch arc (the cosine of its arc angle, which opens by 3 / 4 a
// tick by its second parameter, to 0x3c0)
void takeoffArcUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
