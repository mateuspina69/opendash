#include "engine/pack_charset.hpp"

#include <iterator>
#include <unordered_map>

#include "engine/jp_charset.hpp"
#include "engine/lang.hpp"

namespace od::lang {

namespace {
std::vector<uint32_t> gCodepoints;
std::unordered_map<uint32_t, int> gGlyphOf;
bool gProportional = false;
} // namespace

uint32_t fixedCodepoint(int g) {
    static constexpr uint32_t kFixed[] = {0xd55c, 0xad6d, 0xc5b4, 0x4e2d, 0x6587, 0x420, 0x443, 0x441, 0x43a, 0x438, 0x439, 0x7e41, 0x9ad4};
    const int i = g - kFixedGlyphBase;
    return i >= 0 && i < int(std::size(kFixed)) ? kFixed[i] : 0;
}

void setPackGlyphs(std::vector<uint32_t> codepoints) {
    gCodepoints = std::move(codepoints);
    gGlyphOf.clear();
    for (size_t i = 0; i < gCodepoints.size(); ++i)
        if (gCodepoints[i]) gGlyphOf.emplace(gCodepoints[i], kPackGlyphBase + int(i));
}

bool packGlyphsActive() { return !gCodepoints.empty(); }

void setPackProportional(bool on) { gProportional = on; }
bool packProportional() { return gProportional && !gCodepoints.empty(); }

uint32_t packCodepoint(int g) {
    if (const uint32_t f = fixedCodepoint(g)) return f;
    const int i = g - kPackGlyphBase;
    return i >= 0 && size_t(i) < gCodepoints.size() ? gCodepoints[size_t(i)] : 0;
}

int packGlyphOf(uint32_t cp) {
    const auto it = gGlyphOf.find(cp);
    return it == gGlyphOf.end() ? -1 : it->second;
}

int pcGlyphOf(uint32_t cp) {
    if (const int g = packGlyphOf(cp); g >= 0) return g;
    return jpGlyphOf(cp);
}

std::vector<uint16_t> pcEncode(std::string_view t) {
    std::vector<uint16_t> out;
    for (size_t i = 0; i < t.size();) {
        const auto* u = reinterpret_cast<const unsigned char*>(t.data() + i);
        uint32_t cp = u[0];
        size_t n = 1;
        if ((u[0] & 0xe0) == 0xc0 && i + 1 < t.size()) cp = (u[0] & 0x1fu) << 6 | (u[1] & 0x3fu), n = 2;
        else if ((u[0] & 0xf0) == 0xe0 && i + 2 < t.size())
            cp = (u[0] & 0x0fu) << 12 | (u[1] & 0x3fu) << 6 | (u[2] & 0x3fu), n = 3;
        else if ((u[0] & 0xf8) == 0xf0 && i + 3 < t.size())
            cp = (u[0] & 0x07u) << 18 | (u[1] & 0x3fu) << 12 | (u[2] & 0x3fu) << 6 | (u[3] & 0x3fu), n = 4;
        i += n;
        if (const int g = pcGlyphOf(cp); g >= 0) out.push_back(uint16_t(g));
    }
    return out;
}

} // namespace od::lang
