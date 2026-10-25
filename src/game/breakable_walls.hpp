// The breakable walls: the scenery segments of a room with switchable segments (game/room_grid kRoomSwitchableSegments)
// whose pool entry has hit points. A shot / blast that hits such a wall queues the hit (exe 0x58d4e0); the frame body's
// tick (exe 0x58cc10, after the encounter tick) sums the queued damage per segment (or per shared counter), takes it
// off the segment's hit points and, past a threshold, advances the segment's variant (its volume set: the wall opens)
// and runs the segment's reaction -- the wall's debris, a crumbling controller (game/wall_crumble), pieces, blasts
// (game/explosions). Scripts also switch a cell's segment variant directly (exe 0x58d5f0).
#pragma once

#include <cstdint>

#include "game/room_grid.hpp"

namespace od::port {

struct Actor;
struct PlayerEnv;

// a hit's attack word: the damage in the low 16 bits, the break level in bits 16..17 (a wall breaks only under an
// attack of at least its pool entry's breakLevel; a negative attack reaches every layer)
constexpr uint32_t kBreakLevelShift = 16;
constexpr uint32_t breakLevel(uint32_t level) { return level << kBreakLevelShift; }
constexpr uint32_t kBreakLevelMax = 3; // the drill's: every breakable wall

// exe 0x58d4e0(result, attack): a wall hit (a collision result inside a segment volume) queued when the attack reaches
// the segment (its level, the layer of the volume or a negative attack); at most 0x3f a frame
void queueBreakableWallHit(uint32_t result, int attack);
// exe 0x5672c0 / 0x56abf0's wall probes (a blast sphere of radius r: the energy orb, the charge's shock wave): the
// actor's collision box (exe data) resized to -+r, the room tested at the actor and at the 6 points r / 2 away along
// the axes (exe 0x40f5a0, the actor's layers); each blocked test queues a breakable-wall hit of kBlastWallAttack
void probeBlastWalls(const PlayerEnv& env, Actor& actor, int r);
constexpr int kBlastWallAttack = 4;
// exe 0x58cc10 (the frame body, after the encounter tick): the queued hits summed, the segments damaged, broken steps
// advanced with their reactions; the queue and the counters cleared. Only in a room with switchable segments.
void breakableWallsTick();
// exe 0x58cba0 (the room loads, the in-stage warp): in a room with switchable segments, every breakable word's pending
// damage cleared, the queue and the damage counters cleared
void breakableWallsReset();
// exe 0x58cff0(segment entry, pool entry): a breaking segment's point (x, y, z: its anchor cell, the cell's floor, the
// entry's height and the pool entry's offsets << the size shift (+0x100 / +0x200 by the half-cell bits)) and its two
// sizes (the pool entry's width / height << the same shift, & 0x7fff)
struct BreakPoint {
    int16_t x, y, z;
    uint32_t width, height;
};
BreakPoint segmentBreakPoint(const SegmentEntry& e, const SegmentPoolEntry& pe);
// exe 0x58d5f0(cell, variant): a breakable segment cell's variant set (capped at its steps) and its hit points to the
// variant's threshold
void switchCellSegmentVariant(uint32_t cellWord, uint8_t variant);
// ... of the cell at grid column / row (cell units + kCellBias: the scripts' inlined cell index)
void switchGridCellVariant(uint32_t column, uint32_t row, uint8_t variant);
// exe 0x58d5c0 of the cell at grid column / row (the scripts' inlined cell index)
int32_t gridCellVariant(uint32_t column, uint32_t row);
// exe 0x58d5c0(cell): a breakable segment cell's variant (its entry's flags & kBreakStepsMask); -1 for any other cell
int32_t cellSegmentVariant(uint32_t cellWord);
// exe 0x58d570(cell): in a room with switchable segments, a segment cell's strength: its pool entry's full hit points
// << 16 | the segment's hit points left (< 0 as a short: broken / open); 0 otherwise
uint32_t cellSegmentStrength(uint32_t cellWord);
// the segment of the cell at grid column / row is broken open (its hit points left < 0)
bool gridCellSegmentBroken(uint32_t column, uint32_t row);

} // namespace od::port
