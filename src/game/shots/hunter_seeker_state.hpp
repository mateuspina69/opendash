// The class states of hunter_seeker.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/shots/shot_common.hpp"
#include "game/field_alias.hpp"

namespace od::port::hunter_seeker_state {

// Its block +0xbc: the stats, the crystal's cooldown
struct HunterFields : ActorClassState {
    explicit HunterFields(ShotActor&) {}
    ShotStats stats;
    uint16_t crystalTicks = 0;
};

// ---- the crystal (static global type 0x18) ---------------------------------------------------------------------------
// The crystal's fields: its stats (its life ticks, the weapon's attack and level) and its hit box
struct CrystalFields : ActorClassState {
    explicit CrystalFields(StaticActor&) {}
    ShotStats stats;
    HitBoxHead hitBox;
};

} // namespace od::port::hunter_seeker_state
