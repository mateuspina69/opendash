#include "engine/ps1_data_source.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

#include "engine/dat.hpp"
#include "engine/file_data.hpp"
#include "engine/game_file.hpp"
#include "engine/ps1_container.hpp"
#include "engine/ps1_exe_banks.hpp"
#include "engine/ps1_menu_file.hpp"

namespace od {

namespace {

void tracePointer(uint32_t pointer, uint32_t bytes) {
    if (pointer == 0 || !std::getenv("OPENDASH_TRACE_DATA")) return;
    std::fprintf(stderr, "PS1 unresolved pointer %08x, %u bytes\n", pointer, bytes);
}

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

struct Ps1Label {
    const char* file;
    uint32_t address;
    uint32_t bytes;
};
using SlotKind = Ps1DataSource::SlotKind;
struct Ps1LabelSlot {
    uint32_t label; // kPs1Labels index
    uint32_t offset;
    SlotKind kind;
    RoutineId routine; // a function's (None: no C++ routine)
    uint32_t state;    // a state pointer's: the engine's address of that state
};
struct Ps1LabelTable {
    TableId id;
    uint32_t index; // the table's (a stage's ...; 0: one of its own)
    uint32_t label; // kPs1Labels index
    bool own;        // the PS1's own: its bytes not the PC's (its slots the PS1 words that point)
    uint32_t stride; // a room array (index: the stage): a table per room, this many bytes apart (0: one table)
};
struct Ps1StateTable {
    TableId id;
    uint32_t index;
    uint32_t address; // the engine's address of it
    uint32_t bytes;
};
#include "engine/ps1_labels.inc"

// SLUS_011.40 (USA): the file table of the load indices, {u32 LBA, u32 bytes, u32 flags} each (0 SYSTEM.CNF,
// 1 SLUS_011.40, 2 COMMON/DEMO.BIN ...: the numbering the PS1's data uses)
constexpr uint32_t kFileTable = 0x80068484;
constexpr size_t kLoadIndices = 0x200;
constexpr uint32_t kVoiceLoadIndices = 0x80069608; // twenty u16 load indices, zero: unused logical voice file
constexpr uint32_t kBuiltInVoices = 0x80069638; // five eight-byte descriptors, XA/COM_18.XA
constexpr uint32_t kBuiltInVoicePrefix = 0x7f00 * 4; // voiceClipPlay indexes with the masked built-in id
struct VoiceTable {
    TableId id;
    uint32_t index;
    const char* file;
    uint32_t address, count;
};
constexpr VoiceTable kVoiceTables[] = {
#include "engine/ps1_voice_tables.inc"
};

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(uint8_t(c)));
    return s;
}

