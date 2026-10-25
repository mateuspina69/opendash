// The player's copy of the game (dat/ and dash2.exe): found from opendash.ini / the environment / the default install
// path, or asked for once with the system's folder picker and saved to opendash.ini.
#pragma once

#include <filesystem>

#include "engine/config.hpp"

namespace od::app {

// the game's files at cfg's paths: dat/INIT.DAT, snd/ next to dat/, dash2.exe
bool gameFilesFound(const od::Config& cfg);

// a folder the player picked: the game's (with dat/, snd/ and dash2.exe) or its dat/; false when neither has the files
bool gameFilesAt(const std::filesystem::path& folder, od::Config& cfg);

// cfg's paths when the files are there; else (ask = true) explains what OpenDash needs and lets the player pick the
// game's folder until it has the files (saved as dat_dir= / exe= in opendash.ini). false: not found / the player quit.
bool ensureGameFiles(od::Config& cfg, bool ask);

} // namespace od::app
