// The class states of glyde_base_turret.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port::glyde_base_turret_state {

struct TurretFields : ActorClassState {
    explicit TurretFields(AnimatedActor& actor) : bits(actor.shownParts) {}
    uint8_t& bits; // kTurret*
    HitBox hitBox;
    uint16_t timer = 0;
    int16_t lastHitContact = 0; // the contact of the last hit that hurt it (its reader is not traced)
    int16_t yawOffset = 0; // the aim, added to the barrel's decoded angles ...
    int16_t pitchOffset = 0;
    int16_t yawTarget = 0; // ... eased toward these
    int16_t pitchTarget = 0;
    uint16_t shotTicks = 0; // the ticks to the next shot of the burst

    void clearBlock() override {
        hitBox = {};
        timer = 0;
        lastHitContact = 0;
        yawOffset = 0;
        pitchOffset = 0;
        yawTarget = 0;
        pitchTarget = 0;
        shotTicks = 0;
    }
};

} // namespace od::port::glyde_base_turret_state
