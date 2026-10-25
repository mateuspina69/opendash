// ST27 / ST28 (the fire ruin, story 13): the cutscene cast of pool 0x20 type 0x48 (hypothesis names), see
// content/saul_kada/fire_ruin_cutscene_cast.cpp. All but the prop dropper key on the running script's step (its sub-id
// and its tick).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4d0b80 (ST28 type 0x48 sub 1, record 0x8a8cf8): holds a prop (static type 0x40 sub 0) at its bone 3
void propHolderUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4d0d90 (ST27 type 0x48 sub 2, scripts 0x40 / 0x41): variants 0..3 turn at fixed ticks of the steps; variant 1
// is placed and walks on step 6, freed on step 7
void entranceCastUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4d1040 (ST28 type 0x48 sub 2, scripts 0x44 / 0x47): variant 0 walks, turns, jumps up; 1 walks and turns; 2
// stops / resumes its animation, turns, walks, carrying a thrown prop (static type 0x40 sub 1)
void depthsCastUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4d14b0 (ST28 type 0x48 sub 3, script 0x46): drops three spark emitters at its bones 3 / 4 / 7 and a prop
// (static type 0x40 sub 2), then rises until pushed out of the ground, leaving dust at the ground every 8 ticks
void propDropperUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
