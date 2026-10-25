// The attract demo's pad playback (the title's idle runs the game loop in ST51 sub 1 as the demo): the stage texture
// DAT's pad recording replaces the polled pad frame by frame; the pad type and the options the demo changes are kept
// in the game loop's save area and given back at its end, where the pad turns into the soft reset's start + select.
#pragma once

#include <cstdint>

#include "game/loaded_data.hpp"

namespace od::port {

// The pad recording (the stage texture DAT's type-2 segment): its length, the frame counter it starts at, the pad type,
// then kDemoFrameSize-byte frames from kDemoFirstFrame
struct DemoRecording : DataRecord {
    static constexpr uint32_t kBytes = 0x5;
    uint16_t bytes = 0; // +0x0: the recording's bytes (the frames end there)
    uint16_t frame = 0; // +0x2: the frame counter at the start
    uint8_t padType = 0; // +0x4
    template <class R> static DemoRecording load(const R& r) {
        DemoRecording v;
        v.bytes = r.template read<uint16_t>(0x0);
        v.frame = r.template read<uint16_t>(0x2);
        v.padType = r.template read<uint8_t>(0x4);
        return v;
    }
};
// One recorded frame: what the pad pipeline would have left
struct DemoFrame : DataRecord {
    static constexpr uint32_t kBytes = 0xa;
    uint16_t held = 0; // +0x0
    uint16_t pressed = 0; // +0x2
    uint16_t released = 0; // +0x4
    uint16_t stick = 0; // +0x6: the stick's x | y << 8
    uint16_t dpad = 0; // +0x8: the d-pad's
    template <class R> static DemoFrame load(const R& r) {
        DemoFrame v;
        v.held = r.template read<uint16_t>(0x0);
        v.pressed = r.template read<uint16_t>(0x2);
        v.released = r.template read<uint16_t>(0x4);
        v.stick = r.template read<uint16_t>(0x6);
        v.dpad = r.template read<uint16_t>(0x8);
        return v;
    }
};
constexpr uint32_t kDemoFirstFrame = 6;
constexpr uint32_t kDemoFrameSize = 10;

struct DemoState {
    DataPtr<DemoFrame> next;                 // the next frame to replay
    DataPtr<DemoRecording> recording;        // the recording replayed
};
inline DemoState& demoState() {
    static DemoState s;
    return s;
}

// exe 0x542460's demo part: when the front end is the demo, the pad recording at `recording` (the host loads the stage
// texture DAT's type-2 segment into the spare buffer) starts: the HUD elements not drawn, the pad type and the options
// kept, the frame counter and the pad type of the recording, the options cleared
void demoStart(uint32_t recording);
// exe 0x542530 (every game-loop frame before the mode handler, past the first modes): the demo replaces the polled pad
// with the next recorded frame; at the end or on circle / cross / start the pad type and the options come back and the
// pad becomes start + select held with the soft reset's hold run out (the soft reset fires). Returns true when it ran
// (the host draws the "DEMO" sprite: tpage 0xd, clut 0x7e50, uv (0x30, 0xc0), 96 x 40 at (0xc8, 0xb8),
// semi-transparent)
bool demoPadFrame();

} // namespace od::port
