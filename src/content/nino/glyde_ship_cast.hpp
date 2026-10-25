// Glyde's ship in the cutscenes of his attack on Nino (pool 0x20 type 0x54), content/nino/glyde_ship_cast.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4df510 (ST17 script 0x39): a ship flying in along its heading (variant 0 fast, 1 slow)
void glydeFlyInShipUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4df6a0 (ST18 scripts 0x3a / 0x3b): the ship by its variant -- still, effects at its nose, hit by three blasts,
// destroyed (explosions, debris, smoke, the fall), a bobbing hover
void glydeHoverShipUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
