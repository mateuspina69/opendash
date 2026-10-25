// The screen fades: the fade request (exe 0x53f6b0, task 2 created on the running game task) and the task's tick --
// the fades (a tile's level, exe 0x53f950), the wipes (exe 0x53fa40..0x53fac0: their phases' timing and the state they
// leave; their screen-space strips are render output) and the slides (approximated) -- and what the renderer reads of
// them (the level, the blend). The state is game/fade_state;
#pragma once

#include <cstdint>

namespace od::port {

// The fade task (task 2) as the C++ side sees it.
struct FadeTask {
    bool active = false;       // created and not finished
    bool started = false;      // the fade's prologue ran
    int approximatedSlideTicks = 0; // the slides (types 7..9) are approximated: their ticks so far (see fadeTick)
};

// The fade requests (the byte the transitions and the scripts pass): the range picks the kind (request 0 itself: no
// fade, the display masked a few frames), the low three bits the speed of a fade (exe table 0x8ae808; 0: at once)
constexpr uint8_t kFadeInRequest = 0x00;  // from covered (black) | speed 1..4
constexpr uint8_t kFadeOutRequest = 0x10; // to covered (black) | speed 0..4
constexpr uint8_t kFadeWhiteRequest = 0x08; // | a fade request: to / from white (the Brighten kind)
// the wipes and the slides by their first request (+ their variant)
constexpr uint8_t kWipe20Request = 0x20, kWipe24Request = 0x24, kWipe26Request = 0x26, kWipe28Request = 0x28,
                  kWipe2ARequest = 0x2a;
constexpr uint8_t kSlide30Request = 0x30, kSlide32Request = 0x32, kSlide34Request = 0x34;
constexpr uint8_t kLastFadeRequest = kSlide34Request;

// exe 0x53f6b0(request, param): the fade task set up for `request` (none while one runs; request 0 masks the display;
// requests without a kind do nothing)
void fadeStart(FadeTask& fade, uint32_t request, uint8_t param);
// One tick of the fade task. The fades (types 0 / 1) are the exe's 0x53f950; the wipes (types 2..6) the exe's phases;
// the slides 0x30..0x34 (types 7..9) are approximated: 64 ticks, then the screen uncovered.
void fadeTick(FadeTask& fade);
// The screen's darkening 0..1 for OpenDash's renderer: 1 while the display is masked or a wipe / slide runs, else
// the fade tile's level when it covers the screen
float fadeLevel(const FadeTask& fade);
// The fade's blend (the draw at 0x53f53e: a tile at the fade's level under its draw-mode bits): additive (B + F, the
// Brighten requests) whitens -- the script flashes; subtractive darkens. Not while the display is masked.
bool fadeAdditive();

} // namespace od::port
