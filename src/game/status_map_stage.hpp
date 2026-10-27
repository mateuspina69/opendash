// The full maps the stages' second room callbacks draw on the status sub-screen.
#pragma once

#include <cstddef>
#include <cstdint>

namespace od::port {

// exe 0x5e8be0's call through 0xa72230: the stage's map callback.
void statusMapStageDraw();
// exe 0x415bd0: the preview marker (route false), or the animated destination marker.
void statusMapRouteIconDraw(size_t c, int16_t x, int16_t y, bool route);

} // namespace od::port
