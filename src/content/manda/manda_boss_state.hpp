// The class states of manda_boss.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"

namespace od::port::manda_boss_state {

// ---- the guardian (exe 0x48fb90) ------------------------------------------------------------------------------------
// Its work area (in its third bone block, +0x40): its stats row, the perch it is on and the next, the player's quadrant
// (-1: none), the quadrant at the beam's start, the target / current yaw of a turn
struct GuardianFightBlock {
    uint32_t statsRow = 0; // its stats row (exe data)
    int8_t perch = 0;
    int8_t nextPerch = 0;
    int8_t playerQuadrant = 0;
    int8_t beamQuadrant = 0;
    int16_t targetYaw = 0;
    int16_t turnYaw = 0;
    StateArray<uint8_t, 0x34> unread{}; // cleared with the area (no reader)
};
// Its hit boxes (in its third bone block, +0x90): the mouth's entry's (and the body's while it stomps) and the body's
struct GuardianHitBoxes {
    HitBoxHead main;
    HitBoxHead body;
};

// ---- the fight's controller (pool 0xe0, ST14 type 0xc, exe 0x423170) ----------------------------------------------
// Its bits: the floater alive, the guardian under half its hit points, the floater within the beam's reach, a variant
// guardian died, 0x10 << k: the roller k is up; the respawn timers of the floater / of a roller; the guardian and the
// floater
struct FightFields : ActorClassState {
    explicit FightFields(ControllerActor& actor) : bits(actor.params[0]), floaterTimer(actor.params[1]), rollerTimer(actor.params[2]), guardian(actor.rider) {}
    uint8_t& bits;
    uint8_t& floaterTimer;
    uint8_t& rollerTimer;
    ActorRef<Actor>& guardian;
    ActorRef<AnimatedActor> floater;
};

// ---- the fight's controller (exe 0x423170, states [0x88bd3c]) ------------------------------------------------------
// The rollers it keeps up (static type 0x35) keep their class's state: content/late_game/st43_roller_state RollerFields

struct DebrisFields : ActorClassState {
    explicit DebrisFields(ProjectileActor& actor) : hitBox(actor.hitBox), flags(actor.debrisFlags) {}
    HitBoxHead& hitBox;
    uint32_t& flags; // kDebrisHoming
    int16_t timer = 0;
    int16_t burstTicks = 0;
    int32_t playerDistance = 0;
    int16_t playerYaw = 0;
    int16_t playerPitch = 0;
    int16_t launchSpeed = 0;
    uint8_t column = 0; // the sprite's texture column (the burst's frames)
};

// the puppets' fields: the animation's first key, and in the class block the rising one's fade counter and dust phase,
// the dropping one's timer and ground
struct PuppetFields : ActorClassState {
    explicit PuppetFields(AnimatedActor& actor) : startKey(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> startKey;
    int16_t timer = 0;
    int16_t ground = 0;
    PartField<int8_t, int16_t> fadeCounter{ground, 0};
    PartField<uint8_t, int16_t> dustPhase{ground, 1};

    void clearBlock() override {
        timer = 0;
        ground = 0;
    }
};

// The guardian's own fields: its tint's red (spawn parameter 0), the animation's first key, the controller, the beam's
// target, the beam, the work area, the hit boxes, its flags, a timer, a counter, the yaw / pitch from the player's chest
// to its bone 0
struct GuardianFields : ActorClassState {
    explicit GuardianFields(AnimatedActor& actor) : red(actor.params[0]), startKey(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> red;
    FieldAlias<int8_t, uint8_t> startKey;
    ActorRef<ControllerActor> controller;
    ActorRef<AnimatedActor> target;
    ActorRef<StaticActor> beam;
    BoneBlockArea<GuardianFightBlock> work;
    BoneBlockArea<GuardianHitBoxes> hits;
    uint32_t flags = 0;
    int16_t timer = 0;
    int16_t counter = 0;
    int16_t playerYaw = 0;
    int16_t playerPitch = 0;
    void clearStateBytesBytes() {
        flags = 0;
        timer = 0;
        counter = 0;
        playerYaw = 0;
        playerPitch = 0;
    }

    void clearBlock() override {
        controller = nullptr;
        target = nullptr;
        beam = nullptr;
        work = nullptr;
        hits = nullptr;
        flags = 0;
        timer = 0;
        counter = 0;
        playerYaw = 0;
        playerPitch = 0;
    }
};

// The floater (pool 0x20, ST14 type 0x3e, exe 0x4b83e0): caught by the beam / let go by it (spawn parameters 1 / 2), the
// turn and speed modes (the step bytes), the controller, its flags, a timer, the bob, the distances / angle to the
// player and to the arena's centre, its hit box, its stats row
struct FloaterFields : ActorClassState {
    explicit FloaterFields(AnimatedActor& actor) : caught(actor.params[1]), released(actor.params[2]), startKey(actor.params[3]), turnMode(actor.step), speedMode(actor.stepCounter), playerDistance(actor.bossFlags), turnAccel(actor.spin.spare) {}
    uint8_t& caught;
    uint8_t& released;
    FieldAlias<int8_t, uint8_t> startKey;
    FieldAlias<int8_t, uint8_t> turnMode;
    uint8_t& speedMode;
    FieldAlias<int32_t, uint32_t> playerDistance;
    int16_t& turnAccel; // the spin's spare lane
    ActorRef<ControllerActor> controller;
    uint32_t flags = 0;
    int16_t timer = 0;
    uint16_t bob = 0;
    int16_t centreDistance = 0;
    int16_t centreAngle = 0;
    HitBoxHead hitBox;
    uint32_t statsRow = 0;
    void clearStateBytesBytes() {
        flags = 0;
        timer = 0;
        bob = 0;
        centreDistance = 0;
        centreAngle = 0;
        hitBox = {};
        statsRow = 0;
        playerDistance = 0;
    }
    void setModes(uint16_t word) { set_modes_(uint32_t(word)); }

private:
    uint16_t get_modes_() const { return uint16_t(uint32_t(std::make_unsigned_t<int8_t>(turnMode)) << 0 | uint32_t(std::make_unsigned_t<uint8_t>(speedMode)) << 8); }
    void set_modes_(uint32_t v) { turnMode = int8_t(std::make_unsigned_t<int8_t>(v >> 0)); speedMode = uint8_t(std::make_unsigned_t<uint8_t>(v >> 8)); }
public:

