// The minimap callbacks of the Forbidden Island stages (game/minimap): ST10 / ST11 / ST1D's (exe 0x54c440) and the
// island rooms' of ST3D..ST3F (exe 0x5545c0).
#pragma once

#include "game/content_registry.hpp"

namespace od::content {

void registerForbiddenIslandMinimaps(od::port::ContentRegistry& r);

} // namespace od::content
