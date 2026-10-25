#include "engine/pc_data_source.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <system_error>

#include "engine/dat.hpp"
#include "engine/exe_coverage.hpp"

namespace od {

namespace {

// ---- where the PC exe keeps its tables (addresses only) ----------------------------------------------------------
struct PcTable {
    TableId id;
    uint32_t va;
};
constexpr PcTable kPcTables[] = {
#include "engine/pc_tables.inc"
};

// the tables the program data holds one of per stage / per room / per layout op and the tables of an index listed by
// hand (pc_indexed_tables.inc)
struct PcRoomTables {
    uint32_t exits, skyGradient, skyGradientEarly, skyLayers, params, floorUvs, cameras, spawns;
};
struct PcStageTables {
    uint32_t animatedClasses, staticClasses, projectileClasses, effectClasses, controllerClasses, encounterZones,
        encounterGroups, encounterField, encounterEnemyGroups, clutAnimations, groundKinds, townRoutes,
        transitionHandlers, voices, secondVoices;
};
#include "engine/pc_room_tables.inc"
constexpr int kStages = int(sizeof kPcRoomTables / sizeof kPcRoomTables[0]);
static_assert(kStages == kStageCount, "a room table row per stage");
static_assert(sizeof kPcStageTables / sizeof kPcStageTables[0] == kStageCount, "a stage table row per stage");

struct PcEncounterLayoutTable {
    uint32_t op, va;
};
#include "engine/pc_encounter_tables.inc"

struct PcIndexedTable {
    TableId id;
    uint32_t index, va;
};
constexpr PcIndexedTable kPcIndexedTables[] = {
#include "engine/pc_indexed_tables.inc"
};

// the per-stage table `id` of the stage's row (nullptr: `id` is no per-stage table)
const uint32_t* stageTable(const PcStageTables& s, TableId id) {
    switch (id) {
    case TableId::StageAnimatedClasses: return &s.animatedClasses;
    case TableId::StageStaticClasses: return &s.staticClasses;
    case TableId::StageProjectileClasses: return &s.projectileClasses;
    case TableId::StageEffectClasses: return &s.effectClasses;
    case TableId::StageControllerClasses: return &s.controllerClasses;
    case TableId::StageEncounterZones: return &s.encounterZones;
    case TableId::StageEncounterGroups: return &s.encounterGroups;
    case TableId::StageEncounterField: return &s.encounterField;
    case TableId::StageEncounterEnemyGroups: return &s.encounterEnemyGroups;
    case TableId::StageClutAnimations: return &s.clutAnimations;
    case TableId::StageGroundKinds: return &s.groundKinds;
    case TableId::StageTownRoutes: return &s.townRoutes;
    case TableId::StageTransitionHandlers: return &s.transitionHandlers;
    case TableId::StageVoices: return &s.voices;
    case TableId::StageSecondVoices: return &s.secondVoices;
    default: return nullptr;
    }
}

constexpr uint32_t kRoomParamsSize = 0x2c, kRoomCameraSize = 12, kRoomPointerSize = 4;

// ---- the function map ------------------------------------
struct PcRoutine {
    uint32_t address;
    RoutineId id;
};
constexpr PcRoutine kPcRoutines[] = {
    {0, RoutineId::None}, // keeps the array non-empty; never matched (a null pointer reads as RoutineRef())
#include "engine/pc_routines.inc"
};

int stageOf(TableKey key) { return int(key.index >> 8); }
uint32_t roomOf(TableKey key) { return key.index & 0xff; }

} // namespace

PcDataSource::PcDataSource(std::filesystem::path datDir, const std::filesystem::path& exe)
    : datDir_(std::move(datDir)), exe_(readFile(exe)) {}

bool PcDataSource::hasFile(const std::string& name) const {
    std::error_code ec;
    return !name.empty() && std::filesystem::is_regular_file(datDir_ / name, ec);
}

GameFile PcDataSource::gameFile(const std::string& name) const {
    if (!hasFile(name)) throw DataSourceError("data source: no game file " + (datDir_ / name).string());
    return pcGameFile(name, readFile(datDir_ / name));
}

uint32_t PcDataSource::locate(TableKey key) const {
    const int stage = stageOf(key);
    const uint32_t room = roomOf(key);
    switch (key.id) {
    case TableId::RoomParams: {
        if (stage < 0 || stage >= kStages) return 0;
        const uint32_t t = kPcRoomTables[stage].params;
        return t ? t + room * kRoomParamsSize : 0;
    }
    case TableId::FloorUvs:
        return key.index < uint32_t(kStages) ? kPcRoomTables[key.index].floorUvs : 0;
    case TableId::RoomCamera: {
        if (stage < 0 || stage >= kStages) return 0;
        const uint32_t t = kPcRoomTables[stage].cameras;
        return t ? t + room * kRoomCameraSize : 0;
    }
    case TableId::RoomExits:
    case TableId::RoomSkyGradient:
    case TableId::RoomSkyGradientEarly:
    case TableId::RoomSkyLayers:
    case TableId::RoomSpawnList: {
        if (stage < 0 || stage >= kStages) return 0;
        const PcRoomTables& r = kPcRoomTables[stage];
        const uint32_t t = key.id == TableId::RoomExits              ? r.exits
                           : key.id == TableId::RoomSkyLayers        ? r.skyLayers
                           : key.id == TableId::RoomSkyGradient      ? r.skyGradient
                           : key.id == TableId::RoomSkyGradientEarly ? r.skyGradientEarly
                                                                     : r.spawns;
        return t ? t + room * kRoomPointerSize : 0;
    }
    case TableId::EncounterLayoutTable:
        for (const PcEncounterLayoutTable& t : kPcEncounterLayoutTables)
            if (t.op == key.index) return t.va;
        return 0;
    default: break;
    }
    if (stageTable(kPcStageTables[0], key.id))
        return key.index < uint32_t(kStages) ? *stageTable(kPcStageTables[key.index], key.id) : 0;
    // the tables of an index listed by hand, then the fixed tables (by id, made once)
    struct ById {
        std::array<uint32_t, kTableIds> fixed{};
        std::array<bool, kTableIds> indexed{};
    };
    static const ById kById = [] {
        ById a;
        for (const PcTable& t : kPcTables) a.fixed[static_cast<uint16_t>(t.id)] = t.va;
        for (const PcIndexedTable& t : kPcIndexedTables) a.indexed[static_cast<uint16_t>(t.id)] = true;
        return a;
    }();
    if (kById.indexed[static_cast<uint16_t>(key.id)]) {
        for (const PcIndexedTable& t : kPcIndexedTables)
            if (t.id == key.id && t.index == key.index) return t.va;
        return 0;
    }
    return key.index == 0 ? kById.fixed[static_cast<uint16_t>(key.id)] : 0;
}

bool PcDataSource::hasTable(TableKey key) const {
    const uint32_t va = locate(key);
    return va != 0 && exe_.sectionOf(va, 1) != nullptr;
}

TableReader PcDataSource::bytesAt(uint32_t va, uint32_t n) const {
    const PeImage::Section* s = exe_.sectionOf(va, n);
    if (!s) throw DataSourceError("data source (PC): no section holds the exe's bytes at the pointer");
    auto& block = sectionBlocks_[s->va];
    if (!block) block = std::make_shared<const std::vector<uint8_t>>(exe_.sectionBytes(*s));
    const uint32_t off = va - s->va;
    if (exe_coverage::enabled()) {
        const uint32_t inFile = off >= s->fileBytes ? 0 : std::min(n, s->fileBytes - off);
        exe_coverage::mark(va, inFile, exe_coverage::kFileRead);
        exe_coverage::mark(va + inFile, n - inFile, exe_coverage::kBssRead);
    }
    return TableReader(*this, block, off, n);
}

TableReader PcDataSource::table(TableKey key, uint32_t bytes) const {
    const uint32_t va = locate(key);
    if (va == 0) throw DataSourceError("data source (PC): no table " + describe(key));
    if (!exe_.sectionOf(va, bytes))
        throw DataSourceError("data source (PC): table " + describe(key) + " runs past the exe's sections");
    return bytesAt(va, bytes);
}

uint32_t PcDataSource::stateAddress(TableKey key) const {
    const uint32_t va = locate(key);
    if (va == 0) throw DataSourceError("data source (PC): no table " + describe(key));
    return va;
}

TableReader PcDataSource::pointee(uint32_t pointer, uint32_t n) const {
    // the bytes the image holds from the pointer, at most n (a list read up to its end marker)
    const PeImage::Section* s = exe_.sectionOf(pointer, 1);
    if (!s) return TableReader(*this, {});
    return bytesAt(pointer, std::min<uint32_t>(n, s->va + s->extent() - pointer));
}

std::string PcDataSource::pointeeString(uint32_t pointer, size_t maxLength) const {
    const TableReader r = pointee(pointer, uint32_t(maxLength));
    std::string s;
    for (size_t i = 0; i < r.size() && r.u8(i) != 0; ++i) s += char(r.u8(i));
    return s;
}

RoutineRef PcDataSource::routineOf(uint32_t pointer) const {
    const auto it = std::lower_bound(std::begin(kPcRoutines) + 1, std::end(kPcRoutines), pointer,
                                     [](const PcRoutine& r, uint32_t a) { return r.address < a; });
    if (it != std::end(kPcRoutines) && it->address == pointer) return RoutineRef(it->id, pointer);
    return RoutineRef(RoutineId::None, pointer);
}

uint32_t PcDataSource::routineStateValue(RoutineId id) const {
    // the map's addresses by routine (0: the map has none)
    static const std::array<uint32_t, kRoutineIds> kAddresses = [] {
        std::array<uint32_t, kRoutineIds> a{};
        for (const PcRoutine& r : kPcRoutines) a[static_cast<uint16_t>(r.id)] = r.address;
        return a;
    }();
    if (id == RoutineId::None) return 0;
    const uint32_t a = kAddresses[static_cast<uint16_t>(id)];
    if (a == 0) throw DataSourceError(std::string("data source (PC): no pointer for the routine ") + routineName(id));
    return a;
}

} // namespace od
