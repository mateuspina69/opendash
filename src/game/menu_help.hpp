// OpenDash's help banks of the status sub-screen: a message bank (u16 offsets by id, then the messages) written into the
// game's built data (game/loaded_data) from the bound source's own messages and OpenDash's texts for the rows it adds
// (the options page's video and language rows, the panel's "To Title" row).
#pragma once

#include <cstdint>

#include "game/text_memory.hpp"

namespace od::port {

class HelpWriter {
public:
    // `base`: the bank's region (registered built data); `jp`: the texts are the Japanese ones (their glyph codes are
    // written as they are); `sourceHeaders`: the source's own message headers are the PS1's already (their window
    // columns are in 2 px units, a pack's text of the exe's Japanese header needs them rescaled)
    HelpWriter(uint32_t base, bool jp, bool sourceHeaders) : out(base), base_(base), jp_(jp), sourceHeaders_(sourceHeaders) {}

    // the next byte to write, as an offset from the region's start
    uint32_t at = 0;
    TextBytes out;

    // a message (u16 page header, then the text, ff) of `bank` copied to `at`
    void copy(uint32_t bank, int id);
    // a message's page header and its fb 06 window op (10 bytes), the columns in the pack's units
    void header(uint32_t bank, int id);
    // the window ops' columns of [from, to) in the pack's 2 px units (the JP header's 12 px columns x 6); `fromSource`:
    // the header is the source's own (already the PS1's when `sourceHeaders`)
    void ps1Columns(uint32_t from, uint32_t to, bool fromSource = false);
    // OpenDash's Latin text: PS1 glyphs broken into lines (fc) at the spaces so that each fits the window's width (the
    // cols byte at linesAt - 1, in 2 px units) with the PS1 advance; the window's line count at linesAt grows to fit
    void wrapped(const char* text, uint32_t linesAt);
    // Japanese glyph codes as they are
    void glyphs(const uint16_t* codes, uint32_t count);
    void end() { out.byte(at++) = 0xff; }

    // the message offsets table: entry i of the table at `table` (an offset from the region's start) names `at`
    void setOffset(uint32_t table, uint32_t i, uint32_t value);

private:
    uint32_t base_;
    bool jp_, sourceHeaders_;
};

} // namespace od::port
