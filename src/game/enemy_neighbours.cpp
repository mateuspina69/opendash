// The enemies' neighbours (see enemy_neighbours.hpp).
#include "game/enemy_neighbours.hpp"

#include <cstdio>

#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/player/player_state.hpp"

namespace od::port {

namespace {

constexpr uint32_t kNeighbourIndexShift = 8, kNeighbourIndexMask = 0x3f;
constexpr int kStepAside = kQuarterTurn;

// the slot address of a neighbour id (the exe's arithmetic: the animated or the static pool by kNeighbourStatic)
uint32_t neighbourAddress(uint16_t id) {
    const uint32_t i = (uint32_t(id) >> kNeighbourIndexShift) & kNeighbourIndexMask;
    if (!(id & kNeighbourStatic)) return kAnimatedPool + i * AnimatedPool::stride();
    return kStaticPool + i * StaticPool::stride();
}

// the registered entry of another enemy in one of the four cells around the actor (exe 0x5e5e00's search)
bool findNeighbourEntry(const Actor& actor, uint8_t k, uint32_t& entry) {
    const uint32_t half = uint32_t(int32_t(0x100u << (k & 0x1f)) >> 1);
    const uint32_t mask = (~uint32_t(k)) & 0xff, pair = mask << 8 | mask;
    const uint32_t x = (uint32_t(uint16_t(actor.pos.x.whole)) - half) & 0xff00;
    const uint32_t z = ((uint32_t(uint16_t(actor.pos.z.whole)) - half) >> 8) & 0xff;
    const uint32_t cells[4] = {z | x, (x + 0x100) | z, (z + 1) | x, (z + 1) | (x + 0x100)};
    const uint32_t self = uint32_t(actor.slot) << 24;
    EnemyCells& list = enemyCells();
    size_t e = 0;
    for (uint32_t bits = list.mask; bits != 0; bits <<= 1, ++e) {
        if (int32_t(bits) >= 0) continue;
        const uint32_t v = list.entries[e];
        const uint32_t b = (v & 0xff0000) >> 16;
        const uint32_t t = (b << 8 | b) & pair;
        if (self == (v & 0xff000000u)) continue;
        const uint32_t d = t & v;
        for (const uint32_t c : cells) {
            if ((t & c) == d) {
                entry = v;
                return true;
            }
        }
    }
    return false;
}

// the turn aside of the side byte: a quarter turn right or left
int stepAside(uint8_t side) { return (side & kNeighbourSideRight) ? kStepAside : -kStepAside; }

} // namespace

void registerEnemy(uint32_t slot, uint32_t k) {
    EnemyCells& list = enemyCells();
    uint32_t m = list.mask, bit = 0x80000000u;
    size_t e = 0;
    for (int c = 31;; --c) {
        if (int32_t(m) >= 0) break;
        m <<= 1;
        bit >>= 1;
        ++e;
        if (c == 0) return;
    }
    list.entries[e] = slot << 24 | ((~k & 0xff) << 16);
    list.mask |= bit;
}

uint32_t neighbourIdOfSlot(uint32_t slot) {
    switch (slot & 0xe0) {
    case kAnimatedClass: return (slot - kAnimatedClass) << kNeighbourIndexShift;
    case kStaticClass:
    case kStaticClass + 0x20: return ((slot - kStaticClass) << kNeighbourIndexShift) | kNeighbourStatic;
    default: return 0xffffffffu;
    }
}

// exe 0x5e5cb0's unchecked arithmetic: the 6-bit index of an animated neighbour id reaches 63 against a pool of 32. An
// id is written only from a registered slot (neighbourIdOfSlot), but the word can hold whatever its class left there;
// an index 32..63 reads the exe's memory past the animated pool with the Actor offsets, which the native slots do
// not have: an ActorLayoutError naming it (modelled by name where a recording shows it)
Actor& neighbourActor(uint16_t id) {
    const uint32_t address = neighbourAddress(id);
    if (!(id & kNeighbourStatic) && !AnimatedPool::contains(address)) {
        char b[96];
        std::snprintf(b, sizeof b, "enemy neighbours: the neighbour id %04x reads past the animated pool", unsigned(id));
        throw ActorLayoutError(b);
    }
    return actorLayoutAt(address);
}

uint32_t neighbourStore(uint16_t& word, uint32_t entry) {
    word &= kNeighbourKept;
    const uint32_t slot = entry >> 24;
    word = uint16_t(word | neighbourIdOfSlot(slot));
    return slot;
}

uint32_t neighbourFind(const Actor& actor, uint16_t& word, uint8_t k) {
    uint32_t entry;
    return findNeighbourEntry(actor, k, entry) ? neighbourStore(word, entry) : 0;
}

void neighbourClassify(const Actor& actor, uint16_t& word) {
    const int mine = int(actor.velocity.x) * actor.velocity.x + int(actor.velocity.z) * actor.velocity.z;
    const Actor& other = neighbourActor(uint16_t(word & kNeighbourIdBits));
    const int ox = (int(other.pos.x.whole) - other.prevPos.x.whole) * 16;
    const int oz = (int(other.pos.z.whole) - other.prevPos.z.whole) * 16;
    const int theirs = oz * oz + ox * ox;
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(int32_t(other.rotation.y)));
    uint16_t v;
    if (uint32_t(d + 0x200) < 0x400u) v = mine <= theirs ? kNeighbourAlongAsFast : kNeighbourCatching;
    else if (uint32_t(d + 0x600) <= 0xc00u) v = mine <= theirs ? kNeighbourCatching : kNeighbourCrossingSlow;
    else v = kNeighbourOpposite;
    if (theirs == 0) v = kNeighbourOpposite;
    if (actor.slot > other.slot) v = uint16_t((v ^ kNeighbourMovementBits) | kNeighbourLowerSlot);
    word |= v;
}

int headingFromOther(const Actor& actor, const Actor& other, uint8_t side, int r) {
    const int angle = angleOf(other.pos, actor.pos);
    const int dx = int(actor.pos.x.whole) - other.pos.x.whole, dz = int(actor.pos.z.whole) - other.pos.z.whole;
    if (dz * dz + dx * dx < r * r) return -angle;
    return angle + stepAside(side);
}

int headingFromPlayer(const Actor& actor, uint8_t side, int nearDistance, int farDistance) {
    const Player& p = player();
    const int angle = angleOf(p.pos, actor.pos);
    const int dx = int(actor.pos.x.whole) - p.pos.x.whole, dz = int(actor.pos.z.whole) - p.pos.z.whole;
    const int d = isqrt(int32_t(uint32_t(dz * dz) + uint32_t(dx * dx)));
    if (d < nearDistance) return -angle;
    if (d < farDistance) return angle + stepAside(side);
    return angle;
}

} // namespace od::port
