// ST3C (the pirate base, story 12): the minion spawned near the player by the statue fight's spawner
// (content/saul_kada/st3c_minion_spawner) during Teisel's gold-statue fight (pool 0x20 ST3C type 0x78, exe 0x4ff7d0). See
// content/saul_kada/st3c_minion.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kStatueMinionType = 0x78;
// The minion's spawn parameter: its byte in the spawner's slot block (cleared once its Servbot is gone)
struct StatueMinionParams {
    explicit StatueMinionParams(const AnimatedActor& actor) : StatueMinionParams(const_cast<AnimatedActor&>(actor)) {}
    explicit StatueMinionParams(AnimatedActor& actor) : spawnerSlot(actor.params, 0) {}
    BytesField<uint32_t, 4> spawnerSlot;
};

// exe 0x4ff7d0: rises out of the floor, turns to the player, chases, charges / leaps at him, bounces off walls
// (damaging itself); on its death a blast and a Servbot (pool 0x20 type 0xb variant 3) pops out; freed once that
// Servbot is gone
void statueMinionUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
