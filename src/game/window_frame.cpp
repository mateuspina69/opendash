// A message window's frame (see window_frame.hpp), transcribed from dash2.exe one function per exe function.
#include "game/window_frame.hpp"

#include <cstdio>

#include "game/display.hpp"
#include "game/port_gap.hpp"

namespace od::port {

namespace {

constexpr const char* kMessagePort = "message port"; // the area of its port-gap errors
constexpr int16_t kFrameMinimum = 6; // a frame's side never below this while it grows / shrinks
constexpr int16_t kSideCloseLimit = 5; // style 2: the height shrinks to 4, then the width closes
constexpr int16_t kSideRest = 4;

// fb 05's low nibble: the open / close animation (exe table 0x8b1f14)
enum class FrameAnimation : uint8_t {
    Scale = 0,         // exe 0x558430: from the corner, a quarter of the size a tick
    Centre = 1,        // exe 0x5584b0: from the centre, an eighth a tick on each side
    WidthFirst = 2,    // exe 0x558570: the width, then the height (closing the height first)
    CentreClamped = 3, // exe 0x558650: from the centre, each side clamped at the corner
};

// The styles that grow from the centre (Centre / CentreClamped): the start takes the centre and a step of the size
// >> shift
void centreStart(MessageWindow& w, int shift) {
    const uint16_t a = uint16_t(w.frameTarget.w), b = uint16_t(w.frameTarget.h);
    w.frame.x = int16_t((int16_t(a) >> 1) + w.frameTarget.x);
    w.frameStepW = uint8_t(a >> shift);
    w.frame.y = int16_t((int16_t(b) >> 1) + w.frameTarget.y);
    w.frame.w = 0;
    w.frame.h = 0;
    w.frameStepH = uint8_t(b >> shift);
}
// growing: the corner out by a step, the size by two
void centreGrow(MessageWindow& w) {
    const uint8_t a = w.frameStepW, b = w.frameStepH;
    w.frame.x = int16_t(uint16_t(w.frame.x) - a);
    w.frame.y = int16_t(uint16_t(w.frame.y) - b);
    w.frame.w = int16_t(uint16_t(w.frame.w) + a * 2u);
    w.frame.h = int16_t(uint16_t(w.frame.h) + b * 2u);
}
// shrinking: the corner in by a step, the size by two
void centreShrink(MessageWindow& w) {
    const uint8_t a = w.frameStepW, b = w.frameStepH;
    w.frame.x = int16_t(uint16_t(w.frame.x) + a);
    w.frame.y = int16_t(uint16_t(w.frame.y) + b);
    w.frame.w = int16_t(uint16_t(w.frame.w) - a * 2u);
    w.frame.h = int16_t(uint16_t(w.frame.h) - b * 2u);
}

// the animation style's step of the current phase
void frameStep(MessageWindow& w) {
    const uint8_t style = w.frameStyle & 0xf;
    const FramePhase ph = w.phase();
    switch (FrameAnimation(style)) {
    case FrameAnimation::Scale:
        if (ph == FramePhase::Start) {
            w.frame.x = w.frameTarget.x;
            w.frame.y = w.frameTarget.y;
            const uint8_t a = uint8_t(w.frameTarget.w >> 2);
            w.frame.w = 0;
            w.frame.h = 0;
            w.frameStepW = a;
            const uint8_t b = uint8_t(w.frameTarget.h >> 2);
            w.frameStepH = b;
            if (a == 0) w.frameStepW = 1;
            if (b == 0) w.frameStepH = 1;
        } else if (ph == FramePhase::Growing) {
            w.frame.w = int16_t(uint16_t(w.frame.w) + w.frameStepW);
            w.frame.h = int16_t(uint16_t(w.frame.h) + w.frameStepH);
        } else if (ph == FramePhase::Closing) {
            w.frame.w = int16_t(uint16_t(w.frame.w) - w.frameStepW);
            w.frame.h = int16_t(uint16_t(w.frame.h) - w.frameStepH);
        }
        return;
    case FrameAnimation::Centre:
        if (ph == FramePhase::Start) {
            centreStart(w, 3);
        } else if (ph == FramePhase::Growing) {
            centreGrow(w);
            if (w.frame.x < w.frameTarget.x) w.frame.x = w.frameTarget.x;
            if (w.frame.y < w.frameTarget.y) w.frame.y = w.frameTarget.y;
        } else if (ph == FramePhase::Closing) {
            centreShrink(w);
        }
        return;
    case FrameAnimation::WidthFirst:
        if (ph == FramePhase::Start) {
            w.frame.x = w.frameTarget.x;
            w.frameSide = 0;
            w.frame.w = 0;
            w.frame.y = w.frameTarget.y;
            w.frame.h = kSideRest;
            w.frameStepW = uint8_t(uint16_t(w.frameTarget.w) >> 2);
            w.frameStepH = uint8_t(uint16_t(w.frameTarget.h) >> 2);
        } else if (ph == FramePhase::Growing) {
            if (w.frameSide == 0) {
                w.frame.w = int16_t(uint16_t(w.frame.w) + w.frameStepW);
                if (w.frameTarget.w <= w.frame.w) w.frameSide = 1;
            } else {
                w.frame.h = int16_t(uint16_t(w.frame.h) + w.frameStepH);
                if (w.frameTarget.h <= w.frame.h) w.frameSide = 0;
            }
        } else if (ph == FramePhase::Closing) {
            if (w.frameSide == 0) {
                w.frame.y = int16_t(uint16_t(w.frame.y) + w.frameStepH);
                w.frame.h = int16_t(uint16_t(w.frame.h) - w.frameStepH);
                if (w.frame.h < kSideCloseLimit) {
                    w.frame.h = kSideRest;
                    w.frameSide = 1;
                    w.frame.y = int16_t(w.frameTarget.h + w.frameTarget.y - kSideCloseLimit);
                }
            } else {
                w.frame.x = int16_t(uint16_t(w.frame.x) + w.frameStepW);
                w.frame.w = int16_t(uint16_t(w.frame.w) - w.frameStepW);
                if (w.frame.w < 1) w.frame.h = 0;
            }
        }
        return;
    case FrameAnimation::CentreClamped:
        if (ph == FramePhase::Start) {
            centreStart(w, 2);
        } else if (ph == FramePhase::Growing) {
            centreGrow(w);
            if (w.frame.x <= w.frameTarget.x) {
                w.frame.x = w.frameTarget.x;
                w.frameStepW = uint8_t(uint16_t(w.frameTarget.w) >> 1);
            }
            if (w.frame.y <= w.frameTarget.y) {
                w.frame.y = w.frameTarget.y;
                w.frameStepH = uint8_t(uint16_t(w.frameTarget.h) >> 1);
            }
        } else if (ph == FramePhase::Closing) {
            centreShrink(w);
        }
        return;
    }
    char buf[48];
    std::snprintf(buf, sizeof buf, "frame animation style %u (0x8b1f14)", style);
    notPorted(kMessagePort, buf);
}

} // namespace

void frameAnimate(MessageWindow& w) {
    frameStep(w);
    switch (w.phase()) { // the phase machine (the jump table 0x5583d8)
    case FramePhase::Start:
        w.setPhase(FramePhase::Growing);
        break;
    case FramePhase::Growing: {
        const int16_t dx = w.frameTarget.w, dy = w.frameTarget.h;
        if (w.frame.w > dx) w.frame.w = dx;
        if (w.frame.h > dy) w.frame.h = dy;
        if (w.frame.w <= kFrameMinimum) w.frame.w = kFrameMinimum;
        if (w.frame.h <= kFrameMinimum) w.frame.h = kFrameMinimum;
        if (w.frame.w != dx || w.frame.h != dy) break;
        w.frame.x = w.frameTarget.x;
        w.frame.y = w.frameTarget.y;
        w.setPhase(FramePhase::Open);
        [[fallthrough]];
    }
    case FramePhase::Open:
        if ((w.flags & kWindowRunning) == 0) {
            w.setPhase(FramePhase::Closing);
            w.flags &= ~kWindowFrameOpen;
        }
        break;
    case FramePhase::Closing: {
        bool closed = false;
        if (w.frame.w <= kFrameMinimum) {
            if (w.frame.h <= kFrameMinimum) {
                w.frame.w = 0;
                w.frame.h = 0;
                closed = true;
            } else {
                w.frame.w = kFrameMinimum;
                w.frameStepW = 0;
            }
        }
        if (!closed && w.frame.h <= kFrameMinimum) {
            w.frame.h = kFrameMinimum;
            w.frameStepH = 0;
        }
        if (w.frame.w != 0 || w.frame.h != 0) break;
        w.setPhase(FramePhase::Closed);
        [[fallthrough]];
    }
    case FramePhase::Closed: {
        const uint32_t f = w.flags;
        w.flags = f & ~(kWindowFrameShown | kWindowFrameOpen);
        if ((f & kWindowOpen) == 0) {
            w.setPhase(FramePhase::Hidden);
            w.flags = 0;
        }
        break;
    }
    default: break;
    }
}

void framePrims(std::vector<ScreenPrim>& out, int x, int y, int w, int h, const FrameStyle& st) {
    { // exe 0x40b650: the drawing area of the display buffer
        const DrawArea a = displayDrawArea();
        ScreenPrim c;
        c.kind = ScreenPrim::Clip;
        c.x = a.x;
        c.y = int16_t(a.y > 0xff ? a.y - 0x100 : a.y);
        c.w = a.w;
        c.h = a.h;
        out.push_back(c);
    }
    const int c = st.corner;
    const int16_t L = int16_t(x), T = int16_t(y), R = int16_t(x + w), B = int16_t(y + h);
    int16_t px[8] = {L, int16_t(L + c), int16_t(R - c), R, R, int16_t(R - c), int16_t(L + c), L};
    int16_t py[8] = {int16_t(T + c), T, T, int16_t(T + c), int16_t(B - c), B, B, int16_t(B - c)};
    const int16_t cx = int16_t(L + (int16_t(w) >> 1)), cy = int16_t(T + (int16_t(h) >> 1));
    for (int k = 0; k < 8; ++k)
        for (int pass = 0; pass < st.passes; ++pass) {
            ScreenPrim t;
            t.kind = ScreenPrim::Tri;
            t.qx[0] = px[k];
            t.qy[0] = py[k];
            t.qx[1] = px[(k + 1) & 7];
            t.qy[1] = py[(k + 1) & 7];
            t.qx[2] = cx;
            t.qy[2] = cy;
            t.qc[0] = t.qc[1] = st.edge;
            t.qc[2] = st.centre;
            t.semi = st.semi;
            t.tpage = st.mode;
            out.push_back(t);
        }
    px[0] = int16_t(L - 1);
    px[7] = int16_t(L - 1);
    px[1] = int16_t(px[1] - 1);
    px[6] = int16_t(px[6] - 1);
    for (const uint32_t col : st.outlines) {
        for (int k = 0; k < 8; k += 2)
            for (int s = 0; s < 2; ++s) {
                ScreenPrim l;
                l.kind = ScreenPrim::Line;
                l.qx[0] = px[k + s];
                l.qy[0] = py[k + s];
                l.qx[1] = px[(k + s + 1) & 7];
                l.qy[1] = py[(k + s + 1) & 7];
                l.colour = col;
                out.push_back(l);
            }
        --px[0];
        --px[7];
        ++px[3];
        ++px[4];
        --py[1];
        --py[2];
        ++py[5];
        ++py[6];
    }
}

void windowFrameDraw(const MessageWindow& w, std::vector<ScreenPrim>& out) {
    const FramePhase phase = w.phase();
    const int16_t x = w.frame.x, y = w.frame.y, fw = w.frame.w, fh = w.frame.h;
    if (!(w.flags & kWindowFrameShown) || phase < FramePhase::Growing || phase > FramePhase::Closing || fw == 0 ||
        fh == 0)
        return;
    FrameStyle st;
    switch (w.frameStyle >> 4) {
    case 0: break; // 0x558720
    case 1:        // 0x5589a0
        st.mode = 0;
        st.passes = 1;
        st.semi = false;
        st.edge = st.centre = 0x504048;
        break;
    case 2: { // 0x558c00: one G4 (0x38) over the rect, no drawing area / draw mode packets
        ScreenPrim g;
        g.kind = ScreenPrim::Gouraud;
        g.qx[0] = g.qx[2] = x;
        g.qx[1] = g.qx[3] = int16_t(x + fw);
        g.qy[0] = g.qy[1] = y;
        g.qy[2] = g.qy[3] = int16_t(y + fh);
        g.qc[0] = 0xffffff;
        g.qc[1] = 0xc0c0c0;
        g.qc[2] = 0xa0a0a0;
        g.qc[3] = 0x808080;
        out.push_back(g);
        return;
    }
    case 3:
    case 5: // 0x558ca0
        st.corner = 2;
        st.mode = 7;
        st.passes = 1;
        st.semi = false;
        st.edge = st.centre = 0x504048;
        st.outlines = {0x584830u, 0x584830u};
        break;
    case 4: // 0x558ee0
        st.edge = 0x301818;
        st.centre = 0x0c0808;
        break;
    default: return; // the table has 6 entries
    }
    framePrims(out, x, y, fw, fh, st);
}

} // namespace od::port
