// The class states of st2b_cannon_boss.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st2b_cannon_boss_state {

// The turret's own bytes (0x20 cleared at the bind): its hit box, the barrel's pitch and yaw (relative to its own),
// their targets, the yaw's mover, the pitch moving, a timer, a lob fired (not read), the turn's / the pitch's sound
// ticks, its invulnerable ticks, dying (no lock-on), provoked (hit once); the barrel's yaw acceleration (the spin's
// spare lane; its pitch / yaw speeds are the spin's x / y)
struct BossFields : ActorClassState {
    explicit BossFields(AnimatedActor& actor) : lobFired(actor.shownParts), pitchSoundTicks(actor.hopperBytes[0]), invulnerable(actor.hopperBytes[1]), dying(actor.hopperBytes[2]), provoked(actor.hopperBytes[3]), yawAccel(actor.spin.spare) {}
    uint8_t& lobFired;
    uint8_t& pitchSoundTicks;
    uint8_t& invulnerable;
    uint8_t& dying;
    uint8_t& provoked;
    int16_t& yawAccel;
    HitBoxHead hitBox;
    int16_t pitch = 0;
    int16_t barrelYaw = 0;
    int16_t pitchTarget = 0;
    int16_t yawTarget = 0;
    uint8_t yawMover = 0;
    uint8_t pitchMoving = 0;
    uint16_t timer = 0;
    uint8_t turnSoundTicks = 0;
    PartField<uint8_t, uint16_t> timerLow{timer, 0};

    void clearBlock() override {
        hitBox = {};
        pitch = 0;
        barrelYaw = 0;
        pitchTarget = 0;
        yawTarget = 0;
        yawMover = 0;
        pitchMoving = 0;
        timer = 0;
        turnSoundTicks = 0;
    }
};

} // namespace od::port::st2b_cannon_boss_state
