// The message text's interpreter (see text_script.hpp), transcribed from dash2.exe one function per exe function. The
// codes are dispatched by the exe's handler tables (the named routine the source reads from the table); a handler the
// port does not have throws notPorted with its index so a new scenario names what is missing.
#include "game/text_script.hpp"
#include "game/text_script_state.hpp"

#include <cstdio>
#include <set>

#include "engine/data_source.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/camera_state.hpp"
#include "game/display.hpp"
#include "game/event.hpp"
#include "game/game_state.hpp"
#include "game/hit_points.hpp"
#include "game/input_bindings.hpp"
#include "game/language_pack.hpp"
#include "game/message.hpp"
#include "game/message_choice.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/rewards.hpp"
#include "game/save_slots.hpp"
#include "game/story_flags.hpp"
#include "game/text_format.hpp"
#include "game/transition_state.hpp"

using namespace od::port::text_script_state;

namespace od::port {

namespace {

constexpr const char* kMessagePort = "message port"; // the area of its port-gap errors



// the two passes' handler tables (exe 0x8b14b8 / 0x8b15e8, by the code's index): the named routines
// (RoutineId::TextLogic... / TextLayout..., engine/routines.csv)
const DataTable<RoutineTable<kTextCodeHandlers>> logicHandlers{TableId::TextLogicHandlers};
const DataTable<RoutineTable<kTextCodeHandlers>> layoutHandlers{TableId::TextLayoutHandlers};

// the exe's banks and buffers the codes read and write
// (the place names' and the key names' banks: TableId::TextScriptPlaceNameBank / TableId::TextScriptKeyNameBank, text_script.hpp)
const DataTable<ScalarTable<uint16_t, 8>> keyNameButtons{TableId::TextScriptKeyNameButtons}; // fb 22: the pad button of each key name (0: k = 7's pair)
const DataTable<ScalarTable<uint16_t, 0x2c>> nameGlyphs{TableId::TextScriptNameGlyphs}; // fb 4a: the letters a name is entered from
constexpr uint8_t kKeyGlyphBase = 0xf;      // fb 22: a key's glyph = its index + this
constexpr uint8_t kKeyNamePair = 7;         // fb 22 7: the two keys of L1 / R1
constexpr uint8_t kNoMessage = 0xff;        // a jump table's "fall through"
constexpr uint16_t kNoFlag = 0xffff;        // fb 43: no flag for the answer
constexpr uint8_t kZennyCounter = 0x80;     // fb 21 k: the zenny (else the window's number)
constexpr uint8_t kNumberPadMask = 0x7f;
constexpr int8_t kBestTimeCount = 9;
constexpr uint8_t kNameLetters = 3;         // a best time's name
constexpr int8_t kNameGlyphCount = 0x2c;
constexpr uint16_t kNameBlank = 0x41;       // fb 4b's letters
constexpr uint8_t kLastTier = 2;
constexpr uint8_t kDockCounterCount = 8;
constexpr int8_t kSaveSlotCount = 5;
constexpr int16_t kScrollStep = 4;          // fb 0d: pixels a tick
constexpr int16_t kRubyLineHeight = 0x13;
constexpr int16_t kSmallLineHeight = 0xc;
constexpr int kLineTop = 3;                 // the first line's offset, the system layout's
constexpr int kSystemLineTop = 2;
constexpr int kRubyLineTop = 4;             // the first line has ruby
constexpr int kDoubleGlyphWidth = 0x18;
constexpr int kRubySpaceWidth = 4;          // inside ruby: a space, the narrow glyphs 0xae..0xb6, the others
constexpr int kRubyNarrowWidth = 4;
constexpr int kRubyGlyphWidth = 5;
constexpr int kRubyOffsetX = 0xb;           // fe: the ruby at x + (s8) k - 0xb, 6 px higher
constexpr int kRubyRaise = 6;
constexpr uint8_t kColourCount = 8;         // fb 0a: a colour above it is 0
constexpr uint8_t kColourDim = 0x80;        // fb 0a: the colour's dim bit (+0x1b kShadeDim)
constexpr uint8_t kColourMask = 0x7f;
constexpr uint8_t kOptionColourSmall = 6;   // the small layout's unchosen option colour
constexpr uint8_t kRubyGlyphFirst = 0xae, kRubyGlyphLast = 0xb6;
constexpr uint8_t kSpeakerNoneHandedOver = 0xff; // a speaker value of kSpeakerNone as fb 33 hands it over

std::function<void(uint8_t)> gFadeHook; // exe 0x53f6b0(type, 0) (game/fade fadeStart)
std::set<uint32_t> gSeen; // the handlers run (logic and layout; their pointers' values), for the test's coverage line

// a code whose handler the port does not have: the port gap names the table, the index and the handler
[[noreturn]] void codeNotPorted(const char* table, uint32_t index, uint32_t handler) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s opcode index %u (handler %06x)", table, index, handler);
    notPorted(kMessagePort, buf);
}

// circle / start pressed, or cross held (the waits' "go on")
bool goOnPressed() { return (pad0().pressed & kPadConfirm) || (pad0().held & kPadCross); }

// exe 0x559740: the speaker byte +0x24 handed over: kSpeakerNewLine -> +0x25 (and the event's newLine),
// kSpeakerAnimation -> +0x26 (and the event's speakerAnimation); kSpeakerNone as 0xff
void speakerHandOver(MessageWindow& w) {
    uint8_t v = w.speaker & kSpeakerValueMask;
    const uint8_t k = w.speaker & kSpeakerKindMask;
    if (v == kSpeakerNone) v = kSpeakerNoneHandedOver;
    EventState& ev = eventState();
    if (k == kSpeakerNewLine) {
        w.speakerNewLine = v;
        if (ev.active != 0) ev.newLine = v;
    } else if (k == kSpeakerAnimation) {
        w.speakerAnimation = v;
        if (ev.active != 0) ev.speakerAnimation = v;
    }
}

// the window jumps to message t of its bank (a jump code's target): the frame's shown bit cleared first
void jumpTo(MessageWindow& w, uint8_t t) {
    w.flags &= ~kWindowFrameShown;
    jumpToMessage(w, int16_t(t));
}

// fb 22 k (logic exe 0x55a6e0 / layout 0x55a780): into the key-name string k of the exe bank TableId::TextScriptKeyNameBank, its first glyph
// patched with the key bound to the pad button keyNameButtons[k] (k = kKeyNamePair: L1's and R1's)
void insertKeyName(uint32_t& read, uint32_t& ret) {
    const uint32_t p = read;
    const uint8_t k = TextBytes(p).byte(2);
    read = p + 3;
    ret = p + 3;
    read = keyNameText(k);
}

} // namespace

