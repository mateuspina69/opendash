// The Birdbots' shared body routines (pool 0x20 type 0x2d, the Birdbot classes of ST15..ST18, ST1F, ST30 / ST31: the
// soldier game/birdbot_soldier, the knocked-down one game/birdbot_down, the bombers of content/nino and
// content/calbania): their airborne arc, their actor-collision bits and their wall / ground step, on the two class
// bytes they share (+0x15e behaviour bits, +0x161 contact bits).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port {

struct PlayerEnv;

// The class bytes the shared routines read and write
struct BirdbotBody : ActorClassState {
    explicit BirdbotBody(AnimatedActor&) {}
    uint8_t bits = 0; // kBirdbotBounces | ... (each class's own bits besides)
    uint8_t contact = 0; // kBirdbot* contact bits of this tick

    void clearBlock() override {
        bits = 0;
        contact = 0;
    }
};
constexpr uint8_t kBirdbotBounces = 0x01;    // +0x15e: the walls reflect its rotation
// +0x161
constexpr uint8_t kBirdbotWall = 0x01;       // a wall (an actor's side / the push-out's)
constexpr uint8_t kBirdbotLanded = 0x02;     // landed / bounced off a floor
constexpr uint8_t kBirdbotGroundLost = 0x04; // more than 0x20 above the ground
constexpr uint8_t kBirdbotInWater = 0x08;    // the test move's 0x20000000 (hypothesis: a water / kill volume)
constexpr uint8_t kBirdbotOnPlatform = 0x10; // standing on an actor
constexpr uint8_t kBirdbotActorSide = 0x20;  // an actor's side hit (with kBirdbotWall)
constexpr uint8_t kBirdbotType = 0x2d; // the Birdbots' animated type (their row of the stats table)

// exe 0x492e70(actor, yaw): the airborne arc along the yaw -- landed / blocked (kBirdbotLanded | kBirdbotOnPlatform):
// the forward speed quartered, the fall bounced, 1; else the move and the gravity, 0
int birdbotArc(AnimatedActor& actor, BirdbotBody& body, int yaw);
// exe 0x492ed0: the actor-collision result -> the contact bits (the walls; the floors reflect a bouncing one), standing
// on an actor
void birdbotContactHits(AnimatedActor& actor, BirdbotBody& body);
// exe 0x4958f0: the walls (the push-out's result kept; back to the previous position when it fails) and the ground (its
// height kept in `groundHeight`); 1 = the ground lost (more than 0x20 below)
int birdbotCollide(const PlayerEnv& env, AnimatedActor& actor, BirdbotBody& body, int16_t& groundHeight);

} // namespace od::port
