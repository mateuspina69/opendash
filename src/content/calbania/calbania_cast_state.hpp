// The class states of calbania_cast.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "content/calbania/appo_dah_face.hpp"
#include "game/actor_state.hpp"

namespace od::port::calbania_cast_state {

// ---- the walking puppets (exe 0x446720) ------------------------------------------------------------------------------
struct WalkingPuppetFields : ActorClassState {
    explicit WalkingPuppetFields(AnimatedActor& actor) : noGround(actor.params[3]) {}
    uint8_t& noGround; // not kept on the ground
    uint8_t castFlags = 0; // kPuppetHold
    uint8_t animKey = 0;
    AppoDahMouth mouth;

    void clearBlock() override {
        castFlags = 0;
        animKey = 0;
        mouth = {};
    }
};

// ---- the sitting puppets (exe 0x446b90) --------------------------------------------------------------------------
// The head turned toward a point at a rate (its pitch / yaw kept in the velocity's / the acceleration's spare lanes),
// the animation held, its key, the look on / off; their own mouth bytes
struct SittingPuppetFields : ActorClassState {
    explicit SittingPuppetFields(AnimatedActor& actor) : noGround(actor.params[3]), headPitch(actor.velocity.spare), headYaw(actor.acceleration.spare) {}
    uint8_t& noGround;
    int16_t& headPitch;
    int16_t& headYaw;
    int16_t lookHeight = 0;
    int16_t lookYaw = 0;
    int16_t headRate = 0;
    uint8_t castFlags = 0; // kPuppetHold: the animation decoded without stepping
    uint8_t animKey = 0;
    uint8_t mouthStep = 0;
    uint8_t mouthTicks = 0;
    uint8_t looking = 0; // the head turns toward the look point (else it relaxes)

    void clearBlock() override {
        lookHeight = 0;
        lookYaw = 0;
        headRate = 0;
        castFlags = 0;
        animKey = 0;
        mouthStep = 0;
        mouthTicks = 0;
        looking = 0;
    }
};

// ---- the town birds (exe 0x45d970: pool 0x20 type 0xa sub 0; ST20 records 0x8c875c / 0x8c8770 variant 2, also in
// other stages' tables) ------------------------------------------------------------------------------------------
// They idle, wander, turn away from the player, flee in a flutter (feathers: pool 0xa0 type 0 variant 3), follow the
// actor of their follow slot, can be lifted and thrown, and hitting one costs balance (exe 0x5e51d0). Variant 2 does
// not flee, has no hurt reaction and is not startled by the player's approach.
// States [0x88f9a4]: 0 0x45daa0 init, 1 0x45db70 free (sub-states [0x88f9b8]), 2 0x45e990 lifted (subs [0x88f9e8]),
// 3 0x45ec40 vanish (after its event), 4 0x468fa0 freed.
struct TownBirdFields : ActorClassState {
    explicit TownBirdFields(AnimatedActor& actor) : followSlot(actor.params[0]), bits(actor.shownParts), animKey(actor.hopperBytes[0]), hitsTaken(actor.hopperBytes[1]) {}
    uint8_t& followSlot; // the actor it follows (0: none)
    uint8_t& bits; // kBird*
    uint8_t& animKey;
    uint8_t& hitsTaken;
    HitBox hitBox; // its contact angle: the hit's direction
    int16_t timer = 0;
    int16_t heading = 0;
    uint8_t side = 0; // a random side / a pick
    int16_t homeX = 0;
    int16_t homeZ = 0;
    int16_t hits = 0; // darkens its tint
    int16_t hurtTicks = 0;

    void clearBlock() override {
        hitBox = {};
        timer = 0;
        heading = 0;
        side = 0;
        homeX = 0;
        homeZ = 0;
        hits = 0;
        hurtTicks = 0;
    }
};

// ---- exe 0x45f010: pool 0x20 type 0xa sub 1 -- a standing cast member (variants [0x88f9fc]: 0 0x45f0c0 where the
// record put it, 1 0x45f140 0x490 in front of the player facing it); after the set-up only the animation
struct StandingCastFields : ActorClassState {
    explicit StandingCastFields(AnimatedActor& actor) : recordKey(actor.params[0]) {}
    uint8_t& recordKey; // the key its animation starts at (variant 0)
    uint8_t animKey = 0;

    void clearBlock() override {
        animKey = 0;
    }
};

} // namespace od::port::calbania_cast_state
