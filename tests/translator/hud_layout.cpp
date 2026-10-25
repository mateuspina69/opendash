// The HUD's state in the exe's layout (game/hud): the element records and the scripted sprite banner.
#include "game/hud.hpp"
#include "translator/layout.hpp"

namespace od::test {

using namespace od::port;

void hudLayout(LayoutVisitor& v) {
    HudMemory& h = hudMemory();
    for (size_t i = 0; i < kHudElements; ++i) {
        HudElementRecord& e = h.elements[i];
        const uint32_t a = 0xa7b1a0 + uint32_t(i) * 0x1c;
        v.field(a + 0x00, e.flags);
        v.field(a + 0x01, e.type);
        v.field(a + 0x02, e.index);
        v.field(a + 0x03, e.style);
        v.field(a + 0x04, e.state);
        v.field(a + 0x05, e.unread05);
        v.field(a + 0x06, e.timer);
        v.field(a + 0x08, e.colour);
        v.field(a + 0x0b, e.shift);
        v.field(a + 0x0c, e.x);
        v.field(a + 0x0e, e.clut);
        v.field(a + 0x10, e.lastValue);
        v.field(a + 0x12, e.fill);
        v.field(a + 0x14, e.trail);
        v.field(a + 0x16, e.length);
        v.field(a + 0x18, e.target);
    }
    v.field(0xa7b248, h.elementTail);
    v.field(0xa7b250, h.stateWords);

    HudBanner& b = hudBanner();
    v.field(0xa6bda0, b.shown);
    v.field(0xa6bda1, b.kind);
    v.field(0xa6bda2, b.progress);
    v.field(0xa6bda4, b.count);
    v.field(0xa6bda6, b.unread06);
    v.field(0xa6bda8, b.tpage);
    v.field(0xa6bdaa, b.clut);
    v.field(0xa6bdac, b.x);
    v.field(0xa6bdae, b.y);
    for (size_t i = 0; i < 0x20; ++i) {
        const uint32_t a = 0xa6bdb0 + uint32_t(i) * 8;
        v.field(a, b.sprites[i].x);
        v.field(a + 2, b.sprites[i].y);
        v.field(a + 4, b.sprites[i].radius);
        v.field(a + 6, b.sprites[i].angle);
    }
    v.field(0xa6beb0, b.layout);
}

} // namespace od::test
