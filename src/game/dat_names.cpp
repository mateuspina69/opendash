// The DAT file names of the load indices (see dat_names.hpp).
#include "game/dat_names.hpp"

namespace od {

namespace {
const DataTable<DatFileNames> datFileNames{TableId::DatFileNames};
} // namespace

DatFileNames DatFileNames::load(const TableReader& r) {
    DatFileNames t;
    for (size_t i = 0; i < kCount; ++i) t.names[i] = r.followString(i * 4, kMaxNameLength);
    return t;
}

std::string datFileName(uint16_t index) {
    if (index >= DatFileNames::kCount) tableIndexError(index, DatFileNames::kCount);
    return datFileNames->names[index];
}

uint16_t datFileIndex(std::string_view name) {
    const DatFileNames& t = *datFileNames;
    for (size_t i = 0; i < DatFileNames::kCount; ++i)
        if (t.names[i] == name) return uint16_t(i);
    throw DataSourceError("data source: no load index names " + std::string(name));
}

} // namespace od
