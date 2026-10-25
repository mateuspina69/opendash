// The room collision step of the walking NPC classes (the town's Roll classes 0x445b00 / 0x443330 / 0x4e4c20 and the
// ST0D critter 0x509830; each class has its own copy in the exe: 0x446580, 0x443930, 0x4e5df0, 0x50a470): the walls
// (the push-out, the position back on a failed one; a bouncing walker's rotation reflected off them), else its
// territory, optionally the mover queued for the actor collision, then the ground (a walker on the ground follows it
// unless it drops away; an airborne one lands on a floor / ceiling hit). The result bits go into the class's byte; its
// fall step (exe 0x4844b0 / 0x50a360) bounces on a landing.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"

namespace od::port {

// the class's collision bits
constexpr uint8_t kWalkHitSidesLow = 0x1;  // the push-out's 0x300 sides (its x walls)
constexpr uint8_t kWalkHitSidesHigh = 0x2; // its 0xc00 sides (its z walls)
constexpr uint8_t kWalkHitSides = kWalkHitSidesLow | kWalkHitSidesHigh;
constexpr uint8_t kWalkLanded = 0x4;       // bounced off a wall into the floor / landed
constexpr uint8_t kWalkGroundLost = 0x8;   // the floor dropped away
// the class's behaviour bit the collision reads
constexpr uint8_t kWalkerBounces = 0x40;   // the walls reflect its rotation (and an airborne walker does not land)

// the collision step: the bits cleared, then set; 1 when the ground dropped more than `maxDrop` below the walker on the
// ground (kWalkGroundLost), else 0
int walkerCollide(const PlayerEnv& env, Actor& actor, uint8_t& hits, const uint8_t& behaviour,
                  bool queueMover, int maxDrop);
// the fall step: landed (kWalkLanded) -> the fall reversed and halved, the forward speed halved, the pitch 0, airborne,
// 1; else moved along the yaw by (0, the fall, the forward speed), the fall accelerated, 0
int walkerFallStep(Actor& actor, const uint8_t& hits, int yaw);

} // namespace od::port
