// The game's files by name, each read once from the bound data source (the files do not change while OpenDash
// runs): the player files that every room rebuild and every ghost look and ghost pose read.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "engine/game_file.hpp"

namespace od::app {

class DatCache {
public:
    // the file (read on first use; the reference stays valid for the cache's life); throws what the data source
    // throws for a missing file
    const od::GameFile& file(const std::string& name);

private:
    std::map<std::string, od::GameFile> files_;
};

// the app's cache (over od::dataSource())
DatCache& datCache();

} // namespace od::app
