#include "lang.hpp"

#include <algorithm>
#include <cstring>

namespace od::lang {

const char* code(Language l) {
    switch (l) {
    case Language::English: return "en";
    case Language::Portuguese: return "ptbr";
    case Language::PortugueseFromJp: return "ptjp";
    case Language::EnglishFromJp: return "enjp";
    case Language::PortugueseFromJpUs: return "ptjpus";
    case Language::EnglishFromJpUs: return "enjpus";
    case Language::Spanish: return "es";
    case Language::French: return "fr";
    case Language::Italian: return "it";
    case Language::German: return "de";
    case Language::Korean: return "ko";
    case Language::Chinese: return "zh";
    case Language::SpanishFromJp: return "esjp";
    case Language::FrenchFromJp: return "frjp";
    case Language::ItalianFromJp: return "itjp";
    case Language::GermanFromJp: return "dejp";
    case Language::SpanishFromJpUs: return "esjpus";
    case Language::FrenchFromJpUs: return "frjpus";
    case Language::ItalianFromJpUs: return "itjpus";
    case Language::GermanFromJpUs: return "dejpus";
    case Language::Russian: return "ru";
    case Language::ChineseHK: return "zhhk";
    default: return "jp";
    }
}

bool pcScript(Language l) { return l == Language::Korean || l == Language::Chinese || l == Language::Russian ||
                                  l == Language::ChineseHK; }

const char* uiTextCode(Language l) {
    switch (l) {
    case Language::PortugueseFromJp:
    case Language::PortugueseFromJpUs: return "ptbr";
    case Language::EnglishFromJp:
    case Language::EnglishFromJpUs: return "en";
    case Language::SpanishFromJp: return "es";
    case Language::FrenchFromJp: return "fr";
    case Language::ItalianFromJp: return "it";
    case Language::GermanFromJp: return "de";
    case Language::SpanishFromJpUs: return "es";
    case Language::FrenchFromJpUs: return "fr";
    case Language::ItalianFromJpUs: return "it";
    case Language::GermanFromJpUs: return "de";
    default: return code(l);
    }
}

const char* displayName(Language l) {
    switch (l) {
    case Language::English: return "English (PS1)";
    case Language::Portuguese: return "PT-BR (PS1, fandub)";
    case Language::PortugueseFromJp: return "PT-BR (traduzido do japon\xc3\xaas)";
    case Language::EnglishFromJp: return "English (straight from the Japanese)";
    case Language::PortugueseFromJpUs: return "PT-BR (do japon\xc3\xaas, nomes dos EUA)";
    case Language::EnglishFromJpUs: return "English (from the Japanese, US names)";
    case Language::Spanish: return "Espa\xc3\xb1ol (from the US English)";
    case Language::French: return "Fran\xc3\xa7" "ais (from the US English)";
    case Language::Italian: return "Italiano (from the US English)";
    case Language::German: return "Deutsch (from the US English)";
    case Language::SpanishFromJp: return "Espa\xc3\xb1ol (from the Japanese)";
    case Language::FrenchFromJp: return "Fran\xc3\xa7" "ais (from the Japanese)";
    case Language::ItalianFromJp: return "Italiano (from the Japanese)";
    case Language::GermanFromJp: return "Deutsch (from the Japanese)";
    case Language::SpanishFromJpUs: return "Espa\xc3\xb1ol (from the Japanese, US names)";
    case Language::FrenchFromJpUs: return "Fran\xc3\xa7" "ais (from the Japanese, US names)";
    case Language::ItalianFromJpUs: return "Italiano (from the Japanese, US names)";
    case Language::GermanFromJpUs: return "Deutsch (from the Japanese, US names)";
    case Language::Korean: return "Korean";
    case Language::Chinese: return "Chinese (Simplified)";
    case Language::Russian: return "Russian (from the US English)";
    case Language::ChineseHK: return "Chinese (Traditional, Hong Kong)";
    default: return "Japones (PC)";
    }
}

const char* menuName(Language l) {
    switch (l) {
    case Language::English: return "English";
    // "language\nwhat it is": two lines in the options' row (each ~170 px: a longer line runs past the box)
    case Language::Portuguese: return "Portugu\xc3\xaas (BR)\nfandub";
    case Language::PortugueseFromJp: return "Portugu\xc3\xaas (BR)\ntraduzido do japon\xc3\xaas";
    case Language::EnglishFromJp: return "English\nfrom Japanese";
    case Language::PortugueseFromJpUs: return "Portugu\xc3\xaas (BR)\ndo japon\xc3\xaas, nomes dos EUA";
    case Language::EnglishFromJpUs: return "English\nfrom Japanese, US names";
    case Language::Spanish:
    case Language::SpanishFromJpUs:
    case Language::SpanishFromJp: return "Espa\xc3\xb1ol";
    case Language::French:
    case Language::FrenchFromJpUs:
    case Language::FrenchFromJp: return "Fran\xc3\xa7" "ais";
    case Language::Italian:
    case Language::ItalianFromJpUs:
    case Language::ItalianFromJp: return "Italiano";
    case Language::German:
    case Language::GermanFromJpUs:
    case Language::GermanFromJp: return "Deutsch";
    case Language::Korean: return "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4"; // hangugeo
    case Language::Chinese: return "\xe4\xb8\xad\xe6\x96\x87";             // zhongwen
    case Language::ChineseHK: return "\xe7\xb9\x81\xe9\xab\x94\xe4\xb8\xad\xe6\x96\x87"; // fanti zhongwen
    case Language::Russian: return "\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9"; // Russkiy
    default: return "";
    }
}

Language parseLanguage(std::string_view s) {
    for (int k = 1; k < kLanguageCount; ++k)
        if (s == code(Language(k))) return Language(k);
    if (s == "pt") return Language::Portuguese;
    return Language::Japanese;
}

bool fromJapanese(Language l) {
    return l == Language::PortugueseFromJp || l == Language::EnglishFromJp || l == Language::PortugueseFromJpUs ||
           l == Language::EnglishFromJpUs || l == Language::Korean || l == Language::Chinese || l == Language::ChineseHK ||
           l == Language::SpanishFromJp || l == Language::FrenchFromJp || l == Language::ItalianFromJp ||
           l == Language::GermanFromJp || l == Language::SpanishFromJpUs || l == Language::FrenchFromJpUs ||
           l == Language::ItalianFromJpUs || l == Language::GermanFromJpUs;
}

int languageBase(Language l) {
    switch (l) {
    case Language::English:
    case Language::EnglishFromJp:
    case Language::EnglishFromJpUs: return 1;
    case Language::Portuguese:
    case Language::PortugueseFromJp:
    case Language::PortugueseFromJpUs: return 2;
    case Language::Spanish:
    case Language::SpanishFromJpUs:
    case Language::SpanishFromJp: return 3;
    case Language::French:
    case Language::FrenchFromJpUs:
    case Language::FrenchFromJp: return 4;
    case Language::Italian:
    case Language::ItalianFromJpUs:
    case Language::ItalianFromJp: return 5;
    case Language::German:
    case Language::GermanFromJpUs:
    case Language::GermanFromJp: return 6;
    case Language::Korean: return 7;
    case Language::Chinese: return 8;
    case Language::Russian: return 10;
    case Language::ChineseHK: return 9;
    default: return 0;
    }
}

int languageVersion(Language l) {
    switch (l) {
    case Language::EnglishFromJp:
    case Language::PortugueseFromJp:
    case Language::SpanishFromJp:
    case Language::FrenchFromJp:
    case Language::ItalianFromJp:
    case Language::GermanFromJp: return 1;
    case Language::EnglishFromJpUs:
    case Language::PortugueseFromJpUs:
    case Language::SpanishFromJpUs:
    case Language::FrenchFromJpUs:
    case Language::ItalianFromJpUs:
    case Language::GermanFromJpUs: return 2;
    default: return 0;
    }
}

Language languageOf(int base, int version) {
    for (int k = 0; k < kLanguageCount; ++k)
        if (languageBase(Language(k)) == base && languageVersion(Language(k)) == version) return Language(k);
    return Language::Count;
}

Language uiLanguage(Language l) {
    switch (l) {
    case Language::PortugueseFromJp:
    case Language::PortugueseFromJpUs: return Language::Portuguese;
    case Language::EnglishFromJp:
    case Language::EnglishFromJpUs:
    case Language::Spanish:
    case Language::French:
    case Language::Italian:
    case Language::German:
    case Language::SpanishFromJp:
    case Language::FrenchFromJp:
    case Language::ItalianFromJp:
    case Language::GermanFromJp:
    case Language::SpanishFromJpUs:
    case Language::FrenchFromJpUs:
    case Language::ItalianFromJpUs:
    case Language::GermanFromJpUs: return Language::English;
    case Language::Korean:
    case Language::Chinese:
    case Language::ChineseHK:
    case Language::Russian: return Language::Japanese;
    default: return l;
    }
}

const char* ps1GlyphText(uint8_t g) {
    static const char* const kDigits[10] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    static const char* const kUpper[26] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
                                           "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
    static const char* const kLower[26] = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
                                           "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z"};
    if (g < 0x0a) return kDigits[g];
    if (g >= 0x14 && g < 0x2e) return kUpper[g - 0x14];
    if (g >= 0x2e && g < 0x48) return kLower[g - 0x2e];
    switch (g) {
    case 0x0a: return ",";   // low comma (JP-width cell)
    case 0x0b: return ".";   // small circle (JP full stop)
    case 0x0c: return "'";
    case 0x0d: return "!";
    case 0x0e: return "?";
    case 0x0f: return "\"";  // opening corner bracket
    case 0x10: return "\xc2\xb7"; // middle dot
    case 0x11: return "(";
    case 0x12: return ")";
    case 0x13: return ":";
    case 0x48: return "&";
    case 0x4b: return "/";
    case 0x4c: return " ";
    case 0x4d: return "\"";  // closing corner bracket
    case 0x4e: return "~";
    case 0x4f: return "-";
    case 0x50: return " ";
    case 0x59: return ",";
    case 0x5a: return "\"";
    case 0x5b: return ".";
    case 0x5c: return "...";
    case 0x5e: return "+";
    case 0x5f: return "%";
    case 0x61: return "-";
    case 0x62: return "=";
    case 0x63: return ";";
    case 0x64: return "\xce\xb1"; // alpha (the Buster parts; drawn in HD from the full M+)
    case 0x65: return "\xce\xa9";
    case 0x66: return "\xe2\x86\x91";
    case 0x67: return "\xe2\x86\x93";
    case 0x68: return "\xe2\x86\x90";
    case 0x69: return "\xe2\x86\x92";
    case 0x6a: return "#";
    case 0x6b: return "\xc3\xa1"; // a acute
    case 0x6c: return "\xc3\xa0"; // a grave
    case 0x6d: return "\xc3\xa2"; // a circumflex
    case 0x6e: return "\xc3\xa3"; // a tilde
    case 0x6f: return "\xc3\xa9"; // e acute
    case 0x70: return "\xc3\xaa"; // e circumflex
    case 0x71: return "\xc3\xad"; // i acute
    case 0x72: return "\xc3\xb3"; // o acute
    case 0x73: return "\xc3\xb4"; // o circumflex
    case 0x74: return "\xc3\xb5"; // o tilde
    case 0x75: return "\xc3\xba"; // u acute
    case 0x76: return "\xc3\xa7"; // c cedilla
    case 0x77: return "\xc3\x89"; // E acute
    case 0x78: return "\xc3\x83"; // A tilde
    case 0x79: return "\xc3\x81"; // A acute
    case 0x7a: return "\xc3\x93"; // O acute
    case 0x7b: return "\xc3\xbc"; // u diaeresis: OpenDash's packs translated from the Japanese (Muller), its pixel glyph too (fonte_europa)
    // 0x7c..0xa1: the letters of the Spanish, French, Italian and German packs (ferramentas/texto japones/fonte_europa.py)
    case 0x7c: return "\xc3\xa4"; // latin small letter a with diaeresis
    case 0x7d: return "\xc3\xab"; // latin small letter e with diaeresis
    case 0x7e: return "\xc3\xaf"; // latin small letter i with diaeresis
    case 0x7f: return "\xc3\xb6"; // latin small letter o with diaeresis
    case 0x80: return "\xc3\xa8"; // latin small letter e with grave
    case 0x81: return "\xc3\xac"; // latin small letter i with grave
    case 0x82: return "\xc3\xb2"; // latin small letter o with grave
    case 0x83: return "\xc3\xb9"; // latin small letter u with grave
    case 0x84: return "\xc3\xae"; // latin small letter i with circumflex
    case 0x85: return "\xc3\xbb"; // latin small letter u with circumflex
    case 0x86: return "\xc3\xb1"; // latin small letter n with tilde
    case 0x87: return "\xc5\x93"; // latin small ligature oe
    case 0x88: return "\xc3\xa6"; // latin small letter ae
    case 0x89: return "\xc3\x9f"; // latin small letter sharp s
    case 0x8a: return "\xc2\xbf"; // inverted question mark
    case 0x8b: return "\xc2\xa1"; // inverted exclamation mark
    case 0x8c: return "\xc3\x80"; // latin capital letter a with grave
    case 0x8d: return "\xc3\x82"; // latin capital letter a with circumflex
    case 0x8e: return "\xc3\x84"; // latin capital letter a with diaeresis
    case 0x8f: return "\xc3\x87"; // latin capital letter c with cedilla
    case 0x90: return "\xc3\x88"; // latin capital letter e with grave
    case 0x91: return "\xc3\x8a"; // latin capital letter e with circumflex
    case 0x92: return "\xc3\x8b"; // latin capital letter e with diaeresis
    case 0x93: return "\xc3\x8e"; // latin capital letter i with circumflex
    case 0x94: return "\xc3\x8f"; // latin capital letter i with diaeresis
    case 0x95: return "\xc3\x8c"; // latin capital letter i with grave
    case 0x96: return "\xc3\x8d"; // latin capital letter i with acute
    case 0x97: return "\xc3\x91"; // latin capital letter n with tilde
    case 0x98: return "\xc3\x92"; // latin capital letter o with grave
    case 0x99: return "\xc3\x94"; // latin capital letter o with circumflex
    case 0x9a: return "\xc3\x96"; // latin capital letter o with diaeresis
    case 0x9b: return "\xc3\x99"; // latin capital letter u with grave
    case 0x9c: return "\xc3\x9a"; // latin capital letter u with acute
    case 0x9d: return "\xc3\x9b"; // latin capital letter u with circumflex
    case 0x9e: return "\xc3\x9c"; // latin capital letter u with diaeresis
    case 0x9f: return "\xc5\x92"; // latin capital ligature oe
    case 0xa0: return "\xc2\xab"; // left-pointing double angle quotation mark
    case 0xa1: return "\xc2\xbb"; // right-pointing double angle quotation mark
    default: return "";
    }
}

