// The class states of machine_gun.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/shots/shot_common.hpp"
#include "game/field_alias.hpp"

namespace od::port::machine_gun_state {

// ---- the shell casing (static global type 0x11) ----------------------------------------------------------------------
// The casing's fields: its ticks left, its sprite's spin, the bounce sound's ticks
struct CasingFields : ActorClassState {
    explicit CasingFields(StaticActor& actor) : soundTicks(actor.hitBox.previousX) {}
    int16_t& soundTicks;
    int16_t life = 0;
    int16_t spin = 0;

    void clearBlock() override {
        life = 0;
        spin = 0;
    }
};

// The muzzle effect's fields: its position lanes are the random offset of the shot's capsule (the short after them
// cleared with them), the shot it follows
struct MuzzleFields : ActorClassState {
    explicit MuzzleFields(EffectActor&) {}
    uint32_t shot = 0;
};

// ---- the shot -------------------------------------------------------------------------------------------------------
// Its block +0xbc: the stats, the muzzle effect
struct MachineGunFields : ActorClassState {
    explicit MachineGunFields(ShotActor&) {}
    ShotStats stats;
    uint32_t muzzle = 0;
};

} // namespace od::port::machine_gun_state
