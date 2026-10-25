// The cutscene cast of the chapter's scripts (content/yosyonke/town_story_cast.cpp): the junk shop (ST0A, script 0x4d),
// Roll running off to Joe (ST09, script 0x4e), Joe's lab (ST08 sub 1, script 0x4f) and ST47's script 0x12.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x447aa0 (ST0A pool 0x20 type 0 sub 1, script 0x4d): Roll, the clerk and a third one
void junkShopCastUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x448320 (ST09 pool 0x20 type 0 sub 2, script 0x4e): Roll running off to Joe's lab
void rollRunsOffUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x448520 (ST08 sub 1 pool 0x20 type 0 sub 3, script 0x4f): Roll and Maria at Joe's lab
void joesLabCastUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4474e0 (ST47 pool 0x20 type 0 sub 1): script 0x12's person
void st47PersonUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5915b0 (ST0A static type 1 sub 1): the junk shop's prop -- variant 0 slides in during script 0x4d
void junkShopPropUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
