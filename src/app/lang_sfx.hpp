// The language packs' SFX banks: the PC's type-8 banks carry the Japanese voice barks (fights, shouts); a pack
// replaces the banks whose PS1 copy has its own voices with <pack>/sfx/<STEM>_<arg>_<extra>.bin, the same
// layout. Every SFX bank load goes through here so a language switch can reload the banks in place.
#pragma once

#include "engine/lang_pack.hpp"
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace od::lang_sfx {

// od::port::loadSfxBank of the pack's bank for datName (arg = slot, extra = bank id) if packDir has one, else of
// the PC segment; packDir "" = Japanese
void load(const od::lang::LangPack* pack, std::string_view datName, std::span<const uint8_t> pcSeg,
          uint32_t arg, uint32_t extra);
// the loaded slots again with packDir's banks (language switch)
void reloadAll(const od::lang::LangPack* pack);
// OpenDash (Herbert: the barks' subtitles): the last SFX sample a request started -- its key "<STEM>_<arg>_<extra>_s<k>"
// (k: the bank's sample number, as ferramentas/extrai_falas_sfx.py names the WAVs), when (SDL ns), a count that
// changes with every start
struct Heard {
    std::string key;
    uint64_t ns = 0;
    uint32_t serial = 0;
};
const Heard& lastHeard();

} // namespace od::lang_sfx