// fb 22 k's string. The PS1's bank is by the pad's layout (its table of layouts x 12 gives the bank's message, the pad's
// button glyph); the PC's is by k, its first glyph patched with the key bound to the pad button keyNameButtons[k]
// (k = kKeyNamePair: L1's and R1's)
uint32_t keyNameText(uint8_t k) {
    const MessageBank bank(stateAddress(TableId::TextScriptKeyNameBank));
    if (dataSource().hasTable({TableId::TextScriptKeyNameLayouts, 0})) {
        constexpr uint8_t kAnalogPad = 7;
        constexpr uint32_t kLayoutNames = 12, kAnalogLayouts = 3;
        const GameOptions& o = gameOptions();
        const uint32_t layout = pad0().typeByte() == kAnalogPad ? o.controllerLayout + kAnalogLayouts : o.controlOption;
        const DataArray<uint8_t> layouts(stateAddress(TableId::TextScriptKeyNameLayouts));
        return bank.message(layouts[size_t(layout * kLayoutNames + k)]);
    }
    TextBytes name(bank.message(k));
    const uint16_t button = keyNameButtons[k];
    if (button != 0) {
        name.byte(0) = uint8_t(uint8_t(padButtonKey(button)) + kKeyGlyphBase);
    } else if (k == kKeyNamePair) {
        name.byte(0) = uint8_t(uint8_t(padButtonKey(kPadL1)) + kKeyGlyphBase);
        name.byte(2) = uint8_t(uint8_t(padButtonKey(kPadR1)) + kKeyGlyphBase);
    }
    return name.at;
}

namespace {

// fb 23 k (logic exe 0x55a810 / layout 0x55aaf0): a time into TableId::TextScriptTimeText (k 0: the play time, 1: the window's time as a
// clock, 2: as a lap), ended by fb 1f
void insertTime(MessageWindow& w, uint32_t& read, uint32_t& ret) {
    const uint32_t p = read;
    const int8_t k = TextBytes(p).signedByte(2);
    read = p + 3;
    insertedTexts(); // the buffer (built data at TableId::TextScriptTimeText)
    TextBytes out(stateAddress(TableId::TextScriptTimeText));
    int n = 0;
    if (k == 0) n = messageTimeWrite(int32_t(gameState().playTime), out, TimeFormat::Clock);
    else if (k == 1) n = messageTimeWrite(w.timeValue, out, TimeFormat::Clock);
    else if (k == 2) n = messageTimeWrite(w.timeValue, out, TimeFormat::Lap);
    out.byte(uint32_t(n)) = kTextCodeOp;
    out.byte(uint32_t(n) + 1) = kTextOpReturn;
    ret = read;
    read = stateAddress(TableId::TextScriptTimeText);
}

// fb 44 (logic exe 0x55b600 / layout 0x55b630): into the place name +0x6e of the place bank
void insertPlace(MessageWindow& w, uint32_t& read, uint32_t& ret) {
    const uint32_t q = read + 3;
    read = q;
    ret = q;
    read = MessageBank(bankRedirect(stateAddress(TableId::TextScriptPlaceNameBank))).message(w.place);

}

// fb 21 k (logic exe 0x55a610 / layout 0x55a680): a number (k & kZennyCounter: the zenny, else the window's +0x68) into
// TableId::TextScriptNumberText, ended by fb 1f
void insertNumber(MessageWindow& w, uint32_t& read, uint32_t& ret) {
    const uint32_t p = read;
    const uint8_t k = TextBytes(p).byte(2);
    read = p + 3;
    const int32_t v = (k & kZennyCounter) ? int32_t(gameState().zenny) : int32_t(w.number);
    insertedTexts(); // the buffer (built data at TableId::TextScriptNumberText)
    TextBytes out(stateAddress(TableId::TextScriptNumberText));
    const int n = messageNumberWrite(v, out, k & kNumberPadMask);
    out.byte(uint32_t(n)) = kTextCodeOp;
    out.byte(uint32_t(n) + 1) = kTextOpReturn;
    ret = read;
    read = stateAddress(TableId::TextScriptNumberText);
}

// exe 0x55b1b0 (fb 3c stage room x y z yaw fade arrival: 12 operand bytes): a warp from a message. The first tick (on
// a frame of buffer 0): the operands kept, the screen fade, the transition request (another stage reopens the window
// on TableId::TextScriptWarpBank's message 0), the voices stopped. Then it waits for the request to be taken: the player drawn, past the
// operands, every window closed, the event ended.
uint32_t warpCode(MessageWindow& w) {
    const uint32_t p = w.readAt;
    const TextBytes op(p);
    const uint8_t stage = op.byte(2);
    if ((w.flags & kWindowOpBusy) == 0) {
        if (display().drawBuffer != 0) return kCodeStop;
        for (uint32_t i = 0; i < insertedTexts().warpOperands.size(); ++i) insertedTexts().warpOperands[i] = op.byte(2 + i);
        if (!gFadeHook) notPorted(kMessagePort, "fb 3c: no screen fade hook (exe 0x53f6b0)");
        gFadeHook(op.byte(0xc));
        requestTransition(kTransitionAccepted, stage, op.byte(3), int16_t(op.be16(4)), int16_t(op.be16(6)),
                          int16_t(op.be16(8)), int16_t(op.be16(0xa)), op.byte(0xd), kNoFade);
        if (gameState().stage != stage) {
            windowClose(int8_t(w.index), WindowClose::Force);
            windowOpen(int8_t(w.index), stateAddress(TableId::TextScriptWarpBank), 0);
        }
        windowVoicesStop();
        w.unread27 = 0;
        w.flags |= kWindowOpBusy;
        return kCodeStop;
    }
    if (transitionRequest().state != kTransitionNone) return kCodeStop;
    player().setDrawn(true);
    w.readAt = p + 0xe;
    w.flags &= ~kWindowOpBusy;
    windowCloseAll(WindowClose::Force);
    if (eventState().active != 0) eventAbort();
    return kCodeStop;
}

// a countdown code's first tick: the window's countdown from the code's operand (kWindowOpBusy marks it)
void startCountdown(MessageWindow& w, int16_t ticks) {
    if ((w.flags & kWindowOpBusy) == 0) {
        w.flags |= kWindowOpBusy;
        w.ticks = ticks;
    }
}

// the length of a layout skip handler's code (0: not a skip)
uint32_t skipLength(RoutineId h) {
    switch (h) {
    case RoutineId::TextLayoutSkipOne: return 1;
    case RoutineId::TextLayoutSkipTwo: return 2;
    case RoutineId::TextLayoutSkipThree: return 3;
    case RoutineId::TextLayoutSkipFour: return 4;
    case RoutineId::TextLayoutSkipFive: return 5;
    case RoutineId::TextLayoutSkipSix: return 6;
    case RoutineId::TextLayoutSkipSeven: return 7;
    case RoutineId::TextLayoutSkipEight: return 8;
    case RoutineId::TextLayoutSkipNine: return 9;
    case RoutineId::TextLayoutSkipThirteen: return 0xd;
    case RoutineId::TextLayoutSkipFourteen: return 0xe;
    default: return 0;
    }
}

} // namespace

