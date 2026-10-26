// OpenDash's help banks of the status sub-screen (see menu_help.hpp).
#include "game/menu_help.hpp"

#include <algorithm>
#include <vector>

#include "game/language_pack.hpp"
#include "game/loaded_data.hpp"
#include "game/text_script.hpp"
#include "engine/lang.hpp"

namespace od::port {

void HelpWriter::copy(uint32_t bank, int id) {
    const TextBytes from(MessageBank(bank).message(id));
    uint32_t n = 0;
    while (n < 0x200 && from.mappedAt(n, 1)) {
        const uint8_t c = from.byte(n);
        out.byte(at + n) = c;
        ++n;
        if (c == 0xff) break;
        if (c == 0xfb || (c >= 0xd0 && c < 0xfb)) { // two-byte glyphs, codes: their argument bytes as they are
            const uint32_t len = c == 0xfb ? messageOpLength(TextBytes(from.at + n - 1)) : 2u;
            for (uint32_t k = 1; k < len; ++k, ++n) out.byte(at + n) = from.byte(n);
        }
    }
    at += n;
}

void HelpWriter::ps1Columns(uint32_t from, uint32_t to, bool fromSource) {
    if (jp_ || (fromSource && sourceHeaders_)) return;
    for (uint32_t q = from; q + 7 < to; ++q)
        if (out.byte(q) == 0xfb && out.byte(q + 1) == 0x06)
            out.byte(q + 6) = uint8_t(std::min<uint32_t>(out.byte(q + 6) * 6u, 0xffu));
}

void HelpWriter::header(uint32_t bank, int id) {
    const TextBytes p(MessageBank(bank).message(id));
    const uint32_t from = at;
    for (uint32_t k = 0; k < 10; ++k) out.byte(at++) = p.byte(k);
    ps1Columns(from, at, true);
}

void HelpWriter::wrapped(const char* text, uint32_t linesAt) {
    const int maxPx = int(out.byte(linesAt - 1)) * 2; // the cols byte, already in the pack's 2 px units
    const std::vector<uint8_t> g = lang::ps1Encode(text);
    int x = 0, lines = 1;
    size_t i = 0;
    while (i < g.size()) {
        size_t j = i;
        int wpx = 0;
        while (j < g.size() && g[j] != 0x4c) wpx += ps1Advance(g[j++]);
        const int sp = x > 0 ? ps1Advance(0x4c) : 0;
        if (x > 0 && x + sp + wpx > maxPx) {
            out.byte(at++) = 0xfc;
            ++lines;
            x = 0;
        } else if (x > 0) {
            out.byte(at++) = 0x4c;
            x += sp;
        }
        for (size_t k = i; k < j; ++k) out.byte(at++) = g[k];
        x += wpx;
        i = j < g.size() ? j + 1 : j;
    }
    out.byte(linesAt) = uint8_t((out.byte(linesAt) & 0xf0) | std::max(out.byte(linesAt) & 0xf, lines));
}

void HelpWriter::glyphs(const uint16_t* codes, uint32_t count) {
    for (uint32_t k = 0; k < count; ++k) out.byte(at++) = uint8_t(codes[k]);
}

void HelpWriter::setOffset(uint32_t table, uint32_t i, uint32_t value) {
    storeData(base_ + table + i * 2, uint16_t(value)); // a help bank's message offset
}

} // namespace od::port
