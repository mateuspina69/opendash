// ST4B's scripts 0x5a / 0x5b and their cast, content/late_game/st4b_scripts.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x5f30e0 (the full-screen white TILE of OT entry 1, nothing while the frame draws nothing) is drawn this tick by
// script 0x5b's step 0x535e90 (for the app's white-screen pass, as game/hud scriptWhiteDraw for scripts 0x26 / 0x3c /
// 0x71)
bool st4bScriptWhiteScreen();

void bossRetreatCastUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x4bc9c0: ST4B type 0x3f sub 2 (script 0x5a)
void bossEscapeCastUpdate(const PlayerEnv& env, uint32_t actor);  // exe 0x4bce70: ST4B type 0x3f sub 3 (script 0x5b)
void stillFigureCastUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x48ade0: ST4B type 0x27 sub 0 (script 0x5b)

// Scripts 0x5a / 0x5b, dispatched by the handler [0x89b6d0 + id * 4]
bool st4bScriptPorted(uint8_t id);
bool st4bScriptTick(const PlayerEnv& env, uint8_t id);

} // namespace od::port
