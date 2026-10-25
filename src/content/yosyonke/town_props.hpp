// The town's type-1 statics of ST08 and ST09 (their stage tables' static type 1, content/yosyonke/town_props.cpp): props
// you talk to, a breakable one, and the ST09 block you kick, lift and throw.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x58dff0: the ST08 static type 1 (variant 4: a prop you talk to, message 0x5f; the others: nothing)
void st08PropUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x58e0e0: the ST09 static type 1 (variant 0: breakable, 1: kicked / lifted / thrown, 4: a solid, 5: a prop you
// talk to, message 0x13; 2..4: late-loop scenery)
void st09PropUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
