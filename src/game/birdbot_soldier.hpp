// The Birdbot soldier (pool 0x20 class 0x493a60, type 0x2d sub-type 1 of ST15..ST17, ST30 / ST31: the walking Birdbot
// of Glyde's base and of the Nino missions -- also the ST17 boat's crew): it walks to its target (the player while
// chasing, else its remembered point), gives way to its neighbours, answers its group's call, shoots (game/
// walker_shot) and butts; it can be lifted and thrown; dead it leaves a knocked-down Birdbot (game/birdbot_down).
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x493a60: the soldier's update (ActorUpdateFn)
void birdbotSoldierUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
