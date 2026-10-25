// The visited rooms and the map's route: the block 0xa71740 the room setup fills with the island's room list (exe
// 0x55c000: the rooms, the base of their visited flags, their link records), the rooms' visited flags, the route
// search from the room to a target room over the visited rooms' links (exe 0x55c260) and the map marker the route
// gives (exe 0x55c580 / 0x415520). The save keeps the block's five dwords.
#pragma once

#include <cstddef>
#include <cstdint>

#include "game/loaded_data.hpp"
#include "game/state_array.hpp"

namespace od::port {

// A room list entry (u32): the room key (sub << 8 | stage) in the low 12 bits, kRoomEntryUnroutable, the index of the
// room's first link record in byte 2, its link count in byte 3. A list ends with kRoomListEnd in the low half, the
// base of the rooms' visited flags in the high half
// (the list: exe data, u32 entries; read through the block's pointer)
constexpr uint32_t kRoomKeyMask = 0xfff;
constexpr uint32_t kRoomEntryUnroutable = 0x1000; // no route is searched toward the room
constexpr uint32_t kRoomListEnd = 0xffff;
// the room key of a stage's sub-room
constexpr uint32_t roomKey(uint32_t stage, uint32_t sub) { return sub << 8 | stage; }
// A room's link record (8 bytes): the room it leads to (its key in the low 12 bits; the rest is the map's)
struct RoomLink {
    uint32_t destination = 0; // +0x0
};
constexpr uint32_t kLinkRecordSize = 8;

constexpr size_t kVisitedRoomWords = 5;
// The visited-room block (the map's is 0xa71740): the route's state, the room list of the island the room is on and
// the route's target
struct VisitedRoomBlock {
    uint8_t routeFlags = 0;   // +0x0: kRouteFound | kRouteAtTarget | kRouteOffList
    int8_t listId = 0;        // +0x1: the island's room list (-1: the room is on none)
    uint8_t targetStage = 0;  // +0x2: the route's target room (kNoRouteRoom: none)
    uint8_t targetRoom = 0;   // +0x3
    uint8_t nextLink = 0;     // +0x4: the link out of the route's start toward its second room
    uint8_t roomCount = 0;    // +0x5
    uint8_t linkCount = 0;    // +0x6
    uint8_t unread07 = 0;     // +0x7 (no reader traced; the save copies it)
    uint32_t flagBase = 0;    // +0x8: the story flag of the list's first room (the others follow)
    DataPtr<> rooms;          // +0xc: the island's room list (u32 entries, exe data)
    uint32_t linkRecords = 0; // +0x10: the rooms' link records (RoomLink, exe data)

    // entry i of the room list
    uint32_t roomEntry(size_t i) const { return dataScalar<uint32_t>(rooms.value() + uint32_t(i) * 4); }
    // the block's five dwords as the save copies them
    uint32_t word(size_t i) const {
        switch (i) {
        case 0: return uint32_t(routeFlags) | uint32_t(uint8_t(listId)) << 8 | uint32_t(targetStage) << 16 | uint32_t(targetRoom) << 24;
        case 1: return uint32_t(nextLink) | uint32_t(roomCount) << 8 | uint32_t(linkCount) << 16 | uint32_t(unread07) << 24;
        case 2: return flagBase;
        case 3: return rooms.value();
        case 4: return linkRecords;
        default: stateIndexError(i, kVisitedRoomWords);
        }
    }
    void setWord(size_t i, uint32_t v) {
        switch (i) {
        case 0:
            routeFlags = uint8_t(v);
            listId = int8_t(uint8_t(v >> 8));
            targetStage = uint8_t(v >> 16);
            targetRoom = uint8_t(v >> 24);
            return;
        case 1:
            nextLink = uint8_t(v);
            roomCount = uint8_t(v >> 8);
            linkCount = uint8_t(v >> 16);
            unread07 = uint8_t(v >> 24);
            return;
        case 2: flagBase = v; return;
        case 3: rooms.setValue(v); return;
        case 4: linkRecords = v; return;
        default: stateIndexError(i, kVisitedRoomWords);
        }
    }
};
constexpr uint8_t kRouteFound = 0x1;
constexpr uint8_t kRouteAtTarget = 0x2; // the room set up is the target
constexpr uint8_t kRouteOffList = 0x4;  // the room set up is on no room list (the route waits)
constexpr uint8_t kNoRouteRoom = 0xff;
constexpr int8_t kNoRoomList = -1;
// the flag base of a block without a list (a new run, a room on no list)
constexpr uint32_t kRoomFlagBaseNone = 0x780;
// the map's visited-room block
inline VisitedRoomBlock& visitedRooms() {
    static VisitedRoomBlock s; // 0xa71740
    return s;
}

// The map marker the route gives
struct MapMarker {
    uint32_t records = 0;   // the target room's link records (their address in the game state; 0: none)
    uint32_t foundWord = 0; // cleared when a route is found (its reader is not traced)
    uint8_t shown = 0;
};
inline MapMarker& mapMarker() {
    static MapMarker s;
    return s;
}

// exe 0x415410's map part (the stage setup's template): the marker cleared
void mapMarkerStageReset();
// exe 0x55c220(block, key): the room's entry in the block's list (0: not listed)
uint32_t roomListEntry(const VisitedRoomBlock& block, uint32_t key);
// exe 0x55c5b0(stage, sub): the room's first link record in the map's block (a room not listed: record 0)
uint32_t roomLinkRecords(uint32_t stage, uint32_t sub);
// exe 0x55c560: no route (the map's block without its flags, its target and its next link; no marker)
void mapRouteReset();
// exe 0x55c580: the route's state
enum class MapRouteState : uint8_t {
    None = 0,    // no route (or the room set up is its target)
    Set = 1,     // a route toward the target
    NoList = 2,  // the island has no room list
    OffList = 3, // the route set, the room set up on no room list
};
MapRouteState mapRouteState();
// exe 0x415480 / 0x415520's marker part (a room set up): a route set -> the target room's link records as the map
// marker, shown; else none
void mapMarkerRefresh();
// exe 0x55c000(list id, rooms, links): the map's block for the island's room list (kNoRoomList: none) -- the route
// kept, waiting or dropped by the room entered, the rooms counted, the room's visited flag set, the route searched
// again from a room of the same list
void roomListSetup(int8_t listId, uint32_t rooms, uint32_t links);

} // namespace od::port
