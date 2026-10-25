// The enemies' neighbours: the enemy classes register in a list of the field's enemies (exe 0x5e5c10; the encounter
// tick refreshes their cells), and those that keep apart from each other look up another registered enemy in the cells
// around them (exe 0x5e5e00), keep its id in a word of their own (exe 0x5e5f40 / 0x5e5c70: bits 8..14), find its actor
// again (exe 0x5e5cb0), classify how it moves against them (exe 0x5e5f70) and head away from it (exe 0x5e5d90) or from
// the player (exe 0x5e5cf0).
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/state_array.hpp"

namespace od::port {

// The registered enemies (32 entries at 0x7de700, the mask 0xa6c42c marks the used ones from bit 31): entry = slot id
// << 24 | (~k & 0xff) << 16 | the high bytes of the actor's integer x / z (its cells: the encounter tick refreshes them
// when kEncounterRefreshCells is set)
struct EnemyCells {
    uint32_t mask = 0; // 0xa6c42c
    StateArray<uint32_t, 0x20> entries{}; // 0x7de700
};
inline EnemyCells& enemyCells() {
    static EnemyCells s;
    return s;
}

// exe 0x5e5c10(slot, k): an entry {slot << 24 | (~k & 0xff) << 16} in the first free entry of the list (bit 31 first;
// a full list takes nothing); k sets the cell size its neighbours are looked up by
void registerEnemy(uint32_t slot, uint32_t k);

// A class's neighbour word: bit 0 the side it steps aside to, bits 1..5 the classification (exe 0x5e5f70), bits 8..13
// the neighbour's pool index, kNeighbourStatic its pool (the statics', else the animated actors'); bit 15 the class's
// own
constexpr uint16_t kNeighbourSideRight = 0x0001;
constexpr uint16_t kNeighbourStatic = 0x4000;
constexpr uint16_t kNeighbourKept = 0x80ff;   // the bits a new neighbour id keeps
constexpr uint16_t kNeighbourIdBits = 0xffc1; // the bits the classification reads the neighbour by
// the classification (exe 0x5e5f70) of how the neighbour moves against the asking enemy
constexpr uint16_t kNeighbourAlongAsFast = 0x02;  // the same heading, it moves at least as fast
constexpr uint16_t kNeighbourOpposite = 0x04;     // heading the other way, or standing
constexpr uint16_t kNeighbourCrossingSlow = 0x08; // crossing, slower than the asking one
constexpr uint16_t kNeighbourCatching = 0x10;     // the same heading and slower, or crossing at least as fast
constexpr uint16_t kNeighbourMovementBits = 0x1e;
constexpr uint16_t kNeighbourLowerSlot = 0x20;    // the neighbour's slot is the lower one: the bits above inverted
// the side byte of a class's neighbour word (its low byte: kNeighbourSideRight), as headingFromOther /
// headingFromPlayer read it
inline uint8_t neighbourSide(uint16_t word) { return uint8_t(word); }

// exe 0x5e5c70(slot): the neighbour id of a slot id (an animated or a static slot; -1 for any other)
uint32_t neighbourIdOfSlot(uint32_t slot);
// exe 0x5e5cb0(id): the actor of a neighbour id (unchecked as the exe: an index past the animated pool reads past it)
Actor& neighbourActor(uint16_t id);
// exe 0x5e5f40(word, entry): the neighbour of a registered entry kept in the word (kNeighbourKept bits kept); its slot
uint32_t neighbourStore(uint16_t& word, uint32_t entry);
// exe 0x5e5e00(actor, word, k): another registered enemy in one of the four cells (0x100 << k units, the registration's
// cell size) around the actor: kept in the word; its slot, 0 when none
uint32_t neighbourFind(const Actor& actor, uint16_t& word, uint8_t k);
// exe 0x5e5f70(actor, word): how the neighbour of the word moves against the actor, or-ed into the word
void neighbourClassify(const Actor& actor, uint16_t& word);
// exe 0x5e5d90(actor, other, side, r): the heading away from the other actor (the angle from it turned round within r,
// else a quarter turn aside by the side byte: the neighbour word's low byte, kNeighbourSideRight)
int headingFromOther(const Actor& actor, const Actor& other, uint8_t side, int r);
// exe 0x5e5cf0(actor, side, near, far): the heading away from the player (turned round within near, a quarter turn
// aside by the side byte within far, else the angle from the player)
int headingFromPlayer(const Actor& actor, uint8_t side, int nearDistance, int farDistance);

} // namespace od::port
