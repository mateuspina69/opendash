// The class states of mission_tank.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::mission_tank_state {

// The tank's spawn parameters and class block
struct TankFields : ActorClassState {
    explicit TankFields(AnimatedActor& actor) : targetClass(actor.params[0]), targetType(actor.params[1]), targetIndex(actor.params[2]), damageLevel(actor.params[3]), barrelPitch(actor.rotation.spare), wheelsYaw(actor.acceleration.spare), leadShift(actor.guardianBits), lastLevel(actor.shownParts), timer(actor.hopperBytes[0]), hurtCooldown(actor.hopperBytes[1]), targetHold(actor.hopperBytes[3]), repair(actor.townBirdHits), engineTimer(actor.townBirdHitsHigh), smokeSlots(actor.hopperBytes, 3) {}
    uint8_t& targetClass; // the pool class of its targets (kAnimatedClass / kStaticClass)
    FieldAlias<int8_t, uint8_t> targetType; // ... and their type
    FieldAlias<int8_t, uint8_t> targetIndex; // its first target (the index in the list; past it: a random one)
    FieldAlias<int8_t, uint8_t> damageLevel; // 0, 1 below half its hit points, 2 below a quarter
    int16_t& barrelPitch; // the rotation's spare lane
    int16_t& wheelsYaw; // the acceleration's spare lane
    uint8_t& leadShift; // the aim leads the target by its last move << this
    uint8_t& lastLevel; // the damage level at the start of the tick
    uint8_t& timer; // the pause before the next shot / the idle / shaken ticks
    uint8_t& hurtCooldown;
    int8_t group = 0; // its enemy-group entry (exe 0x5e6cd0)
    uint8_t& targetHold;
    uint8_t& repair; // the extinguisher's hits summed (>> 6: the hit points back)
    uint8_t& engineTimer;
    HitBox hitBox;
    uint8_t target = 0; // the target's slot (0: none)
    uint8_t bits = 0; // kTankFire | kTankSteady
    uint8_t fireTimer = 0;
    // the smokes' slots by damage level: the slot's bytes +0x165..+0x167 (the first is the target hold's)
    ArrayFields<uint8_t, 3, uint8_t, 6> smokeSlots;

    void clearBlock() override {
        group = 0;
        hitBox = {};
        target = 0;
        bits = 0;
        fireTimer = 0;
        for (size_t i = 0; i < smokeSlots.size(); ++i) smokeSlots[i] = 0;
    }
};

} // namespace od::port::mission_tank_state
