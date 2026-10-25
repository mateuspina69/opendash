// The display, the fade task and the front end's tasks in the exe's layout (game/display, game/fade_state,
// game/title).
#include "game/display.hpp"
#include "game/fade_state.hpp"
#include "game/title.hpp"
#include "translator/layout.hpp"

namespace od::test {

using namespace od::port;

void displayLayout(LayoutVisitor& v) {
    DisplayState& d = display();
    v.field(0x6d0004, d.drawBuffer);
    v.field(0x6d0005, d.pace);
    v.field(0x6d0009, d.displayHeld);
    v.field(0x6d000a, d.drawingOff);
    v.field(0x6d000b, d.bufferMode);
    v.field(0xa7172c, d.mode);
    v.field(0xa743e0, d.maskTicks);
    v.field(0x8afaa4, d.frontEndLayer);

    DrawEnvironments& e = drawEnvironments();
    for (size_t i = 0; i < 2; ++i) {
        const uint32_t a = 0xa723c0 + uint32_t(i) * 0x90;
        v.field(a + 0x02, e.envs[i].clipY);
        v.field(a + 0x16, e.envs[i].originY);
        v.field(a + 0x1e, e.envs[i].screenY);
        v.field(a + 0x2c, e.envs[i].flags);
    }
    v.field(0xa72248, e.clearBackground);
    v.field(0xa724e0, e.flagB);
    v.field(0xa72238, e.flagC);
    v.field(0xa72249, e.flagD);

    FadeState& f = fadeState();
    v.field(0xa72260, f.type);
    v.field(0xa72261, f.phase);
    v.field(0xa72262, f.stripStep);
    v.field(0xa72263, f.variant);
    v.field(0xa72264, f.stripTicks);
    v.field(0xa72265, f.counterSpare);
    v.field(0xa72268, f.covered);
    v.field(0xa72269, f.busy);
    v.field(0xa7226a, f.keepBufferMode);
    v.field(0xa7226b, f.savedBufferMode);
    v.field(0xa7226c, f.speed);
    v.field(0xa7226e, f.level);
    v.field(0xa72270, f.blend);
    v.field(0xa59fe8, f.savedClearBackground);

    MainTaskState& m = mainTask();
    v.field(0xa7a000, m.mode);
    v.field(0xa7a001, m.step);
    v.field(0xa7a002, m.args);
    v.field(0xa7a004, m.brightness);
    v.field(0xa7a006, m.logoTicks);
    v.field(0xa7a008, m.scanTicks);
    v.field(0xa7a00a, m.spare);

    TitleBlock& t = titleBlock();
    v.field(0xa6bf80, t.state);
    v.field(0xa6bf81, t.step);
    v.field(0xa6bf82, t.straightToMenu);
    v.field(0xa6bf83, t.argHigh);
    v.field(0xa6bf84, t.ticks);
    v.field(0xa6bf86, t.ticksB);
    v.field(0xa6bf88, t.restartSpare);
    v.field(0xa6bf8c, t.menuRow);
    v.field(0xa6bf8d, t.difficultyRow);
    v.field(0xa6bf8e, t.brightness);
    v.field(0xa6bf90, t.attractToggle);
    v.field(0xa6bf91, t.slideTicks);
    v.field(0xa6bf92, t.previousRow);
    for (size_t i = 0; i < t.unread14.size(); ++i) v.field(0xa6bf94 + uint32_t(i), t.unread14[i]);
}

} // namespace od::test
