// A language pack's files by the pack layout's names (engine/lang: msg/<STEM>.bin, font.bin, widths.bin,
// colours.bin, buttons.bin, exe/<bank>.bin, art/, sfx/, snd/), whichever source holds them:
//   * a pack folder (<root>/<code>/);
//   * the USA PS1 disc image itself: the message banks (the 0x12 block of DAT/<STEM>.BIN), the font page (the first
//     image of COMMON/INIT.BIN), the glyph widths and the width limit (SLUS_011.40), the text and button CLUTs (the
//     CLUT ids in SLUS_011.40, their colours in INIT.BIN's VRAM), the PS1 copies of the banks the PC keeps in dash2.exe
//     (item names, menus, the shops, the memory card screens: SLUS_011.40, SUBSCN.BIN, DEMO.BIN and the stage
//     overlays), the voices (the clips of XA/*.XA: per channel, up to the sector that ends the clip, in (channel,
//     first sector) order = the PC voice file's track order). The art and the SFX banks are not read from the disc
//     yet: the disc source has none.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "engine/disc_image.hpp"

namespace od::lang {

class LangPack {
public:
    virtual ~LangPack() = default;
    // the source's identity (a folder path, the disc image's cue): a font / bank loaded from it stays valid while
    // this does not change
    virtual std::string id() const = 0;
    virtual bool has(const std::string& name) const = 0;
    // the file's bytes; throws std::runtime_error when the pack has none
    virtual std::vector<uint8_t> file(const std::string& name) const = 0;
    // the folder of the pack's streamed / per-file data (snd/, snd_restored/, art/, sfx/); "" for a source without one
    virtual std::filesystem::path folder() const = 0;
    // voice track `track` of the PC voice file `file` ("xa02_37.dat") as an ODXA track (engine/lang parseXaTrack),
    // for a source without a folder (the disc: the clips of XA/ST02_37.XA); empty when there is none. Thread safe.
    virtual std::vector<uint8_t> voiceTrack(const std::string& file, uint32_t track) const = 0;
};

std::unique_ptr<LangPack> folderPack(std::filesystem::path dir);
// throws std::runtime_error when the disc is not the USA release (its boot file is not SLUS_011.40)
std::unique_ptr<LangPack> discPack(std::shared_ptr<const DiscImage> disc, std::string id);

} // namespace od::lang
