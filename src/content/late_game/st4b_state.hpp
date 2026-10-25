// The class states of st4b.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/actor_stats.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/field_alias.hpp"
#include "game/script_state.hpp"
#include "game/state_array.hpp"

namespace od::port::st4b_state {

// Its work block (bone block 4 of its own, used as memory): its hit box, the two eye points (their spare lanes the
// eyes' close flags), 5 drop points, the pending / accumulated damage, a timer, the volley's bend, the taunts used,
// the last cry
struct BossFightBlock {
    HitBoxHead hitBox;
    ShortVec4 eyeA; // (its spare lane: the eye's close flag)
    ShortVec4 eyeB;
    StateArray<ShortVec4, 5> drops{}; // the drop points (the 4th lanes unread)
    int16_t pendingDamage = 0;
    int16_t damageTaken = 0;
    uint16_t timer = 0;
    int16_t bend = 0;
    int16_t tauntsUsed = 0;
    PartField<uint8_t, int16_t> tauntsUsedLow{tauntsUsed, 0};
    uint16_t lastCry = 0;
    StateArray<uint8_t, 0x34> unread{}; // cleared with the block (no reader)

    int16_t& eyeAClosed() { return eyeA.spare; }
    int16_t& eyeBClosed() { return eyeB.spare; }
    void clearBytes() {
        hitBox = {};
        eyeA = {};
        eyeB = {};
        drops = {};
        pendingDamage = 0;
        damageTaken = 0;
        timer = 0;
        bend = 0;
        tauntsUsed = 0;
        lastCry = 0;
        unread = {};
    }
};

// The pieces' bytes: their hit box (the class block), the circling timer (the box's previous x), the travel yaw;
// the rocking's amplitude (the spin's z) and speed (the acceleration's x)
struct PieceFields : ActorClassState {
    explicit PieceFields(StaticActor& actor) : hitBox(actor.hitBox), circleTimer(actor.hitBox.previousX), rockAmplitude(actor.spin.z), rockSpeed(actor.acceleration.x) {}
    HitBoxHead& hitBox;
    FieldAlias<uint16_t, int16_t> circleTimer;
    int16_t& rockAmplitude;
    int16_t& rockSpeed;
    int16_t travelYaw = 0;

    void clearBlock() override {
        travelYaw = 0;
    }
};

// -- the face-row cast member (exe 0x478a90, pool 0x20 type 0x16): its tracks, a word, the face rows' indices, its
// stepping
struct FaceRowCastFields : ActorClassState {
    explicit FaceRowCastFields(AnimatedActor& actor) : stepping(actor.hopperBytes[0]) {}
    uint8_t& stepping;
    KeyframeTracks tracks;
    uint32_t word = 0;
    StateArray<uint8_t, 2> rows{};
    void clearRows() {
        rows = {};
    }

    void clearBlock() override {
        tracks = {};
        word = 0;
        rows = {};
    }
};

// -- the walk-in cast member (exe 0x44b5c0, pool 0x20 type 2): its tracks, a word, its animation key, its stepping
struct WalkInCastFields : ActorClassState {
    explicit WalkInCastFields(AnimatedActor& actor) : stepping(actor.shownParts) {}
    uint8_t& stepping;
    KeyframeTracks tracks;
    uint32_t word = 0;
    PartField<uint8_t, uint32_t> animKey{word, 1};

    void clearBlock() override {
        tracks = {};
        word = 0;
    }
};

// Its bytes: its tint (the spawn's three parameter bytes, s8), the work block's address, its type's stat row, its
// flags, two timers, the target's yaw / pitch / aim pitch / distance, its pitch goal, the eye timer, the neck's bend,
// the head's yaw
struct BossFields : ActorClassState {
    explicit BossFields(AnimatedActor& actor) : tintR(actor.params[0]), tintG(actor.params[1]), tintB(actor.params[2]) {}
    FieldAlias<int8_t, uint8_t> tintR;
    FieldAlias<int8_t, uint8_t> tintG;
    FieldAlias<int8_t, uint8_t> tintB;
    uint32_t workBlock = 0; // its work block's address (the exe's pointer, kept as a value)
    BossFightBlock workBytes;     // ... the block's bytes
    uint32_t statsRow = 0;
    uint32_t flags = 0;
    int16_t timer = 0;
    int16_t counter = 0;
    int16_t targetYaw = 0;
    int16_t targetPitch = 0;
    int16_t aimPitch = 0;
    int16_t targetDistance = 0;
    int16_t pitchGoal = 0;
    uint16_t eyeTimer = 0;
    int16_t neckBend = 0;
    int16_t headYaw = 0;
    PartField<uint8_t, uint32_t> flagsLow{flags, 0};
    void clearBlockAfterWorkBytes() {
        statsRow = 0;
        flags = 0;
        timer = 0;
        counter = 0;
        targetYaw = 0;
        targetPitch = 0;
        aimPitch = 0;
        targetDistance = 0;
        pitchGoal = 0;
        eyeTimer = 0;
        neckBend = 0;
        headYaw = 0;
    }
    BossFightBlock& work() { return workBytes; }
    const BossFightBlock& work() const { return workBytes; }
    AnimatedActorStats row() const { return animatedStatsAt(statsRow); }

    void clearBlock() override {
        workBlock = 0;
        statsRow = 0;
        flags = 0;
        timer = 0;
        counter = 0;
        targetYaw = 0;
        targetPitch = 0;
        aimPitch = 0;
        targetDistance = 0;
        pitchGoal = 0;
        eyeTimer = 0;
        neckBend = 0;
        headYaw = 0;
    }
};

// the script's own byte the fourth variant keys on (its bytes +0x10)
struct Script59Cue : ScriptPart<Script59Cue> {
    uint32_t cue = 0; // +0x1c
    // exe 0x40dc40 over +0xc..+0x1b: none of its members there (past it: kept)
    void clearSetUpRun() override {}
};

} // namespace od::port::st4b_state
