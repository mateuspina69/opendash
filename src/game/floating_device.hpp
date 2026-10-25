// The floating device (pool 0x20 class 0x4f2c40, type 0x6f: ST0F sub 11, the sub-ruins ST4F / ST50 / ST51): a prop that
// bobs on the 64-step sine, spins, is a solid to stand on and is examined -- a plain message, or (+0xc set) the
// event that leads out (the first ruin's "go up?" of script 0x54's records), after which it is freed.
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x4f2c40: the floating device's update (ActorUpdateFn)
void floatingDeviceUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
