// What a stage's worlds share while someone is in the stage: the enemies defeated, the field
// encounter's level, the encounters' suspend bits and the encounter layout drawn at the stage's load. One process keeps these
// across its room changes; separate worlds keep them here. The coordinator drops a stage's record when the stage empties,
// so the next player in finds the enemies back, as a stage change does in the game.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace od::port {

struct StageRecord {
    std::array<uint32_t, 4> defeated{};
    uint8_t fieldLevel = 0;
    uint32_t control = 0;
    uint32_t zoneTables = 0, groupTables = 0;
    bool operator==(const StageRecord&) const = default;
};
constexpr size_t kStageRecordBytes = 4 * 4 + 1 + 4 + 4 + 4;

std::vector<uint8_t> encodeStageRecord(const StageRecord& r);
std::optional<StageRecord> decodeStageRecord(std::span<const uint8_t> bytes);
// two worlds' changes together: a kill in either counts; of the rest, `changed` is the latest
StageRecord mergeStageRecord(const StageRecord& held, const StageRecord& changed);

// this world's state as a record
StageRecord captureStageRecord();
// a record from the other worlds of the stage: what they have defeated adds to this world's; the rest replaces its own
void applyStageRecord(const StageRecord& r);

// A world started on a stage that already has a record: the stage's load draws no layout of its own and takes the record
void setStartingStageRecord(const StageRecord& r);
bool hasStartingStageRecord();
void applyStartingStageRecord(); // after the stage's setup

} // namespace od::port
