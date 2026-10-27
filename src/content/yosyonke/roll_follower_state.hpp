// The class states of roll_follower.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port::roll_follower_state {

// ==== Roll the follower and Roll in the field: their class bytes =====================================================
// Her head, the search heading (the rotation's spare lane), her hit-list box (its contact: the angle of the hit), where
// the player was seen and the point she walks to, the probe's result, the collision and behaviour bits
struct RollFields : ActorClassState {
    explicit RollFields(AnimatedActor& actor) : kind(actor.params[0]), faintedMessage(actor.params[2]), message(actor.params[3]), searchYaw(actor.rotation.spare), headPitch(actor.velocity.spare), headYaw(actor.acceleration.spare), sight(actor.hopperBytes[4]), hits(actor.hopperBytes[5]), behaviour(actor.townBirdHits), coolDown(actor.townBirdHitsHigh), hitsToKnockBack(actor.hopperTimer) {}
    uint8_t& kind; // the class's variant index (forced to 0 every tick)
    FieldAlias<int8_t, uint8_t> faintedMessage; // the field: the fainted talk's message
    FieldAlias<int8_t, uint8_t> message; // the talk's message
    int16_t& searchYaw;
    int16_t& headPitch;
    int16_t& headYaw;
    uint8_t& sight; // the line-of-sight probe's result (kSight*)
    uint8_t& hits; // kWalk*
    uint8_t& behaviour; // kRoll*
    uint8_t& coolDown; // the bump's cool-down; the field: the hurt blink
    uint8_t& hitsToKnockBack; // the field
    HitBox box;
    int16_t timer = 0; // the search's ticks / the bump's turn; the field: the hit's heading,
    int16_t seenX = 0; // where the player was last seen (integer x / z)
    int16_t seenZ = 0;
    int16_t targetX = 0; // the point she walks to (the player's position one sighting earlier)
    int16_t targetZ = 0;
    int8_t animKey = 0; // the field: the key her animations start at

    void clearBlock() override {
        box = {};
        timer = 0;
        seenX = 0;
        seenZ = 0;
        targetX = 0;
        targetZ = 0;
        animKey = 0;
    }
};

// ==== Roll at Joe's lab (exe 0x443330, ST08 pool 0x20 type 0 sub 2; the spawner 0x41af40 after the junk shop) =========
// She waits at the shed's door: the talk (her message: the choice; the flags 0x681 / 0x682 the message sets switch her
// head look aside / off), falls when the ground goes. States (the exe's table 0x88ddc4): 0 exe 0x4435a0, 1 exe
// 0x443690 -> the sub-states (the exe's table 0x88ddcc).
struct ShedRollFields : ActorClassState {
    explicit ShedRollFields(AnimatedActor& actor) : kind(actor.params[0]), message(actor.params[3]), headPitch(actor.velocity.spare), headYaw(actor.acceleration.spare), behaviour(actor.guardianBits) {}
    uint8_t& kind;
    FieldAlias<int8_t, uint8_t> message;
    int16_t& headPitch;
    int16_t& headYaw;
    uint8_t& behaviour; // kShed* (the walls reflect kWalkerBounces)
    HitBox box; // its shape asks for the contact output: the hit list writes it after the head
    uint8_t hits = 0; // kWalk*

    void clearBlock() override {
        box = {};
        hits = 0;
    }
};

} // namespace od::port::roll_follower_state