bool ps1GlyphSpace(uint8_t g) { return g == 0x4c || g == 0x50; }

// UTF-8 -> the PS1 Latin font's codes (engine/lang ps1GlyphText inverted, the Latin punctuation preferred; space 0x4c)
std::vector<uint8_t> ps1Encode(std::string_view t) {
    std::vector<uint8_t> v;
    for (size_t i = 0; i < t.size();) {
        if (t[i] == ' ') {
            v.push_back(0x4c);
            ++i;
            continue;
        }
        int best = -1;
        size_t len = 0;
        for (int g = 0; g < 256; ++g) {
            const std::string_view gt = ps1GlyphText(uint8_t(g));
            if (!gt.empty() && gt.size() >= len && !ps1GlyphSpace(uint8_t(g)) && t.substr(i, gt.size()) == gt) {
                best = g;
                len = gt.size();
            }
        }
        if (best < 0) {
            ++i;
            continue;
        }
        v.push_back(uint8_t(best));
        i += len;
    }
    return v;
}

std::filesystem::path packDir(const std::filesystem::path& root, Language l) {
    if (l == Language::Japanese || root.empty()) return {};
    return root / code(l);
}

std::filesystem::path bankPath(const std::filesystem::path& root, Language l, std::string_view datName) {
    const auto dir = packDir(root, l);
    if (dir.empty()) return {};
    return dir / "msg" / (std::filesystem::path(datName).stem().string() + ".bin");
}

