// The class states of sand_burrower.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"

namespace od::port::sand_burrower_state {

// Its hit record (its third bone block + 0x60): the body's hit box, the limbs', the dust points of its dive / charge
struct BurrowerRecord {
    HitBoxHead body;
    HitBoxHead limbs;
    ShortVec4 startPoint; // (the 4th lanes unread)
    ShortVec4 sinkPoint;
};

// Its own bytes: its record, the second home's x / z, its spawn yaw, the damage taken since its last flinch, its spawn
// height, its flags, two timers, its attack (and the last one, their repeats), its enemy group and the marks it sets;
// its head's sway (the rotation's spare lane), a state's first tick (the velocity's spare lane)
struct BurrowerFields : ActorClassState {
    explicit BurrowerFields(AnimatedActor& actor) : repeats(actor.guardianBits), group(actor.shownParts), headSway(actor.rotation.spare), firstTick(actor.velocity.spare) {}
    uint8_t& repeats;
    uint8_t& group;
    int16_t& headSway;
    int16_t& firstTick;
    BoneBlockArea<BurrowerRecord> record;
    int16_t secondHomeX = 0;
    int16_t secondHomeZ = 0;
    uint16_t spawnYaw = 0;
    uint16_t damageTaken = 0;
    int16_t spawnHeight = 0;
    uint8_t flags = 0;
    uint8_t timerA = 0;
    uint8_t timerB = 0;
    uint8_t attack = 0;
    uint8_t lastAttack = 0;
    uint8_t groupMarks = 0;
    BurrowerRecord& rec() { return *record; }
    const BurrowerRecord& rec() const { return *record; }

    void clearBlock() override {
        record = nullptr;
        secondHomeX = 0;
        secondHomeZ = 0;
        spawnYaw = 0;
        damageTaken = 0;
        spawnHeight = 0;
        flags = 0;
        timerA = 0;
        timerB = 0;
        attack = 0;
        lastAttack = 0;
        groupMarks = 0;
    }
};

} // namespace od::port::sand_burrower_state
