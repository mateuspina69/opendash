// The class states of st48_gunship.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::st48_gunship_state {

// The gunship's bytes: the motion lanes it gives meanings of its own, its spawn parameters (its first sub-state's
// index, the enemy id it waits for, a grid cell) and its class block (its hit box, its guns / flight bits, its contact,
// the pitch it turns to, a count, its flags, its ticks off screen, the strafe's cooldown)
struct ShipFields : ActorClassState {
    explicit ShipFields(StaticActor& actor) : ground(actor.rotation.spare), rollTarget(actor.velocity.spare), heading(actor.acceleration.spare), turnStep(actor.spin.spare), startIndex(actor.params[0]), enemyId(actor.params[1]), cellX(actor.params[2]), cellZ(actor.params[3]), hitBox(actor.hitBox), count(actor.gunshipPitchTarget()), flags(actor.gunshipTimer()), cooldown(actor.gunshipCooldown()) {}
    int16_t& ground; // the rotation's spare lane: the ground's height under it
    int16_t& rollTarget; // the velocity's spare lane: the roll the turn asks for
    int16_t& heading; // the acceleration's spare lane: the cruise's turn limit, a heading, a
    int16_t& turnStep; // the spin's spare lane: the turn rate's step
    FieldAlias<int8_t, uint8_t> startIndex;
    FieldAlias<int8_t, uint8_t> enemyId;
    FieldAlias<int8_t, uint8_t> cellX;
    FieldAlias<int8_t, uint8_t> cellZ;
    HitBoxHead& hitBox;
    uint8_t& count;
    uint8_t& flags;
    uint8_t& cooldown;
    uint8_t guns = 0;
    uint8_t contact = 0;
    int8_t pitchTarget = 0;
    int8_t offScreen = 0;

    void clearBlock() override {
        guns = 0;
        contact = 0;
        pitchTarget = 0;
        offScreen = 0;
    }
};

} // namespace od::port::st48_gunship_state
