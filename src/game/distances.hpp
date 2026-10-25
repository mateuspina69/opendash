// The game's distance math: the x87 integer square root and the distances between points the actors measure (the
// integer parts of their 16.16 positions, or s16 points), with the exe's 16-bit differences.
#pragma once

#include <cstdint>

#include "game/vectors.hpp"

namespace od::port {

// exe 0x40b980: (int) sqrt(n); a negative argument gives the x87's integer indefinite 0x80000000
int isqrt(int32_t n);
// exe 0x5e3a60 (and exe 0x5e3a10 on two actors' positions): the 3D distance of the integer parts of two 16.16 points
// (16-bit differences, 32-bit wrapping square sum)
int distance3(const Fixed16Vec3& a, const Fixed16Vec3& b);
// ... on the integer parts already taken (s16 points {x, y, z})
int distance3(const int16_t a[3], const int16_t b[3]);
// ... from a 16.16 point in memory to an s16 point / to 16.16 values
int distance3(const Fixed16Vec3& a, const int16_t b[3]);
int distance3(const Fixed16Vec3& a, const int32_t b[3]);
// exe 0x5e3b00: the x / z distance of the integer parts of two 16.16 points (16-bit differences)
int planarDistance(const Fixed16Vec3& a, const Fixed16Vec3& b);
// ... from a 16.16 point in memory to 16.16 values
int planarDistance(const Fixed16Vec3& a, const int32_t b[3]);
// exe 0x5e3b40: the x / z distance of two s16 points {x, y, z}
int planarDistance16(const int16_t a[3], const int16_t b[3]);
// ... on two s16 points in memory
int planarDistance16(const ShortVec3& a, const ShortVec3& b);

} // namespace od::port
