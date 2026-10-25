// The hovering reaverbot of the Manda ruin (pool 0x20 type 3, exe 0x44d320), content/manda/st14_enemy.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kHoverReaverbotType = 3; // its animated pool type

// The hovering reaverbot's spawn parameters (its spawners write them: content/manda/ruin_spawners)
struct HoverReaverbotParams {
    explicit HoverReaverbotParams(const AnimatedActor& actor) : HoverReaverbotParams(const_cast<AnimatedActor&>(actor)) {}
    explicit HoverReaverbotParams(AnimatedActor& actor) : reverseCircle(actor.params[1]), range(actor.params[2]) {}
    uint8_t& reverseCircle; // it circles its home the other way round (the spin's sign)
    FieldAlias<int8_t, uint8_t> range; // the index of its home's radius / height (exe table 0x88e91c)
};

// exe 0x44d320 (pool 0x20 type 3: ST14): the hovering reaverbot (drifts around its home point, dives at the player
// when near, returns home; liftable / throwable; falls and explodes when killed)
void hoverReaverbotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
