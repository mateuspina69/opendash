// The hash of the player's game files, exchanged by netplay's handshake: ghosts are posed from each receiver's own
// data by id and table index, so a different dash2.exe or .DAT set would animate them wrong with no error; the host
// refuses a client whose files differ, naming which. The language packs (opendash/lang) are not game files and are
// not read: they only change text.
#pragma once

#include <cstdint>
#include <filesystem>

namespace od {

struct GameFilesHash {
    uint64_t exe = 0; // dash2.exe's bytes
    uint64_t all = 0; // dash2.exe and every *.DAT of the data directory (names, sizes and bytes, by name)
};

// Reads every file once (about 240 MB: computed once at startup, only when netplay is on); throws
// std::runtime_error naming a file that cannot be read
GameFilesHash gameFilesHash(const std::filesystem::path& datDir, const std::filesystem::path& exe);

} // namespace od
