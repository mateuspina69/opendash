// The class states of flutter_cabin.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::flutter_cabin_state {

// ---- Roll in the Flutter (exe 0x4e6dc0, ST04 sub 0 pool 0x20 type 0x60) ---------------------------------------------
// She stands at her seat; talking to her (her message, the travel menu 0x46 -> 0x4a) is the way to the travel map.
// States (the exe's table 0x896334): 0 exe 0x4e6f80 (the model by the variant, the boxes 0x896318 / 0x896328, the bone
// scales exe 0x5e3220), 1 exe 0x4e7010 (the talk test, then the sub-states 0x89633c). Her shadow (exe 0x5f20f0) is render
// output and left out.
struct FlutterRollFields : ActorClassState {
    explicit FlutterRollFields(AnimatedActor& actor) : pose(actor.params[1]), shownPart(actor.params[2]), message(actor.params[3]), mode(actor.shownParts), watchTimer(actor.hopperBytes[0]), hitBits(actor.hopperBytes[1]) {}
    uint8_t& pose; // kPoseHeld | kPoseAside | kPoseTalkFlag
    FieldAlias<int8_t, uint8_t> shownPart; // which of her three parts shows (1 / 2, else the first)
    uint8_t& message;
    uint8_t& mode; // kFlutterRoll*
    uint8_t& watchTimer;
    uint8_t& hitBits; // what the collisions found (their reader is not traced)
    HitBoxHead box;
    uint8_t look = 0; // cleared by the talk (its reader is not traced)
    uint8_t gaze = 0; // 1 / 2: where she looks (its reader is not traced)

    void clearBlock() override {
        box = {};
        look = 0;
        gaze = 0;
    }
};

// Its bytes: the variant (+0xc), floating (+0xf: not kept on the ground), the head's turn (the spare lanes of the
// velocity / the acceleration), the point it looks at (a height and a yaw), the turn's step, its flags, the key its
// animation starts at and whether it looks
// Its bytes: the variant (+0xc), floating (+0xf: not kept on the ground), the head's turn (the spare lanes of the
// velocity / the acceleration), the point it looks at (a height and a yaw), the turn's step, its flags, the key its
// animation starts at and whether it looks
struct CabinNpcFields : ActorClassState {
    explicit CabinNpcFields(AnimatedActor& actor)
        : variantIndex(actor.params[0]), floating(actor.params[3]), headPitch(actor.velocity.spare),
          headYaw(actor.acceleration.spare) {}
    FieldAlias<int8_t, uint8_t> variantIndex;
    uint8_t& floating;
    int16_t& headPitch;
    int16_t& headYaw;
    int16_t lookHeight = 0;
    uint16_t lookYaw = 0;
    int16_t turnStep = 0;
    uint8_t npcFlags = 0; // kRestartAnim
    uint8_t animKey = 0;
    uint8_t looking = 0;

    void clearBlock() override {
        lookHeight = 0;
        lookYaw = 0;
        turnStep = 0;
        npcFlags = 0;
        animKey = 0;
        looking = 0;
    }
};

} // namespace od::port::flutter_cabin_state
