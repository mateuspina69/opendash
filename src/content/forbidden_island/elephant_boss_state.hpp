// The class states of elephant_boss.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/field_alias.hpp"
#include "game/script_state.hpp"
#include "game/state_array.hpp"

namespace od::port::elephant_boss_state {

// The boss's class block: the body's hit box, the pointer to its tusk block (a scratch block in a bone block), its
// flags, the player's planar distance, a counter, the player's angle, a target yaw, its deaths (the hit entries
// harmless after the first), its encounter group's index, a repeat count
struct TuskBlock {
    HitBoxHead hit;           // the tusks' hit box
    int16_t stagger = 0;      // the stagger meter: hits * 0x10, -4 a tick; >= 0x800 -> staggered
    int16_t pitchOffset = 0;
    StateArray<uint8_t, 0x34> unread{}; // cleared with the block (no reader)

    void clearBytes() { *this = TuskBlock{}; }
};

// ---- the crystal (exe 0x590260, ST1D static type 1: variant 0 the floating crystal, 1 its pedestal) ---------------
// Its class block: its hit box, its home height
struct CrystalFields : ActorClassState {
    explicit CrystalFields(StaticActor& actor) : hit(actor.hitBox) {}
    HitBoxHead& hit;
    int16_t homeY = 0;

    void clearBlock() override {
        homeY = 0;
    }
};

// The dust ring's fields (ST1D effect type 0x11): its radius and its shrink a tick
struct DustRingFields : ActorClassState {
    explicit DustRingFields(EffectActor&) {}
    int32_t radius = 0;
    int32_t shrink = 0;
};

// The pillar's fields: its encounter group (the rotation's spare lane), its hit box, the debris' yaw (left 0)
struct PillarFields : ActorClassState {
    explicit PillarFields(StaticActor& actor) : group(actor.rotation.spare), hit(actor.hitBox) {}
    int16_t& group;
    HitBox& hit; // its shape asks for the contact motion (written into the box's bytes after the head)
    int16_t debrisYaw = 0;

    void clearBlock() override {
        debrisYaw = 0;
    }
};

// ---- the exit light (exe 0x5aa060, ST1D static type 0x28; late loop): curtains of light quads drawn every tick -----
// The quads are render output; their RNG steps (one per quad, one more at each row start) are logic: the loop stops
// (return) at the first quad whose average view depth / 16 is beyond the far plane (the stage's depth range).
struct ExitLightFields : ActorClassState {
    explicit ExitLightFields(StaticActor& actor) : originZ(actor.hitBox.previousX) {}
    int16_t& originZ;
    int16_t originX = 0; // the quads' origin (its cell's corner, 0x90 above it)
    int16_t originY = 0;
    int16_t curtainX = 0; // the curtain's offset from the origin
    int16_t columnStep = 0;

    void clearBlock() override {
        originX = 0;
        originY = 0;
        curtainX = 0;
        columnStep = 0;
    }
};

struct BossFields : ActorClassState {
    explicit BossFields(AnimatedActor& actor) : flags(actor.bossFlags), flagsLow(actor.bossFlags, 0), deaths(actor.townBirdHits), groupIndex(actor.townBirdHitsHigh), repeats(actor.hopperTimer) {}
    uint32_t& flags; // kBoss*
    PartField<uint8_t, uint32_t> flagsLow; // the flags' low byte (the exe's byte tests)
    uint8_t& deaths;
    uint8_t& groupIndex;
    uint8_t& repeats;
    HitBoxHead body;
    uint32_t tuskBlock = 0; // its tusk block's address (the exe's pointer, kept as a value)
    TuskBlock tuskBytes;    // ... the block's bytes
    uint32_t distance = 0;
    int16_t counter = 0;
    int16_t playerAngle = 0;
    uint16_t targetYaw = 0;
    TuskBlock& tusks() { return tuskBytes; }
    const TuskBlock& tusks() const { return tuskBytes; }

    void clearBlock() override {
        body = {};
        tuskBlock = 0;
        distance = 0;
        counter = 0;
        playerAngle = 0;
        targetYaw = 0;
    }
};

// The rocks' own values (static 0x20 sub 0): the mode (kRock*) and the attack of the thrown ones, their lifetime (the
// rotation's spare lane), the hit box, and the scale words kept through the model bind
struct RockFields : ActorClassState {
    explicit RockFields(StaticActor& actor) : mode(actor.params[0]), attack(actor.params[1]), lifetime(actor.rotation.spare), hit(actor.hitBox) {}
    uint8_t& mode;
    uint8_t& attack;
    int16_t& lifetime;
    HitBoxHead& hit;
};

// The trail's fields: its owner (the boulder), the oldest / next of its 16 positions (a ring), the positions' x / y / z
struct TrailFields : ActorClassState {
    explicit TrailFields(StaticActor& actor) : oldest(actor.rotation.x), next(actor.rotation.y) {}
    int16_t& oldest;
    int16_t& next;
    ActorRef<Actor> owner;
    StateArray<int16_t, 16> xs{};
    StateArray<int16_t, 16> ys{};
    StateArray<int16_t, 16> zs{};
};

// ---- the boulder / shock ring (exe 0x5c7780, ST1D pool 0x40 type 0x14; sub 0 the boulder, sub 1 the shock ring) -----
// Its class block: the hit box, a timer, the shock ring's radius, the boulder's aim (pitch / yaw), its gait and the
// gait's flags; the step counter is the boulder's homing mode
struct BoulderFields : ActorClassState {
    explicit BoulderFields(ProjectileActor& actor) : hit(actor.hitBox) {}
    HitBoxHead& hit;
    int16_t timer = 0;
    uint16_t ringRadius = 0;
    uint32_t aimWord = 0; // the aim as the start's dword copy of the rotation's x / y
    StateArray<uint8_t, 2> clearedPad0{}; // bytes the block clear covers (no reader)
    uint16_t spareWord = 0; // cleared at the aim's start (no reader traced)
    uint8_t gait = 0; // the bounce gait's phase (<< 1)
    uint8_t gaitFlags = 0; // 1 a high bounce, 2 the side of the next one
    StateArray<uint8_t, 10> clearedPad1{}; // bytes the block clear covers (no reader)
    PartField<int16_t, uint32_t> aimPitch{aimWord, 0};
    PartField<int16_t, uint32_t> aimYaw{aimWord, 2};
    void clearBlockBytes() {
        timer = 0;
        ringRadius = 0;
        aimWord = 0;
        clearedPad0 = {};
        spareWord = 0;
        gait = 0;
        gaitFlags = 0;
        clearedPad1 = {};
        hit = {};
    }
};

// The script's own bytes: the point the player fell from (the arena's arrival) and a wait count
struct Script28Bytes : ScriptPart<Script28Bytes> {
    ShortVec3 fallPoint; // +0xc
    int32_t waitTicks = 0; // +0x1c
    // exe 0x40dc40 over +0xc..+0x1b: the members there zeroed (past it: kept)
    void clearSetUpRun() override {
        fallPoint = {};
    }
};

// The exe globals elephant_boss.cpp keeps
struct ElephantGlobals {
    int16_t trailMarker = 0; // 0xa5a912: the short the trail clears every tick (its reader is not traced)
};
inline ElephantGlobals& elephantGlobals() {
    static ElephantGlobals s;
    return s;
}

} // namespace od::port::elephant_boss_state