void pageRestart(MessageWindow& w) {
    if ((w.flags & kWindowLayouts) == 0) w.flags &= ~kWindowFastModes;
    const uint32_t p = w.pageStart;
    w.logicLine = 1;
    w.ticks = 0;
    w.readAt = p;
    w.pageHeader = TextBytes(p).word(0);
    w.readAt = p + 2;
    speakerHandOver(w);
}

void jumpToMessage(MessageWindow& w, int16_t id) {
    const uint32_t f = w.flags;
    w.flags = f & ~kWindowAnswered; // the exe stores the flags twice
    const uint32_t page = MessageBank(w.bank.value()).message(id);
    w.flags = f & ~(kWindowAnswered | kWindowChoiceMask);
    w.pageStart = page;
    pageRestart(w);
}

int glyphAdvance(MessageWindow& w) {
    uint32_t p = w.layoutAt;
    const uint8_t b = TextBytes(p).byte(0);
    if (ps1Window(w) && ps1WidthsLoaded()) { // a pack window: the PS1 glyph routine's advance (one byte, no ruby)
        w.layoutAt = p + 1;
        return ps1Advance(b);
    }
    int adv;
    if ((w.flags & kWindowRuby) == 0) {
        if (b < kGlyphDouble) {
            if (b >= kGlyphTwoByte) ++p;
            adv = kGlyphWidth;
        } else {
            ++p;
            adv = kDoubleGlyphWidth;
        }
    } else { // a ruby glyph
        if (b == kGlyphSpace) {
            w.layoutAt = p + 1;
            return kRubySpaceWidth;
        }
        adv = (b >= kRubyGlyphFirst && b <= kRubyGlyphLast) ? kRubyNarrowWidth : kRubyGlyphWidth;
    }
    w.layoutAt = p + 1;
    return adv;
}

int messageLineY(const MessageWindow& w, int line) {
    const uint32_t f = w.flags;
    if (f & kWindowSmallLayout) return w.scroll + line * kSmallLineHeight;
    if (!(f & kWindowAltLayout) && (f & kWindowSystemLayout)) return w.scroll + line * kLineHeight + kSystemLineTop;
    uint32_t v;
    if (gameOptions().ruby == 0) {
        v = uint32_t(line * kLineHeight + kLineTop);
    } else { // the lines whose header bit is set are taller
        const uint16_t hdr = w.pageHeader;
        v = uint32_t((hdr & 1) ? kRubyLineTop | kLineTop : kLineTop);
        for (; line != 0; --line) v += ((hdr >> (line & 0x1f)) & 1) ? kRubyLineHeight : kLineHeight;
    }
    return w.scroll + int(v);
}

