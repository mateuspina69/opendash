// The class states of homing_missile.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/shots/shot_common.hpp"
#include "game/field_alias.hpp"

namespace od::port::homing_missile_state {

// Its block +0xbc: the stats, the ticks until it wobbles, the turn rate and its step
struct MissileFields : ActorClassState {
    explicit MissileFields(ShotActor&) {}
    ShotStats stats;
    uint16_t wobbleDelay = 0;
    uint16_t turnRate = 0;
    uint16_t turnStep = 0;
};

} // namespace od::port::homing_missile_state
