// Actor-vs-actor collision ("solid" actors such as doors and platforms against "movers" such as the player),
// transcribed from dash2.exe, bit-exact against the tests/data/player_ST*.bin goldens (PLG3: the pass is captured at
//
// Per tick (exe 0x542280): the actors' updates submit themselves -- solids with exe 0x414050 into the solid queue,
// movers with exe 0x4140b0 into the mover queue (the player from its state handler) -- then exe 0x4140e0 (0x5422ad,
// right before the player's update) runs every mover against every solid and empties both queues. A solid's
// collision mesh (game/actor_state CollisionMesh) is at actor +0x9c (the mesh pools) or +0xb8 (animated actors).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"

namespace od::port {

// The actor collision pass's queues (exe 0xa5f2a0: 64 solid entries of 12 bytes, then the movers' actor words; the end
// pointers 0xa743e8 / 0xa743e4): what submitted itself this frame, in order
struct SolidEntry {
    ActorRef<ActorHeader> actor; // any submitted slot (an effect or controller one included)
    int16_t type = 0;            // SolidType
    int16_t bone = 0;            // kNoBone: the actor's position
    uint32_t param = 0;
};
struct CollisionQueues {
    static constexpr size_t kSolids = 64;   // the solid queue's room (the exe's next entries are the movers')
    static constexpr size_t kMovers = 0x100; // more than every slot of the pools and the player
    StateArray<SolidEntry, kSolids> solids{};
    uint32_t solidCount = 0;
    StateArray<ActorRef<ActorHeader>, kMovers> movers{};
    uint32_t moverCount = 0;
};
// the queues (one set, for the life of the program)
CollisionQueues& actorCollisionQueues();

// The solid types of a queue entry
enum class SolidType : int16_t {
    PushOut = 0,          // pushed out of its mesh, in the solid's rotated frame
    StandOn = 1,          // ... and stood on / carried
    PushOutAligned = 2,   // as 0, axis-aligned
    StandOnAligned = 3,   // as 1, axis-aligned
};
// a solid entry's bone: none (the mesh follows the actor's position)
constexpr int16_t kNoBone = -1;

// The actor-collision result of a mover (+0x68) and of the push-out: 0 = untouched; else the side of the mover the
// solid was mostly on (kMainSide*, 1..6) | every side it was on (kSolidOn*: the mover was pushed away from it)
constexpr uint32_t kMainSideMask = 0x7;
constexpr uint32_t kMainSideXPlus = 1, kMainSideXMinus = 2, kMainSideZPlus = 3, kMainSideZMinus = 4;
constexpr uint32_t kMainSideYPlus = 5, kMainSideYMinus = 6;
constexpr uint32_t kSolidOnZPlus = 0x100, kSolidOnZMinus = 0x200, kSolidOnXPlus = 0x400, kSolidOnXMinus = 0x800;
constexpr uint32_t kSolidOnYPlus = 0x1000, kSolidOnYMinus = 0x2000; // y grows downwards: YPlus = below
constexpr uint32_t kSolidOnSides = kSolidOnXPlus | kSolidOnXMinus | kSolidOnZPlus | kSolidOnZMinus;
constexpr uint32_t kSolidAboveOrBelow = kSolidOnYPlus | kSolidOnYMinus;
constexpr uint32_t kSteppedOff = 0x4000;      // the mover left the solid it stood on (the platform's move inherited)
constexpr uint32_t kPushGaveUp = 0x80000000u; // the push-out did not converge in 9 passes

// exe 0x414030: both queues empty (the game mode's init; exe 0x4140e0 does the same after each pass)
void actorCollisionReset();
// exe 0x414050: queue a solid (only a live / drawn actor: byte 0 & 3) of `type`, `bone` (kNoBone: the actor's
// position; else the bone whose point the mesh follows) and the param its push records on the player; clears the
// actor's last mover
void actorCollisionSubmitSolid(ActorHeader& actor, SolidType type, int16_t bone, uint32_t param);
// exe 0x4140b0: queue a mover (only when byte 0 & 3); clears its actor-collision result
void actorCollisionSubmitMover(ActorHeader& actor);
// exe 0x4140e0: the pass. A hit -> the mover's actor-collision result, the solid's last mover = the mover, and the
// mover is tested again from the first solid (at most 9 hits). Empties both queues.
void actorCollision();
// exe 0x414f20: an actor standing on a platform (+0x52, +0x54) follows the platform's move since the last tick (+0x10
// vs +0x1c) and its turn (+0x28.. vs +0x30..), yaw included; a platform of the effect / controller pools (slot >=
// 0xa0) carries nothing. Called by the player's update (and the rider follower exe 0x4250d0).
void actorCarry(Actor& actor, const Actor& platform);
// exe 0x4108b0: the angle pair (pitch, yaw) turned off an actor-collision result: a solid above or below flips the
// pitch (returns 1 for one below), a side (the main side) mirrors the yaw when it points into it
int reflectOffActorHit(int16_t& pitch, int16_t& yaw, uint32_t result);

} // namespace od::port
