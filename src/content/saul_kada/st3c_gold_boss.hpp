// Teisel's fight with the gold statue (ST3C sub 2, the pirate base; pool 0x20 type 0x73 sub 0, exe 0x4f6e60), spawned by
// the end of script 0x5d (content/saul_kada/pirate_base_scripts: record 0x8ab8c8), see content/saul_kada/st3c_gold_boss.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4f6e60: phase one (idle / turn / throw pieces / swing gold / taunt / stagger / defeat), phase two once the
// statue is destroyed (leaps, chases, rings, lunges, three attacks); its end sets story flag 0x6f
void goldStatueBossUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
