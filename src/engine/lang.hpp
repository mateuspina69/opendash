// Language packs built from the PS1 disc:
//   <root>/<code>/msg/<STEM>.bin   message bank of the PS1 DAT/<STEM>.BIN (same ids / script codes as the PC
//                                  type-6 bank, glyph bytes index the PS1 Latin font)
//   <root>/<code>/font.bin         256x256 4bpp font page (bit plane 0 = Latin font, 12x12 cells, 20 per row)
//   <root>/<code>/widths.bin       256 glyph widths (SLUS_011.40 0x5c10c)
//   <root>/<code>/snd/xa*.dat      voice packs in the PC layout, tracks = ODXA (CD-XA ADPCM sound groups)
// Japanese = the PC game's own data (no pack). No game data ships with OpenDash.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace od::lang {

// The text languages. The first three are the PC's own Japanese and the PS1 discs' scripts; the ones after them are
// OpenDash's packs translated straight from the PC's Japanese script (lang/<code>/msg: the PC's message codes, the
// PS1 Latin font's glyphs and 2 px columns, menus and item names from the pack's disc). A new language: a value
// before Count, its code / names / uiLanguage below, and its pack folder.
// FromJp: faithful to the Japanese (its names, the honorifics, its item names); FromJpUs: the same translation with
// the US localisation's names, no honorifics and the disc's item names
// Spanish .. German: packs in the Latin format, translated from the US English script (its names, its menus);
// SpanishFromJp .. GermanFromJp: the same languages translated from the English made from the Japanese (the
// FromJp packs' names and honorifics; the menus are the US-script ones with the Japanese names); Korean, Chinese (Simplified): packs in
// the PC's own format (pcScript: the PC banks' codes and 12 px columns, their glyphs the pack's charset.bin, drawn by
// the HD text from the pack's vector font), translated from the Japanese
enum class Language {
    Japanese, English, Portuguese, PortugueseFromJp, EnglishFromJp, PortugueseFromJpUs, EnglishFromJpUs,
    Spanish, French, Italian, German, Korean, Chinese, SpanishFromJp, FrenchFromJp, ItalianFromJp, GermanFromJp,
    SpanishFromJpUs, FrenchFromJpUs, ItalianFromJpUs, GermanFromJpUs,
    // OpenDash (Herbert): Russian, a pack in the PC's format translated from the US English script (its names),
    // its Cyrillic set at the vector font's own advance (font.txt `proportional`)
    Russian,
    // OpenDash (Herbert): Traditional Chinese as Hong Kong writes it, a pack in the PC's format made from the
    // Japanese (the Simplified pack's translation, reviewed against the Japanese, in Hong Kong forms and vocabulary)
    ChineseHK,
    Count
};
inline constexpr int kLanguageCount = int(Language::Count);
// the voice languages (the packs with voices: the PC's and the two discs')
inline constexpr int kVoiceLanguageCount = 3;

const char* code(Language l);             // "jp", "en", "ptbr", "ptjp", "enjp", "ptjpus", "enjpus"
const char* displayName(Language l);      // menu label (the debug window)
const char* menuName(Language l);         // the options' language row
Language parseLanguage(std::string_view s); // unknown -> Japanese
// the language OpenDash's own texts (the options, the vector UI's words, the labels) are shown in: a pack translated
// from the Japanese uses the texts of the language it is in. Always Japanese, English or Portuguese (the tables of
// the options' words): the Latin packs after Portuguese -> English, the PC-format ones -> Japanese (the PC's glyph
// codes), their own words from assets/ui/text/<uiTextCode>.txt where there are some
Language uiLanguage(Language l);
// the code of OpenDash's own texts for the language (assets/ui/text/<code>.txt): its own, the language it is in for
// the packs translated from the Japanese
const char* uiTextCode(Language l);
// a pack in the PC's format (Korean, Chinese): its msg/ and exe/ banks are the PC's with the pack's glyphs
// (charset.bin: u32 code points of font 0's glyphs from kPackGlyphBase on), drawn by the vector font font.txt names
bool pcScript(Language l);
inline constexpr int kPackGlyphBase = 0x400;
// a pack translated from the Japanese (its cutscene subtitles are the ones translated from the Japanese voices too:
// text and subtitles always from the same source)
bool fromJapanese(Language l);
// the options' language menu (Herbert): a text language is a base language (0 Japanese, 1 English, 2 Portuguese) and
// a version of it (0 the release's / the fandub, 1 faithful to the Japanese, 2 faithful with the US names)
// (the bases after Portuguese: 3 Spanish, 4 French, 5 Italian, 6 German -- version 0 from the US English, 1 from the
// Japanese -- 7 Korean, 8 Chinese, one version each; 9 Traditional Chinese (Hong Kong), 10 Russian)
inline constexpr int kLanguageBases = 11, kLanguageVersions = 3;
int languageBase(Language l);
int languageVersion(Language l);
Language languageOf(int base, int version); // Count: no such version

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

// ---- Latin text in the PC game's own .DAT files (the dash2eng fan patch) ----
// That patch puts the PS1 scripts (their 2 px columns, their glyphs as ASCII) in the type-6 banks and a proportional
// Latin font in INIT.DAT's font 0, and keeps the glyph widths in its own exe, which OpenDash does not need.
// The advances of the font `font` (4bpp cells of `cell` x `cell` texels, drawn in a 12 px square) when its letters are
// that font's, flush left in their cells; nullopt for any other font (the PC's own centres them). A letter's advance
// is its ink's right edge; digits and punctuation have OpenDash's own table; everything else 12.
std::optional<std::array<uint8_t, 256>> latinDatWidths(std::span<const uint8_t> font, int cell);
// how far below the line that font's glyph g is drawn (its descenders and low punctuation), in 12 px cell pixels
int latinDatDrop(uint8_t g);

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
