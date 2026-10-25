#include "engine/ps1_data_source.hpp"

#include <cctype>
#include <cstring>
#include <optional>

#include "engine/dat.hpp"
#include "engine/game_file.hpp"
#include "engine/ps1_container.hpp"

namespace od {

namespace {

struct Ps1Table {
    TableId id;
    uint32_t index;
    const char* file;
    uint32_t address;
};
constexpr Ps1Table kPs1Tables[] = {
#include "engine/ps1_tables.inc"
};

struct TableRegion {
    TableId id;
    uint32_t index;
    int region, parent;
    uint32_t offset;
};
struct TableRoutineSlot {
    TableId id;
    uint32_t index;
    int region;
    uint32_t offset;
    RoutineId routine;
};
#include "engine/table_slots.inc"

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(uint8_t(c)));
    return s;
}

void put32(std::vector<uint8_t>& b, size_t o, uint32_t v) {
    for (int k = 0; k < 4; ++k) b[o + size_t(k)] = uint8_t(v >> (8 * k));
}

} // namespace

Ps1DataSource::Ps1DataSource(std::shared_ptr<const DiscImage> disc) : disc_(std::move(disc)) {
    const auto cnf = disc_->file("SYSTEM.CNF");
    if (std::string(cnf.begin(), cnf.end()).find("SLUS_011.40") == std::string::npos)
        throw std::runtime_error("PS1 data source: the disc is not the USA release (no SLUS_011.40 boot)");
    const auto exe = disc_->file("SLUS_011.40");
    if (exe.size() < 0x800 || std::memcmp(exe.data(), "PS-X EXE", 8) != 0)
        throw std::runtime_error("PS1 data source: SLUS_011.40 is not a PS-X EXE");
    const uint32_t tAddr = rd32(exe.data() + 0x18), tSize = rd32(exe.data() + 0x1c);
    if (0x800 + size_t(tSize) > exe.size()) throw std::runtime_error("PS1 data source: SLUS_011.40 text past the file");
    parts_.push_back({"SLUS_011.40", tAddr,
                      std::make_shared<const std::vector<uint8_t>>(exe.begin() + 0x800, exe.begin() + 0x800 + tSize)});
    partsByFile_["SLUS_011.40"] = {0};
    slus_ = 0;
    for (const Ps1Table& t : kPs1Tables) tables_[{t.id, t.index}] = {t.file, t.address};
    for (const TableRegion& r : kTableRegions) dataSlots_[{{r.id, r.index}, r.parent}].push_back({r.offset, r.region});
    for (const TableRoutineSlot& s : kTableRoutineSlots)
        routineSlots_[{{s.id, s.index}, s.region}].push_back({s.offset, s.routine});
}

std::string Ps1DataSource::discPath(const std::string& name) const {
    std::string stem = upper(name);
    if (const size_t dot = stem.rfind('.'); dot != std::string::npos) stem.resize(dot);
    for (const char* dir : {"DAT/", "COMMON/"})
        if (const DiscFile* f = disc_->find(dir + stem + ".BIN")) return f->path;
    return {};
}

bool Ps1DataSource::hasFile(const std::string& name) const { return !name.empty() && !discPath(name).empty(); }

GameFile Ps1DataSource::gameFile(const std::string& name) const {
    const std::string path = discPath(name);
    if (path.empty()) throw DataSourceError("data source (PS1): no game file " + name + " on the disc");
    return ps1GameFile(name, disc_->file(path));
}

const std::vector<size_t>& Ps1DataSource::partsOf(const std::string& file) const {
    std::lock_guard lock(lock_);
    if (const auto it = partsByFile_.find(file); it != partsByFile_.end()) return it->second;
    std::vector<size_t>& out = partsByFile_[file];
    const auto bytes = disc_->file(file);
    for (const Ps1Block& b : ps1Blocks(bytes)) {
        if (b.type != kPs1Code) continue;
        out.push_back(parts_.size());
        parts_.push_back({file, b.header[3], std::make_shared<const std::vector<uint8_t>>(ps1Payload(bytes, b))});
    }
    return out;
}

std::optional<size_t> Ps1DataSource::partFor(size_t near, uint32_t address, uint32_t n) const {
    auto holds = [&](size_t p) {
        const Part& part = parts_[p];
        return address >= part.base && size_t(address - part.base) + n <= part.data->size();
    };
    if (holds(near)) return near;
    for (const size_t p : partsOf(parts_[near].file))
        if (p != near && holds(p)) return p;
    if (holds(slus_)) return slus_;
    return std::nullopt;
}

