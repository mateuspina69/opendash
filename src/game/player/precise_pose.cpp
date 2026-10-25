// The precise poses for drawing only (see precise_pose.hpp).
#include "game/player/precise_pose.hpp"

#include <cmath>
#include <unordered_map>


namespace od::precise {

namespace {

bool g_on = true;

template <int N>
struct Entry {
    int16_t fixed[N];
    float v[N];
};
std::unordered_map<const int16_t*, Entry<9>> g_nativeMat;
std::unordered_map<const int16_t*, Entry<3>> g_nativePt;

// ... of a native pose: the fixed values at `fixed` (the storage itself is the key)
template <int N>
void putNative(std::unordered_map<const int16_t*, Entry<N>>& map, const int16_t* fixed, const float* v) {
    if (!g_on) return;
    Entry<N>& e = map[fixed];
    for (int i = 0; i < N; ++i) {
        e.fixed[i] = fixed[i];
        e.v[i] = v[i];
    }
}
template <int N>
bool getNative(const std::unordered_map<const int16_t*, Entry<N>>& map, const int16_t* fixed, float* out) {
    if (!g_on) return false;
    const auto it = map.find(fixed);
    if (it == map.end()) return false;
    for (int i = 0; i < N; ++i)
        if (it->second.fixed[i] != fixed[i]) return false;
    for (int i = 0; i < N; ++i) out[i] = it->second.v[i];
    return true;
}

// the float rotations of a 12-bit angle
constexpr float kAngle = 6.28318530717958647692f / 4096.0f;
F3 rz(int a) {
    const float c = std::cos(float(a) * kAngle), s = std::sin(float(a) * kAngle);
    return {{c, -s, 0, s, c, 0, 0, 0, 1}};
}
F3 rx(int a) {
    const float c = std::cos(float(a) * kAngle), s = std::sin(float(a) * kAngle);
    return {{1, 0, 0, 0, c, -s, 0, s, c}};
}
F3 ry(int a) {
    const float c = std::cos(float(a) * kAngle), s = std::sin(float(a) * kAngle);
    return {{c, 0, s, 0, 1, 0, -s, 0, c}};
}

} // namespace

// the precise pose on / off (off: the records cleared)
bool enabled() { return g_on; }
void setEnabled(bool on) {
    g_on = on;
    if (!on) {
        g_nativeMat.clear();
        g_nativePt.clear();
    }
}

// the records of the matrices / the points and their lookups
void putMatrix(const int16_t* fixed, const float v[9]) { putNative(g_nativeMat, fixed, v); }
void putPoint(const int16_t* fixed, const float v[3]) { putNative(g_nativePt, fixed, v); }
bool matrix(const int16_t* fixed, float out[9]) { return getNative(g_nativeMat, fixed, out); }
bool point(const int16_t* fixed, float out[3]) { return getNative(g_nativePt, fixed, out); }


// the float products and rotations (see precise_pose.hpp)
F3 fmul(const F3& a, const F3& b) {
    F3 o{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            o.e[i * 3 + j] = a.e[i * 3] * b.e[j] + a.e[i * 3 + 1] * b.e[3 + j] + a.e[i * 3 + 2] * b.e[6 + j];
    return o;
}
F3 frotXYZ(const int16_t* ang) { return fmul(rx(ang[0]), fmul(ry(ang[1]), rz(ang[2]))); }
F3 frotYXZ(const int16_t* ang) { return fmul(ry(ang[1]), fmul(rx(ang[0]), rz(ang[2]))); }
F3 fromFixed(const int16_t* e) {
    F3 o;
    for (int i = 0; i < 9; ++i) o.e[i] = float(e[i]) / 4096.0f;
    return o;
}

} // namespace od::precise
