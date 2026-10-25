// The homing shot of the Nino ruin's stage tables (pool 0x40 stage type 0x23: ST37's gauge reaverbots, ST38's boss
// sparks), content/nino/homing_shot.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kHomingShotType = 0x23;
// The shot's spawn parameters: the ticks it homes on the player before it bursts (0: until it hits something) and its
// damage
struct HomingShotParams {
    explicit HomingShotParams(const ProjectileActor& actor) : HomingShotParams(const_cast<ProjectileActor&>(actor)) {}
    explicit HomingShotParams(ProjectileActor& actor) : homingTicks(actor.params, 0), damage(actor.params[2]) {}
    BytesField<int16_t, 4> homingTicks;
    FieldAlias<int8_t, uint8_t> damage;
};
// its variants: 0 / 2 face the player while they rise
enum class HomingShotVariant : uint8_t { Aimed = 0, Spread = 2 };

// exe 0x5cc490 (states [0x8d166c]): the rise (or fall) to its turning point, then it homes on the player's chest until
// its ticks end (a flash); the shield, the walls and the hits end it; a hit entry and a spinning billboard (draw command
// 0x2c)
void homingShotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
