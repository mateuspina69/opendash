// The message text's interpreter (dash2.exe): a window's page, its glyphs and control codes as the logic pass runs
// them (exe table 0x8b14b8: waits, choices, jumps, colours, voices, sounds, flags, the zenny and balances, inserted
// texts, a warp, the naming of a record) and as the layout pass walks them (exe table 0x8b15e8: the pen, the colours,
// the inserted texts again), the page and message starts, the speaker bytes, the glyph advance and the lines' y.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "game/message_state.hpp"

namespace od::port {

// ---- the text's bytes -------------------------------------------------------------------------------------------------
constexpr uint8_t kGlyphSpace = 0x20;      // a space: no typing delay
constexpr uint8_t kGlyphTwoByte = 0xd0;    // 0xd0..0xfa: the first byte of a two-byte glyph
constexpr uint8_t kGlyphDouble = 0xe0;     // 0xe0..0xfa: a double-size glyph (24 px, two rows)
constexpr uint8_t kTextCodeOp = 0xfb;      // fb nn ...: control code nn
constexpr uint8_t kTextCodeNewLine = 0xfc;
constexpr uint8_t kTextCodeWait = 0xfd;    // fd hi lo: a wait, then a new page
constexpr uint8_t kTextCodeRuby = 0xfe;    // fe glyph n k ruby...: a glyph with ruby
constexpr uint8_t kTextCodeEnd = 0xff;     // the message's end
constexpr uint8_t kTextOpChoiceOption = 0x0f; // fb 0f i: the choice's option i (the layout pass colours it)
constexpr uint8_t kTextOpReturn = 0x1f;       // fb 1f: the end of an inserted text
constexpr uint32_t kTextCodeHandlers = 76;    // the entries of each handler table
// the exe's banks the codes insert from (the host's text draw reads them too)


// the handler tables' index of the code at `text`: nn for fb nn, the byte + 4 for fc / fd / fe (fb 00..02 alias them)
inline uint32_t textCodeIndex(const TextBytes& text) {
    const uint8_t c = text.byte(0);
    return c == kTextCodeOp ? text.byte(1) : uint8_t(c + 4);
}

// ---- what a logic-pass code returns (the bits the logic pass's loop tests) --------------------------------------------
constexpr uint32_t kCodeStop = 0;              // the window's text stops for this tick
constexpr uint32_t kCodeGoOnFast = 0x1;        // it goes on this tick in the fast modes
constexpr uint32_t kCodeTyped = 0x2;           // a glyph was typed (the typing sound)
constexpr uint32_t kCodeGoOnNormal = 0x100;    // it goes on this tick at the normal pace
constexpr uint32_t kCodeGoOn = kCodeGoOnFast | kCodeGoOnNormal;
constexpr uint32_t kCodeRederive = 0x10000;    // the pace changed: the loop takes it from the flags again
constexpr uint32_t kFastPace = 0xff;           // the loop's mask in the fast modes
constexpr uint32_t kNormalPace = 0xff00;       // ... at the normal pace

// the logic pass's pace mask of the window's flags
inline uint32_t textPace(uint32_t flags) { return (flags & kWindowFastModes) ? kFastPace : kNormalPace; }

// the logic pass's code `index` (exe table 0x8b14b8) on window w; returns the kCode* bits
uint32_t runLogicCode(MessageWindow& w, uint32_t index);
// the layout pass's code `index` (exe table 0x8b15e8) on window w
void runLayoutCode(MessageWindow& w, uint32_t index);

// fb 22 k: where the key name's string is read from (the host's text draw follows the same string)
uint32_t keyNameText(uint8_t k);

// exe 0x559700: the page at +0x2c (re)started: its header into +0x1c, the read pointer after it, the speaker bytes
void pageRestart(MessageWindow& w);
// exe 0x5596c0: the window jumps to message `id` of its bank (the page started)
void jumpToMessage(MessageWindow& w, int16_t id);
// exe 0x558110 (the advance; the glyph's primitive is the host's): the layout pen's advance of the glyph at +0x34,
// the read pointer moved past it
int glyphAdvance(MessageWindow& w);
// exe 0x559620 (PS1 0x8004bd14, the same): the y of text line `line` of window w from its text origin +0xa (ruby-aware)
int messageLineY(const MessageWindow& w, int line);
// the length in bytes of the control code at `text` (fb nn ... / fc..fe; read from the layout handler table), 0 = unknown
uint32_t messageOpLength(const TextBytes& text);

// fb 3c (a warp from a message) starts the screen fade (exe 0x53f6b0(type, 0)): the app passes game/fade fadeStart with
// its fade task. Without a hook fb 3c throws notPorted.
void setMessageFadeHook(std::function<void(uint8_t)> hook);
// the code handlers (logic and layout table entries) run since the last reset, sorted (test coverage)
std::vector<uint32_t> messageHandlersSeen(bool reset);

} // namespace od::port
