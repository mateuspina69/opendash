// The ST05 / ST06 props: the talk props 0x58dbc0, the decorations 0x58ddf0 and the posed figure 0x481000
// (content/yosyonke/st05_st06_props.cpp).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x58dbc0 (ST05 / ST06 statics): talk props ("use" -> message [0x8cd99c + variant]) and the hanging bar (variant 2)
void townTalkPropUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x58ddf0 (ST05 / ST06 statics): swaying / spinning decorations (by variant)
void decorationUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x481000 (ST05 / ST06 pool 0x20): a posed figure (bind, idle, animation)
void posedFigureUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
