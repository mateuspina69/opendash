// The field reaverbot (pool 0x20 class 0x450310, stage type 5 sub 0 of the fields ST0D / ST0E / ST10 / ST1D / ST22 /
// ST2F: one-entry sub arrays 0x8d9bac / 0x8da50c / 0x8dc670 / 0x8e6344 / 0x8e8b9c / 0x8f370c), placed by the encounter /
// formation spawners (game/encounter): it rises out of the ground facing the encounter's anchor, circles the player,
// gives way to its neighbours, lunges or hops at the player, lobs a shot (game/lobbed_shot, variant bit 0x80), flinches
// and is knocked back / aside; it can be lifted and thrown (game/carry); its death is an explosion and the drops; far
// from the player it sinks away. The sibling of the ruins' walking reaverbot (game/reaverbot_walker). Its shadow (exe
// 0x5f20f0) is render output.
#pragma once

#include <cstdint>

namespace od::port {

struct PlayerEnv;

// exe 0x450310: the field reaverbot's update (ActorUpdateFn)
void fieldReaverbotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
