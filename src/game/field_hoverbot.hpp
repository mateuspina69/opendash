// The field hoverbot (pool 0x20 class 0x44bea0, stage type 3 sub 0 of the fields ST0E / ST10 / ST22: sub arrays 0x8da508
// / 0x8dc66c / 0x8e8b98), placed by the encounter spawners (game/encounter): variant 0 appears around the player (at the
// encounter slot's bearing) and hovers at the player's height, circles it, gives way to its neighbours, spins when
// bumped and fires an aimed shot (pool 0x40 stage type 0); variant 1 pops out of the ground and falls back into it. It
// can be lifted and thrown (game/carry); it explodes, or flies off / sinks away. Its shadow (exe 0x5f20f0) is render
// output.
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x44bea0: the field hoverbot's update (ActorUpdateFn)
void fieldHoverbotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