uint32_t runLogicCode(MessageWindow& w, uint32_t index) {
    const Ps1TextScope ps1(w);
    const RoutineRef& h = logicHandlers[index];
    gSeen.insert(h.pointerValue());
    const uint32_t p = w.readAt;
    const TextBytes op(p);
    MessageSystem& m = messageSystem();
    Player& pl = player();
    GameState& g = gameState();
    switch (h.id()) {
    case RoutineId::TextLogicSkipOne: w.readAt = p + 1; return kCodeGoOn;
    case RoutineId::TextLogicSkipTwo: w.readAt = p + 2; return kCodeGoOn;
    case RoutineId::TextLogicSkipThree: w.readAt = p + 3; return kCodeGoOn;
    case RoutineId::TextLogicNewLine: {
        const uint8_t c = w.logicLine;
        w.logicLine = uint8_t(c + 1u);
        if (w.flags & kWindowDoubleLine) w.logicLine = uint8_t(c + 2u);
        w.readAt = p + 1;
        return kCodeGoOn;
    }
    case RoutineId::TextLogicWaitPage: {
        startCountdown(w, int16_t(op.be16(1)));
        const uint16_t c = uint16_t(w.ticks);
        w.ticks = int16_t(c - 1u);
        if (c == 0) {
            w.flags &= ~kWindowOpBusy;
            w.pageStart = p + 3;
            pageRestart(w);
            return kCodeGoOn;
        }
        return kCodeStop;
    }
    case RoutineId::TextLogicRubyGlyph: {
        const int16_t c = w.ticks;
        w.ticks = int16_t(c - 1);
        if (c != 0) return kCodeGoOnFast;
        w.ticks = int16_t(w.typeDelay);
        uint32_t q = p + 1;
        w.readAt = q;
        if (TextBytes(q).byte(0) >= kGlyphTwoByte) w.readAt = p + 2;
        q = w.readAt + 1;
        w.readAt = q;
        w.readAt = q + TextBytes(q).byte(0) + 2;
        return kCodeGoOnFast | kCodeTyped;
    }
    case RoutineId::TextLogicFrameOpen: {
        const uint32_t f = w.flags;
        if ((f & kWindowFrameShown) == 0) {
            w.flags = f | kWindowFrameShown;
            const uint8_t s = op.byte(2);
            w.setPhase(FramePhase::Start);
            w.frameStyle = s;
        } else if (w.framePhase > uint8_t(FramePhase::Growing)) {
            w.flags = f | kWindowFrameOpen;
            w.readAt = p + 4;
            return kCodeGoOn;
        }
        return kCodeStop;
    }
    case RoutineId::TextLogicGeometry: {
        w.readAt = p + 2;
        w.x = int16_t(op.be16(2));
        w.readAt = p + 4;
        w.y = int16_t(op.be16(4));
        w.readAt = p + 6;
        w.columns = op.byte(6);
        w.readAt = p + 7;
        w.lines = op.byte(7) & 0xf;
        w.rubyRows = int8_t(op.byte(7) >> 4);
        w.readAt = p + 8;
        w.frameTarget.x = int16_t(w.x - kFrameInsetX);
        w.frameTarget.y = int16_t(w.y - kFrameInsetY);
        // the PS1 scripts of a Latin pack size the window in 2 px units (the shop list: PC 0x17 columns = PS1 0x8a)
        w.frameTarget.w = int16_t(w.columns * columnUnit() + kFrameExtra);
        if (gameOptions().ruby == 0) w.frameTarget.h = int16_t(w.lines << 4);
        else w.frameTarget.h = int16_t(w.rubyRows * kRubyRowHeight + w.lines * kLineHeight);
        w.frameTarget.h = int16_t(uint16_t(w.frameTarget.h) + kFrameBorder);
        w.frame.copyWords(w.frameTarget);
        return kCodeGoOn;
    }
    case RoutineId::TextLogicTypeDelay: {
        const uint8_t c = op.byte(2);
        w.readAt = p + 3;
        if (c != 0) {
            w.typeDelay = uint8_t(c - 1u);
            w.ticks = int16_t(uint8_t(c - 1));
            w.flags &= ~kWindowInstant;
            return kCodeGoOn | kCodeRederive;
        }
        w.flags |= kWindowInstant;
        return kCodeGoOn | kCodeRederive;
    }
    case RoutineId::TextLogicChoiceNoCancel: return choiceRun(w, false);
    case RoutineId::TextLogicChoice: return choiceRun(w, true);
    case RoutineId::TextLogicJumpByChoice: {
        const uint8_t n = op.byte(2);
        const uint32_t q = p + 3;
        w.readAt = q;
        const uint8_t t = TextBytes(q).byte(w.choice());
        if (t != kNoMessage && w.bank) {
            jumpTo(w, t);
            return kCodeStop;
        }
        w.readAt = n + q;
        return kCodeStop;
    }
    case RoutineId::TextLogicJumpByStory: {
        uint8_t t;
        if (g.story == op.byte(2)) t = op.byte(4);
        else if (op.byte(2) < g.story) t = op.byte(3);
        else t = op.byte(5);
        if (t == kNoMessage) {
            w.readAt = p + 6;
            return kCodeGoOn;
        }
        jumpTo(w, t);
        return kCodeGoOn;
    }
    case RoutineId::TextLogicTalkCamera: {
        CameraState& cam = cameraState();
        w.readAt = p + 2;
        cam.blendRequest = int8_t(op.byte(2));
        w.readAt = p + 0xd;
        cam.blendToPose.height = int16_t(op.be16(0xb));
        cam.blendToPose.pitch = signedAngle(op.be16(5));
        cam.blendToPose.yaw = signedAngle(op.be16(3));
        cam.blendToPose.roll = signedAngle(op.be16(7));
        cam.blendToPose.distance = int16_t(op.be16(9));
        return kCodeGoOn;
    }
    case RoutineId::TextLogicSound:
        w.readAt = p + 4;
        soundPlayFlat(op.be16(2));
        return kCodeGoOn;
    case RoutineId::TextLogicVoice:
        if (!(w.flags & kWindowOpBusy)) {
            w.flags |= kWindowOpBusy;
            voiceClipPlay(uint32_t(op.byte(3)) << 16 | uint32_t(op.byte(4)) << 8 | op.byte(5));
            w.flags |= kWindowVoiceStarting | kWindowVoice;
            return kCodeStop;
        }
        if (voiceClipPlaying()) {
            w.readAt = p + 6;
            w.flags &= ~(kWindowVoiceStarting | kWindowOpBusy);
            return kCodeGoOn;
        }
        return kCodeStop;
    case RoutineId::TextLogicWaitButtonArrow: {
        const uint32_t f = w.flags;
        if ((f & kWindowWaitButton) == 0) {
            w.flags = f | kWindowMoreArrow | kWindowWaitButton;
            return kCodeStop;
        }
        if (goOnPressed()) {
            w.flags = f & ~(kWindowMoreArrow | kWindowFastModes | kWindowWaitButton);
            w.readAt = p + 2;
        }
        return kCodeStop;
    }
    case RoutineId::TextLogicSetFlag:
        w.readAt = p + 4;
        setStoryFlag(op.be16(2));
        return kCodeGoOn;
    case RoutineId::TextLogicJumpByFlag: {
        const uint8_t t = storyFlag(op.be16(2)) ? op.byte(4) : op.byte(5);
        if (t == kNoMessage) {
            w.readAt = p + 6;
            return kCodeGoOn;
        }
        jumpTo(w, t);
        return kCodeGoOn;
    }
    case RoutineId::TextLogicWaitEvent: {
        const uint32_t bit = 1u << (w.index & 0x1f);
        if ((w.flags & kWindowOpBusy) == 0) {
            w.flags |= kWindowOpBusy;
            m.waitBits |= bit;
        }
        if ((m.waitBits & bit) == 0) {
            w.flags &= ~kWindowOpBusy;
            w.readAt = w.readAt + 2;
        }
        return kCodeStop;
    }
    case RoutineId::TextLogicPlayerShown: {
        const uint8_t c = op.byte(2);
        w.readAt = p + 3;
        pl.setDrawn(c != 0);
        return kCodeGoOn;
    }
    case RoutineId::TextLogicSpeaker:
        w.readAt = p + 2;
        w.speaker = op.byte(2);
        w.readAt = p + 3;
        speakerHandOver(w);
        return kCodeGoOn;
    case RoutineId::TextLogicCloseAfter: {
        startCountdown(w, int16_t(op.be16(2)));
        const uint16_t c = uint16_t(w.ticks);
        w.ticks = int16_t(c - 1u);
        if (c == 0) windowClose(int8_t(w.index), WindowClose::Gentle);
        return kCodeStop;
    }
    case RoutineId::TextLogicWaitWindow: { // win 0xff: the one fb 12 / 13 opened (+0x1e); closed != 0: until it is closed,
                                     // else until it stops typing / has answered
        int8_t n = op.signedByte(2);
        if (n == -1) n = w.openedWindow;
        const uint32_t f = windowRecord(n).flags;
        if (op.byte(3) != 0) {
            if (f == 0) w.readAt = p + 4;
        } else if ((f & kWindowRunning) == 0 || (f & kWindowAnswered)) {
            w.readAt = p + 4;
        }
        return kCodeStop;
    }
    case RoutineId::TextLogicPause: {
        startCountdown(w, int16_t(op.be16(2)));
        const uint16_t c = uint16_t(w.ticks);
        w.ticks = int16_t(c - 1u);
        if (c == 0) {
            w.flags &= ~kWindowOpBusy;
            w.readAt = p + 4;
            w.ticks = int16_t(w.typeDelay);
        }
        return kCodeGoOnFast;
    }
    case RoutineId::TextLogicFrameClose:
        if ((w.flags & kWindowFrameShown) == 0) {
            w.readAt = p + 2;
        } else if (w.framePhase < uint8_t(FramePhase::Closing)) {
            w.setPhase(FramePhase::Closing);
            w.flags &= ~kWindowFrameOpen;
        }
        return kCodeStop;
    case RoutineId::TextLogicScrollPage: {
        startCountdown(w, int16_t(op.be16(2) + int16_t(int8_t(w.logicLine) * kScrollStep)));
        const int16_t c = w.ticks;
        if (int(c) < int(int8_t(w.logicLine)) * kScrollStep) {
            w.scroll = int16_t(uint16_t(w.scroll) - uint16_t(kScrollStep));
            if (c == 0) {
                w.scroll = 0;
                w.flags &= ~kWindowOpBusy;
                w.pageStart = p + 4;
                pageRestart(w);
                return kCodeGoOn;
            }
        }
        w.ticks = int16_t(c - 1);
        return kCodeStop;
    }
    case RoutineId::TextLogicJump:
        w.readAt = p + 3;
        if (w.bank) jumpToMessage(w, int16_t(op.byte(2)));
        return kCodeStop;
    case RoutineId::TextLogicOpenWindow:
        w.readAt = p + 4;
        if (w.bank) w.openedWindow = int8_t(windowOpen(op.signedByte(2), w.bank.value(), int16_t(op.byte(3))));
        return kCodeStop;
    case RoutineId::TextLogicOpenWindowByChoice: {
        const uint8_t n = op.byte(3);
        w.readAt = p + 4;
        const uint8_t t = op.byte(4 + w.choice());
        if (t != kNoMessage && w.bank)
            w.openedWindow = int8_t(windowOpen(op.signedByte(2), w.bank.value(), int16_t(t)));
        w.readAt = w.readAt + n;
        return kCodeStop;
    }
    case RoutineId::TextLogicWaitButton: {
        const uint32_t f = w.flags;
        if ((f & kWindowWaitButton) == 0) {
            w.flags = f | kWindowWaitButton;
            return kCodeStop;
        }
        if (goOnPressed()) {
            w.flags = f & ~(kWindowFastModes | kWindowWaitButton);
            w.readAt = p + 2;
        }
        return kCodeStop;
    }
    case RoutineId::TextLogicWaitVoice:
        if ((w.flags & kWindowVoice) == 0 && !voiceClipPlaying()) w.readAt = p + 2;
        return kCodeStop;
    case RoutineId::TextLogicTypingSoundOn:
        w.flags |= kWindowTypingSound;
        w.readAt = p + 2;
        return kCodeGoOn;
    case RoutineId::TextLogicTypingSoundOff:
        w.flags &= ~kWindowTypingSound;
        w.readAt = p + 2;
        return kCodeGoOn;
    case RoutineId::TextLogicReturn:
        w.readAt = w.readReturn;
        w.readReturn = 0;
        return kCodeGoOn;
    case RoutineId::TextLogicInsertString:
        if (w.readReturn == 0) {
            w.readReturn = p + 4;
            w.readAt = systemString(op.be16(2));
        }
        return kCodeGoOn;
    case RoutineId::TextLogicInsertItemName:
        if (w.readReturn == 0) {
            w.readAt = p + 2;
            w.readReturn = p + 2;
            w.readAt = systemString(w.itemName);
        } else {
            w.readAt = p + 2;
        }
        return kCodeGoOn;
    case RoutineId::TextLogicInsertNumber:
        insertNumber(w, w.readAt, w.readReturn);
        return kCodeGoOn;
    case RoutineId::TextLogicClearFlag:
        w.readAt = p + 4;
        clearStoryFlag(op.be16(2));
        return kCodeGoOn;
    case RoutineId::TextLogicLayoutBits: {
        const uint8_t k = op.byte(2);
        w.readAt = p + 3;
        w.flags &= ~kWindowLayouts;
        if (k == 1) m.openBits |= kLayoutSubScreen;
        else if (k == 2) m.openBits |= kLayoutShopFb25;
        else if (k == 3) m.openBits |= kLayoutSubScreen | kLayoutShopFb25;
        return kCodeGoOn;
    }
    case RoutineId::TextLogicFastForward:
        w.readAt = p + 3;
        if (op.byte(2)) w.flags &= ~kWindowNoFastForward;
        else w.flags |= kWindowNoFastForward;
        return kCodeGoOn;
    case RoutineId::TextLogicJumpByDifficulty: {
        const uint8_t d = g.difficulty;
        if (d <= kHardestDifficulty) {
            const uint8_t t = op.byte(2 + d);
            if (t != kNoMessage) {
                jumpTo(w, t);
                return kCodeStop;
            }
        }
        w.readAt = p + 7;
        return kCodeStop;
    }
    case RoutineId::TextLogicLayer:
        w.layer = op.byte(2);
        w.readAt = p + 3;
        return kCodeGoOn;
    case RoutineId::TextLogicHitPoints:
        hitPointsChange(pl, int16_t(op.be16(2)));
        w.readAt = p + 4;
        return kCodeGoOn;
    case RoutineId::TextLogicWeaponEnergy:
        weaponEnergyAdd(pl, int16_t(op.be16(2)));
        w.readAt = p + 4;
        return kCodeGoOn;
    case RoutineId::TextLogicAddCounter: { // k 1 / 2: the script counters, else the zenny
        const uint8_t k = op.byte(2);
        const int32_t v = op.be32(3);
        w.readAt = p + 7;
        if (k == 1) g.scriptCounters[0] += uint16_t(v);
        else if (k == 2) g.scriptCounters[1] += uint16_t(v);
        else zennyAdd(v);
        return kCodeGoOn;
    }
    case RoutineId::TextLogicWarp: return warpCode(w);
    case RoutineId::TextLogicBalance:
        balanceAdd(int16_t(op.be16(2)));
        w.readAt = p + 4;
        return kCodeGoOn;
    case RoutineId::TextLogicToolKitBalance:
        toolKitBalanceAdd(int16_t(op.be16(2)));
        w.readAt = p + 4;
        return kCodeGoOn;
    case RoutineId::TextLogicJumpByBalanceTier:
    case RoutineId::TextLogicJumpByToolKitTier: {
        const uint8_t tier =
            uint8_t(h.is(RoutineId::TextLogicJumpByBalanceTier) ? g.balanceTier : g.toolKitTier);
        if (tier <= kLastTier) {
            const uint8_t t = op.byte(2 + tier);
            if (t != kNoMessage) {
                jumpTo(w, t);
                return kCodeGoOn;
            }
        }
        w.readAt = p + 5;
        return kCodeGoOn;
    }
    case RoutineId::TextLogicJumpByCounter: { // k 1 / 2: the script counters, else the zenny; ge lt
        const uint8_t k = op.byte(2);
        int32_t a;
        if (k == 1) a = int32_t(g.scriptCounters[0]);
        else if (k == 2) a = int32_t(g.scriptCounters[1]);
        else a = g.zenny;
        const int32_t v = op.be32(3);
        const uint8_t t = a < v ? op.byte(8) : op.byte(7);
        if (t == kNoMessage) {
            w.readAt = p + 9;
            return kCodeGoOn;
        }
        jumpTo(w, t);
        return kCodeGoOn;
    }
    case RoutineId::TextLogicJumpByDockCounter: { // i v ge lt (i past the counters: 0)
        uint8_t i = op.byte(2);
        if (i >= kDockCounterCount) i = 0;
        const uint8_t t = g.dockCounters[i] < op.byte(3) ? op.byte(5) : op.byte(4);
        if (t == kNoMessage) {
            w.readAt = p + 6;
            return kCodeGoOn;
        }
        jumpTo(w, t);
        return kCodeGoOn;
    }
    case RoutineId::TextLogicInsertKeyName: // only when not already inside an inserted text
        if (w.readReturn == 0) insertKeyName(w.readAt, w.readReturn);
        return kCodeGoOn;
    case RoutineId::TextLogicInsertTime:
        insertTime(w, w.readAt, w.readReturn);
        return kCodeGoOn;
    case RoutineId::TextLogicInsertPlace:
        insertPlace(w, w.readAt, w.readReturn);
        return kCodeGoOn;
    case RoutineId::TextLogicOpenWindowOnChoice: { // c win id: on the answer c, another window opened on message id
        const uint8_t c = op.byte(2), n = op.byte(3), id = op.byte(4);
        w.readAt = p + 5;
        if (w.choice() == c && w.bank) w.openedWindow = int8_t(windowOpen(int8_t(n), w.bank.value(), int16_t(id)));
        return kCodeStop;
    }
    case RoutineId::TextLogicHold: return kCodeGoOn;
    case RoutineId::TextLogicPlayerImmune:
        w.readAt = p + 3;
        pl.immunity = op.byte(2) != 0 ? kImmuneHeld : int8_t(0);
        return kCodeGoOn;
    case RoutineId::TextLogicWaitOrHelp: { // id: circle / start (or cross held) goes on; triangle opens the highest free
                                     // window on message id and goes on once that window is done
        const uint32_t f = w.flags;
        if ((f & kWindowWaitButton) == 0) {
            w.flags = f | kWindowWaitButton;
            return kCodeStop;
        }
        if ((f & kWindowOpBusy) == 0) {
            if (goOnPressed()) {
                w.flags = f & ~kWindowWaitButton;
                w.readAt = p + 3;
            } else if (pad0().pressed & kPadTriangle) {
                w.flags = f | kWindowOpBusy;
                if (w.bank) w.openedWindow = int8_t(windowOpen(-1, w.bank.value(), int16_t(op.byte(2))));
            }
            return kCodeStop;
        }
        if (windowStatus(uint8_t(w.openedWindow)) == 0) {
            w.flags = f & ~(kWindowOpBusy | kWindowWaitButton);
            w.readAt = p + 3;
        }
        return kCodeStop;
    }
    case RoutineId::TextLogicAskInWindow: { // id n targets: the highest free window opened on message id; once it is done,
                                      // to the message of its answer (0xff: past the n targets)
        const uint32_t f = w.flags;
        if ((f & kWindowOpBusy) == 0) {
            w.flags = f | kWindowOpBusy;
            if (w.bank) w.openedWindow = int8_t(windowOpen(-1, w.bank.value(), int16_t(op.byte(2))));
            return kCodeStop;
        }
        if (windowStatus(uint8_t(w.openedWindow)) != 0) return kCodeStop;
        const uint32_t g2 = f & ~kWindowOpBusy;
        w.flags = g2;
        const uint8_t n = op.byte(3);
        w.readAt = p + 4;
        const uint32_t sh = uint32_t(int32_t(w.openedWindow) * 4) & 0x1f;
        const uint8_t t = op.byte(4 + ((((0xfu << sh) & m.openBits) >> sh) & 0xff));
        if (t != kNoMessage && w.bank) {
            w.flags = g2 & ~kWindowFrameShown;
            jumpToMessage(w, int16_t(t));
            return kCodeStop;
        }
        w.readAt = p + 4 + n;
        return kCodeStop;
    }
    case RoutineId::TextLogicSmallLayout:
        w.flags |= kWindowSmallLayout;
        w.readAt = p + 2;
        return kCodeGoOn;
    case RoutineId::TextLogicSetFlagByChoice: { // n flags: the answer's flag set (kNoFlag: none, past the n entries); the
                                          // read pointer stays after n when a flag is set
        const uint32_t sel = (w.choice() * 2u) & 0xff;
        const uint8_t n = op.byte(2);
        w.readAt = p + 3;
        const uint16_t fl = op.be16(3 + sel);
        if (fl != kNoFlag) setStoryFlag(uint32_t(int32_t(int16_t(fl))));
        else w.readAt = p + 3 + uint32_t(n) * 2;
        return kCodeGoOn;
    }
    case RoutineId::TextLogicLoadRecordTime: { // k: the record k's time (k < 0: +0x6f; k past the records: -1)
        int8_t k = op.signedByte(2);
        w.readAt = p + 3;
        if (k < 0) k = w.record;
        if (k < kBestTimeCount) {
            w.record = k;
            w.timeValue = bestTimes().records[size_t(int32_t(k))].time;
        } else {
            w.timeValue = -1;
        }
        return kCodeGoOn;
    }
    case RoutineId::TextLogicNameRecord: { // k (k < 0: +0x6f): right / circle and left / cross move between the letters,
                                     // up / down turn the letter; circle / start past the third letter ends (the
                                     // naming bit cleared, the page restarts after the code)
        int8_t k = op.signedByte(2);
        if (k < 0) k = w.record;
        const uint32_t f = w.flags;
        if ((f & kWindowNaming) == 0) {
            w.letter = 0;
            w.flags = f | kWindowNaming;
            w.letters[0] = 0;
            w.letters[1] = 0;
            w.letters[2] = 0;
            return kCodeStop;
        }
        if (k >= kBestTimeCount) return kCodeStop;
        auto&& rec = bestTimes().records[size_t(int32_t(k))];
        const uint16_t pressed = pad0().pressed;
        if (pressed & (kPadConfirm | kPadRight)) w.letter = int8_t(w.letter + 1);
        else if (pressed & (kPadCross | kPadLeft)) w.letter = int8_t(w.letter - 1);
        const bool done = w.letter == kNameLetters && (pad0().pressed & kPadConfirm);
        if (w.letter < 0) w.letter = 0;
        else if (w.letter > kNameLetters - 1) w.letter = int8_t(kNameLetters - 1);
        auto&& glyph = w.letters[size_t(int32_t(w.letter))];
        int8_t c = glyph;
        if (uint8_t(pad0().repeated) & kPadUp) --c;
        else if (uint8_t(pad0().repeated) & kPadDown) ++c;
        if (c < 0) c = kNameGlyphCount - 1;
        else if (c >= kNameGlyphCount) c = 0;
        glyph = c;
        rec.name[size_t(int32_t(w.letter))] = nameGlyphs[size_t(int32_t(c))];
        if (!done) return kCodeStop;
        w.flags &= ~kWindowNaming;
        w.pageStart = p + 3;
        return kCodeGoOn;
    }
    case RoutineId::TextLogicClearRecordName: { // k (k < 0: +0x6f): the name blanked, the letter -1
        int8_t k = op.signedByte(2);
        w.readAt = p + 3;
        if (k < 0) k = w.record;
        if (k < kBestTimeCount) {
            auto&& rec = bestTimes().records[size_t(int32_t(k))];
            rec.name[0] = kNameBlank;
            rec.name[1] = kNameBlank;
            rec.name[2] = kNameBlank;
            rec.name[3] = kBestTimeNameEnd;
            w.letter = -1;
        }
        return kCodeGoOn;
    }
    }
    codeNotPorted("logic", index, h.pointerValue());
}

