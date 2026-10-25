// The guardian of the Manda ruin (ST13 sub 1, after script 9) and its cast. See content/manda/manda_guardian.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"

namespace od::port {

constexpr uint8_t kMandaHopperType = 0x31; // the guardian's hoppers' animated pool type (sub-type 0)

// The hopper's spawn parameters (the guardian's spawner writes them: content/manda/ruin_spawners)
struct MandaHopperParams {
    explicit MandaHopperParams(const AnimatedActor& actor) : MandaHopperParams(const_cast<AnimatedActor&>(actor)) {}
    explicit MandaHopperParams(AnimatedActor& actor) : guardian(actor.params[0]), corner(actor.params[1]) {}
    FieldAlias<int8_t, uint8_t> guardian; // the guardian's animated pool index
    FieldAlias<int8_t, uint8_t> corner; // its place around the guardian (0..3)
};

// 0x484e00 (pool 0x20 type 0x24, variant 0): fades in / out around the arena, a hopper spawner, spins, volleys of
// shots; its hit points in the boss gauge; death -> flag 0x5b1 (script 10)
void mandaGuardianUpdate(const PlayerEnv& env, uint32_t actor);
void mandaShotUpdate(const PlayerEnv& env, uint32_t actor);  // 0x5c5fb0 pool 0x40 ST13 stage type 0xe
void shotTrailUpdate(const PlayerEnv& env, uint32_t actor);  // 0x424800 pool 0xe0 global type 0x10
void hitMuteUpdate(const PlayerEnv& env, uint32_t actor);    // 0x425aa0 pool 0xe0 global type 0x18
void mandaCloudUpdate(const PlayerEnv& env, uint32_t actor); // 0x4331a0 pool 0xa0 ST13 stage type 0xc
// 0x486d00 (pool 0x20 type 0x24 sub 1): the guardian in script 9's cutscene (driven by the script's state / tick)
void mandaCastGuardianUpdate(const PlayerEnv& env, uint32_t actor);
// 0x487250 (pool 0x20 type 0x24 sub 2): the guardian in script 10's cutscene
void mandaOutroGuardianUpdate(const PlayerEnv& env, uint32_t actor);
// 0x49e160 (pool 0x20 type 0x31 sub 0): the guardian's hoppers (spawner 0x41c070 variant 0)
void mandaHopperUpdate(const PlayerEnv& env, uint32_t actor);
// 0x4875e0 (ST12 pool 0x20 type 0x24 sub 1): the guardian in scripts 0xb (variant 0) / 0xc (variant 1)
void mandaCast2GuardianUpdate(const PlayerEnv& env, uint32_t actor);
// 0x59bee0 (static type 0x17 sub 1, variants 0 / 1): the spinning blades of the ST12 sub 5 arena
void mandaBladeUpdate(const PlayerEnv& env, uint32_t actor);
// 0x59c800 (static type 0x17 sub 2): the blade that falls in script 0xb (flag 0x680 on landing)
void mandaFallingBladeUpdate(const PlayerEnv& env, uint32_t actor);
// 0x475ae0 (ST12 pool 0x20 type 0x15 sub 1): the opened chest of script 0xb (a blast on flag 0x680, then freed)
void mandaOpenedChestUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