// the PS1 record's byte count of a table whose PS1 records are shorter than the PC's (0: the PC's): the bytes past it
// read as zero -- the room parameters' D3D fog range (+0x24 / +0x28, the PC's), none on the PS1
uint32_t ps1RecordBytes(TableId id) { return id == TableId::RoomParams ? 0x24 : 0; }

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
    std::vector<std::vector<Label::Slot>> labelSlots(std::size(kPs1Labels));
    for (const Ps1LabelSlot& s : kPs1LabelSlots) labelSlots[s.label].push_back({s.offset, s.kind, s.routine, s.state});
    for (size_t i = 0; i < std::size(kPs1Labels); ++i) {
        const Ps1Label& l = kPs1Labels[i];
        // two PC tables on one PS1 table (the PC keeps a copy per use): the longer extent, the slots of both
        Label& at = labels_[l.file][l.address];
        at.common = at.bytes ? std::min(at.common, l.bytes) : l.bytes;
        at.bytes = std::max(at.bytes, l.bytes);
        for (const Label::Slot& slot : labelSlots[i])
            if (std::none_of(at.slots.begin(), at.slots.end(), [&](const Label::Slot& x) { return x.offset == slot.offset; }))
                at.slots.push_back(slot);
    }
    for (const Ps1StateTable& t : kPs1StateTables) {
        Located at;
        at.state = t.address;
        tables_.emplace(TableKey{t.id, t.index}, std::move(at));
    }
    for (const Ps1LabelTable& t : kPs1LabelTables) {
        const Ps1Label& l = kPs1Labels[t.label];
        if (t.id == TableId::StageVoices || t.id == TableId::StageSecondVoices)
            voiceBytes_[{t.id, t.index}] = l.bytes;
        if (t.stride == 0) {
            const TableKey key{t.id, t.index};
            if (const auto it = tables_.find(key); it != tables_.end() && !it->second.made && it->second.file != l.file)
                it->second.copies.push_back({l.file, l.address}); // another copy of the table
            else tables_.emplace(key, Located{size_t(-1), l.file, l.address, false, {}});
            continue;
        }
        for (uint32_t room = 0; room * t.stride < l.bytes; ++room)
            tables_.emplace(TableKey{t.id, roomIndex(int(t.index), int(room))},
                            Located{size_t(-1), l.file, l.address + room * t.stride, false});
    }
    tables_[{TableId::AudioBuiltInVoices, 0}] = {slus_, "SLUS_011.40", kBuiltInVoices, false};
    voiceBytes_[{TableId::AudioBuiltInVoices, 0}] = 5 * 4;
    voiceCounts_[{TableId::AudioBuiltInVoices, 0}] = 5;
    for (const VoiceTable& v : kVoiceTables) {
        tables_.try_emplace(TableKey{v.id, v.index}, Located{size_t(-1), v.file, v.address, false});
        voiceCounts_[{v.id, v.index}] = v.count;
        auto& bytes = voiceBytes_[{v.id, v.index}];
        bytes = std::max(bytes, v.count * 4);
    }
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
    // The disc packs these four independent page containers into one physical file. Give each page its own
    // otherwise unused logical id, so native code can request the page by name without PC load-index constants.
    constexpr const char* menuFiles[] = {"SUBSCN01.DAT", "SUBSCN02.DAT", "SUBSCN03.DAT", "SUBSCN06.DAT"};
    for (size_t i = 0; i < std::size(menuFiles); ++i) {
        const auto id = uint16_t(kLoadIndices - std::size(menuFiles) + i);
        names[id] = menuFiles[i];
        indexOf.emplace(menuFiles[i], id);
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
    // The voice file numbering is separate from DAT load indices. Build its names from the disc's own load
    // table and ISO paths, preserving the holes in the logical numbering.
    voiceFiles_.resize(20);
    std::vector<uint8_t> voiceStrings, voiceNames(0x100 * 4, 0);
    std::vector<uint32_t> voiceOffsets(voiceFiles_.size(), 0);
    for (size_t i = 0; i < voiceFiles_.size(); ++i) {
        const Part& text = parts_[slus_]; // addPart below may reallocate parts_
        const size_t o = size_t(kVoiceLoadIndices - text.base) + i * 2;
        if (o + 2 > text.data->size()) throw DataSourceError("data source (PS1): voice load table past SLUS");
        const uint16_t load = rd16(text.data->data() + o);
        if (!load) continue;
        const size_t entry = size_t(kFileTable - text.base) + size_t(load) * 12;
        if (entry + 12 > text.data->size()) throw DataSourceError("data source (PS1): voice file index past SLUS");
        const auto file = byLba.find(rd32(text.data->data() + entry));
        if (file == byLba.end() || file->second->path.rfind("XA/", 0) != 0)
            throw DataSourceError("data source (PS1): voice load index has no XA file");
        voiceFiles_[i] = file->second->path;
        std::string stem = voiceFiles_[i].substr(3);
        stem.resize(stem.rfind('.'));
        if (stem.rfind("ST", 0) == 0) stem.erase(0, 2);
        const std::string name = "XA" + stem + ".DAT";
        voiceOffsets[i] = uint32_t(voiceStrings.size()) + 1;
        voiceStrings.insert(voiceStrings.end(), name.begin(), name.end());
        voiceStrings.push_back(0);
    }
    const size_t voiceStringPart = addPart("<voice file names>", 0, std::move(voiceStrings));
    for (size_t i = 0; i < voiceOffsets.size(); ++i)
        if (voiceOffsets[i]) put32(voiceNames, i * 4, parts_[voiceStringPart].vbase + voiceOffsets[i] - 1);
    made({TableId::VoiceFileNames, 0}, "voice name table", std::move(voiceNames));
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
    for (const Ps1ExeBank& b : ps1ExeBanks()) {
        made({b.table, 0}, b.name, ps1ExeBank(*disc_, b));
        if (std::string(b.file) == "SLUS_011.40") {
            bankAliases_[{b.file, parts_[slus_].base + b.offset - 0x800}] = b.table;
            continue;
        }
        const auto file = disc_->file(b.file);
        const size_t start = std::string(b.file) == "COMMON/SUBSCN.BIN" ? kPs1MenuCodeOffset : 0;
        if (start > file.size()) throw DataSourceError("PS1 menu: truncated code container");
        for (const Ps1Block& block : ps1Blocks(std::span(file).subspan(start))) {
            const size_t payload = start + block.offset + kPs1BlockHeader;
            if (block.type == kPs1Code && b.offset >= payload && b.offset - payload < block.size)
                bankAliases_[{b.file, block.header[3] + uint32_t(b.offset - payload)}] = b.table;
        }
    }
    made({TableId::MenuBankShopPriceStrings, 0}, "shop prices", ps1ShopPrices(*disc_));
    // The PC keeps the identity matrix in the exe (0x847ac4). It is a constant of the transform math, not game
    // content: the 3x3 diagonal 0x1000 and the padding short the rotation helper copies along with it.
    {
        std::vector<uint8_t> identity(0x14, 0);
        for (const size_t at : {size_t(0), size_t(8), size_t(16)}) {
            identity[at] = 0x00;
            identity[at + 1] = 0x10; // 0x1000, little-endian
        }
        made({TableId::IdentityMatrix, 0}, "identity matrix", std::move(identity));
    }
}