void runLayoutCode(MessageWindow& w, uint32_t index) {
    const Ps1TextScope ps1(w);
    const RoutineRef& h = layoutHandlers[index];
    gSeen.insert(h.pointerValue());
    const uint32_t p = w.layoutAt;
    const TextBytes op(p);
    if (const uint32_t n = skipLength(h.id())) {
        w.layoutAt = p + n;
        return;
    }
    switch (h.id()) {
    case RoutineId::TextLayoutNewLine: {
        const uint8_t c = w.layoutLine;
        w.layoutLine = uint8_t(c + 1u);
        if (w.flags & kWindowDoubleLine) w.layoutLine = uint8_t(c + 2u);
        w.penX = w.x;
        w.penY = int16_t(messageLineY(w, int8_t(w.layoutLine)) + w.y);
        if (w.flags & kWindowDoubleLine) w.flags &= ~kWindowDoubleLine;
        w.layoutAt = w.layoutAt + 1;
        return;
    }
    case RoutineId::TextLayoutRubyGlyph: { // the ruby laid out only with the ruby option on (and not a system-only window)
        w.layoutAt = p + 1;
        w.penX = int16_t(uint16_t(w.penX) + glyphAdvance(w));
        uint32_t q = w.layoutAt;
        const uint16_t x = uint16_t(w.penX);
        const uint8_t n = TextBytes(q).byte(0), k = TextBytes(q).byte(1);
        q += 2;
        w.layoutAt = q;
        const uint32_t f = w.flags;
        if (((f & kWindowAltLayout) || (f & kWindowSystemLayout) == 0) && gameOptions().ruby != 0) {
            const uint16_t y = uint16_t(w.penY);
            w.flags = f | kWindowRuby;
            w.penX = int16_t(int8_t(k) - kRubyOffsetX + int16_t(x));
            w.penY = int16_t(y - kRubyRaise);
            for (uint32_t i = 0; i < n; ++i) w.penX = int16_t(uint16_t(w.penX) + glyphAdvance(w));
            w.penX = int16_t(x);
            w.penY = int16_t(y);
            w.flags &= ~kWindowRuby;
            return;
        }
        w.layoutAt = q + n;
        return;
    }
    case RoutineId::TextLayoutChoiceOption: { // i: the selected option bright (the confirmed one after the choice), the others
                                        // dim (the small layout: colour 6)
        const uint32_t f = w.flags;
        w.penX = int16_t(uint16_t(w.penX) + kGlyphWidth);
        w.layoutAt = p + 2;
        const uint8_t cur = uint8_t((f >> kWindowChoiceShift) & 0xf);
        bool selected;
        if ((f & kWindowChoice) == 0) selected = (f & kWindowAnswerShown) && op.byte(2) == cur;
        else selected = op.byte(2) == cur;
        w.layoutAt = p + 3;
        if (selected) {
            w.shade = kShadeNormal;
            w.colour = w.baseColour;
        } else if ((f & kWindowSmallLayout) == 0) {
            w.shade = kShadeDim;
            w.colour = w.baseColour;
        } else {
            w.colour = kOptionColourSmall;
            w.shade = kShadeNormal;
        }
        return;
    }
    case RoutineId::TextLayoutSkipOptions:
        w.shade = kShadeNormal;
        w.layoutAt = p + 3 + uint32_t(op.byte(2)) * 2;
        return;
    case RoutineId::TextLayoutSkipJumps: w.layoutAt = p + 2 + op.byte(2) + 1; return;
    case RoutineId::TextLayoutColour: { // c & 0x7f (past the colours: 0; the previous one kept), c & kColourDim: dim
        w.savedColour = w.colour;
        const uint8_t c = op.byte(2);
        w.layoutAt = p + 3;
        w.colour = (c & kColourMask) >= kColourCount ? 0u : uint8_t(c & kColourMask);
        w.shade = (c & kColourDim) ? kShadeDim : kShadeNormal;
        return;
    }
    case RoutineId::TextLayoutColourBack: {
        w.layoutAt = p + 2;
        const uint8_t c = w.savedColour;
        w.colour = c & kColourMask;
        w.shade = (c & kColourDim) ? kShadeDim : kShadeNormal;
        return;
    }
    case RoutineId::TextLayoutSkipWindows: w.layoutAt = p + 3 + 1 + op.byte(3); return;
    case RoutineId::TextLayoutReturn:
        w.layoutAt = w.layoutReturn;
        w.layoutReturn = 0;
        return;
    case RoutineId::TextLayoutInsertString:
        w.layoutReturn = p + 4;
        w.layoutAt = systemString(op.be16(2));
        return;
    case RoutineId::TextLayoutInsertItemName:
        w.layoutReturn = p + 2;
        w.layoutAt = systemString(w.itemName);
        return;
    case RoutineId::TextLayoutInsertNumber: insertNumber(w, w.layoutAt, w.layoutReturn); return;
    case RoutineId::TextLayoutPenShift:
        w.penX = int16_t(w.penX + op.signedByte(2));
        w.layoutAt = p + 3;
        return;
    case RoutineId::TextLayoutInsertKeyName: insertKeyName(w.layoutAt, w.layoutReturn); return;
    case RoutineId::TextLayoutInsertTime: insertTime(w, w.layoutAt, w.layoutReturn); return;
    case RoutineId::TextLayoutInsertPlace: insertPlace(w, w.layoutAt, w.layoutReturn); return;
    case RoutineId::TextLayoutLoadSaveSlot: { // slot (0xff: +0x6f): +0x68 the slot + 1, +0x6e its place, +0x64 its time
        int8_t k = op.signedByte(2);
        w.layoutAt = p + 3;
        if (k < 0) k = w.record;
        if (k < kSaveSlotCount) {
            const SaveSlotSummary& slot = saveSlotSummaries().slots[size_t(k)];
            w.number = int32_t(k + 1);
            w.place = slot.place;
            w.timeValue = slot.time;
        } else {
            w.number = -1;
            w.timeValue = -2;
            w.place = 0;
        }
        return;
    }
    default: break;
    }
    codeNotPorted("layout", index, h.pointerValue());
}

