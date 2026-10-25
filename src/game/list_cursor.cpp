// The scrolling list's cursor (see list_cursor.hpp), transcribed from dash2.exe.
#include "game/list_cursor.hpp"

#include "game/audio_logic.hpp"
#include "game/menu_pointer.hpp"
#include "game/player/player_input.hpp"

namespace od::port {

namespace {
constexpr int kFastScroll = 2;       // a scroll animation above this goes 3 steps a tick
constexpr int8_t kFastScrollStep = 3;
} // namespace

ListAnswer listCursorStep(ListCursor& list) {
    const int8_t c8 = list.rowStep;
    if (c8 != 0) {
        list.rowStep = int8_t(c8 > 0 ? c8 - 1 : c8 + 1);
        return ListAnswer::None;
    }
    const int8_t c9 = list.scrollStep;
    if (c9 != 0) {
        const int a = c9 < 0 ? -c9 : c9;
        if (a > kFastScroll) {
            if (c9 > 0) {
                list.scrollStep = int8_t(c9 - kFastScrollStep);
                list.firstRow = uint8_t(list.firstRow + 1u);
            } else {
                list.scrollStep = int8_t(c9 + kFastScrollStep);
                list.firstRow = uint8_t(list.firstRow - 1u);
            }
            return ListAnswer::None;
        }
        if (c9 < 1) {
            list.scrollStep = int8_t(c9 + 1);
            if (int8_t(c9 + 1) == 0) list.firstRow = uint8_t(list.firstRow - 1u);
        } else {
            list.scrollStep = int8_t(c9 - 1);
            if (int8_t(c9 - 1) == 0) list.firstRow = uint8_t(list.firstRow + 1u);
        }
        return ListAnswer::None;
    }
    if (pad0().pressed & kPadCross) {
        soundPlayFlat(UiSound::Cancel);
        return ListAnswer::Cancelled;
    }
    const uint8_t b1 = list.rows;
    if (b1 == 0) {
        list.row = kListNoRow;
        return ListAnswer::None;
    }
    uint8_t b2 = list.firstRow;
    const uint8_t b3 = list.visibleRows;
    if (uint32_t(b1) < uint32_t(b2) + b3) {
        if (b2 == 0) {
            if (b1 <= list.row) list.row = uint8_t(b1 - 1u);
        } else {
            list.firstRow = uint8_t(b2 - 1u);
        }
    }
    b2 = list.firstRow;
    const uint16_t u5 = list.row;
    uint16_t u7 = b2, u6 = u5;
    const uint16_t held = pad0().held, rep = pad0().repeated;
    if (!(held & kPadR1) || !(held & kPadL1)) { // R1 / L1 alone: a page down / up
        u7 = b2;
        if (rep & kPadR1) {
            if (int32_t(int16_t(u5)) == int32_t(b3) - 1) u7 = uint16_t((b2 - 1) + b3);
            else u6 = uint16_t(b3 - 1);
        }
        if (rep & kPadL1) {
            if (u6 == 0) u7 = uint16_t(u7 + (1 - b3));
            else u6 = 0;
        }
    }
    if (rep & kPadUp) u6 = uint16_t(u6 - 1);
    if (rep & kPadDown) u6 = uint16_t(u6 + 1);
    MenuPointer& pt = menuPointer();
    if (pt.moved && pt.row >= kListCell) { // OpenDash: the mouse's row (game/menu_pointer MenuPointer)
        u6 = uint16_t((pt.row - kListCell) >> 1);
        u7 = b2;
        pt.moved = false;
    }
    if (int16_t(u7) < 0) {
        u6 = 0;
        u7 = 0;
    } else if (int16_t(u6) < 0) {
        if (int32_t(int16_t(u7)) + int16_t(u6) < 0) u7 = 0;
        else u7 = uint16_t(u7 - 1);
        u6 = 0;
    }
    if (b3 < b1) {
        if (int32_t(int16_t(u7)) + int16_t(u6) < int32_t(b1)) {
            if (int16_t(b3) <= int16_t(u6)) {
                u6 = uint16_t(b3 - 1);
                u7 = uint16_t(u7 + 1);
            }
        } else {
            u6 = uint16_t(b3 - 1);
            u7 = uint16_t((b1 - u6) - 1);
        }
    } else {
        u7 = 0;
        if (int16_t(b1) <= int16_t(u6)) u6 = uint16_t(b1 - 1);
    }
    if (u6 != u5) {
        list.rowStep = int8_t(uint8_t(uint8_t(u6) - list.row));
        list.row = uint8_t(u6);
        soundPlayFlat(UiSound::Cursor);
        return ListAnswer::None;
    }
    if (u7 == b2) {
        const bool clicked = pt.click && pt.row == kListCell + int(u5) * 2; // OpenDash: a click on the hand's row
        pt.click = false;
        if ((pad0().pressed & kPadConfirm) || clicked) {
            soundPlayFlat(UiSound::Confirm);
            return ListAnswer::Confirmed;
        }
    } else {
        int8_t c = int8_t(uint8_t(u7) - b2);
        const int a = c < 0 ? -c : c;
        c = int8_t(a < 2 ? c * 2 : c * 3);
        list.scrollStep = c;
        soundPlayFlat(UiSound::Cursor);
    }
    return ListAnswer::None;
}

} // namespace od::port