bool packAvailable(const std::filesystem::path& root, Language l) {
    const auto dir = packDir(root, l);
    std::error_code ec;
    if (pcScript(l)) // the PC's format: its glyphs' code points and its vector font's name instead of the PS1 font
        return !dir.empty() && std::filesystem::exists(dir / "charset.bin", ec) &&
               std::filesystem::exists(dir / "font.txt", ec) && std::filesystem::is_directory(dir / "msg", ec);
    return !dir.empty() && std::filesystem::exists(dir / "font.bin", ec) &&
           std::filesystem::exists(dir / "widths.bin", ec) && std::filesystem::is_directory(dir / "msg", ec);
}

std::optional<std::array<uint8_t, 256>> latinDatWidths(std::span<const uint8_t> font, int cell) {
    constexpr int kInkFrom = 3; // a texel below it is the glyph's soft edge
    constexpr int kGlyphPx = 12;
    if (cell <= 0 || cell % kGlyphPx != 0 || cell % 2 != 0) return std::nullopt;
    const size_t bytes = size_t(cell / 2) * size_t(cell);
    if (font.size() < bytes * 0x80) return std::nullopt;
    const int perPx = cell / kGlyphPx;
    // the first and last column with ink of glyph g; {cell, -1} for an empty cell
    auto ink = [&](int g) {
        const uint8_t* b = font.data() + size_t(g) * bytes;
        int first = cell, last = -1;
        for (int y = 0; y < cell; ++y)
            for (int x = 0; x < cell; ++x) {
                const uint8_t v = b[(y * cell + x) / 2];
                if (((x & 1) ? v >> 4 : v & 15) >= kInkFrom) {
                    first = std::min(first, x);
                    last = std::max(last, x);
                }
            }
        return std::pair{first, last};
    };
    std::array<uint8_t, 256> w;
    w.fill(kGlyphPx);
    for (const auto [from, to] : {std::pair{'A', 'Z'}, std::pair{'a', 'z'}})
        for (int g = from; g <= to; ++g) {
            const auto [first, last] = ink(g);
            if (first != perPx) return std::nullopt; // not flush left: not this font
            w[size_t(g)] = uint8_t((last + 1) / perPx);
        }
    static constexpr std::pair<char, uint8_t> kOthers[] = {
        {' ', 6}, {'!', 10}, {'"', 8}, {'#', 8}, {'\'', 4}, {'(', 5}, {')', 5}, {'+', 11}, {',', 5}, {'-', 8},
        {'.', 8}, {'/', 8},  {':', 7}, {';', 7}, {'=', 8},
    };
    for (const auto [c, width] : kOthers) w[uint8_t(c)] = width;
    for (char d = '0'; d <= '9'; ++d) w[uint8_t(d)] = 10;
    return w;
}

