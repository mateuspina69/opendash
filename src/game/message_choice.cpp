// A message window's choice (see message_choice.hpp), transcribed from dash2.exe one function per exe function.
#include "game/message_choice.hpp"

#include "game/audio_logic.hpp"
#include "game/choice_pointer.hpp"
#include "game/language_pack.hpp"
#include "game/player/player_input.hpp"
#include "game/text_script.hpp"

namespace od::port {

namespace {

// the choice code's bytes: fb nn n {column, line} x n (n & 0x7f options; n is also the cancel's answer)
constexpr uint32_t kChoiceCount = 2;  // the byte of n
constexpr uint32_t kChoiceTable = 3;  // the first option's column (its line follows)
constexpr uint8_t kChoiceCountMask = 0x7f;
constexpr uint8_t kChoiceManyOptions = 5; // 2n at or above it: the hand moves on the repeat bits (else the presses)
constexpr int kSmallHandX = 8;        // the small layout's hand: x * 12 - 8, the line's y + 7
constexpr int kSmallHandY = 7;
constexpr int kSmallColumn = 0xc;
constexpr int kHandSnap = 4;          // a glide shorter than this goes a pixel a tick
constexpr uint8_t kChoiceRepeatDelay = 0xf; // the pad's repeat while a choice is up
constexpr uint8_t kChoiceRepeatRate = 3;

// the hand's place on the option at `column` / optionLine: its x into handTargetX, its y returned
int handTarget(MessageWindow& w, uint8_t column) {
    if ((w.flags & kWindowSmallLayout) == 0) {
        w.handTargetX = int16_t(column * columnUnit());
        return messageLineY(w, w.optionLine);
    }
    w.handTargetX = int16_t(column * kSmallColumn - kSmallHandX);
    return messageLineY(w, w.optionLine) + kSmallHandY;
}

// exe 0x5594a0: the option the hand moves to (the pressed d-pad, or the repeat bits for more than 2 options): up / down
// to the nearest option of the same column, left / right to the nearest of the same line
int choiceMove(MessageWindow& w) {
    const TextBytes op = w.readText();
    const uint8_t n = op.byte(kChoiceCount);
    const uint8_t n2 = uint8_t(n * 2);
    uint8_t sel = uint8_t(w.choice() << 1);
    uint32_t pad = n2 < kChoiceManyOptions ? uint32_t(pad0().pressed) : uint32_t(pad0().repeated);
    ChoicePointer& mouse = choicePointer();
    if (mouse.window == &w && mouse.option >= 0) { // OpenDash: the mouse's option, no d-pad move
        if (mouse.option < (n & kChoiceCountMask)) sel = uint8_t(mouse.option * 2);
        mouse.option = -1;
        pad = 0;
    }
    uint8_t best = 0xff;
    if ((pad & kPadUp) == 0) {
        if ((pad & kPadDown) && (n & kChoiceCountMask) != 0)
            for (uint8_t k = 0; k < n2; k += 2) {
                const uint8_t d = uint8_t(op.byte(kChoiceTable + 1 + k) - w.optionLine);
                if (w.optionX == op.byte(kChoiceTable + k) && d != 0 && d <= best) {
                    best = d;
                    sel = k;
                }
            }
    } else if ((n & kChoiceCountMask) != 0) {
        for (uint8_t k = 0; k < n2; k += 2) {
            const uint8_t d = uint8_t(w.optionLine - op.byte(kChoiceTable + 1 + k));
            if (w.optionX == op.byte(kChoiceTable + k) && d != 0 && d <= best) {
                best = d;
                sel = k;
            }
        }
    }
    best = 0xff;
    w.optionX = op.byte(uint32_t(int8_t(sel)) + kChoiceTable);
    const uint8_t line = op.byte(uint32_t(int8_t(sel)) + kChoiceTable + 1);
    w.optionLine = line;
    if ((pad & kPadLeft) == 0) {
        if ((pad & kPadRight) && n2 != 0)
            for (uint8_t k = 0; k < n2; k += 2) {
                const uint8_t d = uint8_t(op.byte(kChoiceTable + k) - w.optionX);
                if (line == op.byte(kChoiceTable + 1 + k) && d != 0 && d <= best) {
                    best = d;
                    sel = k;
                }
            }
    } else if (n2 != 0) {
        for (uint8_t k = 0; k < n2; k += 2) {
            const uint8_t d = uint8_t(w.optionX - op.byte(kChoiceTable + k));
            if (line == op.byte(kChoiceTable + 1 + k) && d != 0 && d <= best) {
                best = d;
                sel = k;
            }
        }
    }
    if (int(n2) <= int(int8_t(sel))) sel = uint8_t(n2 - 2);
    if (int8_t(sel) < 0) sel = 0;
    return int8_t(sel) >> 1;
}

// the hand's glide step toward its target on one axis: a pixel a tick when closer than kHandSnap, else half the way
int8_t glideStep(int16_t d) {
    const int ad = d < 0 ? -d : d;
    return int8_t(uint8_t(ad < kHandSnap ? (d >= 0 ? 1u : 0xffu) : uint32_t(uint16_t(d) >> 1)));
}

} // namespace

uint32_t choiceRun(MessageWindow& w, bool cancel) {
    const uint32_t f = w.flags;
    if ((f & kWindowChoice) == 0) {
        const uint8_t sel = uint8_t((f >> kWindowChoiceShift) & 0xf);
        w.flags = uint32_t(sel) << kWindowChoiceShift | (f & ~(kWindowChoiceMask | kWindowAnswerShown)) | kWindowChoice;
        const TextBytes option(uint32_t(uint8_t(sel << 1)) + w.readAt);
        const uint8_t x = option.byte(kChoiceTable);
        w.optionX = x;
        w.optionLine = option.byte(kChoiceTable + 1);
        const int16_t y = int16_t(handTarget(w, x));
        w.handX = w.handTargetX;
        w.handTargetY = y;
        w.handY = y;
        w.handStepY = 0;
        w.handStepX = 0;
        messageSystem().openBits &= ~kAnswerPending;
        w.savedRepeat = uint32_t(pad0().repeatDelay | pad0().repeatRate << 8);
        padSetRepeat(kChoiceRepeatDelay, kChoiceRepeatRate);
        return kCodeStop;
    }
    const bool silent = (f & kWindowTypingSound) == 0 && (f & kWindowSmallLayout) != 0;
    if ((pad0().pressed & kPadConfirm) == 0) {
        if (!cancel || (pad0().pressed & kPadCross) == 0) return kCodeStop;
        w.flags = uint32_t(w.readText().byte(kChoiceCount) & 0xf) << kWindowChoiceShift | (f & ~kWindowChoiceMask);
        if (!silent) soundPlayFlat(UiSound::Cancel);
    } else if (!silent) {
        soundPlayFlat(UiSound::Confirm);
    }
    w.colour = w.savedColour;
    w.shade = kShadeNormal;
    w.flags = (w.flags & ~kWindowChoice) | kWindowAnswered | kWindowAnswerShown;
    const uint32_t p = w.readAt;
    w.readAt = p + kChoiceTable + uint32_t(TextBytes(p).byte(kChoiceCount)) * 2;
    const uint32_t sh = uint32_t(w.index & 7) << 2;
    MessageSystem& m = messageSystem();
    m.openBits = ((m.openBits | kAnswerPending) & ~(0xfu << sh)) | ((w.flags >> kWindowChoiceShift) & 0xf) << sh;
    padSetRepeat(uint8_t(w.savedRepeat), uint8_t(w.savedRepeat >> 8));
    return kCodeStop;
}

void choiceHandUpdate(MessageWindow& w) {
    if (w.flags & kWindowMoreArrow) return;
    const uint32_t f = w.flags;
    if ((f & kWindowChoice) == 0) return;
    const int sel = choiceMove(w);
    if (((f >> kWindowChoiceShift) & 0xf) != uint32_t(uint8_t(sel))) {
        w.flags = uint32_t(sel & 0xf) << kWindowChoiceShift | (w.flags & ~kWindowChoiceMask);
        const TextBytes option(w.readAt + uint32_t(int8_t(uint8_t(sel << 1))));
        const uint8_t x = option.byte(kChoiceTable);
        w.optionX = x;
        w.optionLine = option.byte(kChoiceTable + 1);
        const int y = handTarget(w, x);
        w.handTargetY = int16_t(y);
        w.handStepX = glideStep(int16_t(w.handTargetX - w.handX));
        w.handStepY = glideStep(int16_t(w.handTargetY - w.handY));
        soundPlayFlat(UiSound::Cursor);
    }
    const int8_t vx = w.handStepX, vy = w.handStepY;
    w.handX = int16_t(w.handX + vx);
    w.handY = int16_t(w.handY + vy);
    if ((vx < 0 && w.handX < w.handTargetX) || (vx > 0 && w.handTargetX < w.handX)) {
        w.handStepX = 0;
        w.handX = w.handTargetX;
    }
    if ((vy < 0 && w.handY < w.handTargetY) || (vy > 0 && w.handTargetY < w.handY)) {
        w.handStepY = 0;
        w.handY = w.handTargetY;
    }
}

} // namespace od::port
