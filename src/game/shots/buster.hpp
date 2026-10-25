// The buster shot's state (game/shots/buster.cpp; the effects' draw, game/effects_draw, reads it): its block and its
// states.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/shots/shot_common.hpp"

namespace od::port {

// Its block +0xbc: the stats, the deflection's cooldown, the player's lock-on slot at the shot
struct BusterShotFields : ActorClassState {
    explicit BusterShotFields(ShotActor&) {}
    ShotStats stats;
    uint16_t deflectTicks = 0;
    uint16_t lockSlot = 0;
};
// [0x8c4924]
enum class BusterState : uint8_t {
    Spawn = 0, // exe 0x56c060: at the buster, from the player's right hand
    Fly = 1,   // exe 0x56c310: two steps a tick
    Free = 2,  // exe 0x56c560
};

} // namespace od::port
