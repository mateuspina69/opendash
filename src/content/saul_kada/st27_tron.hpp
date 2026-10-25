// ST27 sub 8 (the lava mission: "knock out the Reaverbots and throw them into the lava"): Tron, pool 0x20 type 0x48
// sub 1, exe 0x4cf850. Spawned by the mission's controller (content/saul_kada/st27_mission), which keeps her, reads her
// hit points and sets / clears kPartnerThrowAtPlayer; Bon (content/saul_kada/st27_bon) is linked as her partner.
//
//   tronPartnerUpdate  exe 0x4cf850  the previous pose, the states [0x895284] (0 init 0x4cf9d0, 1 run 0x4cfaa0, 2 free
//                                    0x468fa0), the animation and the pose; while carrying, the carried Reaverbot sits
//                                    at bone 3's offset (0x48, -0x50, -0x10)
//   run 0x4cfaa0: the hit reaction, the sub-states [0x895290] (0 wait for flag 0x681 0x4cfda0, 1 hunt a Reaverbot
//                 0x4cfdc0, 2 knocked back 0x4d0240, 3 carry + throw 0x4d0340), then the hit-list entries
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

void tronPartnerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
