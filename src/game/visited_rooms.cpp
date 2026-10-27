// The visited rooms and the map's route (see visited_rooms.hpp).
#include "game/visited_rooms.hpp"
#include "game/visited_rooms_state.hpp"

#include "game/game_state.hpp"
#include "game/story_flags.hpp"

using namespace od::port::visited_rooms_state;

namespace od::port {

namespace {

constexpr uint32_t kNoEntry = 0xffffffff;
constexpr uint32_t kRouteMaxDepth = 0x20;
constexpr uint32_t kDepthShift = 24;
// the first link record's index (byte 2) and the link count (byte 3) of a room list entry
uint32_t firstLinkOf(uint32_t entry) { return (entry >> 16) & 0xff; }
uint32_t linkCountOf(uint32_t entry) { return entry >> 24; }
// the stage / sub-room of a room key
uint32_t keyStage(uint32_t key) { return key & 0xff; }
uint32_t keySub(uint32_t key) { return (key >> 8) & 0xf; }

// exe 0x55c640(block, key): the visited flag of the room in the block's list (0: not listed)
uint32_t roomFlagOf(const VisitedRoomBlock& block, uint32_t key) {
    const uint32_t n = block.roomCount;
    for (uint32_t i = 0; i < n; ++i)
        if (key == (block.roomEntry(i) & kRoomKeyMask)) return block.flagBase + i;
    return 0;
}
// exe 0x55c610(stage, sub): the room was visited (its flag in the map's list is set)
bool roomVisited(uint32_t stage, uint32_t sub) {
    const uint32_t f = roomFlagOf(visitedRooms(), roomKey(stage, sub));
    return f != 0 && storyFlag(f);
}
// exe 0x55c5e0(stage, sub): the room's visited flag set
void markRoomVisited(uint32_t stage, uint32_t sub) {
    if (const uint32_t f = roomFlagOf(visitedRooms(), roomKey(stage, sub))) setStoryFlag(f);
}

// exe 0x55c4d0(key): the room is not on the path [0, depth]
bool offPath(uint32_t key) {
    RouteSearch& s = routeSearch();
    const int32_t d = int32_t(s.depth);
    for (int32_t i = 0; i <= d; ++i)
        if (key == (s.path[size_t(i)] & kRoomKeyMask)) return false;
    return true;
}
// exe 0x55c510: back to the last branch left (popped), the path past it cleared
void backtrack() {
    RouteSearch& s = routeSearch();
    const int32_t top = s.branchTop;
    const uint32_t v = s.branches[size_t(top)];
    s.branches[size_t(top)] = kNoEntry;
    const uint32_t d = v >> kDepthShift;
    s.depth = d;
    s.path[d] = v;
    s.branchTop = top - 1;
    for (uint32_t k = d + 1; k < kRouteMaxDepth; ++k) s.path[k] = kNoEntry;
}
// the link record `index` of a block
RoomLink linkRecord(const VisitedRoomBlock& block, uint32_t index) {
    return {dataScalar<uint32_t>(block.linkRecords + index * kLinkRecordSize)};
}

// exe 0x55c260(block, from entry, to entry): a depth-first search over the visited rooms' links for the shortest path;
// found: the route set toward `to` (the link out of `from` toward the path's second room) and the map marker on the
// target's links; else none
void searchRoute(VisitedRoomBlock& block, uint32_t from, uint32_t to) {
    RouteSearch& s = routeSearch();
    for (uint32_t k = 0; k < kRouteMaxDepth; ++k) {
        s.branches[k] = kNoEntry;
        s.best[k] = kNoEntry;
        s.path[k] = kNoEntry;
    }
    s.path[0] = from;
    s.depth = 0;
    s.bestDepth = kRouteMaxDepth;
    s.branchTop = -1;
    for (;;) {
        uint32_t d = s.depth;
        const uint32_t e = roomListEntry(block, s.path[d] & kRoomKeyMask);
        uint32_t n = linkCountOf(e);
        int found = 0;
        if (n != 0) {
            uint32_t link = firstLinkOf(e);
            do {
                const uint32_t key = linkRecord(block, link).destination & kRoomKeyMask;
                if (roomVisited(keyStage(key), keySub(key)) && offPath(key)) {
                    d = s.depth;
                    if (found == 0) {
                        found = 1;
                        s.path[d + 1] = ((d + 1) << kDepthShift) | key;
                    } else {
                        const int32_t top = s.branchTop + 1;
                        s.branchTop = top;
                        ++found;
                        s.branches[size_t(top)] = ((d + 1) << kDepthShift) | key;
                    }
                }
                d = s.depth;
                ++link;
            } while (--n != 0);
        }
        if (found != 0) {
            s.depth = d + 1;
        } else {
            if (s.branchTop < 0) break;
            backtrack();
        }
        d = s.depth;
        if ((s.path[d] ^ to) & kRoomKeyMask) continue;
        if (int32_t(d) < int32_t(s.bestDepth)) { // the target reached by a shorter path: keep it
            for (uint32_t k = 0; k <= d; ++k) s.best[k] = s.path[k];
            for (uint32_t k = d + 1; k < kRouteMaxDepth; ++k) s.best[k] = kNoEntry;
            s.bestDepth = d;
        }
        if (s.branchTop < 0) break;
        backtrack();
    }
    if (s.best[1] != kNoEntry) {
        const uint32_t e = roomListEntry(block, from & kRoomKeyMask);
        const uint32_t first = firstLinkOf(e), end = linkCountOf(e) + first;
        for (uint32_t i = first; i < end; ++i) {
            if ((linkRecord(block, i).destination ^ s.best[1]) & kRoomKeyMask) continue;
            block.routeFlags = kRouteFound;
            block.targetStage = uint8_t(keyStage(to));
            block.targetRoom = uint8_t(keySub(to));
            block.nextLink = uint8_t(i);
            MapMarker& m = mapMarker();
            m.records = roomLinkRecords(keyStage(to), keySub(to));
            m.foundWord = 0;
            return;
        }
    }
    block.routeFlags = 0;
    block.targetStage = kNoRouteRoom;
    block.targetRoom = kNoRouteRoom;
    block.nextLink = kNoRouteRoom;
}

// exe 0x55c170(stage, sub): the route from the room set up to that one (a visited room of the list, routable); else
// the route dropped
void routeTo(uint32_t stage, uint32_t sub) {
    const GameState& g = gameState();
    VisitedRoomBlock& block = visitedRooms();
    if (stage == g.stage && sub == g.room) return;
    if (block.listId != kNoRoomList && roomVisited(stage, sub)) {
        const uint32_t from = roomListEntry(block, roomKey(g.stage, g.room));
        if (from != 0) {
            const uint32_t to = roomListEntry(block, roomKey(stage, sub));
            if (!(to & kRoomEntryUnroutable)) {
                searchRoute(block, from, to);
                return;
            }
        }
    }
    block.targetStage = kNoRouteRoom;
    block.targetRoom = kNoRouteRoom;
    block.nextLink = kNoRouteRoom;
    block.routeFlags = 0;
}

} // namespace

void mapRouteTo(uint32_t stage, uint32_t sub) { routeTo(stage, sub); }
bool mapRoomVisited(uint32_t stage, uint32_t sub) { return roomVisited(stage, sub); }

uint32_t roomListEntry(const VisitedRoomBlock& block, uint32_t key) {
    const uint32_t n = block.roomCount;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t entry = block.roomEntry(i);
        if (key == (entry & kRoomKeyMask)) return entry;
    }
    return 0;
}

