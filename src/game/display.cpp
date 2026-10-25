// The display's routines (see display.hpp).
#include "game/display.hpp"

#include "game/fade_state.hpp"

namespace od::port {

void setBufferMode(uint8_t mode) {
    display().bufferMode = mode;
    if (fadeState().busy != 0) fadeState().savedBufferMode = mode;
}

void setDisplayMode(uint8_t mode) {
    DisplayState& d = display();
    d.mode = mode;
    d.maskTicks = 1;
    d.pace = mode == kGameDisplayMode ? kGameDisplayPace : kTitleDisplayPace;
}

void applyDrawEnvironmentFlags() {
    DrawEnvironments& e = drawEnvironments();
    for (size_t i = 0; i < e.envs.size(); ++i) {
        auto&& env = e.envs[i];
        env.flags[0] = e.clearBackground;
        env.flags[1] = e.flagB;
        env.flags[2] = e.flagC;
        env.flags[3] = e.flagD;
    }
}

void displayMaskTick() {
    DisplayState& d = display();
    const uint8_t count = d.maskTicks;
    if (count == 0) return;
    const uint32_t limit = uint32_t(d.mode >> 1) * 4 + 4;
    d.maskTicks = uint8_t(count + 1);
    if (count > limit) d.maskTicks = 0; // exe 0x40b760(1): the display back on
}

} // namespace od::port
