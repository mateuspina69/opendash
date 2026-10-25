// The cutscene actors' shared steps (the classes the scripts spawn and drive on their timeline: the cast of the ST39 /
// ST3A / attract scenes and of the chapters' cutscenes): the previous-pose copies at the top of an update, the common
// state 0 (the model bound, the box, the class block cleared), the velocity / acceleration motion, and the animation
// tail that starts the animation asked for, holds the pose or steps it. Each class keeps its own bytes (the key its
// animation starts at, its flags) in its class block; these steps take their values.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/keyframe_tracks.hpp"

namespace od::port {

// The class block of the talking cast (the layout most cutscene classes share): its texture tracks (its face), its
// flags and the key its next animation starts at
struct CastFields : ActorClassState {
    explicit CastFields(AnimatedActor& actor) : heldKey(actor.townBirdHits) {}
    uint8_t& heldKey; // the key a held pose restarts at (castAnimateFromKey)
    KeyframeTracks faceTracks;
    uint8_t castFlags = 0; // kCast*
    uint8_t animKey = 0;

    void clearBlock() override {
        faceTracks = {};
        castFlags = 0;
        animKey = 0;
    }
};
constexpr uint8_t kCastHoldPose = 0x01;  // posed without stepping (the crew class 0x4e9ae0: restarted at its held key)
constexpr uint8_t kCastShadow = 0x02;    // a shadow under it (render output)
constexpr uint8_t kCastOnGround = 0x04;  // kept on the ground
constexpr uint8_t kCastHoldPoseB = 0x08; // also posed without stepping (its setter is not traced)
constexpr uint8_t kCastHeld = kCastHoldPose | kCastHoldPoseB; // what holds the pose of most cast classes

// the animation byte of a slot with no animation shown yet (prevAnim: the first tick starts `anim`)
constexpr uint8_t kNoAnimShown = 0xff;

// the copies at the top of a cast class's update: the previous position and rotation, and the animation of the last
// tick
void castSavePose(AnimatedActor& actor);
// the common state 0 of the cast classes: model variant 0 bound (none: the actor freed, false), the collision box
// `box` (exe data), the class block cleared, state 1
bool castBindModel(AnimatedActor& actor, uint32_t box);
// ... and the animation `anim` asked (none shown yet), the velocities / accelerations cleared when `clearMotion`
bool castBind(AnimatedActor& actor, uint32_t box, uint8_t anim, bool clearMotion);
// the velocities and the accelerations cleared (the lanes one short at a time, z / y / x)
void clearVelocity(Actor& actor);
// the velocities += the accelerations (x, z, y), then the actor moved by them (exe 0x5e3830)
void castMove(Actor& actor);
// the drift of the ST39 cast: the planar velocities += their accelerations where those are set, then the actor moved
// by (x, 0, z) when it has a planar velocity
void castDrift(Actor& actor);
// the animation tail of a cast class: a new animation started at `key`; else, when `flags & holdMask`, the current key
// decoded and posed without stepping (a held pose); else the animation stepped and posed
void castAnimate(AnimatedActor& actor, uint8_t key, uint8_t flags, uint8_t holdMask);
// the animation tail of the cast classes that restart a held pose (the ST39 crew, the attract's ST03 cast): a new
// animation, or a pose held (castFlags & kCastHeld), started at the held key (kCastHoldPose) or at animKey; else the
// animation stepped and posed
void castAnimateFromKey(AnimatedActor& actor, const CastFields& cast);

} // namespace od::port
