// A view transform as the exe keeps it (0x20 bytes: the 3x3 matrix's nine shorts, its padding short, the translation's
// three words) over native lanes (game/field_alias Lane16 / Lane32): its owner's own members (the camera's view, the
// projection's loaded camera: TransformMembers) or the geometry scratch's (game/geometry_scratch, whose canonical
// members the scratch's transforms name). Assigning one to another copies the 0x20 bytes (the padding short included,
// as the exe's copy does).
#pragma once

#include <array>
#include <cstdint>

#include "game/field_alias.hpp"
#include "game/matrix.hpp"
#include "game/state_array.hpp"

namespace od::port {

// A matrix's nine shorts over lanes
class LaneMatrix3 {
public:
    explicit LaneMatrix3(const std::array<Lane16, 9>& e) : e_(e) {}
    LaneMatrix3(const LaneMatrix3&) = default;
    Matrix3 get() const {
        Matrix3 m{};
        for (size_t k = 0; k < 9; ++k) m.e[k] = e_[k].get();
        return m;
    }
    operator Matrix3() const { return get(); }
    LaneMatrix3& operator=(const Matrix3& m) {
        for (size_t k = 0; k < 9; ++k) e_[k] = m.e[k];
        return *this;
    }
    LaneMatrix3& operator=(const LaneMatrix3& o) { return *this = o.get(); }
    // element k (0..8)
    Lane16 element(size_t k) const { return e_[k]; }

private:
    std::array<Lane16, 9> e_;
};

class ViewTransform {
public:
    ViewTransform(const LaneMatrix3& m, Lane16 pad, const std::array<Lane32, 3>& t)
        : matrix(m), translation(t), pad_(pad) {}
    ViewTransform(const ViewTransform&) = default;
    ViewTransform& operator=(const ViewTransform& o) {
        matrix = o.matrix.get();
        pad_ = o.pad_.get();
        copyTranslation(o);
        return *this;
    }

    // exe 0x40c3c0: the matrix and its padding short = another's (5 dwords)
    void copyMatrixWords(const ViewTransform& o) {
        matrix = o.matrix.get();
        pad_ = o.pad_.get();
    }
    // ... = a matrix value and its padding short (the exe's copy of a stack matrix: 5 dwords)
    void setMatrixWords(const Matrix3& m, int16_t pad) {
        matrix = m;
        pad_ = pad;
    }
    // exe 0x40c3e0: the translation = another's (3 dwords)
    void copyTranslation(const ViewTransform& o) {
        for (size_t i = 0; i < 3; ++i) translation[i] = o.translation[i].get();
    }
    // the identity matrix (its padding short 0) and a zero translation, as the exe stores them (8 dwords)
    void setIdentityWords() {
        setMatrixWords(kIdentityMatrix, 0);
        for (size_t i = 0; i < 3; ++i) translation[i] = 0;
    }
    // exe 0x40bfd0(angles, matrix): the identity table's image, then Ry Rx Rz of the angles' x / y / z
    void setRotationYXZ(const ShortVec4& angles) {
        Matrix3 m{};
        int16_t pad = 0;
        storeRotationYXZ(angles.x, angles.y, angles.z, m, pad);
        setMatrixWords(m, pad);
    }
    void setRotationYXZ(const LaneVec4& angles) { setRotationYXZ(angles.vec4()); }
    // its padding short
    int16_t pad() const { return pad_.get(); }

    LaneMatrix3 matrix;
    std::array<Lane32, 3> translation;

private:
    Lane16 pad_;
};

// A transform's own native members (an owner that keeps one: the camera's view, the projection's loaded camera)
struct TransformMembers {
    Matrix3 matrix{};
    int16_t pad = 0;
    StateArray<int32_t, 3> translation{};

    ViewTransform lanes() {
        return ViewTransform(LaneMatrix3({Lane16(matrix.e[0]), Lane16(matrix.e[1]), Lane16(matrix.e[2]),
                                          Lane16(matrix.e[3]), Lane16(matrix.e[4]), Lane16(matrix.e[5]),
                                          Lane16(matrix.e[6]), Lane16(matrix.e[7]), Lane16(matrix.e[8])}),
                             Lane16(pad), {Lane32(translation[0]), Lane32(translation[1]), Lane32(translation[2])});
    }
};

} // namespace od::port
