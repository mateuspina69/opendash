// The screen fades (see fade.hpp).
#include "game/fade.hpp"

#include "game/display.hpp"
#include "game/fade_state.hpp"

namespace od::port {

namespace {

// What a request sets up (exe 0x53f6b0's switch)
enum class FadeSetup : uint8_t {
    DisplayMask = 0,
    Darken = 1,
    Brighten = 2,
    Wipe20 = 3,
    Wipe24 = 4,
    Wipe26 = 5,
    Wipe28 = 6,
    Wipe2A = 7,
    Slide30 = 8,
    Slide32 = 9,
    Slide34 = 10,
    None = 11,
};
// the switch's byte index table (exe 0x53f904, in .text): the setup of each request 0..kLastFadeRequest
constexpr FadeSetup kSetupOfRequest[kLastFadeRequest + 1] = {
    FadeSetup::DisplayMask, FadeSetup::Darken, FadeSetup::Darken, FadeSetup::Darken, FadeSetup::Darken, FadeSetup::None,
    FadeSetup::None, FadeSetup::None, FadeSetup::None, FadeSetup::Brighten, FadeSetup::Brighten, FadeSetup::Brighten,
    FadeSetup::Brighten, FadeSetup::None, FadeSetup::None, FadeSetup::None, FadeSetup::Darken, FadeSetup::Darken,
    FadeSetup::Darken, FadeSetup::Darken, FadeSetup::Darken, FadeSetup::None, FadeSetup::None, FadeSetup::None,
    FadeSetup::None, FadeSetup::Brighten, FadeSetup::Brighten, FadeSetup::Brighten, FadeSetup::Brighten,
    FadeSetup::None, FadeSetup::None, FadeSetup::None, FadeSetup::Wipe20, FadeSetup::Wipe20, FadeSetup::Wipe20,
    FadeSetup::Wipe20, FadeSetup::Wipe24, FadeSetup::Wipe24, FadeSetup::Wipe26, FadeSetup::Wipe26, FadeSetup::Wipe28,
    FadeSetup::Wipe28, FadeSetup::Wipe2A, FadeSetup::Wipe2A, FadeSetup::None, FadeSetup::None, FadeSetup::None,
    FadeSetup::None, FadeSetup::Slide30, FadeSetup::Slide30, FadeSetup::Slide32, FadeSetup::Slide32, FadeSetup::Slide34,
};
// a wipe setup's task type and its first request
struct WipeSetup {
    FadeType type;
    uint8_t firstRequest;
};
constexpr WipeSetup kWipeSetups[] = {{FadeType::Wipe20, kWipe20Request}, {FadeType::Wipe24, kWipe24Request},
                                     {FadeType::Wipe26, kWipe26Request}, {FadeType::Wipe28, kWipe28Request},
                                     {FadeType::Wipe2A, kWipe2ARequest}};
constexpr uint32_t kFadeSpeedMask = 7;
constexpr uint32_t kFadeDirectionShift = 4;
// the tile's semi-transparency modes (exe 0x40b8c0's abr)
constexpr uint32_t kBlendAdd = 1, kBlendSubtract = 2;
constexpr uint32_t kBlendShift = 5;

// exe 0x40b8c0(2, abr, 0, 0): the draw-mode bits of a tile with semi-transparency `abr`
uint16_t tileDrawMode(uint32_t abr) { return uint16_t((((2u & 3) << 2) | (abr & 3)) << 3 << 2); }

// the wipes' environment placements (exe 0x53fdd4 / 0x5409a5): the first environment's clip at y, the second's origin
// and screen at y, the others 0
void placeDrawEnvironments(int16_t y) {
    auto&& first = drawEnvironments().envs[0];
    auto&& second = drawEnvironments().envs[1];
    first.originY = 0;
    first.screenY = 0;
    first.clipY = y;
    second.originY = y;
    second.screenY = y;
    second.clipY = 0;
}
constexpr int16_t kSecondBufferY = 0x100;

// the strip count of a wipe's Strips phase by its type
uint8_t wipeStripCount(FadeType type) {
    const FadeState& f = fadeState();
    if (type == FadeType::Wipe20) return uint8_t(((~f.variant & 2u) << 4) | 0x1fu);
    if (type == FadeType::Wipe2A) return 0x3f;
    return uint8_t(((((uint32_t(display().mode) + 1u) >> 1) + 1u) << 6) - 1u);
}

// One tick of the wipe tasks (types 2..6): each frame the phase function [0x8ae83c + (type - 2) * 12 + phase * 4],
// then yield.
//   CloseDisplay (exe 0x53fdc0): on a frame of buffer 1 -- the environments placed at 0 in the game's display mode --,
//     the display held and drawing off, the background flag saved and cleared, the buffer mode saved and set to 2,
//     Strips, the flags into the environments
//   Strips (type 2 0x53fe50, 3 0x53ff60, 4 0x5402b0, 5 0x540540, 6 0x5407c0): Start: the display released, the screen
//     uncovered, the count by the type; every tick (the Start one too) the strips, then the count - 1: at 0 the screen
//     covered at full level (until the next fade starts), Reopen
//   Reopen (exe 0x540960): on a frame of buffer 0, drawing back on (once) and the buffer mode restored unless the
//     request kept it; on a frame of buffer 1 with drawing on: the environments back at kSecondBufferY (game display
//     mode), the display released, the background flag restored, the flags into the environments, the task ends
void wipeTick(FadeType type) {
    FadeState& f = fadeState();
    DisplayState& d = display();
    switch (WipePhase(f.phase)) {
    case WipePhase::CloseDisplay:
        if (d.drawBuffer == 0) return;
        if (d.mode == kGameDisplayMode) placeDrawEnvironments(0);
        d.displayHeld = 1;
        d.drawingOff = 1;
        f.savedClearBackground = drawEnvironments().clearBackground;
        drawEnvironments().clearBackground = 0;
        f.savedBufferMode = d.bufferMode;
        d.bufferMode = 2;
        f.stripStep = int8_t(WipeStripStep::Start);
        ++f.phase;
        applyDrawEnvironmentFlags();
        return;
    case WipePhase::Strips: {
        if (WipeStripStep(f.stripStep) == WipeStripStep::Start) {
            d.displayHeld = 0;
            f.covered = 0;
            f.stripTicks = wipeStripCount(type);
            ++f.stripStep;
        } else if (WipeStripStep(f.stripStep) != WipeStripStep::Running) {
            return;
        }
        // (the strips: render output)
        --f.stripTicks;
        if (f.stripTicks == 0) {
            f.covered = 1;
            f.level = kFadeCoveredLevel;
            ++f.phase;
            f.clearStripWord();
        }
        return;
    }
    case WipePhase::Reopen:
        if (d.drawBuffer == 0) {
            if (d.drawingOff != 0) {
                d.drawingOff = 0;
                if (f.keepBufferMode == 0) d.bufferMode = f.savedBufferMode;
            }
            if (d.drawBuffer == 0) return;
        }
        if (d.drawingOff != 0) return;
        if (d.mode == kGameDisplayMode) placeDrawEnvironments(kSecondBufferY);
        d.displayHeld = 0;
        d.drawingOff = 0;
        drawEnvironments().clearBackground = f.savedClearBackground;
        applyDrawEnvironmentFlags();
        f.busy = 0;
        return;
    default: return; // [0x8ae83c..] has three phases per type
    }
}

// The slides 0x30..0x34 (types 7..9, tasks 0x53fae0 / 0x53fbc0 / 0x53fcc0) are not ported: approximated by 64 ticks,
// then the screen uncovered as their end (start 0x540a00 clears the cover, end 0x540b10) leaves it
constexpr int kApproximatedSlideTicks = 64;
void approximatedSlideTick(FadeTask& fade) {
    if (++fade.approximatedSlideTicks >= kApproximatedSlideTicks) {
        FadeState& f = fadeState();
        f.covered = 0;
        f.busy = 0;
        fade.active = false;
    }
}

} // namespace

void fadeStart(FadeTask& fade, uint32_t request, uint8_t param) {
    FadeState& f = fadeState();
    if (f.busy != 0 || request > kLastFadeRequest) return;
    const FadeSetup setup = kSetupOfRequest[request];
    uint32_t variantBits = 0;
    switch (setup) {
    case FadeSetup::DisplayMask:
        display().maskTicks = 1;
        f.covered = 0;
        return;
    case FadeSetup::Darken:
    case FadeSetup::Brighten:
        f.type = uint8_t(setup == FadeSetup::Darken ? FadeType::Darken : FadeType::Brighten);
        f.busy = fadeDirections[request >> kFadeDirectionShift];
        f.speed = fadeSpeeds[request & kFadeSpeedMask];
        variantBits = setup == FadeSetup::Darken ? 0 : 1;
        break;
    case FadeSetup::Wipe20:
    case FadeSetup::Wipe24:
    case FadeSetup::Wipe26:
    case FadeSetup::Wipe28:
    case FadeSetup::Wipe2A: {
        const WipeSetup& w = kWipeSetups[uint8_t(setup) - uint8_t(FadeSetup::Wipe20)];
        f.type = uint8_t(w.type);
        f.phase = int8_t(WipePhase::CloseDisplay);
        f.stripStep = int8_t(WipeStripStep::Start);
        f.variant = uint8_t(request - w.firstRequest);
        f.clearCounterWord();
        f.busy = kFadeToCovered;
        variantBits = request - w.firstRequest;
        break;
    }
    case FadeSetup::Slide30:
    case FadeSetup::Slide32:
    case FadeSetup::Slide34:
        if (setup == FadeSetup::Slide34) {
            f.type = uint8_t(FadeType::Slide34);
            f.phase = 0;
        } else {
            f.variant = uint8_t(request - (setup == FadeSetup::Slide30 ? kSlide30Request : kSlide32Request));
            f.type = uint8_t(setup == FadeSetup::Slide30 ? FadeType::Slide30 : FadeType::Slide32);
            f.stripStep = 0;
            f.phase = 0;
        }
        f.clearCounterWord();
        f.busy = kFadeFromCovered;
        break;
    default: return; // FadeSetup::None
    }
    f.keepBufferMode = param;
    f.blend = tileDrawMode(kBlendSubtract - (variantBits & 1)); // even: subtractive (darken), odd: additive
    fade.active = true; // exe 0x604aa4(2, 0x53f940): the task created
    fade.started = false;
    fade.approximatedSlideTicks = 0;
}

void fadeTick(FadeTask& fade) {
    if (!fade.active) return;
    FadeState& f = fadeState();
    const FadeType type = FadeType(f.type);
    // the wipes: tasks 0x53fa40.. (phases [0x8ae83c + (type - 2) * 12])
    if (type >= FadeType::Wipe20 && type <= FadeType::Wipe28) {
        wipeTick(type);
        if (f.busy == 0) fade.active = false; // the Reopen phase ended the task (exe 0x604b3d)
        return;
    }
    if (type > FadeType::Brighten) {
        approximatedSlideTick(fade);
        return;
    }
    int16_t level;
    if (!fade.started) { // exe 0x53f950's prologue
        fade.started = true;
        if (f.busy > 0) {
            f.phase = int8_t(FadePhase::Uncover);
            level = kFadeCoveredLevel;
        } else {
            f.phase = int8_t(FadePhase::Cover);
            level = 0;
        }
        f.level = level;
        f.stripTicks = 0;
        f.covered = 1;
    } else {
        level = f.level;
    }
    switch (FadePhase(f.phase)) {
    case FadePhase::Uncover:
        level = int16_t(level - int16_t(f.speed));
        f.level = level;
        if (level <= 0) {
            f.level = 0;
            f.speed = display().pace;
            f.phase = int8_t(FadePhase::Hold);
        }
        break;
    case FadePhase::Cover:
        level = int16_t(level + int16_t(f.speed));
        f.level = level;
        if (level >= 0x100) {
            f.level = kFadeCoveredLevel;
            f.speed = display().pace;
            f.phase = int8_t(FadePhase::Hold);
        }
        break;
    case FadePhase::Hold:
        if (f.speed == 0) {
            if (f.busy == kFadeFromCovered) f.covered = 0;
            f.busy = 0;
            fade.active = false; // exe 0x604b3d: the task ends
        } else {
            --f.speed;
        }
        break;
    default: break;
    }
}

float fadeLevel(const FadeTask& fade) {
    if (display().maskTicks != 0) return 1.0f; // exe 0x53f500: the display is off (exe 0x40b760(0))
    if (fade.active && fadeState().type > uint8_t(FadeType::Brighten)) return 1.0f;
    if (fadeState().covered == 0) return 0.0f;
    return float(uint16_t(fadeState().level) & 0x1ff) / 255.0f;
}

bool fadeAdditive() {
    return display().maskTicks == 0 && ((fadeState().blend >> kBlendShift) & 3) == kBlendAdd; // masked: black
}

} // namespace od::port
