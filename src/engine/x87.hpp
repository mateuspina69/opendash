// The exe's x87 code runs with the precision control at 24 bits (FPU control word PC = 00: the oracle's FPCW, as
// Direct3D 7 leaves it), so every FMUL / FDIV rounds its exact result once to a float mantissa. A double product
// cast to float rounds twice; mul24 corrects the halfway cases with the product's exact error (fma).
#pragma once

#include <cmath>
#include <cstdint>

namespace od::port::x87 {

// x * y rounded once to 24 bits (round to nearest even), as FMUL under PC = 24
inline float mul24(double x, double y) {
    const double p = x * y;
    const float r = float(p);
    const double rd = double(r);
    if (rd == p) return r;
    const float other = std::nextafter(r, p > rd ? INFINITY : -INFINITY);
    const double mid = (rd + double(other)) * 0.5;
    if (p != mid) return r;
    const double err = std::fma(x, y, -p); // the exact product is p + err
    if (err == 0) return r;                // a real tie: float(p) already rounded to even
    return (err > 0) == (double(other) > rd) ? other : r;
}

// FUN_0040b960(a, b): FPATAN(a / b) * 2048.0 * [0x831848], __ftol (truncation)
inline int atanAngle(double k, float a, float b) {
    const float t = float(std::atan2(double(a), double(b))); // FPATAN, rounded at the precision control
    const float v = mul24(double(t), 2048.0);
    return int(int64_t(mul24(double(v), k)));
}

} // namespace od::port::x87