std::string Ps1DataSource::discPath(const std::string& name) const {
    if (isPs1MenuFile(name)) return disc_->find("COMMON/SUBSCN.BIN") ? "COMMON/SUBSCN.BIN" : "";
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
    {
        std::lock_guard lock(lock_);
        loadedAt_[path] = ++loads_;
    }
    if (isPs1MenuFile(name)) return ps1MenuFile(*disc_, name);
    return ps1GameFile(name, disc_->file(path));
}

const std::vector<size_t>& Ps1DataSource::partsOf(const std::string& file) const {
    std::lock_guard lock(lock_);
    if (const auto it = partsByFile_.find(file); it != partsByFile_.end()) return it->second;
    std::vector<size_t> out;
    const auto bytes = disc_->file(file);
    const size_t start = file == "COMMON/SUBSCN.BIN" ? kPs1MenuCodeOffset : 0;
    if (start > bytes.size()) throw DataSourceError("PS1 menu: truncated code container");
    const auto container = std::span(bytes).subspan(start);
    for (const Ps1Block& b : ps1Blocks(container))
        if (b.type == kPs1Code) out.push_back(addPart(file, b.header[3], ps1Payload(container, b)));
    return partsByFile_[file] = std::move(out);
}

std::optional<size_t> Ps1DataSource::partFor(size_t near, uint32_t address, uint32_t n) const {
    auto holds = [&](size_t p) {
        const Part& part = parts_[p];
        return address >= part.base && size_t(address - part.base) + n <= part.data->size();
    };
    if (holds(near)) return near;
    const std::string nearFile = parts_[near].file;
    for (const size_t p : partsOf(nearFile))
        if (p != near && holds(p)) return p;
    if (holds(slus_)) return slus_;
    // SLUS's cursor tree points into the shared sub-screen overlay, loaded separately from the page images.
    if (address >= 0x801d6800u && address < 0x801e4000u)
        for (const size_t p : partsOf("COMMON/SUBSCN.BIN"))
            if (holds(p)) return p;
    return std::nullopt;
}

uint32_t Ps1DataSource::virtualOf(size_t part, uint32_t address, std::optional<Region> region) const {
    std::lock_guard lock(lock_);
    const uint32_t v = parts_[part].vbase + (address - parts_[part].base);
    if (region) regionAt_.emplace(v, *region);
    return v;
}