bool Ps1DataSource::hasTable(TableKey key) const { return tables_.count(key) != 0; }

uint32_t Ps1DataSource::handleOf(const Handle& h) const {
    std::lock_guard lock(lock_);
    if (const auto it = handleIndex_.find(h); it != handleIndex_.end()) return kDataTag | it->second;
    const uint32_t n = uint32_t(handles_.size());
    if (n >= 0x1fffffffu) throw DataSourceError("data source (PS1): out of data handles");
    handles_.push_back(h);
    handleIndex_[h] = n;
    return kDataTag | n;
}

TableReader Ps1DataSource::regionBytes(const Handle& h, uint32_t n) const {
    const Part& part = parts_[h.part];
    const size_t off = h.address - part.base;
    const size_t take = std::min<size_t>(n, part.data->size() - off);
    std::vector<uint8_t> b(part.data->begin() + std::ptrdiff_t(off), part.data->begin() + std::ptrdiff_t(off + take));
    // the function pointers: this source's routine values
    if (const auto it = routineSlots_.find({h.key, h.region}); it != routineSlots_.end())
        for (const auto& [o, routine] : it->second)
            if (o + 4 <= b.size()) put32(b, o, kRoutineTag | static_cast<uint16_t>(routine));
    // the data pointers: handles to the PS1 data they point at (null stays null)
    if (const auto it = dataSlots_.find({h.key, h.region}); it != dataSlots_.end())
        for (const auto& [o, child] : it->second) {
            if (o + 4 > b.size()) continue;
            const uint32_t p = rd32(b.data() + o);
            if (p == 0) continue;
            const auto at = partFor(h.part, p, 1);
            put32(b, o, at ? handleOf({h.key, child, *at, p}) : 0);
        }
    return TableReader(*this, std::move(b));
}

TableReader Ps1DataSource::table(TableKey key, uint32_t bytes) const {
    const auto it = tables_.find(key);
    if (it == tables_.end()) throw DataSourceError("data source (PS1): the disc has no table " + describe(key));
    const std::vector<size_t>& ps = partsOf(it->second.file);
    std::optional<size_t> part;
    for (const size_t p : ps)
        if (!part) {
            const Part& pt = parts_[p];
            if (it->second.address >= pt.base && size_t(it->second.address - pt.base) + bytes <= pt.data->size())
                part = p;
        }
    if (!part) throw DataSourceError("data source (PS1): " + describe(key) + " runs past its block in " + it->second.file);
    return regionBytes({key, 0, *part, it->second.address}, bytes);
}

uint32_t Ps1DataSource::stateAddress(TableKey key) const {
    const auto it = tables_.find(key);
    if (it == tables_.end()) throw DataSourceError("data source (PS1): the disc has no table " + describe(key));
    for (const size_t p : partsOf(it->second.file)) {
        const Part& pt = parts_[p];
        if (it->second.address >= pt.base && it->second.address - pt.base < pt.data->size())
            return handleOf({key, 0, p, it->second.address});
    }
    throw DataSourceError("data source (PS1): " + describe(key) + " is in no block of " + it->second.file);
}

TableReader Ps1DataSource::pointee(uint32_t pointer, uint32_t n) const {
    if ((pointer & 0xe0000000u) != kDataTag) return TableReader(*this, {});
    Handle h;
    {
        std::lock_guard lock(lock_);
        const uint32_t i = pointer & 0x1fffffffu;
        if (i >= handles_.size()) return TableReader(*this, {});
        h = handles_[i];
    }
    return regionBytes(h, n);
}

std::string Ps1DataSource::pointeeString(uint32_t pointer, size_t maxLength) const {
    const TableReader r = pointee(pointer, uint32_t(maxLength));
    std::string s;
    for (size_t i = 0; i < r.size() && r.u8(i) != 0; ++i) s += char(r.u8(i));
    return s;
}

RoutineRef Ps1DataSource::routineOf(uint32_t pointer) const {
    if ((pointer & 0xffff0000u) == kRoutineTag) return RoutineRef(RoutineId(pointer & 0xffffu), pointer);
    return RoutineRef(RoutineId::None, pointer);
}

uint32_t Ps1DataSource::routineStateValue(RoutineId id) const {
    return id == RoutineId::None ? 0 : kRoutineTag | static_cast<uint16_t>(id);
}

} // namespace od