uint32_t roomLinkRecords(uint32_t stage, uint32_t sub) {
    const VisitedRoomBlock& b = visitedRooms();
    return b.linkRecords + firstLinkOf(roomListEntry(b, roomKey(stage, sub))) * kLinkRecordSize;
}

void mapMarkerStageReset() { mapMarker() = MapMarker{}; }

void mapRouteReset() {
    VisitedRoomBlock& r = visitedRooms();
    r.routeFlags = 0;
    r.targetStage = kNoRouteRoom;
    r.targetRoom = kNoRouteRoom;
    r.nextLink = 0xff;
    mapMarker().records = 0;
}

MapRouteState mapRouteState() {
    const VisitedRoomBlock& r = visitedRooms();
    if (r.routeFlags & kRouteOffList) return MapRouteState::OffList;
    if (r.listId == kNoRoomList) return MapRouteState::NoList;
    return MapRouteState(r.routeFlags & kRouteFound);
}

void mapMarkerRefresh() {
    const VisitedRoomBlock& r = visitedRooms();
    MapMarker& m = mapMarker();
    if (mapRouteState() == MapRouteState::Set) {
        m.records = roomLinkRecords(r.targetStage, r.targetRoom);
        m.shown = 1;
    } else {
        m.records = 0;
        m.shown = 0;
    }
}

void roomListSetup(int8_t listId, uint32_t rooms, uint32_t links) {
    const GameState& g = gameState();
    VisitedRoomBlock& block = visitedRooms();
    bool sameList = false;
    const uint8_t flags = block.routeFlags;
    bool reset = true;
    if (!(flags & kRouteFound)) {
        block.routeFlags = 0;
    } else if (block.targetStage != g.stage || block.targetRoom != g.room) {
        if (listId == kNoRoomList) { // the route kept, waiting off the list
            block.routeFlags = uint8_t((flags & ~(kRouteAtTarget | kRouteOffList)) | kRouteOffList);
            reset = false;
        } else if (block.listId == listId) { // the same island: searched again from this room
            sameList = true;
            block.routeFlags = uint8_t(flags & ~(kRouteAtTarget | kRouteOffList));
            reset = false;
        } else {
            block.routeFlags = 0;
        }
    } else {
        block.routeFlags = kRouteAtTarget;
    }
    if (reset) {
        block.listId = listId;
        block.targetStage = kNoRouteRoom;
        block.targetRoom = kNoRouteRoom;
        block.nextLink = kNoRouteRoom;
    }
    block.linkRecords = links;
    block.roomCount = 0;
    block.linkCount = 0;
    block.flagBase = kRoomFlagBaseNone;
    block.rooms.pointAt(rooms);
    if (listId == kNoRoomList) return;
    const DataArray<uint32_t> list(rooms);
    uint32_t i = 0;
    for (uint32_t v = list[i]; (v & 0xffff) != kRoomListEnd; v = list[i]) {
        ++block.roomCount;
        block.linkCount += linkCountOf(list[i]);
        ++i;
    }
    block.flagBase = list[i] >> 16;
    markRoomVisited(g.stage, g.room);
    if (sameList) routeTo(block.targetStage, block.targetRoom);
}

} // namespace od::port