TableReader Ps1DataSource::regionBytes(size_t part, uint32_t address, uint32_t n, std::optional<Region> region) const {
    // Following a shared-menu pointer can lazily append a part; keep this block stable across vector growth.
    const Part pt = parts_[part];
    const size_t off = address - pt.base;
    const size_t take = off < pt.data->size() ? std::min<size_t>(n, pt.data->size() - off) : 0;
    std::vector<uint8_t> b(pt.data->begin() + std::ptrdiff_t(off), pt.data->begin() + std::ptrdiff_t(off + take));
    const uint8_t* raw = pt.data->data() + off; // the PS1 words (every translation reads them, not b)
    auto dataPointer = [&](size_t o, std::optional<Region> to) {
        const uint32_t p = rd32(raw + o);
        if (p == 0) return;
        for (const std::string& file : {pt.file, std::string("SLUS_011.40"), std::string("COMMON/SUBSCN.BIN")}) {
            const auto bank = bankAliases_.find({file, p});
            if (bank != bankAliases_.end()) {
                put32(b, o, stateAddress({bank->second, 0}));
                return;
            }
        }
        const auto at = partFor(part, p, 1);
        if (!at) {
            if (std::getenv("OPENDASH_TRACE_DATA"))
                std::fprintf(stderr, "PS1 unresolved pointer %08x at %s+%08x+%zx (%s)\n", p, pt.file.c_str(),
                             address, o, region ? describe(region->key).c_str() : "label");
            else
                tracePointer(p, 1);
        }
        put32(b, o, at ? virtualOf(*at, p, to) : 0);
    };
    // the slots of the tables the game reads by address whose extent [address, address + take) meets
    if (const auto f = labels_.find(pt.file); f != labels_.end() && take > 0) {
        auto it = f->second.upper_bound(address + uint32_t(take) - 1);
        while (it != f->second.begin()) {
            --it;
            const uint32_t start = it->first;
            const Label& l = it->second;
            if (start + l.bytes <= address) {
                if (address - start > kMaxLabelBytes) break;
                continue;
            }
            for (const Label::Slot& slot : l.slots) {
                const uint32_t at = start + slot.offset;
                if (at < address || at + 4 > address + take) continue;
                const uint32_t v = rd32(raw + (at - address));
                switch (slot.kind) {
                case SlotKind::Data: dataPointer(at - address, std::nullopt); break;
                case SlotKind::Function:
                    if (v != 0) put32(b, at - address, kRoutineTag | static_cast<uint16_t>(slot.routine));
                    break;
                case SlotKind::Ram: // the engine's address of that PS1 RAM (engine/file_data LoadedFileData)
                    if (v != 0) put32(b, at - address, (v & 0x1fffffffu) + LoadedFileData::kPsxBase);
                    break;
                case SlotKind::State: // the engine's address of that state (game/loaded_data: the game's built data)
                    if (v != 0) put32(b, at - address, slot.state);
                    break;
                }
            }
        }
    }
    if (region) {
        // the function pointers: this source's routine values
        if (const auto it = routineSlots_.find({region->key, region->region}); it != routineSlots_.end())
            for (const auto& [o, routine] : it->second)
                if (o + 4 <= b.size() && rd32(raw + o) != 0) put32(b, o, kRoutineTag | static_cast<uint16_t>(routine));
        // the data pointers: this source's addresses of the PS1 data they point at (null stays null)
        if (const auto it = dataSlots_.find({region->key, region->region}); it != dataSlots_.end())
            for (const auto& [o, child] : it->second)
                if (o + 4 <= b.size()) dataPointer(o, Region{region->key, child});
    }
    return TableReader(*this, std::move(b));
}

bool Ps1DataSource::hasTable(TableKey key) const { return tables_.count(key) != 0; }

