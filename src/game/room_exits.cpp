// The exit lists of the rooms (see room_exits.hpp).
#include "game/room_exits.hpp"

#include "game/transition_state.hpp"

namespace od::port {

namespace {
RoomPoint point(const TableReader& r, size_t o) { return {r.s16(o), r.s16(o + 2), r.s16(o + 4), r.s16(o + 6)}; }
} // namespace

RoomExitList RoomExitList::load(const TableReader& r) {
    RoomExitList t;
    const TableReader list = r.follow(0, kRecordSize * kMaxRecords);
    for (size_t o = 0; o + kRecordSize <= list.size() && list.u8(o) != kExitEnd; o += kRecordSize) {
        RoomExit e;
        e.id = list.u8(o);
        e.byte1 = list.u8(o + 1);
        e.byte2 = list.u8(o + 2);
        e.lockedMessage = list.s16(o + 4);
        e.destStage = list.u8(o + 6);
        e.destRoom = list.u8(o + 7);
        e.trigger = point(list, o + 8);
        e.destination = point(list, o + 0x10);
        t.exits.push_back(e);
    }
    return t;
}

namespace {
const TableFamily<RoomExitList> roomExitLists{TableId::RoomExits};
} // namespace

const std::vector<RoomExit>& roomExits(int stage, int room) {
    static const std::vector<RoomExit> none;
    if (stage < 0 || room < 0) return none;
    const uint32_t index = roomIndex(stage, room);
    if (!roomExitLists.has(index)) return none;
    return roomExitLists[index].exits;
}

} // namespace od::port
