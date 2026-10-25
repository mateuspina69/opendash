// Language packs built from the PS1 disc:
//   <root>/<code>/msg/<STEM>.bin   message bank of the PS1 DAT/<STEM>.BIN (same ids / script codes as the PC
//                                  type-6 bank, glyph bytes index the PS1 Latin font)
//   <root>/<code>/font.bin         256x256 4bpp font page (bit plane 0 = Latin font, 12x12 cells, 20 per row)
//   <root>/<code>/widths.bin       256 glyph widths (SLUS_011.40 0x5c10c)
//   <root>/<code>/snd/xa*.dat      voice packs in the PC layout, tracks = ODXA (CD-XA ADPCM sound groups)
// Japanese = the PC game's own data (no pack). No game data ships with OpenDash.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace od::lang {

enum class Language { Japanese, English, Portuguese };

const char* code(Language l);             // "jp", "en", "ptbr"
const char* displayName(Language l);      // menu label
Language parseLanguage(std::string_view s); // unknown -> Japanese

// pack directory of a language ("" for Japanese)
std::filesystem::path packDir(const std::filesystem::path& root, Language l);
// <pack>/msg/<stem of datName>.bin ("" for Japanese)
std::filesystem::path bankPath(const std::filesystem::path& root, Language l, std::string_view datName);
// the pack exists (font, widths, msg/)
bool packAvailable(const std::filesystem::path& root, Language l);

// ---- PS1 Latin font <-> Unicode ----
// UTF-8 text of PS1 glyph g (the script byte, < 0xd0): digits 0x00, A 0x14, a 0x2e, punctuation 0x0a..0x13 /
// 0x48..0x63, arrows 0x65..0x69, # 0x6a, the PT-BR accents 0x6b..0x7a (read off the packs' font pages: EN and PT-BR
// agree up to 0x6a; EN has empty cells from 0x6b). "" = no Unicode form (button / pad icons, the zenny sign, the
// hands ...): draw the pack's bitmap glyph.
const char* ps1GlyphText(uint8_t g);
// the glyph separates words (space 0x4c, wide space 0x50)
bool ps1GlyphSpace(uint8_t g);
// UTF-8 -> PS1 glyph codes (OpenDash's own strings drawn in the pack font); unknown characters are dropped
std::vector<uint8_t> ps1Encode(std::string_view t);

// ---- CD-XA ADPCM ----
inline constexpr size_t kXaSectorBytes = 2304; // 18 sound groups x 128 bytes

struct XaTrack {
    bool stereo = false;
    uint32_t rate = 37800;
    uint32_t sectors = 0;
    const uint8_t* data = nullptr; // sectors * kXaSectorBytes
};
// "ODXA" u8 coding, 3 pad, u32 sectors, sound groups. nullopt if not an ODXA track or truncated.
std::optional<XaTrack> parseXaTrack(std::span<const uint8_t> t);

struct XaDecoder {
    int32_t hist[2][2]{};
    // appends one sector: 4032 mono samples to l, or 2016 frames to l and r
    void decodeSector(const uint8_t* groups, bool stereo, std::vector<int16_t>& l, std::vector<int16_t>& r);
};

} // namespace od::lang
