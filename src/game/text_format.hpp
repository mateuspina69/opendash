// The number and time formatters of the message system (exe 0x557be0, 0x55a890): the glyphs fb 21 / fb 23 insert and
// the shop list's prices, in the PC font's encoding or, under a Latin pack (game/language_pack), the PS1 font's; and
// the same texts as byte strings for OpenDash's renderer, which walks a page again after the layout pass.
#pragma once

#include <cstdint>
#include <vector>

#include "game/message_state.hpp"

namespace od::port {

// exe 0x55a890's mode
enum class TimeFormat : int {
    Clock = 0,  // h:m:s of a count of frames at 60 / s (at most 99:59:59)
    Lap = 1,    // m'ss"cc of v >> 4 (the hundredths' units: v & 0xf)
    Dashes = 2, // "--:--:--" (also any negative time)
};

// exe 0x557be0(v, out, pad): the decimal of v (7 digits; leading zeros as spaces unless pad != 0, the last digit
// always), "-" for a negative v, into the text at `out`; returns the length
int messageNumberWrite(int32_t v, TextBytes& out, int pad);
// exe 0x55a890(v, out, format): the 8 glyphs of a time into the text at `out`; returns 8
int messageTimeWrite(int32_t v, TextBytes& out, TimeFormat format);

// The text fb 21 / fb 23 insert (a number / a time), ended by fb 1f -- for OpenDash's renderer, which walks a page again
// after the layout pass (the layout pass prints into the exe's buffers at each code, so only the last values stay
// there). ps1 = false: the PC encoding even under a Latin pack (a window of an exe bank, see ps1Window)
std::vector<uint8_t> messageNumberText(int32_t v, int pad, bool ps1 = true);
std::vector<uint8_t> messageTimeText(int32_t v, TimeFormat format, bool ps1 = true);

} // namespace od::port
