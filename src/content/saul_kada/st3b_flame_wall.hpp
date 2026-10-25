// The pirate base's flame wall (static ST3B type 0x1c, exe 0x5a3330), content/saul_kada/st3b_flame_wall.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kFlameWallType = 0x1c; // the stage static type (ST3B)
// The wall's spawn parameter (its spawners write it): its life (ticks)
struct FlameWallParams {
    explicit FlameWallParams(const StaticActor& actor) : FlameWallParams(const_cast<StaticActor&>(actor)) {}
    explicit FlameWallParams(StaticActor& actor) : life(actor.params, 0) {}
    BytesField<uint16_t, 4> life;
};

// exe 0x5a3330 (static ST3B type 0x1c): a burning flame wall (timed, stacked hit capsules; its polygons not drawn)
void flameWallUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
