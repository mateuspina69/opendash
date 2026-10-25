// The statue of the pirate base's script 0x5d (ST3C pool 0x20 type 0x73 variant 1, exe 0x4fa800) and its hit part
// (static ST3C type 0x50, exe 0x5b7890), content/saul_kada/st3c_statue.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kStatuePartType = 0x50; // the stage's static type of the hit part
// The hit part's parameter: the statue it rides (its spawner writes it)
struct StatuePartParams {
    explicit StatuePartParams(const StaticActor& actor) : StatuePartParams(const_cast<StaticActor&>(actor)) {}
    explicit StatuePartParams(StaticActor& actor) : statue(actor.params, 0) {}
    BytesField<uint32_t, 4> statue;
};

// exe 0x4fa800 (ST3C pool 0x20 type 0x73 variant 1): the statue of script 0x5d (follows the script's step sub-id)
void scriptStatueUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5b7890 (static ST3C type 0x50): the hit part riding the statue's bone 0 (takes the hits for it, flag 0x68 when
// destroyed)
void statuePartUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
