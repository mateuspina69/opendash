// The language packs' SFX banks: the PC's type-8 banks carry the Japanese voice barks (fights, shouts); a pack
// replaces the banks whose PS1 copy has its own voices with <pack>/sfx/<STEM>_<arg>_<extra>.bin, the same
// layout. Every SFX bank load goes through here so a language switch can reload the banks in place.
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace od::lang_sfx {

// od::port::loadSfxBank of the pack's bank for datName (arg = slot, extra = bank id) if packDir has one, else of
// the PC segment; packDir "" = Japanese
void load(const std::filesystem::path& packDir, std::string_view datName, std::span<const uint8_t> pcSeg,
          uint32_t arg, uint32_t extra);
// the loaded slots again with packDir's banks (language switch)
void reloadAll(const std::filesystem::path& packDir);

} // namespace od::lang_sfx
