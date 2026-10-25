// The ice ruin's story scripts: 0x48 (ST2D sub 5: the shrinking block's rooms), 0x49 (ST2F sub 6: the critters'
// scene), 0x4a / 0x4b (ST2F sub 7: the tutorial of class 0x4d6b40 and its scene), in the story's script chain
// (content/registry.cpp), content/yosyonke/ice_ruin_scripts.cpp.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"

namespace od::port {

struct PlayerEnv;

// the scripts' handlers 0x52f8f0 (0x48), 0x52fb60 (0x49), 0x52fe00 (0x4a), 0x530010 (0x4b): whether `handler` is one
// of them / its tick
bool iceRuinScriptPorted(RoutineId handler);
bool iceRuinScriptTick(const PlayerEnv& env, RoutineId handler);

} // namespace od::port
