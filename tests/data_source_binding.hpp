// The tests' data source (engine/pc_data_source): the PC source over the user's install (DAT/ next to dash2.exe, as
// OPENDASH_DAT_DIR / OPENDASH_EXE default to), bound for the whole process like the app binds it.
#pragma once

#include <filesystem>

#include "engine/data_source.hpp"
#include "engine/pc_data_source.hpp"

namespace od {

inline const PcDataSource& bindTestDataSource(const std::filesystem::path& exe) {
    static const PcDataSource source(exe.parent_path() / "dat", exe);
    bindDataSource(&source);
    return source;
}

} // namespace od
