// A message's text read outside the windows (see message_text.hpp).
#include "game/message_text.hpp"

#include <cstdio>

#include "game/message_state.hpp"
#include "game/text_script.hpp"

namespace od::port {

namespace {
constexpr int kTextGuard = 4096; // the bytes a broken message is read at most
constexpr uint8_t kAsciiFirst = 0x20, kAsciiEnd = 0x7f;
constexpr uint32_t kPageHeader = 2;
} // namespace

std::string messageText(uint32_t bank, int id) {
    std::string s;
    char buf[32];
    uint32_t p = MessageBank(bank).message(id) + kPageHeader;
    for (int guard = 0; guard < kTextGuard; ++guard) {
        const TextBytes t(p);
        const uint8_t c = t.byte(0);
        if (c < kTextCodeOp) {
            if (c >= kGlyphTwoByte) {
                std::snprintf(buf, sizeof buf, "<k:%02x%02x>", c, t.byte(1));
                s += buf;
                p += 2;
            } else if (c >= kAsciiFirst && c < kAsciiEnd) {
                s += char(c);
                ++p;
            } else {
                std::snprintf(buf, sizeof buf, "<%02x>", c);
                s += buf;
                ++p;
            }
            continue;
        }
        if (c == kTextCodeEnd) break;
        if (c == kTextCodeNewLine) {
            s += "\\n";
            ++p;
            continue;
        }
        if (c == kTextCodeRuby) { // glyph, n, k, n ruby glyphs
            const bool two = t.byte(1) >= kGlyphTwoByte;
            if (two) std::snprintf(buf, sizeof buf, "<k:%02x%02x>(", t.byte(1), t.byte(2));
            else std::snprintf(buf, sizeof buf, "<%02x>(", t.byte(1));
            s += buf;
            const uint32_t q = 1 + (two ? 2 : 1);
            const uint8_t n = t.byte(q);
            for (uint32_t i = 0; i < n; ++i) {
                std::snprintf(buf, sizeof buf, "%s%02x", i ? " " : "", t.byte(q + 2 + i));
                s += buf;
            }
            s += ")";
            p += q + 2 + n;
            continue;
        }
        if (c == kTextCodeWait) {
            std::snprintf(buf, sizeof buf, "[wait %u, page]", t.be16(1));
            s += buf;
            p += 5; // the next page starts with its own u16 header (exe 0x559830 -> 0x559700)
            continue;
        }
        const uint8_t op = t.byte(1);
        if (op == kTextOpChoiceOption) {
            std::snprintf(buf, sizeof buf, "[opt %u]", t.byte(2));
            s += buf;
            p += 3;
            continue;
        }
        const uint32_t len = messageOpLength(t);
        if (len == 0) {
            std::snprintf(buf, sizeof buf, "[fb%02x ?]", op);
            s += buf;
            break;
        }
        std::snprintf(buf, sizeof buf, "[fb%02x", op);
        s += buf;
        for (uint32_t i = 2; i < len; ++i) {
            std::snprintf(buf, sizeof buf, " %02x", t.byte(i));
            s += buf;
        }
        s += "]";
        p += len;
    }
    return s;
}

} // namespace od::port
