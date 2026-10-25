// The breakable walls (see breakable_walls.hpp).
#include "game/breakable_walls.hpp"
#include "game/breakable_walls_state.hpp"

#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/collision.hpp"
#include "game/explosions.hpp"
#include "game/fixed_point.hpp"
#include "game/global_class_types.hpp"
#include "game/throwable.hpp"
#include "game/wall_debris.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"
#include "game/wall_crumble.hpp"

using namespace od::port::breakable_walls_state;

namespace od::port {

namespace {

inline constexpr const char* kBreakableWallsArea = "breakable walls";

// the walls' state's one instance
constexpr uint16_t kQueueCapacity = 0x3f;
constexpr uint32_t kQueueSegmentMask = 0x7ff;

// the reactions [0x8cd8e0 + (pool entry reaction & 0xf) * 4] of a segment that broke a step
enum class BreakReaction : uint8_t {
    Debris = 0,  // exe 0x58cf10: the wall's debris (static type 0xc) and an explosion
    Crumble = 1, // exe 0x58d120: a crumbling controller (pool 0xe0 type 9, game/wall_crumble)
    Pieces = 2,  // exe 0x58d1b0: pieces thrown out (static type 9), an explosion, the debris
    Blast = 3,   // exe 0x58d450: an explosion
    Nothing = 4, // exe 0x52ef00
};
constexpr uint32_t kCrumbleVariantShift = 4;

// the segment list's header and the breakable word of segment i (after the list's entries)
SegmentListHeader listHeader() { return SegmentListHeader(roomGrid().segments); }
// segment i's word of the breakable list (after the list's header)
BreakableWord breakableWord(uint32_t i) {
    const uint32_t list =
        roomGrid().segments + uint32_t(listHeader().segmentCount) * kSegmentEntrySize + kListHeaderSize;
    return BreakableWord(list + i * kBreakableWordSize);
}

// the static actor at a fresh slot's address
StaticActor& newStatic(uint32_t address) { return staticActorAt(address); }

// an explosion (static type 8) at the point, its spread and flashes given
void spawnExplosion(uint32_t address, const BreakPoint& r, uint16_t spread, uint16_t flashes) {
    StaticActor& x = newStatic(address);
    x.flags = kActorLive;
    x.type = typeByte(GlobalStaticType::Explosion);
    x.variant = 0;
    x.loopFlags |= kActorSweepGroup2;
    x.pos.x.whole = r.x;
    x.pos.y.whole = r.y;
    x.pos.z.whole = r.z;
    auto&& params = ExplosionParams(x);
    params.spread = spread;
    params.flashes = flashes;
}

// the reaction of a segment that broke a step
void breakReaction(const SegmentEntry& e, const SegmentPoolEntry& pe) {
    const uint32_t step = uint32_t(e.flags & kBreakStepsMask);
    switch (BreakReaction(pe.reaction & kSegmentReactionMask)) {
    case BreakReaction::Debris: {
        const uint32_t debris = allocActor60(0);
        if (!debris) return;
        const BreakPoint r = segmentBreakPoint(e, pe);
        placeWallDebris(debris, pe, r.x, r.y, r.z);
        const uint32_t x = allocActor60(0);
        if (!x) return;
        spawnExplosion(x, r, uint16_t(uint32_t(int32_t(r.width + r.height * 2) >> 1) + 0x20), uint16_t(step * 4 + 4));
        return;
    }
    case BreakReaction::Crumble: {
        const uint32_t address = allocActorE0();
        if (!address) return;
        const BreakPoint r = segmentBreakPoint(e, pe);
        ControllerActor& crumble = controllerActorAt(address);
        crumble.type = kWallCrumbleType;
        crumble.flags |= kActorLive;
        crumble.variant = uint8_t(pe.reaction >> kCrumbleVariantShift);
        auto&& fields = classState<WallCrumbleFields>(crumble);
        fields.point.x = r.x;
        fields.point.y = r.y;
        fields.point.z = r.z;
        fields.height = int16_t(uint16_t(r.height));
        fields.poolEntry = pe.at;
        fields.segment = e.at;
        fields.width = int16_t(uint16_t(r.width));
        return;
    }
    case BreakReaction::Pieces: {
        // ((width + height) >> 7) + 1 + step pieces (variant n & 3) thrown out at random yaws / pitches, then an
        // explosion and the debris
        const BreakPoint r = segmentBreakPoint(e, pe);
        const int32_t reach = int32_t(r.width) + 1;
        const int32_t half = int32_t(r.height >> 1) + 1;
        int32_t n = int32_t((r.width + r.height) >> 7) + 1 + int32_t(step);
        uint32_t address = allocActor60(0);
        if (!address) return;
        for (;;) {
            StaticActor& piece = newStatic(address);
            piece.variant = uint8_t(n & 3);
            piece.loopFlags |= kActorSweepGroup2;
            piece.flags = kActorLive;
            piece.type = typeByte(GlobalStaticType::Throwable);
            piece.rotation.x = wrap16(-int32_t(rngStep() & 0x3ff));
            piece.rotation.y = int16_t(rngStep() & kAngleMask);
            piece.pos.y.whole =
                wrap16(int16_t(int32_t(int16_t(uint16_t(rngStep()))) % (half * 2)) - int16_t(half) + r.y);
            const int yaw = uint16_t(piece.rotation.y) & kAngleMask;
            piece.pos.x = int32_t(uint32_t(reach - int32_t(rngStep() & 0x1f)) * uint32_t(int32_t(fixedSin(yaw))) *
                                  uint32_t(-0x10));
            piece.pos.z = int32_t(uint32_t(reach - int32_t(rngStep() & 0x1f)) * uint32_t(int32_t(fixedCos(yaw))) *
                                  uint32_t(-0x10));
            piece.pos.x.whole = wrap16(piece.pos.x.whole + r.x);
            piece.pos.z.whole = wrap16(piece.pos.z.whole + r.z);
            const uint32_t q = rngStep();
            --n;
            ThrowableLaunch(piece).speed = int16_t(0x500 - (q & 0xff));
            if (n <= 0) break;
            address = allocActor60(0);
            if (!address) return;
        }
        if (const uint32_t x = allocActor60(0))
            spawnExplosion(x, r, uint16_t(int16_t((reach + half * 2) >> 1) + 0x20), uint16_t(step * 4 + 4));
        const uint32_t debris = allocActor60(0);
        if (!debris) return;
        placeWallDebris(debris, pe, r.x, r.y, r.z);
        return;
    }
    case BreakReaction::Blast: {
        const uint32_t x = allocActor60(0);
        if (!x) return;
        const BreakPoint r = segmentBreakPoint(e, pe);
        spawnExplosion(x, r, uint16_t(int16_t((r.width + r.height) >> 1)), uint16_t(step * 8 + 8));
        return;
    }
    case BreakReaction::Nothing: return;
    default:
        notPortedAt(kBreakableWallsArea, "breakable segment reaction past [0x8cd8e0 + k * 4], k",
                    pe.reaction & kSegmentReactionMask);
    }
}

// the segment's next step when its hit points fell past a threshold of its kind of breaking (1 step: below 0; 2
// steps: below half, below 0; 3 steps: below two thirds, a third, 0), -1 = none
int nextBreakFlags(const SegmentEntry& e, uint8_t steps, int16_t full) {
    const int16_t hp = e.hitPoints;
    const uint8_t flags = e.flags;
    const uint8_t done = flags & kBreakStepsMask;
    if (steps == 1) {
        if (hp < 0) return (flags & 0xfd) | 1;
    } else if (steps == 2) {
        if (hp < 0 && done < 2) return (flags & 0xfe) | 2;
        if (hp < (full >> 1) && done == 0) return (flags & 0xfd) | 1;
    } else {
        const int16_t third = int16_t(int32_t((int64_t(full) * 0x55555556) >> 32) - (full >> 15)); // full / 3
        if (hp < 0 && done < 3) return flags | 3;
        if (hp < third && done < 2) return (flags & 0xfe) | 2;
        if (int(hp) < third * 2 && done == 0) return (flags & 0xfd) | 1;
    }
    return -1;
}

} // namespace

BreakPoint segmentBreakPoint(const SegmentEntry& e, const SegmentPoolEntry& pe) {
    const int shift = int((pe.breakFlags() >> kBreakSizeShift) & 3) + 2;
    const RoomGridState& g = roomGrid();
    const GridCell cell(g.cells + gridCellNumberUnchecked(g.bounds(), e.cellX, e.cellZ) * kGridCellSize);
    const uint32_t flags = pe.flags;
    BreakPoint r{};
    r.y = wrap16((kCellBias - int(cell.baseLevel)) * 16 - int((uint16_t(e.heightWord) >> 4) & 0x7f0) +
                 (int(pe.offsetY()) << shift));
    r.x = wrap16((int(pe.offsetX()) << shift) + ((flags & kPoolHalfCellX) ? kCellSize : kCellSize / 2) +
                 (int(e.cellX) - kCellBias) * kCellSize);
    r.z = wrap16((int(pe.offsetZ()) << shift) + (int(e.cellZ) - kCellBias) * kCellSize +
                 ((flags & kPoolHalfCellZ) ? kCellSize : kCellSize / 2));
    r.width = (uint32_t(pe.breakWidth) << shift) & 0x7fff;
    r.height = (uint32_t(pe.breakHeight) << shift) & 0x7fff;
    return r;
}

void probeBlastWalls(const PlayerEnv& env, Actor& actor, int r) {
    BoxAccess box = *actor.collisionBox;
    box.minZ = int16_t(uint16_t(uint32_t(-r)));
    box.minY = box.minZ;
    box.minX = box.minY;
    box.maxZ = int16_t(uint16_t(uint32_t(r)));
    box.maxY = int16_t(uint16_t(uint32_t(r)));
    box.maxX = int16_t(uint16_t(uint32_t(r)));
    const uint32_t mask = collisionLayerMask(actor.slot); // exe 0x413240
    const int16_t from[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    auto test = [&](int32_t x, int32_t y, int32_t z) {
        const int16_t p[3] = {wholePart(x), wholePart(y), wholePart(z)};
        const uint32_t result = testMoveBlocked(env, p, from, actor.collisionBox, mask);
        if (int32_t(result) < 0) queueBreakableWallHit(result, kBlastWallAttack); // exe 0x58d4e0
    };
    const int32_t x = actor.pos.x, y = actor.pos.y, z = actor.pos.z, d = int32_t(uint32_t(r) << 15);
    test(x, y, z);
    test(x - d, y, z);
    test(x + d, y, z);
    test(x, y - d, z);
    test(x, y + d, z);
    test(x, y, z - d);
    test(x, y, z + d);
}

void queueBreakableWallHit(uint32_t result, int attack) {
    BreakableWallState& s = state();
    if (!(roomGrid().roomFlags & kRoomSwitchableSegments) || int16_t(s.queuedHits) == kQueueCapacity ||
        !(result & kCellCollides) || (result & kCellFloor))
        return;
    if (!(result & (uint32_t(kVolumeLayerHigh) << 12)) && attack >= 0) return;
    const SegmentEntry e = segmentEntry(result & kCellSegmentMask);
    const SegmentPoolEntry pe = segmentPoolEntry(e.poolIndex);
    if (uint32_t(pe.breakLevel & kBreakLevelMax) > ((uint32_t(attack) >> kBreakLevelShift) & kBreakLevelMax)) return;
    const uint32_t n = s.queuedHits;
    s.queuedHits = uint16_t(n + 1);
    s.queue[n] = uint32_t(attack) << 16 | (result & kQueueSegmentMask);
}

void breakableWallsReset() {
    if (!(roomGrid().roomFlags & kRoomSwitchableSegments)) return;
    // the words after the list's entries, one per segment (the exe clears as many as there are segments)
    for (uint32_t i = 0, n = listHeader().segmentCount; i < n; ++i) breakableWord(i).clearPendingDamageWord();
    BreakableWallState& s = state();
    s.queuedHits = 0;
    s.queue = {};
    s.damage = {};
    s.sharedDamage = {};
}

void breakableWallsTick() {
    if (!(roomGrid().roomFlags & kRoomSwitchableSegments)) return;
    BreakableWallState& s = state();
    // the queued hits added to their segments' counters
    for (uint32_t k = 0, n = s.queuedHits; k < n; ++k) {
        const uint32_t hit = s.queue[k];
        const uint32_t segment = hit & 0xffff;
        const uint16_t counter = breakableWord(segment).counter;
        // a segment of its own counter past the counters (no breakable segment: they are the list's first 0x40): the
        // exe's add lands past the array, on bytes no reader of the walls takes (the pass reads the breakable ones')
        if (!(counter & kBreakSharedCounter) && segment >= s.damage.size()) continue;
        auto&& total =
            (counter & kBreakSharedCounter) ? s.sharedDamage[counter & kBreakCounterMask] : s.damage[segment];
        total = uint16_t(total + uint16_t(hit >> 16));
    }
    const uint32_t count = listHeader().breakableCount;
    for (uint32_t i = 0; i < count; ++i) {
        auto&& word = breakableWord(i);
        const uint16_t counter = word.counter;
        const int16_t summed = (counter & kBreakSharedCounter) ? int16_t(s.sharedDamage[counter & kBreakCounterMask])
                                                               : int16_t(s.damage[i]);
        const uint16_t v = uint16_t(summed + word.pendingDamage);
        if (v == 0) continue;
        auto&& e = segmentEntry(i);
        const SegmentPoolEntry pe = segmentPoolEntry(e.poolIndex);
        if (pe.hitPoints > 0 && (pe.breakFlags() & kBreakStepsMask) != 0 && e.hitPoints >= 0) {
            // [0x8cd8d0 + steps * 4](segment entry, pool entry, word): 0x58cd90 / 0x58cde0 / 0x58ce70 -- the damage
            // taken off the segment's hit points, the pending damage cleared; past a threshold the next step
            word.pendingDamage = int16_t(v);
            const uint8_t steps = pe.breakFlags() & kBreakStepsMask;
            e.hitPoints = wrap16(e.hitPoints - word.pendingDamage);
            word.pendingDamage = 0;
            const int next = nextBreakFlags(e, steps, pe.hitPoints);
            if (next >= 0) {
                e.flags = uint8_t(next);
                breakReaction(e, pe);
            }
        }
    }
    s.queuedHits = 0;
    for (size_t k = 0; k < s.queue.size(); ++k) s.queue[k] = 0;
    for (size_t k = 0; k < s.damage.size(); ++k) s.damage[k] = 0;
    for (size_t k = 0; k < s.sharedDamage.size(); ++k) s.sharedDamage[k] = 0;
}

int32_t cellSegmentVariant(uint32_t cellWord) {
    if (!(cellWord & kCellCollides) || (cellWord & kCellFloor)) return -1;
    return segmentEntry(cellWord & kCellSegmentMask).flags & kBreakStepsMask;
}

uint32_t cellSegmentStrength(uint32_t cellWord) {
    if (!(roomGrid().roomFlags & kRoomSwitchableSegments) || !(cellWord & kCellCollides) || (cellWord & kCellFloor))
        return 0;
    const SegmentEntry e = segmentEntry(cellWord & kCellSegmentMask);
    const SegmentPoolEntry pe = segmentPoolEntry(e.poolIndex);
    return uint32_t(uint16_t(pe.hitPoints)) << 16 | uint16_t(e.hitPoints);
}

int32_t gridCellVariant(uint32_t column, uint32_t row) {
    return cellSegmentVariant(GridCell(gridCellAtIndex(column, row)).head);
}

bool gridCellSegmentBroken(uint32_t column, uint32_t row) {
    return int16_t(uint16_t(cellSegmentStrength(GridCell(gridCellAtIndex(column, row)).head))) < 0;
}

void switchGridCellVariant(uint32_t column, uint32_t row, uint8_t variant) {
    switchCellSegmentVariant(GridCell(gridCellAtIndex(column, row)).head, variant);
}

void switchCellSegmentVariant(uint32_t cellWord, uint8_t variant) {
    if (!(cellWord & kCellCollides) || (cellWord & kCellFloor)) return;
    auto&& e = segmentEntry(cellWord & kCellSegmentMask);
    const SegmentPoolEntry pe = segmentPoolEntry(e.poolIndex);
    const uint8_t steps = pe.breakFlags() & kBreakStepsMask;
    if (steps == 0) return;
    uint8_t sel = variant & kBreakStepsMask;
    if (steps < sel) sel = steps;
    e.flags = uint8_t((e.flags & 0xfc) | sel);
    if (e.hitPoints <= 0) return;
    const int16_t full = pe.hitPoints;
    auto third = [&](int n) { return int16_t(full / 3) * n; }; // the exe's * 0x55555556 division
    switch (steps) {
    case 1: e.hitPoints = sel ? int16_t(-1) : full; return;
    case 2: e.hitPoints = sel == 0 ? full : sel == 1 ? int16_t(full >> 1) : int16_t(-1); return;
    case 3:
        e.hitPoints = sel == 0 ? full : sel == 1 ? wrap16(third(1)) : sel == 2 ? wrap16(third(2)) : int16_t(-1);
        return;
    default: return;
    }
}

} // namespace od::port