uint32_t messageOpLength(const TextBytes& text) {
    const RoutineId h = layoutHandlers[textCodeIndex(text)].id();
    if (const uint32_t n = skipLength(h)) return n;
    switch (h) {
    case RoutineId::TextLayoutNewLine: return 1;
    case RoutineId::TextLayoutChoiceOption: return 3;
    case RoutineId::TextLayoutSkipOptions: return 3 + uint32_t(text.byte(2)) * 2;
    case RoutineId::TextLayoutSkipJumps: return 3 + text.byte(2);
    case RoutineId::TextLayoutColour: return 3;
    case RoutineId::TextLayoutColourBack: return 2;
    case RoutineId::TextLayoutSkipWindows: return 4 + uint32_t(text.byte(3));
    case RoutineId::TextLayoutReturn: return 2;
    case RoutineId::TextLayoutInsertString: return 4;
    case RoutineId::TextLayoutInsertNumber: return 3;
    case RoutineId::TextLayoutPenShift: return 3;
    case RoutineId::TextLayoutInsertKeyName: return 3;
    case RoutineId::TextLayoutInsertTime: return 3;
    case RoutineId::TextLayoutInsertPlace: return 3;
    case RoutineId::TextLayoutLoadSaveSlot: return 3;
    default: return 0; // unknown length
    }
}

std::vector<uint32_t> messageHandlersSeen(bool reset) {
    std::vector<uint32_t> v(gSeen.begin(), gSeen.end());
    if (reset) gSeen.clear();
    return v;
}
void setMessageFadeHook(std::function<void(uint8_t)> hook) { gFadeHook = std::move(hook); }

} // namespace od::port
