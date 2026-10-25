// The two-form reaverbot of ST26 sub 1 (pool 0x20 type 0x29, exe 0x48d640), see
// content/saul_kada/two_form_reaverbot.cpp.
#pragma once

#include <cstdint>

#include "game/actor_movement.hpp"

namespace od::port {

// exe 0x48d640: rises out (form A) and keeps its beat / stands / idles; hit, pushed or its group marked it turns into
// form B, which wanders around the player, gives way to its neighbours and dies into an explosion and its drops
void twoFormReaverbotUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
