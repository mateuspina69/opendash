// The class states of manda_guardian.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "content/manda/manda_guardian.hpp"
#include "game/field_alias.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port::manda_guardian_state {

// Its fields: the lean's target (the rotation's spare lane), the head's turn (the velocity's spare lane), and its
// class block -- the body's capsule box, the landing's box, the roll's lean speed, its flags, the volleys shot (variants
// 1 / 2), the collision's results, the retreats left (variants 1 / 2), the fade's counter, the spin sound's timer, the
// damage taken during a spin, the bob switch, the enemy group, the invulnerability ticks, a count (the dust's phase
// while fading, the shots / ticks left otherwise) and a timer
struct GuardianFields : ActorClassState {
    explicit GuardianFields(AnimatedActor& actor) : leanTarget(actor.rotation.spare), headTurn(actor.velocity.spare), volleys(actor.guardianBits), hits(actor.shownParts), soundTimer(actor.hopperBytes[1]), bobs(actor.hopperBytes[4]), invulnerable(actor.townBirdHits), count(actor.townBirdHitsHigh) {}
    int16_t& leanTarget;
    int16_t& headTurn;
    uint8_t& volleys;
    uint8_t& hits;
    int8_t fadeCounter = 0;
    uint8_t& soundTimer;
    uint8_t& bobs;
    int8_t group = 0;
    uint8_t& invulnerable;
    uint8_t& count;
    HitBoxHead bodyBox;
    HitBoxHead landingBox;
    int16_t rollSpeed = 0;
    uint8_t flags = 0;
    uint8_t retreats = 0;
    int16_t damageTaken = 0;
    int16_t timer = 0;

    void clearBlock() override {
        fadeCounter = 0;
        group = 0;
        bodyBox = {};
        landingBox = {};
        rollSpeed = 0;
        flags = 0;
        retreats = 0;
        damageTaken = 0;
        timer = 0;
    }
};

struct ShotTrailFields : ActorClassState {
    explicit ShotTrailFields(ControllerActor& actor) : roll(actor.params[0]), yaw(actor.params[1]), prevRoll(actor.params[2]), prevYaw(actor.params[3]) {}
    uint8_t& roll;
    uint8_t& yaw;
    uint8_t& prevRoll;
    uint8_t& prevYaw;
    ShortVec3 head;
    ShortVec3 tail;
    uint8_t ticks = 0;
};

// ---- the blades of the ST12 sub 5 arena (exe 0x59bee0: static type 0x17 sub 1) --------------------------------------
// States [0x8ce7b0]: its set-up (exe 0x59bf30: the model by variant; variant 0 at its corner; the guardian's enemy
// group; on the ground), its run ([0x8ce7c0 + variant]; variants >= 0x80 do nothing), idle. Variant 0 (exe 0x59c120)
// spins (its box hits the player) through: waiting for the guardian's spin, a 90-tick wobble, the slides to the points
// 0x2000 / 0x5000 ahead of its corner (accelerate, coast, brake; with the shakes), the slide back to the next corner
// (then the mark kMarkBladesBack), then the wait or the next round. Variant 1 (exe 0x59c700) spins in place, wobbling
// while the guardian spins. Variant 2 (exe 0x59c760, ST43 sub 2) shuttles back and forth along its facing, spinning.
// Its fields: its corner (spawn parameter 0), its travel's yaw (the velocity's spare lane), a timer (the spin's spare
// lane), its hit box, the guardian's enemy group, the shuttle's travel left, the slide's target x / z
struct BladeFields : ActorClassState {
    explicit BladeFields(StaticActor& actor) : corner(actor.params[0]), travelYaw(actor.velocity.spare), timer(actor.spin.spare), hitBox(actor.hitBox) {}
    FieldAlias<int8_t, uint8_t> corner;
    int16_t& travelYaw;
    int16_t& timer;
    HitBoxHead& hitBox;
    int8_t group = 0;
    int16_t travel = 0;
    int16_t targetX = 0;
    int16_t targetZ = 0;

    void clearBlock() override {
        group = 0;
        travel = 0;
        targetX = 0;
        targetZ = 0;
    }
};

// ---- the blade that falls in script 0xb (exe 0x59c800: static type 0x17 sub 2) ---------------------------------------
// States [0x8ce818]: its set-up (exe 0x59c850: the model 0x8ce820, its box), then it falls (its vertical speed +0x40 a
// tick) until the room collision reports the ground -> flag 0x680 (script 0xb's step 1 waits on it), then only spins.
// Its field: the collision's results (exe 0x59c930)
struct FallingBladeFields : ActorClassState {
    explicit FallingBladeFields(StaticActor&) {}
    uint8_t hits = 0;

