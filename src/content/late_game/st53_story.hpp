// ST53 (the snowy track, story 0xf): the room handler of sub 0 (0x58c0c0, table 0x8cd800 = story 0xf of 0x8cd80c),
// scripts 0x60 (0x537260) and 0x61 (0x5376b0) and their pool-0x20 cutscene cast (content/late_game/ch4_story holds the
// sub 1 handler 0x58c100 and script 0x62). Shadows (exe 0x5f20f0) are render output and left out. Names are hypotheses
// from the observed behaviour.
#pragma once

#include <cstdint>

#include "engine/routine_ids.hpp"
#include "game/actor_movement.hpp"

namespace od::port {

// Room-script handler of ST53 sub 0 at story 0xf (0x58c0c0): fn = the handler, run = tick it (false: only "is it known")
bool st53RoomHandler(RoutineId fn, bool run);
// Scripts 0x60 (0x537260) / 0x61 (0x5376b0), dispatched by the handler [0x89b6d0 + id * 4]
bool st53ScriptPorted(uint8_t id);
bool st53ScriptTick(const PlayerEnv& env, uint8_t id);

// The cast (pool 0x20, spawned by the camera scripts' op 0x40 records). Script 0x60 (camera 0x8ac044): the extras
// 0x503a80 (type 0x7a / 0) and 0x506bc0 (0x7b / 0), the walker 0x4b35d0 (0x3a / 0), the fading figure 0x488a90 (0x24 /
// 0), the faced talker 0x45c840 (9 / 0), the lead talker 0x4f57d0 (0x72 / 0), the masked talker 0x4f0ed0 (0x69 / 0).
// Script 0x61 (camera 0x8ac328): the faced talker 0x45cbf0 (9 / 1), the lead talker 0x4f5f70 (0x72 / 1), the masked
// talker 0x4f1220 (0x69 / 1), the standing figure 0x4991a0 (0x2d / 0).
void script60FacedTalkerUpdate(const PlayerEnv& env, uint32_t actor);   // exe 0x45c840
void script61FacedTalkerUpdate(const PlayerEnv& env, uint32_t actor);   // exe 0x45cbf0
void script60FadingFigureUpdate(const PlayerEnv& env, uint32_t actor);  // exe 0x488a90
void script61StandingFigureUpdate(const PlayerEnv& env, uint32_t actor); // exe 0x4991a0
void script60WalkerUpdate(const PlayerEnv& env, uint32_t actor);        // exe 0x4b35d0
void script60MaskedTalkerUpdate(const PlayerEnv& env, uint32_t actor);  // exe 0x4f0ed0
void script61MaskedTalkerUpdate(const PlayerEnv& env, uint32_t actor);  // exe 0x4f1220
void script60LeadTalkerUpdate(const PlayerEnv& env, uint32_t actor);    // exe 0x4f57d0
void script61LeadTalkerUpdate(const PlayerEnv& env, uint32_t actor);    // exe 0x4f5f70
void script60ExtraAUpdate(const PlayerEnv& env, uint32_t actor);        // exe 0x503a80
void script60ExtraBUpdate(const PlayerEnv& env, uint32_t actor);        // exe 0x506bc0

} // namespace od::port
