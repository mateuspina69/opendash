// Language packs (engine/lang): XA-ADPCM decoding vs. the Python reference on a
// synthetic sector (no game data), pack paths, ODXA track parsing.
//   test_lang
#include "check.hpp"

#include "engine/lang.hpp"

#include <cstdint>
#include <initializer_list>
#include <numeric>
#include <string>
#include <vector>

namespace {

// PS1 Latin glyphs -> UTF-8 (the vector font path): text of ST09's PT-BR bank ("Nao" with a tilde, "Serio?" with an
// acute e, "1,000 zenny!"); icons have no Unicode form (drawn from the pack's bitmap)
void testGlyphText() {
    using od::lang::ps1GlyphText;
    auto text = [](std::initializer_list<uint8_t> gs) {
        std::string s;
        for (uint8_t g : gs) s += ps1GlyphText(g);
        return s;
    };
    CHECK(text({0x21, 0x6e, 0x3c}) == "N\xc3\xa3o");
    CHECK(text({0x26, 0x6f, 0x3f, 0x36, 0x3c, 0x0e}) == "S\xc3\xa9rio?");
    CHECK(text({0x01, 0x59, 0x00, 0x00, 0x00, 0x4c, 0x47, 0x32, 0x3b, 0x3b, 0x46, 0x0d}) == "1,000 zenny!");
    CHECK(std::string(ps1GlyphText(0x76)) == "\xc3\xa7" && std::string(ps1GlyphText(0x5b)) == ".");
    CHECK(std::string(ps1GlyphText(0x51)).empty() && std::string(ps1GlyphText(0x5d)).empty());
    CHECK(od::lang::ps1GlyphSpace(0x4c) && !od::lang::ps1GlyphSpace(0x4b));
}

std::vector<uint8_t> syntheticSector() {
    std::vector<uint8_t> d(od::lang::kXaSectorBytes);
    for (size_t i = 0; i < d.size(); ++i) d[i] = uint8_t(((i * 37 + 11) ^ (i >> 3)) & 0xff);
    return d;
}

int64_t sum(const std::vector<int16_t>& v) { return std::accumulate(v.begin(), v.end(), int64_t{0}); }

void testMono() {
    // python: decode_clip(sector x2, coding 0x04) -> 8064 samples, sum -21448304
    const auto s = syntheticSector();
    od::lang::XaDecoder dec;
    std::vector<int16_t> l, r;
    dec.decodeSector(s.data(), false, l, r);
    dec.decodeSector(s.data(), false, l, r);
    CHECK(l.size() == 8064 && r.empty());
    CHECK(sum(l) == -21448304);
    CHECK(l[0] == -1 && l[1] == -3 && l[5] == -13);
    CHECK(l[l.size() - 1] == -6675 && l[l.size() - 3] == -3813);
}

void testStereo() {
    // python: coding 0x01 -> 4032 + 4032, sums -10807709 / -10099993
    const auto s = syntheticSector();
    od::lang::XaDecoder dec;
    std::vector<int16_t> l, r;
    dec.decodeSector(s.data(), true, l, r);
    dec.decodeSector(s.data(), true, l, r);
    CHECK(l.size() == 4032 && r.size() == 4032);
    CHECK(sum(l) == -10807709 && sum(r) == -10099993);
    CHECK(r[0] == 1280 && r[4] == -3343);
}

void testTrack() {
    // ODXA track: magic, coding, 3 pad, u32 sectors, 2304 bytes per sector
    const auto s = syntheticSector();
    std::vector<uint8_t> t = {'O', 'D', 'X', 'A', 0x01, 0, 0, 0, 2, 0, 0, 0};
    t.insert(t.end(), s.begin(), s.end());
    t.insert(t.end(), s.begin(), s.end());
    const auto h = od::lang::parseXaTrack(t);
    CHECK(h.has_value());
    if (h) {
        CHECK(h->stereo && h->rate == 37800 && h->sectors == 2 && h->data == t.data() + 12);
    }
    t[4] = 0x04;
    const auto m = od::lang::parseXaTrack(t);
    CHECK(m && !m->stereo && m->rate == 18900);
    t[9] = 1; // sector count larger than the data
    CHECK(!od::lang::parseXaTrack(t));
    CHECK(!od::lang::parseXaTrack(std::vector<uint8_t>{'I', 'D', '3', 4}));
}

void testNames() {
    using od::lang::Language;
    CHECK(od::lang::parseLanguage("ptbr") == Language::Portuguese);
    CHECK(od::lang::parseLanguage("en") == Language::English);
    CHECK(od::lang::parseLanguage("jp") == Language::Japanese);
    CHECK(od::lang::parseLanguage("xx") == Language::Japanese);
    CHECK(od::lang::bankPath("D:/l", Language::Portuguese, "ST0902.DAT") ==
          std::filesystem::path("D:/l") / "ptbr" / "msg" / "ST0902.bin");
    CHECK(od::lang::bankPath("D:/l", Language::Japanese, "ST09.DAT").empty());
}

} // namespace

int main() {
    testGlyphText();
    testMono();
    testStereo();
    testTrack();
    testNames();
    return testResult("test_lang");
}
