// The minimap callbacks of the sub-ruins (game/minimap): ST4F's (exe 0x552290), the one ST4E / ST51 share (exe
// 0x551890) and the one of ST4A / ST4D / ST50 / ST55 (exe 0x550d50).
#pragma once

#include "game/content_registry.hpp"

namespace od::content {

void registerSubruinsMinimaps(od::port::ContentRegistry& r);

} // namespace od::content
