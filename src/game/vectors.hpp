// The native state's small vectors:
// the s16 triples and quads the systems keep (angles, points, speeds).
#pragma once

#include <cstdint>

namespace od::port {

// Three shorts (x, y, z): an s16 point or angle triple
struct ShortVec3 {
    int16_t x = 0, y = 0, z = 0;
};

// Four shorts: x, y, z and a spare 4th lane (padding to the generic code; a class may keep a value of its own there).
// The exe copies the four lanes as two dwords (the helpers' words). A routine on a point takes its x / y / z as a
// ShortVec3.
struct ShortVec4 : ShortVec3 {
    int16_t spare = 0;

    ShortVec4() = default;
    constexpr ShortVec4(int16_t vx, int16_t vy, int16_t vz, int16_t s = 0) : ShortVec3{vx, vy, vz}, spare(s) {}

    void clearWords() { *this = ShortVec4{}; }
    void clearBytes() { *this = ShortVec4{}; }
    void setXY(int16_t vx, int16_t vy) {
        x = vx;
        y = vy;
    }
    // the four lanes from two dwords (x | y << 16, z | spare << 16)
    void setWords(uint32_t xy, uint32_t zSpare) {
        x = int16_t(uint16_t(xy));
        y = int16_t(uint16_t(xy >> 16));
        z = int16_t(uint16_t(zSpare));
        spare = int16_t(uint16_t(zSpare >> 16));
    }
    void setZSpare(int16_t vz, int16_t s) {
        z = vz;
        spare = s;
    }
    // x, y, z and a zero spare lane
    void assign(int16_t vx, int16_t vy, int16_t vz) {
        x = vx;
        y = vy;
        z = vz;
        spare = 0;
    }
    uint32_t xyWord() const { return uint32_t(uint16_t(x)) | uint32_t(uint16_t(y)) << 16; }
    uint32_t zSpareWord() const { return uint32_t(uint16_t(z)) | uint32_t(uint16_t(spare)) << 16; }
};

// A 16.16 fixed-point value: .frac (the low half), .whole (the high half, signed) and the whole 32-bit value through
// the conversions. The compound operators wrap at 32 bits like the exe's arithmetic (unsigned + - << , arithmetic >>).
struct Fixed16 {
    uint16_t frac = 0;
    int16_t whole = 0;

    constexpr int32_t value() const { return int32_t(uint32_t(frac) | uint32_t(uint16_t(whole)) << 16); }
    constexpr operator int32_t() const { return value(); }
    constexpr Fixed16& operator=(int32_t v) {
        frac = uint16_t(uint32_t(v));
        whole = int16_t(uint16_t(uint32_t(v) >> 16));
        return *this;
    }
    template <class U> Fixed16& operator+=(U v) { return *this = int32_t(uint32_t(value()) + uint32_t(v)); }
    template <class U> Fixed16& operator-=(U v) { return *this = int32_t(uint32_t(value()) - uint32_t(v)); }
    template <class U> Fixed16& operator|=(U v) { return *this = int32_t(uint32_t(value()) | uint32_t(v)); }
    template <class U> Fixed16& operator&=(U v) { return *this = int32_t(uint32_t(value()) & uint32_t(v)); }
    template <class U> Fixed16& operator<<=(U v) { return *this = int32_t(uint32_t(value()) << uint32_t(v)); }
    template <class U> Fixed16& operator>>=(U v) { return *this = int32_t(value() >> uint32_t(v)); }
};

// Three 16.16 values (x, y, z): a world position
struct Fixed16Vec3 {
    Fixed16 x, y, z;
};

} // namespace od::port
