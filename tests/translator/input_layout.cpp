// The input's state in the exe's layout (game/player/player_input, game/input_bindings, game/key_config): the two pads,
// the replay pad, the host's capture flag, the vibration request, the binding tables and the key configuration's keep.
#include "game/input_bindings.hpp"
#include "game/key_config.hpp"
#include "game/player/player_input.hpp"
#include "translator/layout.hpp"

namespace od::test {

using namespace od::port;

namespace {

void pad(LayoutVisitor& v, uint32_t a, PadState& p) {
    v.field(a + 0x02, p.raw);
    v.field(a + 0x04, p.analogX);
    v.field(a + 0x05, p.analogY);
    for (size_t i = 0; i < p.driverBytes.size(); ++i) v.field(a + 0x06 + uint32_t(i), p.driverBytes[i]);
    v.field(a + 0x24, p.type);
    v.field(a + 0x28, p.held);
    v.field(a + 0x2a, p.prevHeld);
    v.field(a + 0x2c, p.pressed);
    v.field(a + 0x2e, p.released);
    v.field(a + 0x30, p.doubleTapped);
    v.field(a + 0x32, p.repeated);
    v.field(a + 0x36, p.repeatDelay);
    v.field(a + 0x37, p.repeatRate);
    v.field(a + 0x38, p.stickX);
    v.field(a + 0x39, p.stickY);
    v.field(a + 0x3a, p.dpadX);
    v.field(a + 0x3b, p.dpadY);
    v.field(a + 0x3c, p.tapState);
    v.field(a + 0x42, p.tapStateSpare);
    v.field(a + 0x44, p.tapTicks);
    v.field(a + 0x4a, p.tapTicksSpare);
    v.field(a + 0x4c, p.repeatTicks);
}
template <size_t N> void table(LayoutVisitor& v, uint32_t a, BindingTable<N>& t) {
    for (size_t i = 0; i < N; ++i) {
        v.field(a + uint32_t(i) * 4, t.bindings[i].code);
        v.field(a + uint32_t(i) * 4 + 2, t.bindings[i].ps1Bit);
    }
}

} // namespace

void inputLayout(LayoutVisitor& v) {
    pad(v, 0xa6bec0, pad0());
    pad(v, 0xa6bf20, pad1());

    ReplayPad& r = replayPad();
    v.field(0xa6c3e8, r.held);
    v.field(0xa6c3ea, r.prevHeld);
    v.field(0xa6c3ec, r.pressed);
    v.field(0xa6c3ee, r.released);
    v.field(0xa6c3f0, r.doubleTapped);
    v.field(0xa6c3f2, r.repeated);

    v.field(0xa2d4c0, hostInput().captured);

    PadVibration& vb = padVibration();
    v.field(0xa7ada0, vb.requested);
    v.field(0xa7ada4, vb.motorLow);
    v.field(0xa7ada5, vb.motorHigh);
    v.field(0xa7ada6, vb.strength);
    v.field(0xa7ada7, vb.durationLow);
    v.field(0xa7ada8, vb.durationHigh);

    InputBindings& b = inputBindings();
    table(v, 0xa2d4c4, b.mouse);
    table(v, 0xa2d4cc, b.keys);
    table(v, 0xa2d7c0, b.pad);
    v.field(0xa2d814, b.rawDevices);

    KeyConfigKeep& k = keyConfigKeep();
    v.field(0xa5e570, k.keys);
    v.field(0xa5e5b8, k.mouse);
    v.field(0xa5e5d0, k.action);
    v.field(0xa5e5d8, k.pad);
}

} // namespace od::test
