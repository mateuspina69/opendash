// The hopping Reaverbot of the Saul Kada area (ST26 / ST27 / ST34: pool 0x20 type 0x64, exe 0x4eadf0). See
// content/saul_kada/hopping_reaverbot.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kHoppingReaverbotType = 100;

// The hopping Reaverbot's spawn parameters: the wanderers' heading (<< 4) and range (<< 4), and its handling byte --
// what Tron and Bon (content/saul_kada/st27_tron, st27_bon) do with it
struct HoppingReaverbotParams {
    explicit HoppingReaverbotParams(const AnimatedActor& actor) : HoppingReaverbotParams(const_cast<AnimatedActor&>(actor)) {}
    explicit HoppingReaverbotParams(AnimatedActor& actor) : wanderHeading(actor.params[1]), wanderRange(actor.params[2]), handling(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> wanderHeading;
    uint8_t& wanderRange;
    uint8_t& handling;
};
constexpr uint8_t kHandlingTaken = 0x01;    // lifted by the player or grabbed by a partner
constexpr uint8_t kHandlingStunned = 0x02;  // resting after a jump attack / a spin: a partner may grab it
constexpr uint8_t kHandlingCarried = 0x04;  // carried (the carrier sets it every tick)
constexpr uint8_t kHandlingGrabbed = 0x08;  // grabbed (the carrier's lift)
constexpr uint8_t kHandlingThrown = 0x10;   // thrown into the lava ...
constexpr uint8_t kHandlingAtPlayer = 0x20; // ... or at the player
constexpr uint8_t kHandlingHeld = kHandlingCarried | kHandlingGrabbed;
constexpr uint8_t kHandlingBusy = kHandlingTaken | kHandlingHeld; // not to be picked by a partner

// exe 0x4eadf0 (pool 0x20 type 0x64): hops toward the player (variant 0), wanders around its spawn point (1), or
// starts in the air and targets the player / Tron / Bon (2); liftable and throwable, knocked over into a spin by heavy
// damage
void hoppingReaverbotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
