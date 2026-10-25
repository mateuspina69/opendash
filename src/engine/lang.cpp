#include "lang.hpp"

#include <algorithm>
#include <cstring>

namespace od::lang {

const char* code(Language l) {
    switch (l) {
    case Language::English: return "en";
    case Language::Portuguese: return "ptbr";
    default: return "jp";
    }
}

const char* displayName(Language l) {
    switch (l) {
    case Language::English: return "English (PS1)";
    case Language::Portuguese: return "Portugues (PS1, fandub)";
    default: return "Japones (PC)";
    }
}

Language parseLanguage(std::string_view s) {
    if (s == "en") return Language::English;
    if (s == "ptbr" || s == "pt") return Language::Portuguese;
    return Language::Japanese;
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
    return !dir.empty() && std::filesystem::exists(dir / "font.bin", ec) &&
           std::filesystem::exists(dir / "widths.bin", ec) && std::filesystem::is_directory(dir / "msg", ec);
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
