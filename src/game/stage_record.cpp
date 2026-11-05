// A stage's shared state (see stage_record.hpp).
#include "game/stage_record.hpp"

#include "game/encounter.hpp"

namespace od::port {

namespace {
std::optional<StageRecord> g_starting;

void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}
uint32_t get32(std::span<const uint8_t> b, size_t at) {
    return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24;
}
} // namespace

std::vector<uint8_t> encodeStageRecord(const StageRecord& r) {
    std::vector<uint8_t> out;
    out.reserve(kStageRecordBytes);
    for (const uint32_t w : r.defeated) put32(out, w);
    out.push_back(r.fieldLevel);
    put32(out, r.control);
    put32(out, r.zoneTables);
    put32(out, r.groupTables);
    return out;
}

std::optional<StageRecord> decodeStageRecord(std::span<const uint8_t> b) {
    if (b.size() != kStageRecordBytes) return std::nullopt;
    StageRecord r;
    for (size_t i = 0; i < r.defeated.size(); ++i) r.defeated[i] = get32(b, i * 4);
    r.fieldLevel = b[16];
    r.control = get32(b, 17);
    r.zoneTables = get32(b, 21);
    r.groupTables = get32(b, 25);
    return r;
}

StageRecord mergeStageRecord(const StageRecord& held, const StageRecord& changed) {
    StageRecord r = changed;
    for (size_t i = 0; i < r.defeated.size(); ++i) r.defeated[i] |= held.defeated[i];
    return r;
}

StageRecord captureStageRecord() {
    const EncounterState& e = encounterState();
    StageRecord r;
    for (size_t i = 0; i < r.defeated.size(); ++i) r.defeated[i] = e.defeated[i];
    if (const int8_t field = e.slotOfKind[uint32_t(EncounterKind::Field)]; field >= 0) r.fieldLevel = e.slots[uint8_t(field)].level;
    r.control = e.control;
    r.zoneTables = e.zoneTables;
    r.groupTables = e.groupTables;
    return r;
}

void applyStageRecord(const StageRecord& r) {
    EncounterState& e = encounterState();
    for (size_t i = 0; i < r.defeated.size(); ++i) e.defeated[i] |= r.defeated[i];
    if (const int8_t field = e.slotOfKind[uint32_t(EncounterKind::Field)]; field >= 0) e.slots[uint8_t(field)].level = r.fieldLevel;
    e.control = r.control;
    e.zoneTables = r.zoneTables;
    e.groupTables = r.groupTables;
}

void setStartingStageRecord(const StageRecord& r) { g_starting = r; }
bool hasStartingStageRecord() { return g_starting.has_value(); }
void applyStartingStageRecord() {
    if (g_starting) applyStageRecord(*g_starting);
}

} // namespace od::port
