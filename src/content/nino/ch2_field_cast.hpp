// The pool-0x20 actors of the Nino chapter's fields (ST16 / ST17 / ST46), content/nino/ch2_field_cast.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x49d1d0 (ST17 type 0x30 sub 1; records 0x8a7638 / 0x8a7798 of content/nino/ch2_scripts): the hovering target --
// bobs, two hit capsules + a solid, rocks when hit, smokes below 1/2 / 1/4 of its hit points, falls and marks enemy id
// 0xd defeated
void hoverTargetUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x49de80 (ST46 type 0x30 sub 0): the hovering drifter -- variants 0 / 1 drift forward (1 also turns), variant 2
// rocks, sinks forward and trails smoke (static type 3 variant 2)
void hoverDrifterUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4d2e80 (ST16 type 0x49 sub 1; record 0x8a6224 of content/nino/ch2_scripts): the crashing carrier -- drifts forward
// until flag 0x690, then spins down trailing smoke and explodes (a blast + 3 static type 0x26 smoke columns)
void carrierCrashUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x4ea500 (ST46 type 0x63 sub 0): the scripted walker -- walks 30 ticks, turns 30, walks 45, stops
void scriptedWalkerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