size_t Ps1DataSource::voicePart(TableKey key, const Located& table) const {
    std::lock_guard lock(lock_);
    const auto place = placeOf(table);
    if (const auto it = voiceParts_.find(place); it != voiceParts_.end()) return it->second;
    const uint32_t count = voiceCounts_.at(key);
    const uint32_t prefix = key.id == TableId::AudioBuiltInVoices ? kBuiltInVoicePrefix : 0;
    std::vector<uint8_t> converted(prefix + voiceBytes_.at(key), 0);
    for (const size_t p : partsOf(place.first)) {
        const Part& part = parts_[p];
        if (place.second < part.base || place.second - part.base >= part.data->size()) continue;
        const auto block = part.data; // indexing clips/addPart must not invalidate a reference into parts_
        const size_t offset = place.second - part.base;
        for (uint32_t i = 0; i < count; ++i) {
            const size_t at = offset + size_t(i) * 8;
            if (at + 8 > block->size()) throw DataSourceError("data source (PS1): voice descriptors past their block");
            const auto descriptor = std::span<const uint8_t, 8>(block->data() + at, 8);
            const uint8_t file = descriptor[0];
            if (file >= voiceFiles_.size() || voiceFiles_[file].empty())
                throw DataSourceError("data source (PS1): " + describe(key) + " descriptor " + std::to_string(i) +
                                      " names absent XA file " + std::to_string(file));
            const DiscFile& xa = *disc_->find(voiceFiles_[file]);
            auto clips = voiceClips_.find(xa.path);
            if (clips == voiceClips_.end()) clips = voiceClips_.emplace(xa.path, ps1XaClips(*disc_, xa)).first;
            const uint16_t track = ps1XaTrack(descriptor, xa.lba, clips->second);
            const size_t to = prefix + size_t(i) * 4;
            converted[to] = file;
            converted[to + 2] = uint8_t(track);
            converted[to + 3] = uint8_t(track >> 8);
        }
        const size_t made = addPart("<voice descriptors " + place.first + ">", 0, std::move(converted));
        voiceParts_[place] = made;
        return made;
    }
    throw DataSourceError("data source (PS1): voice descriptor list is in no block of " + place.first);
}

TableReader Ps1DataSource::table(TableKey key, uint32_t bytes) const {
    const auto it = tables_.find(key);
    if (it == tables_.end()) throw DataSourceError("data source (PS1): the disc has no table " + describe(key));
    const Located& t = it->second;
    if (t.state) return TableReader(*this, std::vector<uint8_t>(bytes, 0)); // the state's initial bytes
    if (voiceBytes_.count(key)) {
        TableReader r = regionBytes(voicePart(key, t), 0, bytes, std::nullopt);
        if (std::getenv("OPENDASH_TRACE_DATA") && r.size() < bytes)
            std::fprintf(stderr, "PS1 voice table %s: %zu of %u bytes\n", describe(key).c_str(), r.size(), bytes);
        return r;
    }
    if (t.made) { // a made table: as many bytes as asked (zeros past its own)
        TableReader r = regionBytes(t.part, 0, bytes, std::nullopt);
        if (r.size() >= bytes) return r;
        std::vector<uint8_t> b(bytes, 0);
        if (!r.empty()) std::memcpy(b.data(), r.bytes(0, r.size()).data(), r.size());
        return TableReader(*this, std::move(b));
    }
    const uint32_t own = ps1RecordBytes(key.id) ? std::min(bytes, ps1RecordBytes(key.id)) : bytes;
    const auto [file, address] = placeOf(t);
    for (const size_t p : partsOf(file)) {
        const Part& pt = parts_[p];
        if (address >= pt.base && address - pt.base < pt.data->size()) {
            const size_t have = pt.data->size() - (address - pt.base);
            if ((key.id == TableId::CrashLandingScriptBurnRecords || key.id == TableId::TownNpcSpeeds) && have > 0) {
                // Its fourth record is the first bytes of ST3A01.DAT, which the script loads later: keep the
                // overlay's own records and let the rest read as zero until the data file is in.
                std::vector<uint8_t> b(bytes, 0);
                std::memcpy(b.data(), pt.data->data() + (address - pt.base), std::min<size_t>(have, bytes));
                return TableReader(*this, std::move(b));
            }
            if (have >= own) {
                (void)virtualOf(p, address, Region{key, 0});
                TableReader r = regionBytes(p, address, own, Region{key, 0});
                if (own == bytes) return r;
                std::vector<uint8_t> b(bytes, 0);
                std::memcpy(b.data(), r.bytes(0, own).data(), own);
                return TableReader(*this, std::move(b));
            }
        }
    }
    throw DataSourceError("data source (PS1): " + describe(key) + " runs past its block in " + file);
}

