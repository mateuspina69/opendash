// The player's copy of the game (PC install or USA PS1 disc): found from opendash.ini / the environment / the default install
// path, or asked for once with the system's folder picker and saved to opendash.ini.
#pragma once

#include <filesystem>

#include "engine/config.hpp"

namespace od::app {

// The selected source's files: PC dat/INIT.DAT, snd/ and dash2.exe; data=disc: a readable USA .cue and its tracks.
bool gameFilesFound(const od::Config& cfg);

// a folder the player picked: the game's (with dat/, snd/ and dash2.exe) or its dat/; false when neither has the files
bool gameFilesAt(const std::filesystem::path& folder, od::Config& cfg);

// cfg's paths when the files are there; else (ask = true) explains what OpenDash needs and lets the player pick the
// game's folder (PC) or .cue file (disc) until it has the files (saved in opendash.ini). false: not found / the player quit.
bool ensureGameFiles(od::Config& cfg, bool ask);

} // namespace od::app
