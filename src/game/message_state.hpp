// The message windows' state (game/message runs them, game/text_script interprets their text): the five windows at
// 0xa7adc0 (0x80 bytes each) and their flag bits, the windows' open / answer / wait bits, the text a window reads (a
// bank's offset table, the bytes of a page), and OpenDash's copies of the windows the layout pass closed.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "game/loaded_data.hpp"
#include "game/state_array.hpp"
#include "game/text_memory.hpp"

namespace od::port {

constexpr size_t kWindowCount = 5;
// OpenDash: the windows the layout pass closed at their end mark this tick (the exe drew them while laying them out:
// e.g. the sub-screen's help in window 0, opened again every frame); 5 x 0x80, cleared every tick
constexpr uint32_t kMessageBank = 0x73c000; // the stage DAT's type-6 segment (copied there at the stage load)

// ---- the text a window reads ---------------------------------------------------------------------------------------

// (a message bank and the bytes of a text: game/text_memory MessageBank / TextBytes)

// ---- the window record -----------------------------------------------------------------------------------------------

// +0: the window's flags
constexpr uint32_t kWindowLogic = 0x1;          // the logic pass types the window's text
constexpr uint32_t kWindowLayoutPass = 0x2;     // the layout pass lays it out
constexpr uint32_t kWindowAnswerShown = 0x8;    // the chosen option stays highlighted (the choice was confirmed)
constexpr uint32_t kWindowWaitButton = 0x10;    // a code waits for a button (fb 18 / 24 / 32)
constexpr uint32_t kWindowChoice = 0x20;        // a choice is up (fb 10 / 39)
constexpr uint32_t kWindowOpBusy = 0x40;        // the current code runs over several ticks (its first tick done)
constexpr uint32_t kWindowOpen = 0x80;          // open: the text is typed / shown
constexpr uint32_t kWindowRunning = kWindowWaitButton | kWindowChoice | kWindowOpBusy | kWindowOpen; // the text runs
constexpr uint32_t kWindowChoiceMask = 0xf00;   // the option chosen (bits 8..11)
constexpr uint32_t kWindowChoiceShift = 8;
constexpr uint32_t kWindowVoiceStarting = 0x1000; // fb 1a asked for its voice clip and waits for it to play
constexpr uint32_t kWindowVoice = 0x2000;       // the window's voice clip sounds
constexpr uint32_t kWindowFrameOpen = 0x4000;   // the frame has opened (fb 05 went on)
constexpr uint32_t kWindowFrameShown = 0x8000;  // the frame is shown (opening, open or closing)
constexpr uint32_t kWindowTypingSound = 0x10000; // the typing sound plays
constexpr uint32_t kWindowNoFastForward = 0x20000; // circle / start does not speed the text up (fb 29 0)
constexpr uint32_t kWindowInstant = 0x40000;    // the page is shown at once (fb 09 0, the system layouts)
constexpr uint32_t kWindowFast = 0x80000;       // circle / start sped the text up
constexpr uint32_t kWindowFastModes = kWindowInstant | kWindowFast;
constexpr uint32_t kWindowRuby = 0x100000;      // the layout pass lays out a glyph's ruby
constexpr uint32_t kWindowMoreArrow = 0x200000; // fb 24's wait: the "more" arrow shown (no choice cursor)
constexpr uint32_t kWindowDoubleLine = 0x400000; // a double-size glyph: the line takes two rows
constexpr uint32_t kWindowSmallLayout = 0x800000; // fb 3a: 12-px lines, the option hand at x * 12 - 8 (no sounds)
constexpr uint32_t kWindowNaming = 0x1000000;   // fb 4a: a record's three letters are entered
constexpr uint32_t kWindowAnswered = 0x8000000; // the choice was answered (fb 07 stops waiting; a jump clears it)
constexpr uint32_t kWindowAltLayout = 0x40000000; // opened in the shop's layout (the default lines and ruby)
constexpr uint32_t kWindowSystemLayout = 0x80000000; // opened in the sub-screen's layout (16-px lines from +2, no ruby)
constexpr uint32_t kWindowLayouts = kWindowAltLayout | kWindowSystemLayout;
// the bits a gentle close keeps while the frame closes (the frame, the choice, the passes)
constexpr uint32_t kWindowCloseKeeps = kWindowFrameShown | kWindowFrameOpen | kWindowChoiceMask | kWindowLayoutPass |
                                       kWindowLogic;

// The window's frame rectangle (four shorts); the exe copies one into the other as two dwords
struct WindowRect {
    int16_t x = 0, y = 0, w = 0, h = 0;

