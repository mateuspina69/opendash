// The minimap callbacks of the Yosyonke stages and the towns (game/minimap): the abandoned mine's (ST0F, exe
// 0x54fd10), the ruin under the town's (ST0D / ST0E, exe 0x54e6d0), the towns' shared one (exe 0x54ed50) and the ruin
// of ST2D..ST2F's (exe 0x54d940).
#pragma once

#include "game/content_registry.hpp"

namespace od::content {

void registerYosyonkeMinimaps(od::port::ContentRegistry& r);

} // namespace od::content
