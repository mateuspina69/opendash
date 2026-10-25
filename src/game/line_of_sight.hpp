// The line-of-sight probe: an actor that wants to know whether it sees a point spawns a probe (exe 0x5e58f0 / 0x5e5970;
// pool 0xe0 type 7 of every stage, exe 0x418bd0) that walks a small box from the actor's point toward the target (an
// actor's point by its slot, exe 0x5e5840, or a fixed point) a few steps a tick through the room's collision, and
// reports in a result byte of the owner: running, reached, blocked.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/vectors.hpp"

namespace od::port {

// the owner's result byte
constexpr uint8_t kSightRunning = 0x80;
constexpr uint8_t kSightReached = 0xc0;
constexpr uint8_t kSightBlocked = 0xc1;
constexpr uint8_t kSightDone = 0x40; // reached or blocked: the probe is freed
constexpr uint8_t kLineOfSightType = 7; // the probe's type in the controller tables

// exe 0x5e5840(slot, bone, out): the s16 point (x, y, z) of the actor of a slot id -- the player's bone point `bone`,
// an animated actor's bone point `bone`, a static's position (integer parts); -1 for the other pools (out left as it
// was), else 0. The animated branch is content/yosyonke/town_story's transcription: the exe calls exe 0x5e1810 and drops
// its result, leaving out as it was
int actorPointOfSlot(uint32_t slot, uint32_t bone, int16_t out[3]);

// exe 0x5e58f0(owner, mask, point, box, target, result, steps): a probe from the s16 point (its four lanes copied) with
// the collision layers `mask`, the box half-size box & 0xffff and step length box >> 12 & ~0xf, toward the target
// (slot | bone << 16 of an actor), `steps` steps a tick; the result byte set running. exe 0x5e5970: target 0 (the
// player's bone 0)
void lineOfSightSpawn(const ActorHeader& owner, uint16_t mask, const ShortVec4& point, uint32_t box, uint32_t target,
                      uint8_t& result, uint8_t steps);
// exe 0x418bd0 (pool 0xe0 type 7)
void lineOfSightUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
