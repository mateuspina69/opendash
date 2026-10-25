// The DAT files read once by name (see dat_cache.hpp).
#include "app/dat_cache.hpp"

#include "engine/data_source.hpp"

namespace od::app {

const od::GameFile& DatCache::file(const std::string& name) {
    auto it = files_.find(name);
    if (it == files_.end()) it = files_.emplace(name, od::dataSource().gameFile(name)).first;
    return it->second;
}

DatCache& datCache() {
    static DatCache cache;
    return cache;
}

} // namespace od::app
