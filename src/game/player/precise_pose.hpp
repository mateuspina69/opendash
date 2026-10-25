// Precise poses for drawing only. The game poses its models in PS1 fixed point: bone matrices are s16 with the
// 1/8 model scale folded into the columns (~9 bits left per element) and bone points are rounded to whole units,
// so slowly moving limbs / props flicker by a unit between ticks (the PS1 "jitter"). The pose ports
// (player_pose exe 0x5e25b0, actors exe 0x5e2de0) keep writing the exact fixed-point values the game logic
// reads, and alongside each one record the same value computed in float here. The draw code asks for the
// float version by the fixed value's address; an entry is only returned while the fixed value it was recorded
// with is still in memory, so anything that rewrites a bone afterwards (class code, snapshots) falls back to
// the fixed value. Off (F1) = the original look.
#pragma once

#include <cstdint>


namespace od::precise {

bool enabled();
void setEnabled(bool on);

// record after the fixed value was written (9 s16 of a matrix, same units / 3 s16 of a point) and its float version:
// the record keyed by the fixed values' storage (a bone block's, the player's), returned while that storage still holds
// the fixed values it was recorded with (false = off / none / stale: use the fixed value)
void putMatrix(const int16_t* fixed, const float v[9]);
void putPoint(const int16_t* fixed, const float v[3]);
bool matrix(const int16_t* fixed, float out[9]);
bool point(const int16_t* fixed, float out[3]);


// float 3x3 helpers (row major, 1 = identity scale)
struct F3 {
    float e[9];
};
F3 fmul(const F3& a, const F3& b);
F3 frotXYZ(const int16_t* ang); // exe 0x40bf80 order: Rx * Ry * Rz
F3 frotYXZ(const int16_t* ang); // the game's Ry * Rx * Rz order (game/matrix rotationYXZ)
F3 fromFixed(const int16_t* e); // s16 / 4096

} // namespace od::precise
