// The message windows of dash2.exe: the five windows (game/message_state) opened on a message of a bank, typed by the
// logic pass and laid out by the layout pass every tick (exe 0x5578a0: 0x557c90, then the state part of 0x557eb0),
// closed, their voices, the layout the next windows open in. The text's codes are game/text_script's, the frame's
// animation and look game/window_frame's, the event that opens a talk's window game/event's. Bit-exact against
// tests/data/msg_ST*.bin.
//
// The draw side of exe 0x557eb0 (the glyph primitives, the cursor sprite) is the host's (app/ui/message_ui): the port
// keeps the state it leaves in the window (the pen, the layout read pointer, the choice's hand, the colours, the close
// at the end of the text), which the logic reads back.
#pragma once

#include <cstdint>

#include "engine/data_source.hpp"
#include "game/message_state.hpp"

namespace od::port {

// exe 0x557320: the windows' indices, all closed, the open / wait bits cleared (the draw-list setup 0x4016c0 is the
// host's)
void messageInit();
// exe 0x5578a0: unless the frame draws nothing, the logic pass then the layout pass (OpenDash's drawn copies cleared
// first)
void messageTick();
// exe 0x557c90: per window: circle / start (or cross held) speeds the text up; while the text runs, the glyphs and
// codes of the tick; the frame's animation; the typing sound
void windowLogic();
// exe 0x557eb0 (state only): per laid-out window: the choice's hand, the pen walked over the text typed so far; the end
// of the text closes the window
void windowLayout();
// exe 0x557390: window `win` (< 0: the highest free one) opened on message `id` of `bank`; returns the window or -1
int windowOpen(int8_t win, uint32_t bank, int16_t id);
// exe 0x557810: the window's status bits
constexpr uint8_t kWindowStatusOpen = 0x1;    // open / typing (+0 & kWindowOpen)
constexpr uint8_t kWindowStatusFrame = 0x2;   // its frame is shown
constexpr uint8_t kWindowStatusWaiting = 0x4; // it waits for the event runner (fb 2b)
uint8_t windowStatus(uint8_t win);
// How a window closes (exe 0x5576b0's force argument)
enum class WindowClose : int {
    Gentle = 0, // a window whose frame is shown closes its frame first (kWindowCloseKeeps kept, the rest cleared)
    Force = 1,  // the window's flags cleared at once
};
// exe 0x5576b0: window `win` closed (its voice stopped, its open / wait bits cleared)
void windowClose(int8_t win, WindowClose how);
// exe 0x5577d0(how): the default layout, the pad's repeat back to 0xf / 1, every window closed
void windowCloseAll(WindowClose how);
// exe 0x557b80: the voice of every window stopped
void windowVoicesStop();
// The layout the next windows open in (exe 0x557860's argument: the kLayout* bits of MessageSystem::openBits)
enum class WindowLayout : int8_t {
    Dialogue = 0,      // the default: the talk windows (typed, at (0x20, 0xb0))
    SubScreen = 1,     // the status sub-screen's: shown at once, 16-px lines from +2, no ruby
    Shop = 2,          // the shop's: shown at once, at (0x18, 0xac)
    SubScreenShop = 3, // both (the dev room's): at (0x1c, 0xb4)
};
// exe 0x557860(layout): the layout bits set for the next windows
void windowLayoutVariant(WindowLayout layout);

// the choice hand's bob (exe 0x8b1f3c), 16 pixel offsets by (frame >> 1) & 15
extern const DataTable<ScalarTable<int8_t, 16>> messageHandBob;

} // namespace od::port
