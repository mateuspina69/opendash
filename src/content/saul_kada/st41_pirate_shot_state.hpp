// The class states of st41_pirate_shot.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::st41_pirate_shot_state {

// The shot's own bytes: its turn delay (its fade's ticks at the end), the height it levels at, its hit box, its
// billboard's u, its bob's amplitude, its life, its billboard's CLUT word; its bob's phase (the spin's spare lane)
struct ShotFields : ActorClassState {
    explicit ShotFields(ProjectileActor& actor) : turnDelay(actor.hitBox.previousX), levelY(actor.hitBox.previousZ), bobPhase(actor.spin.spare) {}
    FieldAlias<uint16_t, int16_t> turnDelay;
    int16_t& levelY;
    FieldAlias<uint16_t, int16_t> bobPhase;
    HitBoxHead hitBox;
    uint16_t u = 0;
    int16_t bobAmplitude = 0;
    uint16_t life = 0;
    uint16_t clut = 0;
    StateArray<uint8_t, 8> clearedPad0{}; // bytes the block clear covers (no reader)
    void clearBytes() {
        hitBox = {};
        u = 0;
        bobAmplitude = 0;
        life = 0;
        clut = 0;
        clearedPad0 = {};
        turnDelay = 0;
        levelY = 0;
    }
};

} // namespace od::port::st41_pirate_shot_state