    void clearBlock() override {
        hits = 0;
    }
};

// ---- the opened chest of ST12 sub 5 (exe 0x475ae0: pool 0x20 type 0x15 sub 1, script 0xb's cast) ----------------------
// States [0x890774]: its set-up (exe 0x475b80: animation 2 blended over 0x10 keys, on the ground, its box), then (exe
// 0x475c10) once the falling blade lands (flag 0x680) a blast (the global static type 7, size 0xc0) at the chest, and the
// chest is freed.
// Its field: the key the next animation starts from (its blend)
struct ChestFields : ActorClassState {
    explicit ChestFields(AnimatedActor&) {}
    uint8_t startKey = 0;

    void clearBlock() override {
        startKey = 0;
    }
};

struct CloudFields : ActorClassState {
    explicit CloudFields(EffectActor& actor) : ringIndex(actor.subState), size(actor.step), shrinkTicks(actor.stepCounter), spin(actor.params[0]), shade(actor.params[1]), phase(actor.params[2]), phaseStep(actor.params[3]), holdTicks(actor.posSpare) {}
    uint8_t& ringIndex;
    uint8_t& size;
    uint8_t& shrinkTicks;
    uint8_t& spin;
    uint8_t& shade;
    FieldAlias<int8_t, uint8_t> phase;
    FieldAlias<int8_t, uint8_t> phaseStep;
    int16_t& holdTicks;
    StateArray<uint8_t, 0x10> ring{};
};

// ---- the guardian's hoppers (exe 0x49e160: pool 0x20 type 0x31 sub 0, spawned by the guardian's spawner) ------------
// States [0x893a24]: its set-up, its act (sub-states [0x893a34]), carried by the player, dying. They follow the
// guardian's enemy-group marks (read each tick): the spin heals them and gathers them round the guardian (the orbit),
// the recall sends them to their corners, the release lets them go. The glow (0x20..0x3e) tints them; a hit leaves a
// slow regeneration of half its damage.
// Its fields: its spawn parameters (the guardian's pool index, its corner), its turn rate, its hit box (the contact
// motion's angle: the knock-back's yaw), a timer, the heading of a flight, the collision's results, its flags, the
// regeneration left and its tick, the guardian's enemy group, the group's marks of this tick, its mode, the knocked-out
// switch, the hops left of a chase, the glow, the side of the orbit's roll
struct HopperFields : ActorClassState, MandaHopperParams {
    explicit HopperFields(AnimatedActor& actor) : MandaHopperParams(actor), turnRate(actor.params[2]), hits(actor.shownParts), regenTicks(actor.hopperBytes[2]), group(actor.hopperBytes[3]), marks(actor.hopperBytes[4]), mode(actor.hopperBytes[5]), knockedOut(actor.townBirdHits), hopsLeft(actor.townBirdHitsHigh), glow(actor.hopperTimer) {}
    uint8_t& turnRate;
    uint8_t& hits;
    uint8_t& regenTicks;
    uint8_t& group;
    uint8_t& marks;
    uint8_t& mode;
    uint8_t& knockedOut;
    uint8_t& hopsLeft;
    uint8_t& glow;
    HitBox hitBox;
    int16_t timer = 0;
    int16_t heading = 0;
    uint8_t flags = 0;
    int16_t regen = 0;
    uint8_t rollSide = 0;

    void clearBlock() override {
        hitBox = {};
        timer = 0;
        heading = 0;
        flags = 0;
        regen = 0;
        rollSide = 0;
    }
};

// ---- the guardian's shot (exe 0x5c5fb0: pool 0x40, ST13 stage type 0xe) ---------------------------------------------
// States [0x8d1284]: its model, sound 300, its heading kept (the rotation at its spawn); then it flies 0x480 a tick
// along the heading spinning, a capsule hit entry and a trail segment each tick; a wall or a hit: sound 0x12d, it
// falls tumbling until it lands, freed.
// Its fields: its hit box and its heading (x pitch, y yaw)
struct ShotFields : ActorClassState {
    explicit ShotFields(ProjectileActor& actor) : hitBox(actor.hitBox) {}
    HitBoxHead& hitBox;
    ShortVec4 heading;
    StateArray<uint8_t, 16> clearedPad0{}; // bytes the block clear covers (no reader)
    void clearClassBytesBytes() {
        heading = {};
        clearedPad0 = {};
        hitBox = {};
    }
};

} // namespace od::port::manda_guardian_state