uint32_t Ps1DataSource::stateAddress(TableKey key) const {
    const auto it = tables_.find(key);
    if (it == tables_.end()) throw DataSourceError("data source (PS1): the disc has no table " + describe(key));
    const Located& t = it->second;
    if (t.state) return t.state;
    if (voiceBytes_.count(key)) return parts_[voicePart(key, t)].vbase;
    if (t.made) return parts_[t.part].vbase;
    const auto [file, address] = placeOf(t);
    for (const size_t p : partsOf(file)) {
        const Part& pt = parts_[p];
        if (address >= pt.base && address - pt.base < pt.data->size()) return virtualOf(p, address, Region{key, 0});
    }
    throw DataSourceError("data source (PS1): " + describe(key) + " is in no block of " + file);
}

std::pair<std::string, uint32_t> Ps1DataSource::placeOf(const Located& t) const {
    std::pair<std::string, uint32_t> at{t.file, t.address};
    if (t.copies.empty()) return at;
    std::lock_guard lock(lock_);
    auto when = [&](const std::string& f) {
        const auto it = loadedAt_.find(f);
        return it == loadedAt_.end() ? uint64_t(0) : it->second;
    };
    uint64_t best = when(t.file);
    for (const auto& c : t.copies)
        if (when(c.first) > best) best = when(c.first), at = c;
    return at;
}

TableReader Ps1DataSource::pointee(uint32_t pointer, uint32_t n) const {
    // the program's state: its initial bytes (zero), as the PC reads its .bss
    if (pointer >= kEngineState[0] && pointer < kEngineState[1]) return TableReader(*this, std::vector<uint8_t>(n, 0));
    if (pointer < kVirtualBase || pointer >= kRoutineTag) {
        tracePointer(pointer, n);
        return TableReader(*this, {});
    }
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

std::vector<Ps1DataSource::AddressTable> Ps1DataSource::addressTables() {
    std::vector<AddressTable> out;
    std::set<TableKey> seen;
    for (const Ps1LabelTable& t : kPs1LabelTables) {
        if (t.stride == 0) {
            if (seen.insert({t.id, t.index}).second) out.push_back({{t.id, t.index}, kPs1Labels[t.label].bytes, t.own}); // the first copy
            continue;
        }
        for (uint32_t room = 0; room * t.stride < kPs1Labels[t.label].bytes; ++room)
            out.push_back({{t.id, roomIndex(int(t.index), int(room))}, t.stride, t.own});
    }
    for (const VoiceTable& v : kVoiceTables)
        if (seen.insert({v.id, v.index}).second) out.push_back({{v.id, v.index}, v.count * 4, true});
    return out;
}

uint32_t Ps1DataSource::tableBytesFrom(uint32_t pointer) const {
    if (pointer < kVirtualBase || pointer >= kRoutineTag) return 0;
    size_t part;
    {
        std::lock_guard lock(lock_);
        auto it = partByVbase_.upper_bound(pointer);
        if (it == partByVbase_.begin()) return 0;
        part = std::prev(it)->second;
    }
    const Part& pt = parts_[part];
    const uint32_t address = pt.base + (pointer - pt.vbase);
    const auto f = labels_.find(pt.file);
    if (f == labels_.end()) return 0;
    auto it = f->second.upper_bound(address);
    while (it != f->second.begin()) {
        --it;
        if (address - it->first > kMaxLabelBytes) break;
        if (address < it->first + it->second.common) return it->first + it->second.common - address;
    }
    return 0;
}

uint32_t Ps1DataSource::routineStateValue(RoutineId id) const {
    return id == RoutineId::None ? 0 : kRoutineTag | static_cast<uint16_t>(id);
}

} // namespace od
