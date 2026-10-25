// The dropship parked in the Flutter's hangar (ST07 sub 1; class exe 0x4f29e0, pool 0x20 type 0x6d): spawned by the
// ST07 sub-1 room handler (content/yosyonke/town_rooms, exe 0x57c650) from story 1 on -- the craft of the landing on the
// Forbidden Island (content/forbidden_island/st49_dropship) kept aboard.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x4f29e0 (states [0x896c00]): bound with animation 2, liftable; the animation runs while it is drawn
void hangarDropshipUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
