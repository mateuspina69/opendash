// A message window's choice (fb 10 / fb 39 n {column, line} x n): the hand shown on the option chosen last, moved
// with the d-pad (or the mouse: game/choice_pointer) among the options and gliding to it, confirmed with circle /
// start or cancelled with cross; the answer goes to the window's flags and its nibble of MessageSystem::openBits.
#pragma once

#include <cstdint>

#include "game/message_state.hpp"

namespace od::port {

// exe 0x559f10(window, cancel): the logic pass's choice code: the first tick shows the hand (the pad's repeat 0xf / 3),
// then circle / start confirms the option under it (cross picks the "none" answer, the code's n, when `cancel`); the
// read pointer past the option table once answered. Returns the kCode* bits (always kCodeStop)
uint32_t choiceRun(MessageWindow& w, bool cancel);
// exe 0x559160 (the state part; the "more" arrow and the hand's sprite are drawn by the host): the layout pass's hand
// -- a move to another option (its sound), the glide toward it
void choiceHandUpdate(MessageWindow& w);

} // namespace od::port
