// The dust puff (effect type 7 of the global effect table, exe 0x55f850): two textured bands on the ground growing to
// their size over their ticks (draw commands) -- the player's landing dust (game/player/player_effects) and the pop of
// an enemy shot (game/enemy_shot).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

// The puff's fields (its effect slot): the spawner's size, ticks and placement, its own tick
struct DustPuffFields : ActorClassState {
    explicit DustPuffFields(EffectActor& actor) : size(actor.params, 0), ticks(actor.params[2]), airborne(actor.params[3]) {}
    BytesField<uint16_t, 4> size;
    FieldAlias<int8_t, uint8_t> ticks; // its life (the bands grow to `size` over it)
    uint8_t& airborne; // 0: put on the ground first
    uint16_t tick = 0;
};

// exe 0x55f850 (effect type 7)
void dustPuffUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
