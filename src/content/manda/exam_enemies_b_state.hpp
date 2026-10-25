// The class states of exam_enemies_b.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::exam_enemies_b_state {

// Its hit block (in its third bone block, +0x60): the body's box (its contact.x: the hitter's direction) and the
// claws'
struct DiggerHitBlock {
    HitBox body;
    HitBoxHead claws;
};

struct AmbushFields : ActorClassState {
    explicit AmbushFields(ControllerActor& actor) : records(actor.params[0]), holdNear(actor.params[1]) {}
    FieldAlias<int8_t, uint8_t> records; // the list's records
    uint8_t& holdNear; // non-zero: the near boxes do not alert the group
    StateArray<uint32_t, 3> spawned{}; // each record's actor (0: the pool was full; kAmbushDefeated)
    uint8_t group = 0;
    uint8_t woken = 0; // the index of the woken one (0xff: none)
};

constexpr uint8_t kFlyerContactWall = 0x01;
constexpr uint8_t kMineAlerted = 0x01, kMineUnseen = 0x80; // unseen: not a lock-on target until the player faces it

// The drone's fields: a timer (the acceleration's spare lane), its hit box, the chase's retarget timer and height
// offset; its step byte alternates the offset's sign
struct DroneFields : ActorClassState {
    explicit DroneFields(StaticActor& actor) : timer(actor.acceleration.spare), hitBox(actor.hitBox) {}
    FieldAlias<uint16_t, int16_t> timer;
    HitBoxHead& hitBox;
    int16_t retarget = 0;
    int16_t heightOffset = 0;

    void clearBlock() override {
        retarget = 0;
        heightOffset = 0;
    }
};

struct MineFields : ActorClassState {
    explicit MineFields(StaticActor& actor) : skipNeighbour(actor.stepCounter), returnHeight(actor.velocity.spare), swayAngle(actor.spin.spare), hitBox(actor.hitBox), flags(actor.gunshipCooldown()), phase(actor.gunshipAttackCooldown()) {}
    uint8_t& skipNeighbour; // a neighbour already compared this tick (the header's step counter)
    int16_t& returnHeight; // the velocity's spare lane: the height it sinks back to
    int16_t& swayAngle; // the spin's spare lane (its rate: the spin's y)
    HitBoxHead& hitBox;
    uint8_t& flags; // kMineAlerted / kMineUnseen
    uint8_t& phase; // the bob's 64-step phase
    int16_t baseHeight = 0;
    int16_t timer = 0; // also the heading toward the player
    StateArray<uint8_t, 2> glows{}; // the glows' slot ids (0: none)

    void clearBlock() override {
        baseHeight = 0;
        timer = 0;
        glows = {};
    }
};

struct DiggerFields : ActorClassState {
    explicit DiggerFields(AnimatedActor& actor) : nextSub(actor.params[0]), cooldown(actor.params[1]), offset(actor.params, 2), scale(actor.scale), tint(actor.tint) {}
    uint8_t& nextSub; // the sub-state after the turn
    uint8_t& cooldown; // the lunge's wall cooldown
    BytesField<int16_t, 4> offset; // the walk's angle offset from the player
    ShortVec3& scale; // the slot's scale and tint (the class stores them as two dwords)
    uint16_t& tint;
    BoneBlockArea<DiggerHitBlock> hits;
    uint16_t timer = 0;
    uint16_t knockThreshold = 0; // a hit above this knocks it back
    uint16_t unused154 = 0; // cleared (its reader is not traced)
    uint16_t fadeTicks = 0;
    int16_t offscreen = 0;
    uint8_t flags = 0; // kDigger*
    uint8_t red = 0; // its colour (5-bit channels)
    uint8_t green = 0;
    uint8_t blue = 0;
    uint8_t contact = 0; // kDiggerContact* of this tick
    void setScaleWords(uint32_t xy, uint32_t zTint) {
        scale.x = int16_t(uint16_t(xy));
        scale.y = int16_t(uint16_t(xy >> 16));
        scale.z = int16_t(uint16_t(zTint));
        tint = uint16_t(zTint >> 16);
    }

    void clearBlock() override {
        hits = nullptr;
        timer = 0;
        knockThreshold = 0;
        unused154 = 0;
        fadeTicks = 0;
        offscreen = 0;
        flags = 0;
        red = 0;
        green = 0;
        blue = 0;
        contact = 0;
    }
};

struct FlyerFields : ActorClassState {
    explicit FlyerFields(AnimatedActor& actor) : keepZ(actor.stepCounter), wake(actor.params[0]), flags(actor.hopperBytes[2]), contact(actor.hopperBytes[3]), group(actor.hopperBytes[4]) {}
    uint8_t& keepZ; // the return keeps its current z (the header's step counter)
    uint8_t& wake; // 1 woken, 2 the group alerted (the ambush writes it)
    uint8_t& flags; // kFlyer*
    uint8_t& contact; // kFlyerContactWall of this tick
    uint8_t& group;
    HitBoxHead hitBox;
    ShortVec3 perch;
    int16_t perchYaw = 0;
    uint16_t timer = 0;
    int16_t pitch = 0;
    uint16_t gauge = 0; // the damage taken lately (beyond the row's +9: the stagger)
    int16_t reflectedYaw = 0;

    void clearBlock() override {
        hitBox = {};
        perch = {};
        perchYaw = 0;
        timer = 0;
        pitch = 0;
        gauge = 0;
        reflectedYaw = 0;
    }
};

// The hatch's own bytes: the grid cells it covers (its spawn bytes), its hit box (the previous-x word's low byte the
// script marks at its tick 0x53b), its blink ticks, release timer and mode (kHatchNoHitEntry / kHatchNoHurt)
struct HatchFields : ActorClassState {
    explicit HatchFields(AnimatedActor& actor)
        : cellX0(actor.params[0]), cellX1(actor.params[1]), cellZ0(actor.params[2]), cellZ1(actor.params[3]),
          mode(actor.hopperBytes[1]) {}
    FieldAlias<int8_t, uint8_t> cellX0, cellX1, cellZ0, cellZ1;
    HitBoxHead hitBox;
    PartField<uint8_t, int16_t> scriptMark{hitBox.previousX, 0};
    uint16_t blinkTicks = 0;
    uint16_t releaseTimer = 0;
    uint8_t& mode;

    void clearBlock() override {
        hitBox = {};
        blinkTicks = 0;
        releaseTimer = 0;
    }
};

// the drones alive (the hatches release one while fewer than 0x10)
struct ExamDroneCount {
    int16_t alive = 0; // 0xa59e68
};
inline ExamDroneCount& examDroneCount() {
    static ExamDroneCount s;
    return s;
}

} // namespace od::port::exam_enemies_b_state
