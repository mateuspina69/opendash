// The bars the player hangs from on ST16 / ST17 and the ST17 gate its lever opens, content/nino/bars_and_gate.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x58f040 (ST16 static type 1): by the variant -- 0 / 1 the halves of a gate swinging open on the script's tick,
// 2 a bar that comes down when hung from (its timer started, back up when it ends), 3 the mission lever (it rises with
// flag 0x682; pulled down: flag 0x27)
void st16BarUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x58f820 (ST17 static type 1): variant 1 the lever of the gate (hung from, it comes down and opens the gate)
void st17LeverUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5abe60 (ST17 static type 0x2f): the gate its lever opens (a slide, a bounce; story 6: a wave of two statics)
void st17GateUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
