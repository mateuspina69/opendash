// The town's side scripts 0x27 (ST0B), 0x70 (ST06), 0x72 (ST09), 0x73 (ST47) and their cast
// (content/yosyonke/town_side_scripts.cpp).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// The cast (registered by content/yosyonke/register.cpp)
void st09BystanderUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x448e40: ST09 pool 0x20 type 0 sub 3 (0x72)
void st47VisitorUpdate(const PlayerEnv& env, uint32_t actor);   // exe 0x449270: ST47 pool 0x20 type 0 sub 2 (0x73)
void st47FlyerUpdate(const PlayerEnv& env, uint32_t actor);     // exe 0x5ba960: ST47 pool 0x60 type 0x59 (0x73)

// Scripts 0x27 / 0x70 / 0x72 / 0x73, dispatched by the handler [0x89b6d0 + id * 4]
bool townSideScriptPorted(uint8_t id);
bool townSideScriptTick(const PlayerEnv& env, uint8_t id);

} // namespace od::port
