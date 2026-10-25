// Animated VRAM tiles: the image moves (exe 0x40b710, a VRAM-to-VRAM copy packet linked into the ordering table) the
// texture animators (pool 0xe0 controllers of the stages' tables) emit every few ticks to animate a texture like a flip
// book or a scroll (ST48 sub 1's sand flowing into the pit, ST27's lava). The animators queue them here; the app applies
// them to the room VRAM and re-decodes the room textures they touch. The animators are data-driven (a tile row of the
// exe, or a strip's VRAM coordinates per stage): the per-stage values are named data (Phase 2: data tables).
//
// ST48 table 0x909274, also ST5B), whose packets (exe 0x40b710) copy a sliding window of a tall texture strip.
#pragma once

#include <cstdint>
#include <vector>

#include "game/actor_movement.hpp"

namespace od::port {

struct VramMove {
    int sx, sy, w, h; // source rect (VRAM pixels; exe 0x40b710 masks x & 0x3ff, y & 0x1ff)
    int dx, dy;       // destination
};

// exe 0x40b710(packet, rect, dx, dy): queues the copy (render output of the tick)
void vramMove(int sx, int sy, int w, int h, int dx, int dy);
// the moves queued since the last call, in order
std::vector<VramMove> takeVramMoves();
// the flip-book tiles' cue: a global byte after the stage block (exe 0xa6d2d8; its writer is not traced, the race's stage
// clear zeroes it)
uint8_t& flipBookCue();

// exe 0x4262e0 (pool e0 type 0x1a: ST48 sub 1, ST5B): the sand flowing into the pit -- a count down every tick; on even
// counts a 16 x 64 window of the strip at (0x540, 0x100 + (count >> 1 & 0x3f)) copied to (0x560, 0x100), on counts
// divisible by 4 an 8 x 32 window at (0x550, 0x100 + (count >> 2 & 0x1f)) to (0x570, 0x100)
void sandFlowUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x425eb0 (pool e0 type 0x1a: ST27 sub 8, the fire ruin): the lava texture (16 x 64 at (0x6b0, 0x180)) scrolled
// into (0x6b0, 0x140); the lava turned off (its story flag) slows it to a stop and frees it after 0x20 ticks
void lavaFlowUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x426180 (pool e0 type 0x1a: ST4C): the same scroller for the strip (0x5b0, 0x1c0) into (0x5b0, 0x180), no stop
void stripScrollUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x425b90 (pool e0 type 1: ST00): 6 flip-book tiles (the exe's tile rows 0x88be88), started by a cue
void flipBookTilesUpdate(const PlayerEnv& env, uint32_t actor);
// exe 0x425d00 (pool e0 type 1: ST20): a VRAM tile (the tile row 0x88beb8) scrolled by a sine of the frame
void waveTileUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
