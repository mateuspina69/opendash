// The message windows in the exe's layout (game/message_state): the five windows and the windows' global bits.
#include "game/message_state.hpp"
#include "translator/layout.hpp"

namespace od::test {

using namespace od::port;

namespace {

void rect(LayoutVisitor& v, uint32_t a, WindowRect& r) {
    v.field(a, r.x);
    v.field(a + 2, r.y);
    v.field(a + 4, r.w);
    v.field(a + 6, r.h);
}

} // namespace

void messageLayout(LayoutVisitor& v) {
    for (size_t i = 0; i < kWindowCount; ++i) {
        MessageWindow& w = messageWindow(i);
        const uint32_t a = 0xa7adc0 + uint32_t(i) * 0x80;
        v.field(a + 0x00, w.flags);
        v.field(a + 0x04, w.ticks);
        v.field(a + 0x06, w.index);
        v.field(a + 0x07, w.typeDelay);
        v.field(a + 0x08, w.x);
        v.field(a + 0x0a, w.y);
        v.field(a + 0x0c, w.unread0c);
        v.field(a + 0x10, w.penX);
        v.field(a + 0x12, w.penY);
        v.field(a + 0x14, w.columns);
        v.field(a + 0x15, w.lines);
        v.field(a + 0x16, w.logicLine);
        v.field(a + 0x17, w.layoutLine);
        v.field(a + 0x18, w.scroll);
        v.field(a + 0x1a, w.unread1a);
        v.field(a + 0x1b, w.shade);
        v.field(a + 0x1c, w.pageHeader);
        v.field(a + 0x1e, w.openedWindow);
        v.field(a + 0x1f, w.rubyRows);
        v.field(a + 0x20, w.baseColour);
        v.field(a + 0x21, w.colour);
        v.field(a + 0x22, w.savedColour);
        v.field(a + 0x23, w.layer);
        v.field(a + 0x24, w.speaker);
        v.field(a + 0x25, w.speakerNewLine);
        v.field(a + 0x26, w.speakerAnimation);
        v.field(a + 0x27, w.unread27);
        v.field(a + 0x28, w.bank);
        v.field(a + 0x2c, w.pageStart);
        v.field(a + 0x30, w.readAt);
        v.field(a + 0x34, w.layoutAt);
        v.field(a + 0x38, w.readReturn);
        v.field(a + 0x3c, w.layoutReturn);
        v.field(a + 0x40, w.handX);
        v.field(a + 0x42, w.handY);
        v.field(a + 0x44, w.handTargetX);
        v.field(a + 0x46, w.handTargetY);
        v.field(a + 0x48, w.optionX);
        v.field(a + 0x49, w.optionLine);
        v.field(a + 0x4a, w.handStepX);
        v.field(a + 0x4b, w.handStepY);
        v.field(a + 0x4c, w.frameStyle);
        v.field(a + 0x4d, w.unread4d);
        v.field(a + 0x50, w.framePhase);
        v.field(a + 0x51, w.frameSide);
        v.field(a + 0x52, w.frameStepW);
        v.field(a + 0x53, w.frameStepH);
        rect(v, a + 0x54, w.frame);
        rect(v, a + 0x5c, w.frameTarget);
        v.field(a + 0x64, w.timeValue);
        v.field(a + 0x68, w.number);
        v.field(a + 0x6c, w.itemName);
        v.field(a + 0x6e, w.place);
        v.field(a + 0x6f, w.record);
        v.field(a + 0x70, w.letter);
        v.field(a + 0x71, w.letters);
        v.field(a + 0x74, w.savedRepeat);
        v.field(a + 0x78, w.unread78);
    }
    MessageSystem& m = messageSystem();
    v.field(0xa72234, m.openBits);
    v.field(0xa711a0, m.waitBits);
}

} // namespace od::test
