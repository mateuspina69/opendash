// A message window's frame (dash2.exe): its open / close animation the logic pass runs (exe 0x5582d0: the four
// animation styles of fb 05's low nibble, the phases Start .. Closed) and its look the draw pass builds (exe 0x5583f0:
// the six looks of the high nibble) as ScreenPrims for the host.
#pragma once

#include <cstdint>
#include <vector>

#include "engine/screen_prim.hpp"
#include "game/message_state.hpp"

namespace od::port {

// exe 0x5582d0: the frame's animation step and its phase machine (the jump table 0x5583d8): it grows to the target
// rect, stays open while the text runs, shrinks, then the window's flags are cleared unless it was reopened
void frameAnimate(MessageWindow& w);

// An octagonal frame as the exe's frame builders make it: the drawing area (the display's table), the draw mode `mode`
// (0xe1 word), 8 G3 fans from the edge points to the centre (`passes` times each: twice = the 0x32 semi fill drawn over
// itself), then `outlines.size()` rings of 0x48 polylines (p0 p1 p2 / p2 p3 p4 / p4 p5 p6 / p6 p7 p0), each 1 px
// further out. Points: p0 (x, y + c), p1 (x + c, y), p2 (x + w - c, y), p3 (x + w, y + c), p4 (x + w, y + h - c),
// p5 (x + w - c, y + h), p6 (x + c, y + h), p7 (x, y + h - c); centre (x + w / 2, y + h / 2).
struct FrameStyle {
    int corner = 3;
    uint16_t mode = 0x200;
    int passes = 2;
    bool semi = true;
    uint32_t edge = 0x603030, centre = 0x181010; // 0xBBGGRR
    std::vector<uint32_t> outlines{0x808080, 0xf8f8f8, 0x606060};
};
void framePrims(std::vector<ScreenPrim>& out, int x, int y, int w, int h, const FrameStyle& st);
// exe 0x5583f0(window): the frame of window w when it is shown (kWindowFrameShown, the phases Growing .. Closing, a
// size), by its look (frameStyle >> 4, the table 0x8b1f24: 0 0x558720 blue (semi), 1 0x5589a0 grey, 2 0x558c00 the G4
// white -> grey panel, 3 / 5 0x558ca0 grey with a brown outline (tpage 7), 4 0x558ee0 dark blue (semi)). Nothing when
// not shown. The exe links it into the OT 0xa67840 + 0x70 + layer (+0x23) * 4 after the window's text (so drawn under
// it).
void windowFrameDraw(const MessageWindow& w, std::vector<ScreenPrim>& out);

} // namespace od::port
