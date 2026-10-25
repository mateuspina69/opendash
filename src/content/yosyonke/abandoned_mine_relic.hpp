// The relic of the Abandoned Mine's hall (ST0F sub 11; pool 0x20 class 0x4dfc70, type 0x55): a talking prop of scripts
// 0x51 / 0x54's records (content/yosyonke/abandoned_mine_scripts) -- examined, it talks (message +0xf); outside a talk its
// animation follows +0xd (the scripts' cue), in a talk the message's speaker animations; its two texture tracks run on
// the class block; on the ground every tick, a solid to stand on.
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x4dfc70: the relic's update (ActorUpdateFn)
void abandonedMineRelicUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
