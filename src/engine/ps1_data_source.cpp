#include "engine/ps1_data_source.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "engine/dat.hpp"
#include "engine/game_file.hpp"
#include "engine/ps1_container.hpp"
#include "engine/ps1_exe_banks.hpp"

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

// SLUS_011.40 (USA): the file table of the load indices, {u32 LBA, u32 bytes, u32 flags} each
constexpr uint32_t kFileTable = 0x8006849c;
constexpr size_t kLoadIndices = 0x200;

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
    slus_ = addPart("SLUS_011.40", tAddr, std::vector<uint8_t>(exe.begin() + 0x800, exe.begin() + 0x800 + tSize));
    partsByFile_["SLUS_011.40"] = {slus_};
    for (const Ps1Table& t : kPs1Tables) tables_[{t.id, t.index}] = {size_t(-1), t.file, t.address, false};
    for (const TableRegion& r : kTableRegions) dataSlots_[{{r.id, r.index}, r.parent}].push_back({r.offset, r.region});
    for (const TableRoutineSlot& s : kTableRoutineSlots)
        routineSlots_[{{s.id, s.index}, s.region}].push_back({s.offset, s.routine});
    makeTables();
}

size_t Ps1DataSource::addPart(std::string file, uint32_t base, std::vector<uint8_t> data) const {
    std::lock_guard lock(lock_);
    const size_t i = parts_.size();
    const uint32_t size = uint32_t(data.size());
    parts_.push_back({std::move(file), base, nextVbase_, std::make_shared<const std::vector<uint8_t>>(std::move(data))});
    partByVbase_[nextVbase_] = i;
    nextVbase_ += (size + 0x1fffu) & ~0xfffu; // a gap after every part: an offset past one never lands in the next
    if (nextVbase_ >= kRoutineTag) throw DataSourceError("data source (PS1): out of address space");
    return i;
}

