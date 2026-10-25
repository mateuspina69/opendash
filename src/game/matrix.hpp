// The exe's 3x3 matrices: rotation matrices of 4.12 fixed-point shorts (0x1000 = 1.0), row-major, 9 elements (the
// exe stores them in 5 dwords; the 10th short is padding): products, the rotation builders of the 12-bit angles,
// and the field proxy of a matrix kept in the game state.
#pragma once

#include <cstdint>

#include "game/vectors.hpp"

namespace od::port {

struct Matrix3 {
    int16_t e[9];
};

// the identity (the exe's copy is the table 0x847ac4)
inline constexpr Matrix3 kIdentityMatrix{{0x1000, 0, 0, 0, 0x1000, 0, 0, 0, 0x1000}};

// exe 0x40bc90: out = a * b, each element the sum of the three products in 32-bit wrapping arithmetic, >> 12
// (arithmetic), truncated to a short. The element type T only carries the values (shorts; some callers keep them
// in ints). out may be a or b.
template <class T> void mulMatrix3x3(const T* a, const T* b, T* out) {
    T o[9];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            const uint32_t sum = uint32_t(int32_t(a[i * 3])) * uint32_t(int32_t(b[j])) +
                                 uint32_t(int32_t(a[i * 3 + 1])) * uint32_t(int32_t(b[3 + j])) +
                                 uint32_t(int32_t(a[i * 3 + 2])) * uint32_t(int32_t(b[6 + j]));
            o[i * 3 + j] = T(int16_t(uint16_t(uint32_t(int32_t(sum) >> 12))));
        }
    for (int k = 0; k < 9; ++k) out[k] = o[k];
}
// exe 0x40bc90 on Matrix3 values: a * b
Matrix3 mulMatrix3x3(const Matrix3& a, const Matrix3& b);

// a matrix with the padding short of its 5-dword image (the routines that copy the image whole)
struct PaddedMatrix3 {
    int16_t e[10];
};
// exe 0x40bc90 on padded matrices: a * b; the exe's 5-dword store leaves stack garbage in the result's padding
// short, OpenDash keeps a's
PaddedMatrix3 mulMatrix3x3(const PaddedMatrix3& a, const PaddedMatrix3& b);


// ---- rotations of the 12-bit angles (the sines of game/angles) ---------------------------------------------------
// the elementary rotations (exe 0x40be30 / 0x40bea0 / 0x40bf10 apply them to a matrix: R * m)
Matrix3 rotationX(int angle);
Matrix3 rotationY(int angle);
Matrix3 rotationZ(int angle);
// exe 0x40bf80: Rx(x) * Ry(y) * Rz(z)
Matrix3 rotationXYZ(int x, int y, int z);
// exe 0x40bfd0 (the rotation of an actor's angle triple): Ry(y) * Rx(x) * Rz(z)
Matrix3 rotationYXZ(int x, int y, int z);
// exe 0x40c020 (the inverse order of 0x40bfd0): Rz(z) * Rx(x) * Ry(y)
Matrix3 rotationZXY(int x, int y, int z);
// exe 0x40c070: Rz(z) * Ry(y) * Rx(x)
Matrix3 rotationZYX(int x, int y, int z);

// exe 0x40c0e0: the columns of m scaled by s[0..2] (element * s >> 12, truncated to a short)
Matrix3 scaleColumns(const Matrix3& m, const int32_t s[3]);
// exe 0x40c170: the transpose
Matrix3 transposeMatrix(const Matrix3& m);
// exe 0x40bab0: out = (m * v) >> 12 per row (32-bit sums)
void transformVector(const Matrix3& m, const int16_t v[3], int32_t out[3]);
// exe 0x40bb30(m, v, out): the s16 out = (m * v) >> 12 per row (32-bit sums, each truncated to a short)
void transformShortVector(const Matrix3& m, const int16_t v[3], int16_t out[3]);
// exe 0x5e5b40(origin, point, m, length, radius): the point inside the beam from the origin along -z of m (the point
// - origin, 16-bit, through the transpose of m, exe 0x40c170 / 0x40c0e0 / 0x40bb30: |x| and |y| below the radius, -z
// below length + radius; the radius and the length taken & 0xffff, unsigned)
bool pointInBeam(const int16_t origin[3], const int16_t point[3], const Matrix3& m, uint32_t length, uint32_t radius);
// exe 0x40bbb0: out = m * v / 4096 per row as the exe's x87 computes it at 24-bit precision (engine/x87.hpp): v / 4096
// exact in double (the exe rounds it to a float mantissa: equal while |v| < 2^24), each product and each partial sum
// rounded to a float, in the exe's order (m2 v2 + m1 v1) + m0 v0, truncated toward zero (__ftol)
void transformVectorFloat(const Matrix3& m, const int32_t v[3], int32_t out[3]);
// the padding short exe 0x40bfd0 leaves in a matrix it builds (its copy of the identity table's 5 dwords)
int16_t identityTablePad();
// exe 0x40bfd0(angles, m) on a matrix and its padding short: the identity table's image (its padding
// short included) first, then Ry Rx Rz of the angles x / y / z
void storeRotationYXZ(int x, int y, int z, Matrix3& matrix, int16_t& pad);
// exe 0x5e3970(out, rot, x, y, z): Ry Rx Rz(angles x / y / z) * (x, y, z) << 12 through exe 0x40bbb0 (a 16.16 vector)
void rotateVector(const int16_t angles[3], int x, int y, int z, int32_t out[3]);
// ... on an angle quad
void rotateVector(const ShortVec4& angles, int x, int y, int z, int32_t out[3]);

// exe 0x40c320(v, out): the 32-bit vector scaled to length 4096 (the x87's: x / y rounded to floats, the length in
// extended precision -- a double here; a zero vector divides by 1), truncated
void normalizeVector(const int32_t v[3], int32_t out[3]);

} // namespace od::port
