// The minimap callbacks of the late-game stages (game/minimap): ST40's (exe 0x554d20), the ruin of ST43..ST45 / ST4C /
// ST58's (exe 0x556360) and ST59..ST5B's (exe 0x557110).
#pragma once

#include "game/content_registry.hpp"

namespace od::content {

void registerLateGameMinimaps(od::port::ContentRegistry& r);

} // namespace od::content
