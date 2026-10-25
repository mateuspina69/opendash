// The gunship over Nino's square (ST18 static type 0x1b sub 1, exe 0x59f880), content/nino/square_gunship.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x59f880: the gunship that appears over Nino's square after script 0x37 (record 0x8a6840: variant 1, its story
// flag 0x2b): it circles the player, fires pairs of shots and dives at him; shot down it sinks to the square and rocks
// until it settles (its flag set: the ST18 handler 0x57f620 starts script 0x38). Variants 0 / 3 crash instead; variant
// 3 bombs.
void squareGunshipUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
