// The attract demo's pad playback (see demo_playback.hpp).
#include "game/loaded_data.hpp"
#include "game/demo_playback.hpp"

#include "game/game_state.hpp"
#include "game/hud.hpp"
#include "game/player/player_input.hpp"
#include "game/title.hpp"

namespace od::port {

void demoStart(uint32_t recording) {
    if (gameState().frontEnd != kFrontEndDemo) return;
    HudMemory& hud = hudMemory();
    for (size_t i = 0; i < kHudElements; ++i) hud.elements[i].flags &= uint8_t(~kHudElementDrawn);
    GameLoopSaves& saves = gameLoopSaves();
    GameOptions& options = gameOptions();
    PadState& pad = pad0();
    saves.demoPadType = pad.typeByte();
    saves.demoControlOption = options.controlOption;
    saves.demoControllerLayout = options.controllerLayout;
    saves.demoLookDirection = options.lookDirection;
    saves.demoAutoLockSpecial = options.autoLockSpecial;
    saves.demoAutoLockBuster = options.autoLockBuster;
    DemoState& demo = demoState();
    demo.recording.pointAt(recording);
    const DemoRecording rec = dataRecord<DemoRecording>(recording, DemoRecording::kBytes);
    gameState().frame = rec.frame;
    pad.setTypeByte(rec.padType);
    demo.next.pointAt(recording + kDemoFirstFrame);
    options.controlOption = 0;
    options.controllerLayout = 0;
    options.lookDirection = 0;
    options.autoLockSpecial = 0;
    options.autoLockBuster = 0;
}

bool demoPadFrame() {
    GameState& game = gameState();
    if (int8_t(game.mode) <= 1 || game.frontEnd != kFrontEndDemo) return false;
    DemoState& demo = demoState();
    PadState& pad = pad0();
    const uint32_t base = demo.recording.value(), at = demo.next.value();
    if (int32_t(at - base) < int32_t(uint16_t(demo.recording->bytes)) &&
        !(pad.held & (kPadCircle | kPadCross | kPadStart))) {
        pad.setTypeByte(demo.recording->padType);
        pad.held = demo.next->held;
        pad.pressed = demo.next->pressed;
        pad.released = demo.next->released;
        pad.setStickWord(demo.next->stick);
        pad.setDpadWord(demo.next->dpad);
        demo.next.pointAt(at + kDemoFrameSize);
    } else {
        GameLoopSaves& saves = gameLoopSaves();
        GameOptions& options = gameOptions();
        pad.setTypeByte(uint8_t(saves.demoPadType));
        options.controlOption = saves.demoControlOption;
        options.controllerLayout = saves.demoControllerLayout;
        options.lookDirection = saves.demoLookDirection;
        options.autoLockSpecial = saves.demoAutoLockSpecial;
        options.autoLockBuster = saves.demoAutoLockBuster;
        pad.held = kPadSelect | kPadStart;
        pad.pressed = 0;
        pad.released = 0;
        pad.setStickWord(0);
        pad.setDpadWord(0);
        game.resetHoldTicks = uint16_t(kSoftResetHold + 1);
    }
    return true;
}

} // namespace od::port