    // this rect = o (the exe's two dword copies)
    void copyWords(const WindowRect& o) { *this = o; }
};

constexpr int kGlyphWidth = 0xc;
constexpr int16_t kLineHeight = 0x10;
constexpr int16_t kRubyRowHeight = 3;
constexpr int16_t kFrameInsetX = 7, kFrameInsetY = 3;
constexpr int16_t kFrameExtra = 3, kFrameBorder = 7;

// The frame's open / close animation (+0x50)
enum class FramePhase : uint8_t {
    Hidden = 0,
    Start = 1,   // fb 05 asked for it: the animation's first step
    Growing = 2,
    Open = 3,
    Closing = 4, // fb 0c, the end of the text, a gentle close
    Closed = 5,  // the window's flags cleared unless it is still open
};

struct MessageWindow {
    uint32_t flags = 0;          // kWindow*
    int16_t ticks = 0;           // the typing countdown / the current code's countdown
    uint8_t index = 0;           // the window's index (0..4)
    uint8_t typeDelay = 0;       // the ticks between two typed glyphs
    int16_t x = 0;               // the text's origin (screen pixels)
    int16_t y = 0;
    StateArray<uint8_t, 4> unread0c{}; // (copied / cleared with the window; no reader)
    int16_t penX = 0;            // the layout pass's pen
    int16_t penY = 0;
    uint8_t columns = 0;         // the text area (fb 06): columns, lines
    uint8_t lines = 0;
    uint8_t logicLine = 0;       // the line the logic pass / the layout pass is on
    uint8_t layoutLine = 0;
    int16_t scroll = 0;          // the text's vertical offset (fb 0d scrolls it up)
    uint8_t unread1a = 0;
    uint8_t shade = 0;           // the glyphs' brightness (kShadeNormal / kShadeDim)
    uint16_t pageHeader = 0;     // the page's header: bit k = text line k has ruby
    int8_t openedWindow = 0;     // the window fb 12 / 13 / 14 / 32 / 38 opened (-1: none)
    int8_t rubyRows = 0;         // fb 06: the ruby lines the frame makes room for
    uint8_t baseColour = 0;      // the page's colour, the current one, the one fb 0a saved
    uint8_t colour = 0;
    uint8_t savedColour = 0;
    uint8_t layer = 0;           // the OT layer (fb 30)
    uint8_t speaker = 0;         // fb 33: kSpeakerNewLine / kSpeakerAnimation | the value
    uint8_t speakerNewLine = 0;  // the values fb 33 handed over (also to the event's newLine / speakerAnimation)
    uint8_t speakerAnimation = 0;
    uint8_t unread27 = 0;        // cleared by the open and by fb 3c (its reader is not traced)
    DataPtr<> bank;              // the bank the window reads (0: none; game/text_memory MessageBank)
    uint32_t pageStart = 0;      // the page's header (a text address)
    uint32_t readAt = 0;         // the logic pass's read pointer
    uint32_t layoutAt = 0;       // the layout pass's
    uint32_t readReturn = 0;     // where fb 1f returns to from an inserted text (0: not inside one)
    uint32_t layoutReturn = 0;
    int16_t handX = 0;           // the choice's hand: where it is, where it glides to
    int16_t handY = 0;
    int16_t handTargetX = 0;
    int16_t handTargetY = 0;
    uint8_t optionX = 0;         // the option under the hand: its column and line
    uint8_t optionLine = 0;
    int8_t handStepX = 0;        // the hand's glide per tick
    int8_t handStepY = 0;
    uint8_t frameStyle = 0;      // fb 05: the open animation (low nibble), the look (high nibble)
    StateArray<uint8_t, 3> unread4d{};
    uint8_t framePhase = 0;      // FramePhase
    uint8_t frameSide = 0;       // style 2: which side grows / shrinks (0 the width, 1 the height)
    uint8_t frameStepW = 0;      // the animation's step per tick
    uint8_t frameStepH = 0;
    WindowRect frame;            // the frame now
    WindowRect frameTarget;      // the frame open (fb 06 / the open set it)
    int32_t timeValue = 0;       // a time fb 23 prints (fb 45 / 48 load it)
    int32_t number = 0;          // the number fb 21 prints (a price, a slot)
    uint16_t itemName = 0;       // the system string fb 3d inserts (an item's)
    uint8_t place = 0;           // the place name fb 44 inserts
    int8_t record = 0;           // the best-time record fb 48 / 4a / 4b work on
    int8_t letter = 0;           // fb 4a: the letter being entered (-1: the name cleared)
    StateArray<int8_t, 3> letters{}; // fb 4a: the three letters' glyph indices
    uint32_t savedRepeat = 0;    // the pad's repeat delay | rate << 8 before the choice
    StateArray<uint8_t, 8> unread78{};

