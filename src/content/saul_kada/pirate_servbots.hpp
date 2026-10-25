// The Servbots of the Saul Kada pirate attack (ST29 / 2A / 2B / 3B / 3C / 48; pool 0x20 type 0xb), see
// content/saul_kada/pirate_servbots.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kPirateServbotType = 0xb;
constexpr uint8_t kServbotOnFootSubtype = 0, kBombServbotSubtype = 1;
constexpr uint8_t kServbotReportsGone = 3; // the variant (& 0x7f) that tells its spawner when it is freed
constexpr uint8_t kServbotSingle = 0x80;   // its variant's bit: only one at a time (another live Servbot frees it)
// The Servbot's spawn parameter for that variant: the spawner's byte it sets to 1 when it is freed
struct PirateServbotParams {
    explicit PirateServbotParams(const AnimatedActor& actor) : PirateServbotParams(const_cast<AnimatedActor&>(actor)) {}
    explicit PirateServbotParams(AnimatedActor& actor) : goneFlag(actor.params, 0) {}
    BytesField<uint32_t, 4> goneFlag;
};

// exe 0x45f240 (type 0xb sub 0): the Servbot on foot (wanders / runs around the player, knocked down, liftable /
// throwable)
void pirateServbotUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x464270 (type 0xb sub 1, ST2A): the bomb-lobbing Servbot; beaten (or its floor cell opened) it pops out a
// Servbot on foot
void bombServbotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
