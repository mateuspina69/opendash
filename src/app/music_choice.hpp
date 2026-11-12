// The music choice (OpenDash's own, PC data): dat_dir's music (the extended versions made for this install) or the
// originals'. The originals are full .DAT files of the same names in a folder next to dat_dir
// (<dat_dir>/../musicas-originais); only their music parts are used, the rest of each file is still dat_dir's. The
// choice takes effect at the next music load (the next room / the title screen). extended_music= 0|1 in opendash.ini.
#pragma once

#include <filesystem>
#include <string>

#include "engine/game_file.hpp"

namespace od::music_choice {

void configure(bool extended, std::filesystem::path originalsDir);
void setExtended(bool extended);
bool extended();
// whether there is an original to switch to (the folder holds files)
bool available();
// the file whose music parts to load for `name`: the original's with the extended music off when the folder holds
// one, else `file` itself (the caller's copy from the data source)
const GameFile& musicOf(const std::string& name, const GameFile& file, GameFile& storage);

} // namespace od::music_choice