int latinDatDrop(uint8_t g) {
    switch (g) {
    case 'g': case 'j': case ',': case '.': return 1;
    case 'p': case 'q': case 'y': return 2;
    default: return 0;
    }
}

std::optional<XaTrack> parseXaTrack(std::span<const uint8_t> t) {
    if (t.size() < 12 || std::memcmp(t.data(), "ODXA", 4) != 0) return std::nullopt;
    XaTrack x;
    const uint8_t coding = t[4];
    x.stereo = (coding & 1) != 0;
    x.rate = ((coding >> 2) & 3) == 1 ? 18900u : 37800u;
    std::memcpy(&x.sectors, t.data() + 8, 4);
    if (t.size() - 12 < size_t(x.sectors) * kXaSectorBytes) return std::nullopt;
    x.data = t.data() + 12;
    return x;
}

// Sound group (128 bytes): 16 header bytes (unit k's shift/filter at 4 + k), 112 bytes of 4-bit samples
// interleaved per unit; 8 units of 28 samples (stereo: even units left, odd units right).
void XaDecoder::decodeSector(const uint8_t* groups, bool stereo, std::vector<int16_t>& l, std::vector<int16_t>& r) {
    static constexpr int kPos[5] = {0, 60, 115, 98, 122};
    static constexpr int kNeg[5] = {0, 0, -52, -55, -60};
    for (int g = 0; g < 18; ++g) {
        const uint8_t* grp = groups + g * 128;
        for (int unit = 0; unit < 8; ++unit) {
            const uint8_t hdr = grp[4 + unit];
            const int shift = hdr & 0x0f, filt = std::min(hdr >> 4, 4);
            const int c = stereo ? (unit & 1) : 0;
            int32_t* h = hist[c];
            std::vector<int16_t>& out = c ? r : l;
            for (int s = 0; s < 28; ++s) {
                const uint8_t b = grp[16 + s * 4 + unit / 2];
                int n = (unit & 1) ? (b >> 4) : (b & 0x0f);
                if (n & 8) n -= 16;
                int32_t v = ((n << 12) >> shift) + ((h[0] * kPos[filt] + h[1] * kNeg[filt] + 32) >> 6);
                v = std::clamp(v, -32768, 32767);
                h[1] = h[0];
                h[0] = v;
                out.push_back(int16_t(v));
            }
        }
    }
}

} // namespace od::lang
