// The exe's 3x3 matrices: products, the rotation builders, the scale and the vector transform (see matrix.hpp).
#include "game/matrix.hpp"

#include <cmath>

#include "engine/data_source.hpp"
#include "game/angles.hpp"
#include "game/fixed_point.hpp"
#include "game/vectors.hpp"

namespace od::port {

Matrix3 mulMatrix3x3(const Matrix3& a, const Matrix3& b) {
    Matrix3 o{};
    mulMatrix3x3(a.e, b.e, o.e);
    return o;
}

PaddedMatrix3 mulMatrix3x3(const PaddedMatrix3& a, const PaddedMatrix3& b) {
    PaddedMatrix3 o{};
    mulMatrix3x3(a.e, b.e, o.e);
    o.e[9] = a.e[9];
    return o;
}

Matrix3 rotationX(int angle) {
    const int c = fixedCos(angle), s = fixedSin(angle);
    return {{kFixedOne, 0, 0, 0, wrap16(c), wrap16(-s), 0, wrap16(s), wrap16(c)}};
}
Matrix3 rotationY(int angle) {
    const int c = fixedCos(angle), s = fixedSin(angle);
    return {{wrap16(c), 0, wrap16(s), 0, kFixedOne, 0, wrap16(-s), 0, wrap16(c)}};
}
Matrix3 rotationZ(int angle) {
    const int c = fixedCos(angle), s = fixedSin(angle);
    return {{wrap16(c), wrap16(-s), 0, wrap16(s), wrap16(c), 0, 0, 0, kFixedOne}};
}

// Each builder starts from the identity and left-multiplies the elementary rotations in the exe's order (the
// products with the identity are exact, the later ones truncate as the exe's do).
Matrix3 rotationXYZ(int x, int y, int z) {
    return mulMatrix3x3(rotationX(x), mulMatrix3x3(rotationY(y), mulMatrix3x3(rotationZ(z), kIdentityMatrix)));
}
Matrix3 rotationYXZ(int x, int y, int z) {
    return mulMatrix3x3(rotationY(y), mulMatrix3x3(rotationX(x), mulMatrix3x3(rotationZ(z), kIdentityMatrix)));
}
Matrix3 rotationZXY(int x, int y, int z) {
    return mulMatrix3x3(rotationZ(z), mulMatrix3x3(rotationX(x), mulMatrix3x3(rotationY(y), kIdentityMatrix)));
}
Matrix3 rotationZYX(int x, int y, int z) {
    return mulMatrix3x3(rotationZ(z), mulMatrix3x3(rotationY(y), mulMatrix3x3(rotationX(x), kIdentityMatrix)));
}

Matrix3 scaleColumns(const Matrix3& m, const int32_t s[3]) {
    Matrix3 o{};
    for (int i = 0; i < 9; ++i)
        o.e[i] = wrap16(int32_t(uint32_t(int32_t(m.e[i])) * uint32_t(s[i % 3])) >> 12);
    return o;
}

Matrix3 transposeMatrix(const Matrix3& m) {
    Matrix3 t{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) t.e[i * 3 + j] = m.e[j * 3 + i];
    return t;
}

bool pointInBeam(const int16_t origin[3], const int16_t point[3], const Matrix3& m, uint32_t length, uint32_t radius) {
    static constexpr int32_t kUnitScale[3] = {kFixedOne, kFixedOne, kFixedOne};
    const int16_t d[3] = {wrap16(point[0] - origin[0]), wrap16(point[1] - origin[1]), wrap16(point[2] - origin[2])};
    int16_t r[3];
    transformShortVector(scaleColumns(transposeMatrix(m), kUnitScale), d, r);
    const int reach = int(radius & 0xffff);
    if ((r[0] < 0 ? -r[0] : r[0]) >= reach || (r[1] < 0 ? -r[1] : r[1]) >= reach) return false;
    return uint32_t(-int(r[2])) < (length & 0xffff) + uint32_t(reach);
}

void transformVector(const Matrix3& m, const int16_t v[3], int32_t out[3]) {
    for (int i = 0; i < 3; ++i) {
        const uint32_t sum = uint32_t(int32_t(m.e[i * 3])) * uint32_t(int32_t(v[0])) +
                             uint32_t(int32_t(m.e[i * 3 + 1])) * uint32_t(int32_t(v[1])) +
                             uint32_t(int32_t(m.e[i * 3 + 2])) * uint32_t(int32_t(v[2]));
        out[i] = int32_t(sum) >> 12;
    }
}

void transformShortVector(const Matrix3& m, const int16_t v[3], int16_t out[3]) {
    int16_t o[3];
    for (int i = 0; i < 3; ++i)
        o[i] = wrap16(int32_t(uint32_t(int32_t(m.e[i * 3])) * uint32_t(int32_t(v[0])) +
                              uint32_t(int32_t(m.e[i * 3 + 1])) * uint32_t(int32_t(v[1])) +
                              uint32_t(int32_t(m.e[i * 3 + 2])) * uint32_t(int32_t(v[2]))) >>
                      12);
    for (int i = 0; i < 3; ++i) out[i] = o[i];
}

void transformVectorFloat(const Matrix3& m, const int32_t v[3], int32_t out[3]) {
    auto product = [](int16_t element, int32_t x) { return float(double(element) * (double(x) / 4096.0)); };
    for (int i = 0; i < 3; ++i) {
        float s = product(m.e[i * 3 + 2], v[2]);
        s = float(double(s) + double(product(m.e[i * 3 + 1], v[1])));
        s = float(double(s) + double(product(m.e[i * 3], v[0])));
        out[i] = int32_t(s);
    }
}

void rotateVector(const int16_t angles[3], int x, int y, int z, int32_t out[3]) {
    const int32_t v[3] = {int32_t(uint32_t(x) << 12), int32_t(uint32_t(y) << 12), int32_t(uint32_t(z) << 12)};
    transformVectorFloat(rotationYXZ(angles[0], angles[1], angles[2]), v, out);
}

void rotateVector(const ShortVec4& angles, int x, int y, int z, int32_t out[3]) {
    const int16_t a[3] = {angles.x, angles.y, angles.z};
    rotateVector(a, x, y, z, out);
}

namespace {

// the exe's identity table 0x847ac4 (exe data: the matrix and its padding short)
struct IdentityTable : DataRecord {
    static constexpr uint32_t kBytes = 0x14;
    Matrix3 matrix{};
    int16_t pad = 0;
    static IdentityTable load(const TableReader& r) {
        IdentityTable t;
        for (size_t k = 0; k < 9; ++k) t.matrix.e[k] = r.s16(k * 2);
        t.pad = r.s16(0x12);
        return t;
    }
};
const DataTable<IdentityTable> identityTable{TableId::IdentityMatrix};

} // namespace

void storeRotationYXZ(int x, int y, int z, Matrix3& matrix, int16_t& pad) {
    matrix = identityTable->matrix;
    pad = identityTable->pad;
    matrix = rotationYXZ(x, y, z);
}

int16_t identityTablePad() { return identityTable->pad; }

void normalizeVector(const int32_t v[3], int32_t out[3]) {
    const float x = float(v[0]), y = float(v[1]);
    const double z = double(v[2]);
    double len = std::sqrt(z * z + double(y) * double(y) + double(x) * double(x));
    if (len == 0.0) len = 1.0;
    out[0] = int32_t(double(x) * 4096.0 / len);
    out[1] = int32_t(double(y) * 4096.0 / len);
    out[2] = int32_t(z * 4096.0 / len);
}

} // namespace od::port
