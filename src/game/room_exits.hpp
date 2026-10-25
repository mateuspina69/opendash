// The exit lists of the rooms as the data source holds them (exe data: a stage's room function stores [table + room
// * 4], the room's list of 0x18-byte exit records, into 0xa7180c; an id of kExitEnd ends a list): what the host reads
// for a room it builds (the endless track's window, the arrivals of a free-play spawn, --doortest). The game's own
// exit reads go through the room's state (game/transition_state exitEntry: the pointer the room setup stored).
#pragma once

#include <cstdint>
#include <vector>

#include "engine/data_source.hpp"

namespace od::port {

// a point and a facing of a room (s16 x / y / z, s16 yaw)
struct RoomPoint {
    int16_t x = 0, y = 0, z = 0, yaw = 0;
};
// one exit record (game/transition_state ExitEntry's layout)
struct RoomExit {
    uint8_t id = 0, byte1 = 0, byte2 = 0;
    int16_t lockedMessage = 0;
    uint8_t destStage = 0, destRoom = 0;
    RoomPoint trigger, destination;
};
// a room's exits, up to the end record
struct RoomExitList : DataRecord {
    static constexpr uint32_t kBytes = 4;             // the room's pointer
    static constexpr uint32_t kRecordSize = 0x18;
    static constexpr uint32_t kMaxRecords = 0x100;    // the read's bound (a list ends at its end record)
    std::vector<RoomExit> exits;
    static RoomExitList load(const TableReader& r);
};
// the exits of the stage's room (none when the stage keeps no exit table)
const std::vector<RoomExit>& roomExits(int stage, int room);

} // namespace od::port
