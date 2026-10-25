// ST4C (Elysium): the boss (exe 0x507280; pool 0x20 type 0x7d sub 0, ST4C table 0x90bdb0 -> 0x90bda4) and the classes
// it spawns: its projectiles (pool 0x40 ST4C types 0x25 / 0x27 / 0x2c / 0x2d), its statics (pool 0x60 ST4C types 0x54
// / 0x55 / 0x57), its effects (pool 0xa0 ST4C types 0x1a / 0x1d) and its fog controller (pool 0xe0 ST4C type 0x26).
// See content/late_game/st4c_boss.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x507280: pool 0x20 type 0x7d sub 0 -- the boss: fades out / teleports / fades in (11 bone tints), attacks
// (twin charges 0x55 + 5 homing shots 0x27; the beam 0x2d + 4 sparkle statics 0x57; the 8 orbiting shots 0x25 + the
// slam's shockwave; the volley of 0x2c bolts), the fog controller 0x26 at hit-point thresholds, the boss gauge; its
// defeat -> story flag 0x681
void elysiumBossUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5ccc40: pool 0x40 ST4C type 0x25 -- an orbiting shot (variants 0..7) that then homes on the player (shootable)
void elysiumOrbitShotUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5ce6b0: pool 0x40 ST4C type 0x27 -- a homing shot from between the boss's hands (variants 0..4)
void elysiumHomingShotUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5cfb40: pool 0x40 ST4C type 0x2c -- a bolt aimed from the boss's bone 1 (blast + light column on impact)
void elysiumBoltUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5d0060: pool 0x40 ST4C type 0x2d -- the beam held at the boss's bone 8 (draw records 0xf8)
void elysiumBeamUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5b8cf0: pool 0x60 ST4C type 0x54 -- 16 rising sparks (draw records 0xf0) where the boss vanished
void elysiumSparksUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5b9010: pool 0x60 ST4C type 0x55 -- the charge between the boss's hands (variant 0: a glow 0xf4, then a disc;
// variant 1: rays)
void elysiumChargeUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x5ba240: pool 0x60 ST4C type 0x57 -- sparkles around the boss's bone 8 while the beam lasts (draw records 0xf0)
void elysiumSparklesUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x43a2a0: pool 0xa0 ST4C type 0x1a -- the teleport flash: 0xc0 random additive G4 pairs for 0x28 ticks
void elysiumFlashUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x43ac70: pool 0xa0 ST4C type 0x1d -- a rising mote (draw records 0xf0)
void elysiumMoteUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x427640: pool 0xe0 ST4C type 0x26 -- the fog darkens while the boss attacks (the fog colour / distance, the
// player's burden), then fades back and frees itself
void elysiumFogUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
