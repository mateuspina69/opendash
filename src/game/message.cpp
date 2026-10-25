// The message windows (see message.hpp), transcribed from dash2.exe one function per exe function.
#include "game/message.hpp"
#include "game/message_state.hpp"

#include "game/audio_logic.hpp"
#include "game/display.hpp"
#include "game/game_state.hpp"
#include "game/language_pack.hpp"
#include "game/message_choice.hpp"
#include "game/player/player_input.hpp"
#include "game/text_script.hpp"
#include "game/window_frame.hpp"

namespace od::port {

const DataTable<ScalarTable<int8_t, 16>> messageHandBob{TableId::MessageHandBob};

namespace {

// the open's defaults (exe 0x557390)
constexpr uint32_t kOpenFlags = kWindowTypingSound | kWindowOpen | kWindowLayoutPass | kWindowLogic;
constexpr int16_t kDialogueX = 0x20, kDialogueY = 0xb0;
constexpr int16_t kShopX = 0x18, kShopY = 0xac;
constexpr int16_t kSubScreenShopX = 0x1c, kSubScreenShopY = 0xb4;
constexpr uint8_t kDefaultColumns = 0x18, kDefaultLines = 3, kDefaultLayer = 2;
constexpr uint8_t kVoiceFadeStep = 4; // a stopped window's voice fades out by this a tick

// circle / start pressed or cross held: the text sped up
bool fastForwardPressed() { return (pad0().pressed & kPadConfirm) || (pad0().held & kPadCross); }

// the window's voice stopped (exe 0x601220 / 0x601200): faded out, or paused while fb 1a still waits for it to play
void stopVoice(const MessageWindow& w) {
    if (!(w.flags & kWindowVoiceStarting)) voiceClipFadeOut(kVoiceFadeStep);
    else voiceClipPause();
}

// the window's glyphs and codes of this tick (the logic pass's loop in exe 0x557c90); returns whether a glyph was typed
bool typeText(MessageWindow& w) {
    bool typed = false;
    uint32_t pace = textPace(w.flags);
    for (;;) {
        if (pace & kFastPace) w.ticks = 0;
        const uint32_t p = w.readAt;
        const TextBytes text(p);
        const uint8_t c = text.byte(0);
        uint32_t r;
        if (c < kTextCodeOp) {
            const uint16_t cnt = uint16_t(w.ticks);
            r = kCodeGoOnFast;
            w.ticks = int16_t(cnt - 1u);
            if (cnt == 0) {
                w.ticks = int16_t(w.typeDelay);
                if (c == kGlyphSpace) {
                    r = kCodeGoOn;
                    w.readAt = p + 1;
                } else {
                    if (c >= kGlyphTwoByte) {
                        if (c >= kGlyphDouble) w.flags |= kWindowDoubleLine;
                        w.readAt = w.readAt + 1;
                    }
                    typed = true;
                    w.readAt = w.readAt + 1;
                }
            }
        } else {
            if (c == kTextCodeEnd) {
                w.readAt = p + 1;
                break;
            }
            r = runLogicCode(w, textCodeIndex(text));
            const uint32_t nf = w.flags;
            if (nf & kWindowNoFastForward) {
                w.flags = nf & ~kWindowFast;
                pace = textPace(nf);
            }
            if (r & kCodeRederive) pace = textPace(w.flags);
            if (r & kCodeTyped) typed = true;
        }
        if ((pace & r) == 0) break;
    }
    return typed;
}

} // namespace

void messageInit() {
    for (size_t i = 0; i < kWindowCount; ++i) {
        MessageWindow& w = messageWindow(i);
        w.flags = 0;
        w.index = uint8_t(i);
        w.readAt = 0;
        w.pageStart = 0;
        w.bank = nullptr;
        w.layoutReturn = 0;
        w.readReturn = 0;
    }
    messageSystem().openBits = 0;
    messageSystem().waitBits = 0;
}

void windowLogic() {
    uint8_t typed = 0;
    for (size_t i = 0; i < kWindowCount; ++i) {
        MessageWindow& w = messageWindow(i);
        uint32_t f = w.flags;
        if (f == 0) continue;
        const uint8_t bit = uint8_t(1u << i);
        if (fastForwardPressed() && !(f & kWindowNoFastForward)) w.flags = f | kWindowFast;
        f = w.flags;
        if ((f & kWindowVoiceStarting) == 0) {
            if ((f & kWindowFast) && (f & kWindowVoice)) {
                voiceClipFadeOut(kVoiceFadeStep);
                w.flags &= ~kWindowVoice;
            }
            if ((w.flags & kWindowVoice) && !voiceClipPlaying()) w.flags &= ~kWindowVoice;
        }
        f = w.flags;
        if ((f & kWindowRunning) && (f & kWindowLogic) && typeText(w)) typed |= bit;
        if (w.flags & (kWindowFrameShown | kWindowLogic)) frameAnimate(w);
        if ((w.flags & kWindowTypingSound) == 0 || (w.flags & kWindowLayouts) != 0) typed &= uint8_t(~bit);
    }
    if (typed) soundPlayFlat(UiSound::Typing);
}

void windowLayout() {
    for (size_t i = 0; i < kWindowCount; ++i) {
        MessageWindow& w = messageWindow(i);
        const uint32_t f = w.flags;
        if (f == 0 || (f & kWindowRunning) == 0 || (f & kWindowLayoutPass) == 0) continue;
        {
            const Ps1TextScope ps1(w);
            choiceHandUpdate(w);
        }
        w.colour = w.baseColour;
        w.shade = kShadeNormal;
        w.layoutAt = w.pageStart + 2;
        w.penX = w.x;
        const int y = messageLineY(w, 0);
        w.layoutLine = 0;
        w.penY = int16_t(y + w.y);
        for (;;) {
            const uint32_t p = w.layoutAt;
            if (p == w.readAt && w.layoutReturn == w.readReturn) break;
            const TextBytes text(p);
            const uint8_t c = text.byte(0);
            if (c < kTextCodeOp) {
                if (c >= kGlyphDouble) w.flags |= kWindowDoubleLine;
                w.penX = int16_t(uint16_t(w.penX) + glyphAdvance(w));
                continue;
            }
            if (c == kTextCodeEnd) {
                // the exe linked this window's glyphs while laying it out: OpenDash's draw pass reads the windows
                // after the tick, so a window closed by its end mark here is kept for it (kWindowsDrawn)
                w.copyTo(drawnWindowCopy(i));
                windowClose(int8_t(w.index), WindowClose::Gentle);
                break;
            }
            runLayoutCode(w, textCodeIndex(text));
        }
    }
}

void messageTick() {
    drawnWindowCopies().fill(MessageWindow{});
    if (display().drawingOff != 0) return;
    windowLogic();
    windowLayout();
}

int windowOpen(int8_t n, uint32_t bankAddress, int16_t id) {
    bankAddress = bankRedirect(bankAddress);
    if (display().drawingOff != 0 || n >= int8_t(kWindowCount) || bankAddress == 0) return -1;
    MessageSystem& m = messageSystem();
    if (n < 0) { // the highest free window
        int i = int(kWindowCount) - 1;
        while (messageWindow(size_t(i)).flags != 0) {
            if (i == 0) return -1;
            --i;
        }
        n = int8_t(i);
    } else if (messageWindow(size_t(n)).flags != 0) {
        return -1;
    }
    MessageWindow& w = messageWindow(size_t(n));
    w.bank.pointAt(bankAddress);
    w.flags = kOpenFlags;
    w.typeDelay = 1;
    w.x = kDialogueX;
    w.y = kDialogueY;
    w.columns = kDefaultColumns;
    w.lines = kDefaultLines;
    w.colour = 0;
    w.baseColour = 0;
    w.savedColour = 0;
    const uint32_t g = m.openBits;
    if ((g & kLayoutSubScreen) == 0) {
        if (g & kLayoutShop) {
            w.x = kShopX;
            w.y = kShopY;
            w.flags |= kWindowAltLayout | kWindowInstant;
        }
    } else {
        const uint32_t f = w.flags;
        w.flags = f | kWindowSystemLayout | kWindowInstant;
        if ((g & kLayoutShop) == 0) {
            w.colour = 0;
            w.baseColour = 0;
            w.savedColour = 0;
        } else {
            w.x = kSubScreenShopX;
            w.y = kSubScreenShopY;
            w.flags = f | kWindowLayouts | kWindowInstant;
        }
    }
    w.frameTarget.x = int16_t(w.x - kFrameInsetX);
    w.frameTarget.y = int16_t(w.y - kFrameInsetY);
    w.frameTarget.w = int16_t(w.columns * kGlyphWidth + kFrameExtra);
    if (gameOptions().ruby == 0) w.frameTarget.h = int16_t(w.lines << 4);
    else w.frameTarget.h = int16_t(w.rubyRows * kRubyRowHeight + w.lines * kLineHeight);
    w.frameTarget.h = int16_t(uint16_t(w.frameTarget.h) + kFrameBorder);
    w.frame.copyWords(w.frameTarget);
    w.layer = kDefaultLayer;
    w.scroll = 0;
    w.readReturn = 0;
    w.layoutReturn = 0;
    w.clearSpeaker();
    jumpToMessage(w, id);
    w.setPhase(FramePhase::Hidden);
    w.frameStyle = 0;
    w.shade = kShadeNormal;
    if (n >= 0 && n < int8_t(kWindowCount)) m.openBits |= kWindowOpenBits << n;
    return n;
}

uint8_t windowStatus(uint8_t n) {
    uint8_t r = 0;
    if (n < kWindowCount) {
        const uint32_t f = messageWindow(n).flags;
        if (f & kWindowOpen) r = kWindowStatusOpen;
        if (f & kWindowFrameShown) r |= kWindowStatusFrame;
        if (r != 0 && ((messageSystem().waitBits >> (int8_t(n) & 0x1f)) & 1)) r |= kWindowStatusWaiting;
    }
    return r;
}

void windowClose(int8_t n, WindowClose how) {
    if (n >= int8_t(kWindowCount) || n < 0) return;
    MessageWindow& w = messageWindow(size_t(n));
    if (w.flags & kWindowVoice) stopVoice(w);
    if (how == WindowClose::Gentle && (w.flags & kWindowFrameShown)) w.flags &= kWindowCloseKeeps;
    else w.flags = 0;
    MessageSystem& m = messageSystem();
    m.openBits &= ~(kWindowOpenBits << n);
    m.waitBits &= ~(1u << n);
    m.openBits &= ~kAnswerPending;
}

void windowCloseAll(WindowClose how) {
    windowLayoutVariant(WindowLayout::Dialogue);
    padSetRepeat(kPadRepeatDelay, kPadRepeatRate);
    for (int i = int(kWindowCount) - 1; i >= 0; --i) windowClose(int8_t(i), how);
}

void windowVoicesStop() {
    for (int i = int(kWindowCount) - 1; i >= 0; --i) {
        MessageWindow& w = messageWindow(size_t(i));
        const uint32_t f = w.flags;
        if ((f & kWindowVoice) == 0) continue;
        if ((f & kWindowOpBusy) == 0 || (f & kWindowVoiceStarting) == 0) {
            voiceClipFadeOut(kVoiceFadeStep);
            w.flags &= ~kWindowVoice;
        } else { // fb 1a still waits for its clip: paused, and the read pointer past fb 1a's operands
            voiceClipPause();
            w.flags &= ~(kWindowVoice | kWindowVoiceStarting | kWindowOpBusy);
            w.readAt = w.readAt + 6;
        }
    }
}

void windowLayoutVariant(WindowLayout layout) {
    MessageSystem& m = messageSystem();
    uint32_t g = m.openBits & ~kLayoutBits;
    if (layout == WindowLayout::SubScreen) g |= kLayoutSubScreen;
    else if (layout == WindowLayout::Shop) g |= kLayoutShop;
    else if (layout == WindowLayout::SubScreenShop) g |= kLayoutBits;
    m.openBits = g;
}

} // namespace od::port