    void clearBlock() override {
        controller = nullptr;
        flags = 0;
        timer = 0;
        bob = 0;
        centreDistance = 0;
        centreAngle = 0;
        hitBox = {};
        statsRow = 0;
    }
};

// ---- the beam (static, ST14 type 0x2c, exe 0x5aad40) ---------------------------------------------------------------
// The head (variant 0, exe 0x5aad60, spawned by the guardian): 16 segments (variants 1..16, exe 0x5ab210) chained by
// their matrices, grown one a tick, held 0x10 ticks, shrunk, then done (the guardian frees it) and the floater it
// carried told. Head: the sway pattern (0..5) and its tick, done, the segments out (its spawn bytes), the aimed pitch /
// yaw (the spin's z / spare lanes, written by the guardian's exe 0x4919b0), the pitch offset (the hit points' bytes);
// over the root matrix and the model fields: the floater, its flags, the hold timer, the segments.
struct BeamHeadFields : ActorClassState {
    explicit BeamHeadFields(StaticActor& actor)
        : pattern(actor.params[0]), tick(actor.params[1]), done(actor.params[2]), segments(actor.params[3]),
          aimPitch(actor.spin.z), aimYaw(actor.spin.spare), pitchOffset(actor.hitPoints) {}
    FieldAlias<int8_t, uint8_t> pattern;
    uint8_t& tick;
    uint8_t& done;
    FieldAlias<int8_t, uint8_t> segments;
    int16_t& aimPitch;
    int16_t& aimYaw;
    int16_t& pitchOffset;
    ActorRef<AnimatedActor> target; // the floater
    uint16_t headFlags = 0;
    PartField<uint8_t, uint16_t> headFlagsByte{headFlags, 0};
    int16_t holdTicks = 0;
    StateArray<ActorRef<StaticActor>, 16> segmentSlots{};
};
// A segment: its growth (the width's index; its spawn byte), the short after its matrix (exe 0x40bfd0 writes it); over
// the model fields: its flags, the previous segment, its hit box, the link to the previous one's projected corners (the
// draw's: the exe keeps their address, the previous segment's +0xa4), its width / length, the floater.
struct BeamSegmentFields : ActorClassState {
    explicit BeamSegmentFields(StaticActor& actor) : segments(actor.params[3]), matrixPad(actor.rootPad) {}
    FieldAlias<int8_t, uint8_t> segments;
    int16_t& matrixPad;
    uint16_t segmentFlags = 0;
    PartField<uint8_t, uint16_t> segmentFlagsByte{segmentFlags, 0};
    ActorRef<StaticActor> previous;
    HitBoxHead hitBox;
    ActorRef<StaticActor> cornersLink;
    int16_t width = 0;
    int16_t length = 0;
    ActorRef<AnimatedActor> floater;
};

} // namespace od::port::manda_boss_state