    FramePhase phase() const { return FramePhase(framePhase); }
    void setPhase(FramePhase p) { framePhase = uint8_t(p); }
    // the option chosen (flags bits 8..11)
    uint8_t choice() const { return uint8_t((flags >> kWindowChoiceShift) & 0xf); }
    // the speaker bytes cleared (the open's dword store at +0x24)
    void clearSpeaker() {
        speaker = 0;
        speakerNewLine = 0;
        speakerAnimation = 0;
        unread27 = 0;
    }
    // the text the logic / layout pass reads
    const TextBytes readText() const { return TextBytes(readAt); }
    const TextBytes layoutText() const { return TextBytes(layoutAt); }
    // the window copied into `to`
    void copyTo(MessageWindow& to) const { to = *this; }
    // the window cleared
    void clearWords() { *this = MessageWindow{}; }
};
constexpr uint8_t kShadeNormal = 8; // the glyphs at full brightness
constexpr uint8_t kShadeDim = 4;    // an option not chosen, a dim colour code
// +0x24 (fb 33): what the speaker byte's value is for, and its value's bits
constexpr uint8_t kSpeakerKindMask = 0xc0;
constexpr uint8_t kSpeakerNewLine = 0x40;   // the value goes to +0x25 (the talking player nods on new lines)
constexpr uint8_t kSpeakerAnimation = 0x80; // the value goes to +0x26 (an animation the speaking actor plays)
constexpr uint8_t kSpeakerValueMask = 0x3f;
constexpr uint8_t kSpeakerNone = 0x3f;      // the value "none" (handed over as 0xff)

// the five windows
inline StateArray<MessageWindow, kWindowCount>& messageWindows() {
    static StateArray<MessageWindow, kWindowCount> w{};
    return w;
}
inline MessageWindow& messageWindow(size_t i) { return messageWindows()[i]; }
// window record n as the exe indexes it from the code's byte (fb 07: the exe does not check n; an n past the five
// windows is an explicit StateIndexError)
inline MessageWindow& windowRecord(int8_t n) { return messageWindows()[size_t(int32_t(n))]; }
// OpenDash's copy of window i the layout pass closed this tick (the host draws it for that tick)
inline StateArray<MessageWindow, kWindowCount>& drawnWindowCopies() {
    static StateArray<MessageWindow, kWindowCount> w{};
    return w;
}
inline MessageWindow& drawnWindowCopy(size_t i) { return drawnWindowCopies()[i]; }

// ---- the windows' global bits -----------------------------------------------------------------------------------------

// 0xa72234: per window an answer nibble (bits 4n..4n+3) and an open bit (0x4000000 << n), the answer pending bit, the
// layout the next windows open in
constexpr uint32_t kAnswerPending = 0x1000000;     // a choice was answered (cleared when the next one starts)
constexpr uint32_t kWindowOpenBits = 0x4000000;    // << the window's index: the window is open
constexpr uint32_t kLayoutShop = 0x800000;         // the next windows open in the shop's layout
constexpr uint32_t kLayoutShopFb25 = 0x40000000;   // fb 25 2's layout bit (no open reads it)
constexpr uint32_t kLayoutSubScreen = 0x80000000;  // the next windows open in the sub-screen's layout
constexpr uint32_t kLayoutBits = kLayoutSubScreen | kLayoutShop; // the bits windowLayoutVariant sets

struct MessageSystem {
    uint32_t openBits = 0;  // kAnswerPending / kWindowOpenBits / kLayout* / the answers
    uint32_t waitBits = 0;  // bit n: window n waits for the event runner (fb 2b)

    // the answer window n's choice left (its nibble)
    uint8_t answerOf(uint32_t n) const { return uint8_t((openBits >> (n * 4)) & 0xf); }
};
inline MessageSystem& messageSystem() {
    static MessageSystem s;
    return s;
}

} // namespace od::port
