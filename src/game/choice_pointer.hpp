// OpenDash: the mouse on the message windows' choices (fb 39 / fb 10: game/message_choice). The host hit-tests the
// options (choiceOptionAt) and points one (choicePointer); the choice's next move (exe 0x5594a0) takes it as if the
// d-pad had moved the hand there (its sound, its glide). The click is the host's circle press.
#pragma once

#include <cstdint>

#include "game/message_state.hpp"

namespace od::port {

struct ChoicePointer {
    const MessageWindow* window = nullptr; // the window whose option is pointed
    int option = -1;     // its option index, -1 = none waiting
};
ChoicePointer& choicePointer();

// the window with a choice up (its frame shown, kWindowChoice, not kWindowMoreArrow), nullptr = none
const MessageWindow* choiceWindow();
// the option of window w's choice under screen pixel (px, py) of the 320 x 240 frame, -1 = none: an option spans its
// line (16 px from its cell top) from the hand's place to the next option on the line (else the frame's right side)
int choiceOptionAt(const MessageWindow& w, float px, float py);

} // namespace od::port
