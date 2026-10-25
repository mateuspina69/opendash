// The class states of carried_npc.cpp's classes: the fields each keeps in its slot (game/actor_state
// ActorClassState).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/keyframe_tracks.hpp"

namespace od::port::carried_npc_state {

// The NPC's own fields. Its class block +0x14c holds the cast variants' keyframe tracks (game/story) or the worker
// variants' hit box.
struct CarriedNpcFields : ActorClassState {
    explicit CarriedNpcFields(AnimatedActor& actor) : cue(actor.params[0]), lastCue(actor.params[1]), scriptSignal(actor.params[3]), bits(actor.shownParts), wanderPoint(actor.townBirdHits), animHold(actor.townBirdHitsHigh) {}
    uint8_t& cue; // the script's cue (a cast member steps when it changes)
    uint8_t& lastCue;
    uint8_t& scriptSignal; // set for the script (variant 5's last step)
    uint8_t& bits; // kNpc* bits
    uint8_t& wanderPoint;
    uint8_t& animHold; // the key a new animation starts at; kHoldFrame: no step
    // the class block's head: the cast variants' face tracks, or the worker variants' hit box (the same bytes; each
    // variant keeps its own)
    KeyframeTracks faceTracks;
    HitBox hitBox;
    uint16_t timer = 0; // the hit reaction's / the idle pause's / the hands' sound ticks
    uint16_t hitYaw = 0; // the yaw of the last hit
    uint8_t collisionBits = 0; // kNpcBlocked | kNpcGroundLost
    uint16_t targetX = 0; // the wander target x / z
    uint16_t targetZ = 0;
    uint16_t damageTaken = 0; // the hits' damage summed (the flash tint fades past 0x3c)
    uint16_t soundTimer = 0;

    void clearBlock() override {
        faceTracks = {};
        hitBox = {};
        timer = 0;
        hitYaw = 0;
        collisionBits = 0;
        targetX = 0;
        targetZ = 0;
        damageTaken = 0;
        soundTimer = 0;
    }
};

} // namespace od::port::carried_npc_state
