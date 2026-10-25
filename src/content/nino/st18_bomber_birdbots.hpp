// The bomber Birdbots of the last Nino mission (ST18 pool 0x20 class 0x495af0, type 0x2d sub-type 1): dropped in, each
// takes a bomb (game/bomb, held between its bones 2 and 3), walks toward the centre of the deck until within 0x480,
// throws the bomb, spins and hops off the edge. Shot down they leave a knocked-down Birdbot (game/birdbot_down).
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x495af0: the bomber's update (ActorUpdateFn)
void st18BomberBirdbotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
