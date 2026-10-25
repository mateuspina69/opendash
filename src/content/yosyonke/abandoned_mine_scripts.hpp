// the Abandoned Mine's story scripts: script 0x50 (Roll at the lift of sub 1, the ride down), 0x51 (the hall of sub 11),
// 0x52 / 0x53 (the guardian of sub 12: its entrance and its end) and 0x54 (back in sub 11: the device that leads out), in
// the story's script chain (content/registry.cpp); and the stage block the scripts and the ruin's room handlers
// (content/yosyonke/abandoned_mine_rooms) share.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_state.hpp"
#include "game/stage_state.hpp"

namespace od::port {

struct PlayerEnv;

// the Abandoned Mine's stage block (game/stage_state StageState::block, 0xa6d298): the actors its scripts and room handlers
// hand each other
struct AbandonedMineStageBlock : StagePart<AbandonedMineStageBlock> {
    ActorRef<Actor> guardian; // +0x0: the guardian of sub 12 (script 0x52's cast), watched by the sub-12 handler
    ActorRef<Actor> roll; // +0x4: Roll by the lift of sub 1 (class 0x4e6110), spawned by the sub-1 handler
};
inline AbandonedMineStageBlock& abandonedMineStageBlock() {
    return stageBlock().part<AbandonedMineStageBlock>();
}

// the scripts' ids (the story's script table: game/story)
constexpr uint8_t kRuinLiftScript = 0x50;
constexpr uint8_t kRuinHallScript = 0x51;
constexpr uint8_t kGuardianEntranceScript = 0x52;
constexpr uint8_t kGuardianEndScript = 0x53;
constexpr uint8_t kRuinDeviceScript = 0x54;

// the scripts' handlers 0x531840 (0x50), 0x532090 (0x51), 0x532560 (0x52), 0x532790 (0x53), 0x5329c0 (0x54): whether
// `handler` is one of them / its tick
bool abandonedMineScriptPorted(RoutineId handler);
bool abandonedMineScriptTick(const PlayerEnv& env, RoutineId handler);

} // namespace od::port
