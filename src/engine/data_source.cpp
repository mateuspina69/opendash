#include "engine/data_source.hpp"

#include <atomic>
#include <cstdio>

namespace od {

namespace {
const DataSource* g_bound = nullptr;
std::atomic<uint64_t> g_serials{0};
std::string g_debugTableName;
} // namespace

std::string& debugTableName() { return g_debugTableName; }

std::string describe(TableKey key) {
    char b[24];
    std::snprintf(b, sizeof b, "[0x%x]", key.index);
    return std::string(tableName(key.id)) + b;
}

RoutineId RoutineRef::require(const char* area, const char* what) const {
    if (!mapped() && !null()) {
        char b[24];
        std::snprintf(b, sizeof b, " 0x%x", pointer_);
        throw std::logic_error(std::string(area) + ": not ported: " + what + b);
    }
    return id_;
}

TableReader TableReader::sub(size_t o, size_t n) const {
    at(o, n);
    TableReader r = *this;
    r.offset_ = offset_ + o;
    r.size_ = n;
    return r;
}

TableReader TableReader::follow(size_t o, uint32_t n) const {
    const uint32_t p = u32(o);
    return p == 0 ? TableReader() : source_->pointee(p, n);
}

std::string TableReader::followString(size_t o, size_t maxLength) const {
    const uint32_t p = u32(o);
    return p == 0 ? std::string() : source_->pointeeString(p, maxLength);
}

RoutineRef TableReader::routine(size_t o) const {
    const uint32_t p = u32(o);
    return p == 0 ? RoutineRef() : source_->routineOf(p);
}

const uint8_t* TableReader::at(size_t o, size_t n) const {
    if (o + n > size_ || !bytes_) {
        char b[160];
        std::snprintf(b, sizeof b, "data source: a read of %zu bytes at +0x%zx of a %zu-byte table (%s)", n, o, size_,
                      g_debugTableName.empty() ? "no table" : g_debugTableName.c_str());
        throw DataSourceError(b);
    }
    return bytes_->data() + offset_ + o;
}

DataSource::DataSource() : serial_(++g_serials) {}

namespace {
std::vector<TableUse>& uses() {
    static std::vector<TableUse> v; // constructed on the first handle's registration (static initialisation order)
    return v;
}
} // namespace

const std::vector<TableUse>& tableUses() { return uses(); }
void registerTableUse(const TableUse& use) { uses().push_back(use); }

const DataSource& dataSource() {
    if (!g_bound) throw DataSourceError("data source: none bound (the host binds one before the game runs)");
    return *g_bound;
}

bool dataSourceBound() { return g_bound != nullptr; }

void bindDataSource(const DataSource* s) { g_bound = s; }

ScopedDataSource::ScopedDataSource(const DataSource& s) : previous_(g_bound) { g_bound = &s; }
ScopedDataSource::~ScopedDataSource() { g_bound = previous_; }

void tableIndexError(size_t index, size_t count) {
    char b[96];
    std::snprintf(b, sizeof b, "data source: index %zu of a %zu-entry table", index, count);
    throw DataSourceError(b);
}

void tableIndexError(const std::string& table, size_t index, size_t count) {
    char b[160];
    std::snprintf(b, sizeof b, "data source: index %zu of the %zu-entry table %s", index, count, table.c_str());
    throw DataSourceError(b);
}

} // namespace od
