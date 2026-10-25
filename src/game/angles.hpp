// The game's angle math: 12-bit angles (4096 = one turn), the sine / cosine table of the exe, its x87 arctangent,
// the signed difference of two angles and the angle of one point seen from another. Every system and class that turns
// or aims calls these (the actors, combat, the camera, the story casts, the viewer's walker): angle math is a system
// of its own, not part of any one of them.
#pragma once

#include <cstdint>

#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port {

constexpr int kAngleMask = 0xfff;     // a 12-bit angle
constexpr int kFullTurn = 0x1000;     // 360 degrees
constexpr int kHalfTurn = 0x800;      // 180 degrees
constexpr int kQuarterTurn = 0x400;   // 90 degrees
constexpr int16_t kFixedOne = 0x1000; // 1.0 in the 4.12 sines and matrices

// a 12-bit angle as a signed one (-0x800 .. 0x7ff)
inline int16_t signedAngle(int v) { return int16_t(int16_t(uint16_t(uint32_t(v) << 4)) >> 4); }
// exe table 0x847ad8: sine and cosine (4.12) of the 12-bit angle (taken & 0xfff)
int16_t fixedSin(int angle);
int16_t fixedCos(int angle);
// the table's sine / cosine of the angle is there (the bound data source has the table; a host picture without one asks)
bool fixedSinMapped(int angle);
bool fixedCosMapped(int angle);
// exe constant 0x831848: the double 1 / 3.14 that turns the arctangent's radians into angle units (with * 2048; the
// data source's)
double atanScale();

// exe 0x40b960: atan2(a, b) * 2048 * (1 / 3.14, the exe constant 0x831848), truncated, at the x87's 24-bit precision
// (engine/x87); not masked to 12 bits
int atanAngle(float a, float b);
// exe 0x5e3d80: the signed shortest difference to - from of two 12-bit angles (-0x800 .. 0x800)
int angleDiff(uint32_t from, uint32_t to);
// exe 0x5e3bc0: the angle (& 0xfff) of the 16.16 point `to` seen from `from` (x / z plane)
int angleOf(const Fixed16Vec3& from, const Fixed16Vec3& to);
// ... on 16.16 values (the exe's stack points)
int angleOf(int32_t fromX, int32_t fromZ, int32_t toX, int32_t toZ);
// exe 0x5e3c00: the same on two s16 points {x, y, z}
int angleOf(const int16_t from[3], const int16_t to[3]);
// ... on two s16 points in memory
int angleOf(const ShortVec3& from, const ShortVec3& to);
// exe 0x5e3c50(a, b): the angle (& 0xfff) of the s16 point b seen from a, both given as {x, z} pairs
int planarAngleOf(int16_t fromX, int16_t fromZ, int16_t toX, int16_t toZ);
// exe 0x5e3ca0(a, b): the pitch of the 16.16 point b seen from a: (-0x400 - atan(|xz|, dy)) & 0xfff on the integer parts
int pitchOf(const int32_t from[3], const int32_t to[3]);
// ... on two 16.16 points in memory / from one to an s16 point
int pitchOf(const Fixed16Vec3& from, const Fixed16Vec3& to);
int pitchOf(const Fixed16Vec3& from, const int16_t to[3]);
// exe 0x5e3d10(a, b): the pitch of the s16 point `to` seen from `from` (-0x400 - atan(|xz|, dy), & 0xfff; the
// differences not wrapped to 16 bits)
int pitchOf(const int16_t from[3], const int16_t to[3]);
// ... on s16 points in memory (either or both)
int pitchOf(const ShortVec3& from, const ShortVec3& to);
int pitchOf(const ShortVec3& from, const int16_t to[3]);
int pitchOf(const int16_t from[3], const ShortVec3& to);
// exe 0x5e3db0(actor, angle, step): the yaw turned by step toward the angle when it is at least step away; returns the
// difference measured before the turn
int turnToward(int16_t& yaw, int angle, int step);
// exe 0x5e3df0(actor, angle, step): the yaw snapped onto the angle when it is within step (|step|) of it, else turned
// by step toward it; returns the difference measured before the turn (0: already facing it)
int turnYawTo(int16_t& yaw, uint32_t angle, int step);
// exe 0x5e40c0(angle, r): the planar vector of length r along the angle, (sine, cosine)(angle + a half turn) * r >> 12
// (each truncated to a short)
void angleVector(int angle, int r, int16_t& dx, int16_t& dz);
// exe 0x5e4070(out, angle, r): (sine, 0, cosine)(angle + a half turn) * r, the 32-bit products (a 16.16 vector of
// length r)
void angleRadiusVector(int angle, int r, int32_t out[3]);

// The 64-step sine / cosine tables the boot builds in the game state (exe 0x54a683, game/audio_logic: every 64th
// entry of the exe table 0x847ad8); the classes that read these, rather than the exe table, read the game state
struct Trig64Tables {
    StateArray<int16_t, 64> sine{}; // 0xa6c660
    StateArray<int16_t, 64> cosine{}; // 0xa72500
};
inline Trig64Tables& trig64Tables() {
    static Trig64Tables s;
    return s;
}
// the sine / cosine of 64-step angle i (i & 0x3f) from the game state's tables
inline int16_t sin64(uint32_t i) { return trig64Tables().sine[i & 0x3f]; }
inline int16_t cos64(uint32_t i) { return trig64Tables().cosine[i & 0x3f]; }
// the same 64-step angles read from the exe table (the classes that read it rather than the game state's copies)
inline int16_t fixedSin64(uint32_t i) { return fixedSin(int((i & 0x3f) << 6)); }
inline int16_t fixedCos64(uint32_t i) { return fixedCos(int((i & 0x3f) << 6)); }

} // namespace od::port
