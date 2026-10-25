// The draw pass over the frame's draw commands and ordering-table packets (exe 0x5f0580, the records of
// game/draw_commands), as OpenDash draws them: od::fx::Prim for the app (presentation only: reads the game state and
// the side channel, never writes them).
//
// Drawing improvements over the exe (kept): the records are never culled for the PS1 camera (the picture
// is drawn from the modern camera), a ring keeps one vertex count while its arc grows / shrinks, a band draws every
// segment from both sides, a screen ring is placed at a fixed view depth in the camera plane.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "game/effects_draw.hpp"

namespace od::dc {

struct Stats {
    int records = 0, b2c = 0, f0 = 0, f4 = 0, f8 = 0, unknown = 0, culled = 0, ot = 0, otG4 = 0;
};
// exe 0x5f0580 over [buffer, cursor) of the game state and the frame's OT packets: prims into `out` (centres in game
// units, Y down; billboards / rings in the camera plane, bands and OT packets as world offsets), textures registered in
// `tex`. The visibility tests use the game camera (0x6d0014 / 0x6d0028), as the exe.
Stats decode(fx::TextureCache& tex, std::vector<fx::Prim>& out);
// The same over explicit words (tests): the records starting at w.
Stats decodeRecords(const uint32_t* w, size_t words, fx::TextureCache& tex, std::vector<fx::Prim>& out,
                    uint32_t object = 0x800000u);

} // namespace od::dc
