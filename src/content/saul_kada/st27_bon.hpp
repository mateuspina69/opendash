// ST27 sub 8, the lava mission ("knock out the Reaverbots and throw them into the lava; Tron and Bon also throw
// them"): Bon Bonne, pool 0x20 type 0x51 sub 1 (exe 0x4dc340), spawned by the mission's controller
// (content/saul_kada/st27_mission) at (-0x400, -0x200, 0x280) yaw 0x800, Tron linked as his partner.
//
// States [0x8957d4]: 0 init 0x4dc4c0, 1 main 0x4dc590, 2 free 0x468fa0. Sub-states [0x8957e0]: 0 walk in 0x4dc890,
// 1 pick / chase a Reaverbot 0x4dc960, 2 knocked down 0x4dcb30, 3 carry and throw 0x4dcb90. His bytes: the partners'
// (content/saul_kada/st27_partners).
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

void bonPartnerUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
