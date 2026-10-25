// A message's text read outside the windows, for logs: its glyphs and control codes printed as ASCII / codes.
#pragma once

#include <cstdint>
#include <string>

namespace od::port {

// The text of message `id` of the bank at `bank`: ASCII for 0x20..0x7e (hypothesis, see messages.md), <xx> other glyphs,
// <k:xxxx> two-byte glyphs, ruby in (), \n for fc, [fbNN ...] for control codes, [opt i] the choice's option markers
// (fb 0f), [wait n, page] for fd
std::string messageText(uint32_t bank, int id);

} // namespace od::port