void Ps1DataSource::makeTables() {
    auto made = [&](TableKey key, std::string what, std::vector<uint8_t> bytes) {
        tables_[key] = {addPart("<" + what + ">", 0, std::move(bytes)), {}, 0, true};
    };
    // the load indices' DAT names: SLUS_011.40's file table, each entry's file named by the ISO directory (a .BIN as
    // the game names it: ST09.BIN -> "ST09.DAT")
    std::map<uint32_t, const DiscFile*> byLba;
    for (const DiscFile& f : disc_->files()) byLba[f.lba] = &f;
    const Part& slus = parts_[slus_];
    std::vector<std::string> names(kLoadIndices);
    std::map<std::string, uint16_t> indexOf;
    for (size_t i = 0; i < kLoadIndices; ++i) {
        const size_t o = size_t(kFileTable - slus.base) + 12 * i;
        if (o + 12 > slus.data->size()) break;
        const uint32_t lba = rd32(slus.data->data() + o);
        if (lba == 0) break;
        const auto f = byLba.find(lba);
        if (f == byLba.end()) break;
        const std::string& path = f->second->path;
        if (path.size() > 4 && path.compare(path.size() - 4, 4, ".BIN") == 0) {
            const size_t slash = path.rfind('/');
            names[i] = path.substr(slash + 1, path.size() - slash - 5) + ".DAT";
            indexOf.emplace(names[i], uint16_t(i));
        }
    }
    std::vector<uint8_t> strings;
    std::vector<uint32_t> at(kLoadIndices, 0);
    for (size_t i = 0; i < kLoadIndices; ++i) {
        if (names[i].empty()) continue;
        at[i] = uint32_t(strings.size()) + 1;
        strings.insert(strings.end(), names[i].begin(), names[i].end());
        strings.push_back(0);
    }
    const size_t stringPart = addPart("<DAT names>", 0, std::move(strings));
    std::vector<uint8_t> nameTable(4 * kLoadIndices, 0);
    for (size_t i = 0; i < kLoadIndices; ++i)
        if (at[i]) put32(nameTable, 4 * i, parts_[stringPart].vbase + at[i] - 1);
    made({TableId::DatFileNames, 0}, "DAT name table", std::move(nameTable));
    // the stages' geometry / texture files: ST%02X / ST%02XT
    for (const auto& [id, suffix] : {std::pair{TableId::StageGeometryFiles, ""}, std::pair{TableId::StageTextureFiles, "T"}}) {
        std::vector<uint8_t> ids(2 * size_t(kStageCount), 0);
        for (int s = 0; s < kStageCount; ++s) {
            char n[16];
            std::snprintf(n, sizeof n, "ST%02X%s.DAT", s, suffix);
            if (const auto it = indexOf.find(n); it != indexOf.end()) {
                ids[2 * size_t(s)] = uint8_t(it->second);
                ids[2 * size_t(s) + 1] = uint8_t(it->second >> 8);
            }
        }
        made({id, 0}, suffix[0] ? "stage texture files" : "stage geometry files", std::move(ids));
    }
    // the banks the PC keeps in dash2.exe: their PS1 copies
    for (const Ps1ExeBank& b : ps1ExeBanks()) made({b.table, 0}, b.name, ps1ExeBank(*disc_, b));
    made({TableId::MenuBankShopPriceStrings, 0}, "shop prices", ps1ShopPrices(*disc_));
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
    std::vector<size_t> out;
    const auto bytes = disc_->file(file);
    for (const Ps1Block& b : ps1Blocks(bytes))
        if (b.type == kPs1Code) out.push_back(addPart(file, b.header[3], ps1Payload(bytes, b)));
    return partsByFile_[file] = std::move(out);
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

uint32_t Ps1DataSource::virtualOf(size_t part, uint32_t address, std::optional<Region> region) const {
    std::lock_guard lock(lock_);
    const uint32_t v = parts_[part].vbase + (address - parts_[part].base);
    if (region) regionAt_.emplace(v, *region);
    return v;
}

TableReader Ps1DataSource::regionBytes(size_t part, uint32_t address, uint32_t n, std::optional<Region> region) const {
    const Part& pt = parts_[part];
    const size_t off = address - pt.base;
    const size_t take = off < pt.data->size() ? std::min<size_t>(n, pt.data->size() - off) : 0;
    std::vector<uint8_t> b(pt.data->begin() + std::ptrdiff_t(off), pt.data->begin() + std::ptrdiff_t(off + take));
    if (region) {
        // the function pointers: this source's routine values
        if (const auto it = routineSlots_.find({region->key, region->region}); it != routineSlots_.end())
            for (const auto& [o, routine] : it->second)
                if (o + 4 <= b.size()) put32(b, o, kRoutineTag | static_cast<uint16_t>(routine));
        // the data pointers: this source's addresses of the PS1 data they point at (null stays null)
        if (const auto it = dataSlots_.find({region->key, region->region}); it != dataSlots_.end())
            for (const auto& [o, child] : it->second) {
                if (o + 4 > b.size()) continue;
                const uint32_t p = rd32(b.data() + o);
                if (p == 0) continue;
                const auto at = partFor(part, p, 1);
                put32(b, o, at ? virtualOf(*at, p, Region{region->key, child}) : 0);
            }
    }
    return TableReader(*this, std::move(b));
}

bool Ps1DataSource::hasTable(TableKey key) const { return tables_.count(key) != 0; }

TableReader Ps1DataSource::table(TableKey key, uint32_t bytes) const {
    const auto it = tables_.find(key);
    if (it == tables_.end()) throw DataSourceError("data source (PS1): the disc has no table " + describe(key));
    const Located& t = it->second;
    if (t.made) { // a made table: as many bytes as asked (zeros past its own)
        TableReader r = regionBytes(t.part, 0, bytes, std::nullopt);
        if (r.size() >= bytes) return r;
        std::vector<uint8_t> b(bytes, 0);
        if (!r.empty()) std::memcpy(b.data(), r.bytes(0, r.size()).data(), r.size());
        return TableReader(*this, std::move(b));
    }
    for (const size_t p : partsOf(t.file)) {
        const Part& pt = parts_[p];
        if (t.address >= pt.base && size_t(t.address - pt.base) + bytes <= pt.data->size()) {
            (void)virtualOf(p, t.address, Region{key, 0});
            return regionBytes(p, t.address, bytes, Region{key, 0});
        }
    }
    throw DataSourceError("data source (PS1): " + describe(key) + " runs past its block in " + t.file);
}

uint32_t Ps1DataSource::stateAddress(TableKey key) const {
    const auto it = tables_.find(key);
    if (it == tables_.end()) throw DataSourceError("data source (PS1): the disc has no table " + describe(key));
    const Located& t = it->second;
    if (t.made) return parts_[t.part].vbase;
    for (const size_t p : partsOf(t.file)) {
        const Part& pt = parts_[p];
        if (t.address >= pt.base && t.address - pt.base < pt.data->size()) return virtualOf(p, t.address, Region{key, 0});
    }
    throw DataSourceError("data source (PS1): " + describe(key) + " is in no block of " + t.file);
}

TableReader Ps1DataSource::pointee(uint32_t pointer, uint32_t n) const {
    if (pointer < kVirtualBase || pointer >= kRoutineTag) return TableReader(*this, {});
    size_t part;
    std::optional<Region> region;
    {
        std::lock_guard lock(lock_);
        auto it = partByVbase_.upper_bound(pointer);
        if (it == partByVbase_.begin()) return TableReader(*this, {});
        part = std::prev(it)->second;
        if (const auto r = regionAt_.find(pointer); r != regionAt_.end()) region = r->second;
    }
    const Part& pt = parts_[part];
    const uint32_t off = pointer - pt.vbase;
    if (off >= pt.data->size()) return TableReader(*this, {});
    return regionBytes(part, pt.base + off, n, region);
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
