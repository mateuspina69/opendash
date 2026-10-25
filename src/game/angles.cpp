// The game's angle math: the sine table, the arctangent, angle differences and directions (see angles.hpp).
#include "game/angles.hpp"
#include "game/fixed_point.hpp"
#include "game/distances.hpp"
#include "game/vectors.hpp"

#include "engine/data_source.hpp"
#include "engine/x87.hpp"

namespace od::port {

namespace {

// exe table 0x847ad8: 4096 records {s16 sine, s16 cosine}
struct TrigRecord : DataRecord {
    int16_t sine = 0, cosine = 0;
    static TrigRecord load(const TableReader& r) { return {{}, r.s16(0), r.s16(2)}; }
};
const DataTable<RecordTable<TrigRecord, 0x1000, 4>> trigTable{TableId::TrigTable};

// exe constant 0x831848: the double 1 / 3.14 that turns the arctangent's radians into angle units (with * 2048)
const DataTable<ScalarTable<double, 1>> atanScaleTable{TableId::AtanScale};

} // namespace

int16_t fixedSin(int angle) { return trigTable[uint32_t(angle) & kAngleMask].sine; }
int16_t fixedCos(int angle) { return trigTable[uint32_t(angle) & kAngleMask].cosine; }
bool fixedSinMapped(int) { return trigTable.available(); }
bool fixedCosMapped(int) { return trigTable.available(); }

double atanScale() { return atanScaleTable[0]; }

int atanAngle(float a, float b) { return x87::atanAngle(atanScale(), a, b); }

int angleDiff(uint32_t from, uint32_t to) {
    int d = int(to & kAngleMask) - int(from & kAngleMask);
    if (uint32_t(d + kHalfTurn) > 0x1000u) d = int((uint32_t(-d) ^ uint32_t(kAngleMask)) + 1u);
    return d;
}

int angleOf(const Fixed16Vec3& from, const Fixed16Vec3& to) { return angleOf(from.x, from.z, to.x, to.z); }

int angleOf(int32_t fromX, int32_t fromZ, int32_t toX, int32_t toZ) {
    const int32_t dx = int32_t(uint32_t(toX) - uint32_t(fromX)), dz = int32_t(uint32_t(toZ) - uint32_t(fromZ));
    return atanAngle(float(dx), float(dz)) & kAngleMask;
}

int angleOf(const int16_t from[3], const int16_t to[3]) {
    return atanAngle(float(int(to[0]) - int(from[0])), float(int(to[2]) - int(from[2]))) & kAngleMask;
}

int planarAngleOf(int16_t fromX, int16_t fromZ, int16_t toX, int16_t toZ) {
    return atanAngle(float(int(toX) - int(fromX)), float(int(toZ) - int(fromZ))) & kAngleMask;
}

int angleOf(const ShortVec3& from, const ShortVec3& to) {
    const int16_t a[3] = {from.x, from.y, from.z}, b[3] = {to.x, to.y, to.z};
    return angleOf(a, b);
}

int pitchOf(const int32_t from[3], const int32_t to[3]) {
    const int dx = wholePart(to[0]) - wholePart(from[0]);
    const int dy = wholePart(to[1]) - wholePart(from[1]);
    const int dz = wholePart(to[2]) - wholePart(from[2]);
    const int d = isqrt(int32_t(uint32_t(dz * dz) + uint32_t(dx * dx)));
    return int((uint32_t(-kQuarterTurn) - uint32_t(atanAngle(float(d), float(dy)))) & kAngleMask);
}

int pitchOf(const Fixed16Vec3& from, const Fixed16Vec3& to) {
    const int16_t a[3] = {from.x.whole, from.y.whole, from.z.whole};
    const int16_t b[3] = {to.x.whole, to.y.whole, to.z.whole};
    return pitchOf(a, b);
}

int pitchOf(const Fixed16Vec3& from, const int16_t to[3]) {
    const int16_t a[3] = {from.x.whole, from.y.whole, from.z.whole};
    return pitchOf(a, to);
}

int pitchOf(const int16_t from[3], const int16_t to[3]) {
    const int32_t dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
    const int d = isqrt(int32_t(uint32_t(dz) * uint32_t(dz) + uint32_t(dx) * uint32_t(dx)));
    return int((uint32_t(-kQuarterTurn) - uint32_t(atanAngle(float(d), float(dy)))) & kAngleMask);
}

int pitchOf(const ShortVec3& from, const ShortVec3& to) {
    const int16_t a[3] = {from.x, from.y, from.z}, b[3] = {to.x, to.y, to.z};
    return pitchOf(a, b);
}

int pitchOf(const ShortVec3& from, const int16_t to[3]) {
    const int16_t a[3] = {from.x, from.y, from.z};
    return pitchOf(a, to);
}

int pitchOf(const int16_t from[3], const ShortVec3& to) {
    const int16_t b[3] = {to.x, to.y, to.z};
    return pitchOf(from, b);
}

int turnYawTo(int16_t& yaw, uint32_t angle, int step) {
    const int d = angleDiff(uint32_t(int32_t(yaw)), angle);
    const int t = step < 0 ? -step : step;
    if (uint32_t(t + d) <= uint32_t(t * 2)) {
        yaw = wrap16(yaw + (step > 0 ? d : -d));
        return d;
    }
    if (t < d) yaw = wrap16(yaw + step);
    if (d < -t) yaw = wrap16(yaw - step);
    return d;
}

void angleVector(int angle, int r, int16_t& dx, int16_t& dz) {
    dx = wrap16((int32_t(fixedSin(angle + kHalfTurn)) * r) >> 12);
    dz = wrap16((int32_t(fixedCos(angle + kHalfTurn)) * r) >> 12);
}

void angleRadiusVector(int angle, int r, int32_t out[3]) {
    out[0] = int32_t(uint32_t(int32_t(fixedSin(angle + kHalfTurn))) * uint32_t(r));
    out[1] = 0;
    out[2] = int32_t(uint32_t(int32_t(fixedCos(angle + kHalfTurn))) * uint32_t(r));
}

int turnToward(int16_t& yaw, int angle, int step) {
    const int d = angleDiff(uint32_t(int32_t(yaw)), uint32_t(angle));
    const int t = step < 0 ? -step : step;
    if (t <= d) yaw = int16_t(uint16_t(yaw + step));
    if (d <= -t) yaw = int16_t(uint16_t(yaw - step));
    return d;
}

} // namespace od::port
