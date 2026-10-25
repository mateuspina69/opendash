// The game's distance math: the integer square root and the distances between points (see distances.hpp).
#include "game/distances.hpp"
#include "game/fixed_point.hpp"
#include "game/vectors.hpp"

#include <cmath>

namespace od::port {

int isqrt(int32_t n) {
    if (n < 0) return int32_t(0x80000000u);
    return int(std::sqrt(double(n)));
}

int distance3(const Fixed16Vec3& a, const Fixed16Vec3& b) {
    const int16_t from[3] = {a.x.whole, a.y.whole, a.z.whole}, to[3] = {b.x.whole, b.y.whole, b.z.whole};
    return distance3(from, to);
}

int distance3(const Fixed16Vec3& a, const int16_t b[3]) {
    const int16_t from[3] = {a.x.whole, a.y.whole, a.z.whole};
    return distance3(from, b);
}

int distance3(const Fixed16Vec3& a, const int32_t b[3]) {
    const int16_t to[3] = {int16_t(uint32_t(b[0]) >> 16), int16_t(uint32_t(b[1]) >> 16), int16_t(uint32_t(b[2]) >> 16)};
    return distance3(a, to);
}

int distance3(const int16_t a[3], const int16_t b[3]) {
    const int dx = wrap16(b[0] - a[0]), dy = wrap16(b[1] - a[1]), dz = wrap16(b[2] - a[2]);
    return isqrt(int32_t(uint32_t(dx * dx) + uint32_t(dy * dy) + uint32_t(dz * dz)));
}

int planarDistance(const Fixed16Vec3& a, const Fixed16Vec3& b) {
    const int dx = wrap16(b.x.whole - a.x.whole), dz = wrap16(b.z.whole - a.z.whole);
    return isqrt(int32_t(uint32_t(dx * dx) + uint32_t(dz * dz)));
}

int planarDistance(const Fixed16Vec3& a, const int32_t b[3]) {
    const int dx = wrap16(wholePart(b[0]) - a.x.whole), dz = wrap16(wholePart(b[2]) - a.z.whole);
    return isqrt(int32_t(uint32_t(dx * dx) + uint32_t(dz * dz)));
}

int planarDistance16(const int16_t a[3], const int16_t b[3]) {
    const int dx = wrap16(b[0] - a[0]), dz = wrap16(b[2] - a[2]);
    return isqrt(int32_t(uint32_t(dx * dx) + uint32_t(dz * dz)));
}

int planarDistance16(const ShortVec3& a, const ShortVec3& b) {
    const int16_t from[3] = {a.x, a.y, a.z}, to[3] = {b.x, b.y, b.z};
    return planarDistance16(from, to);
}

} // namespace od::port
