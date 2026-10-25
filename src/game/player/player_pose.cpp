// The player's pose (see player_pose.hpp).
#include "game/player/player_pose.hpp"

#include <array>
#include <stdexcept>
#include <utility>
#include <vector>

#include "game/fixed_point.hpp"
#include "game/geometry_scratch.hpp"
#include "game/animation_data.hpp"
#include "game/player/player_snapshot.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/player/precise_pose.hpp"
#include "game/stage_state.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port {

namespace {

// The pose's share of the geometry scratch (game/geometry_scratch, over the room collision queries' state; the lanes
// past +0x87 are the pose's own): the current matrix (+0x00), the decoded bone angles of both channels (s16 x / y /
// z, a zero lane; +0x20 / +0xa0), the frames' packed words (+0x64 / +0xc8; the same lanes as the angles, as the exe
// decodes over them); its matrix stack: poseStack() (player_pose.hpp)
struct PoseScratch {
    LanePoseMatrix current = matrixAt(0x00);
    std::array<LaneVec4, 16> bodyAngles = geometryScratch().vec4s<16>(0x20);
    std::array<LaneVec4, 9> upperAngles = geometryScratch().vec4s<9>(0xa0);
    std::array<Lane32, 32> bodyWords = geometryScratch().words<32>(0x64); // the frame's words, then the blend target's
    std::array<Lane32, 18> upperWords = geometryScratch().words<18>(0xc8);

private:
    static LanePoseMatrix matrixAt(uint32_t at) {
        GeometryScratch& g = geometryScratch();
        return {{g.lane(at), g.lane(at + 2), g.lane(at + 4), g.lane(at + 6), g.lane(at + 8), g.lane(at + 0xa),
                 g.lane(at + 0xc), g.lane(at + 0xe), g.lane(at + 0x10), g.lane(at + 0x12)},
                {g.word(at + 0x14), g.word(at + 0x18), g.word(at + 0x1c)}};
    }
};
} // namespace

PoseStack& poseStack() {
    static PoseStack s;
    return s;
}

namespace {

// the pose's scratch (one block for the program)
PoseScratch& scratch() {
    static PoseScratch s;
    return s;
}
constexpr uint32_t kStackBottom = kPoseStackBottom;
constexpr uint32_t kPoseMatrixSize = 0x20;
constexpr uint32_t kBodyBones = 16; // the body channel's decoded entries (the upper body's: kUpperBones)
constexpr uint32_t kUpperBones = 9;

// The player's skeleton (PS1 0x110800: each bone's offset from its parent)
constexpr uint32_t kPlayerSkeleton = 0x740800;
// The frame tables (the packed frames' dword offsets by frame-data index) of the player's animations, the special
// weapon arm's (0x60..), the buster arm's (0x70..); the stage's (0x80..) at its record + 8
constexpr uint32_t kPlayerBank = 0x740800;
constexpr uint32_t kPlayerFrames = 0x743a00;
constexpr uint32_t kSpecialArmFrames = 0x751e20;
constexpr uint32_t kBusterArmFrames = 0x751400;
constexpr uint32_t kStageFramesOffset = 8;
constexpr uint32_t kFrameWords = 0x10; // the dwords of one frame in a packed animation

// the 9 elements of a pose matrix's rotation
template <class M> Matrix3 rotationOf(const M& m) {
    Matrix3 r{};
    for (size_t i = 0; i < 9; ++i) r.e[i] = m.rotation[i];
    return r;
}
// ... the 9 elements stored (the padding short kept)
template <class M> void setRotation(M& m, const Matrix3& r) {
    for (size_t i = 0; i < 9; ++i) m.rotation[i] = r.e[i];
}

// the x / y / z lanes of a vector in memory
template <class V> std::array<int16_t, 3> xyz(const V& v) { return {v.x, v.y, v.z}; }
// ... of an s16 triple
std::array<int16_t, 3> xyz(const int16_t (&v)[3]) { return {v[0], v[1], v[2]}; }

// one angle lane of a packed frame word: 10-bit fields at 0 / 10 / 20 with a shared exponent in the top 2 bits
int16_t lane(uint32_t w, int k) {
    const int e = 6 - int(w >> 30);
    int16_t v;
    if (k == 0) v = int16_t(uint16_t(uint32_t(int16_t(w)) << 6));
    else if (k == 1) v = int16_t(w >> 4);
    else v = int16_t(w >> 14);
    return int16_t(int(v) >> e);
}

struct Pose {
    Player& p;
    const PlayerPoseData& data;
    PoseScratch& s = scratch();
    // the float twin of the current matrix (rotation, translation) and its stack, for the draw-only precise pose
    // (game/player/precise_pose)
    precise::F3 fm{};
    float ft[3]{};
    std::vector<std::pair<precise::F3, std::array<float, 3>>> fstack;

    // the stage's coordinate shift (world units -> bone-point units)
    int shift() const { return data.coordinateShift & 0x1f; }
    // a 16.16 position axis in float bone-point units
    float fpos(const Fixed16& v, int sh) const { return float(int32_t(v)) / 65536.0f * float(1 << sh); }

    // the stack's matrix at the exe address `top` (the exe's stack pointer: kStackBottom - (k + 1) * 0x20)
    PoseMatrix& stackEntry(uint32_t top) {
        PoseStack& st = poseStack();
        const uint32_t k = (kStackBottom - top) / kPoseMatrixSize - 1;
        if (top >= kStackBottom || (kStackBottom - top) % kPoseMatrixSize != 0 || k >= st.matrices.size())
            throw std::logic_error("player pose: the matrix stack's pointer past its entries");
        return st.matrices[k];
    }
    // exe 0x5e2d40: the current matrix pushed
    void push() {
        fstack.push_back({fm, {ft[0], ft[1], ft[2]}});
        const uint32_t top = poseStack().top - kPoseMatrixSize;
        poseStack().top = top;
        stackEntry(top).copyFrom(s.current);
    }
    // exe 0x5e2d60: the current matrix popped
    void pop() {
        if (!fstack.empty()) {
            fm = fstack.back().first;
            for (int i = 0; i < 3; ++i) ft[i] = fstack.back().second[size_t(i)];
            fstack.pop_back();
        }
        const uint32_t top = poseStack().top;
        s.current.copyFrom(stackEntry(top));
        poseStack().top = top + kPoseMatrixSize;
    }

    // exe 0x5e2000 / 0x5e2120: one channel's frame decoded (and blended toward its blend target)
    template <class Angles, class Words>
    void decode(const AnimBlend& blendOf, Angles& out, Words& words, uint32_t n, bool blend, size_t channel) {
        const int8_t frame = data.frames[channel];
        auto copy = [&](int8_t f, uint32_t first) {
            const auto src = data.frameBanks[channel].relative(int64_t(f) * kFrameWords * 4);
            for (uint32_t i = 0; i < n; ++i) words[first + i] = src.read<uint32_t>(i * 4);
        };
        copy(frame, 0);
        uint32_t u = 0;
        if (blend) {
            copy(blendOf.targetFrame, n);
            u = blendOf.steps & 0xf;
        }
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t w = words[i];
            int16_t a[3];
            for (int k = 0; k < 3; ++k) {
                a[k] = lane(w, k);
                if (!blend) continue;
                const int16_t b = lane(words[n + i], k);
                a[k] = wrap16(int16_t((int(b) - int(a[k])) * int(u) >> 4) + a[k]);
            }
            out[i].assign(a[0], a[1], a[2]);
        }
    }

    // exe 0x5e29b0 (Rx * Ry * Rz) / 0x5e2b70 (Ry * Rx * Rz): bone b placed at its offset, its matrix and point stored
    void placeBone(uint32_t b, const int16_t ang[3], const int16_t ofs[3], bool yxz) {
        const int sh = shift();
        const int16_t v[3] = {wrap16(ofs[0] << sh), wrap16(ofs[1] << sh), wrap16(ofs[2] << sh)};
        const Matrix3 cur = rotationOf(s.current);
        int32_t t[3];
        transformVector(cur, v, t);
        const Matrix3 r = yxz ? rotationYXZ(ang[0], ang[1], ang[2]) : rotationXYZ(ang[0], ang[1], ang[2]);
        setRotation(s.current, mulMatrix3x3(cur, r));
        for (size_t i = 0; i < 3; ++i) s.current.translation[i] = int32_t(uint32_t(t[i]) + uint32_t(s.current.translation[i]));
        const Fixed16* posAxes[3] = {&p.pos.x, &p.pos.y, &p.pos.z};
        auto&& point = p.bonePoints[b];
        int16_t* pointAxes[3] = {&point.x, &point.y, &point.z};
        for (size_t i = 0; i < 3; ++i) {
            const int pos = wrap16(posAxes[i]->whole << sh);
            *pointAxes[i] = wrap16((pos * 7 + 4 + s.current.translation[i]) >> 3);
        }
        auto&& stored = p.bones[b];
        stored.copyFrom(s.current);
        const int scale = 0x200 << sh; // exe 0x40c0e0: the columns * the model scale >> 12
        const int32_t scales[3] = {scale, scale, scale};
        setRotation(stored, scaleColumns(rotationOf(stored), scales));
        // the same bone without the fixed-point rounding (draw only): unrounded products, the 16.16 position
        if (precise::enabled()) {
            for (int i = 0; i < 3; ++i)
                ft[i] += fm.e[i * 3] * float(v[0]) + fm.e[i * 3 + 1] * float(v[1]) + fm.e[i * 3 + 2] * float(v[2]);
            fm = precise::fmul(fm, yxz ? precise::frotYXZ(ang) : precise::frotXYZ(ang));
            float pt[3], mf[9];
            for (int i = 0; i < 3; ++i) pt[i] = (fpos(*posAxes[i], sh) * 7.0f + ft[i]) / 8.0f;
            for (int i = 0; i < 9; ++i) mf[i] = fm.e[i] * float(scale);
            precise::putPoint(&point.x, pt);
            precise::putMatrix(stored.rotation.data(), mf);
        }
    }
    // ... with the angles / the offset read from a vector in memory
    template <class A, class O> void bone(uint32_t b, const A& ang, const O& ofs, bool yxz) {
        placeBone(b, xyz(ang).data(), xyz(ofs).data(), yxz);
    }

    // the decoded entry i of a channel's angles (the body's or the upper body's)
    LaneVec4 angles(bool upper, size_t i) const { return upper ? s.upperAngles[i] : s.bodyAngles[i]; }

    // exe 0x5e1f90 + 0x5e25b0: the channels decoded, the skeleton walked from the actor matrix
    void run(bool blend) {
        // exe 0x5e1f90
        decode(p.bodyBlend, s.bodyAngles, s.bodyWords, kBodyBones, blend && (p.bodyBlend.steps & 0x80) != 0, 0);
        decode(p.upperBlend, s.upperAngles, s.upperWords, kUpperBones,
               blend && (p.upperBlend.steps & 0x80) != 0, 1);
        // exe 0x5e25b0
        poseStack().top = kStackBottom;
        const int sh = shift();
        s.current.translation[0] = int32_t(uint32_t(int32_t(p.pos.x.whole)) << sh);
        s.current.translation[1] = int32_t(uint32_t(int32_t(p.pos.y.whole)) << sh);
        s.current.translation[2] = int32_t(uint32_t(int32_t(p.pos.z.whole)) << sh);
        const int16_t yawAng[3] = {0, wrap16(p.rotation.y - p.directionOffset.y), 0};
        setRotation(s.current, rotationXYZ(yawAng[0], yawAng[1], yawAng[2]));
        fm = precise::frotXYZ(yawAng);
        ft[0] = fpos(p.pos.x, sh);
        ft[1] = fpos(p.pos.y, sh);
        ft[2] = fpos(p.pos.z, sh);
        p.setRootPose(s.current.get());
        auto&& root = s.bodyAngles[0]; // the root's decoded offset + the skeleton's
        const LoadedVector rootOffset = skeletonOffset(data.skeleton, 0);
        root.y = wrap16(rootOffset.y + root.y);
        root.z = wrap16(rootOffset.z + root.z);
        root.x = wrap16(root.x + rootOffset.x);
        push();
        // the hips and the legs
        bone(0, s.bodyAngles[1], root, false);
        bone(kHipsBone, s.bodyAngles[9], skeletonOffset(data.skeleton, 8), false);
        push();
        bone(9, s.bodyAngles[10], skeletonOffset(data.skeleton, 9), false);
        bone(10, s.bodyAngles[11], skeletonOffset(data.skeleton, 10), false);
        bone(kLeftFootBone, s.bodyAngles[12], skeletonOffset(data.skeleton, 11), false);
        pop();
        bone(12, s.bodyAngles[13], skeletonOffset(data.skeleton, 12), false);
        bone(13, s.bodyAngles[14], skeletonOffset(data.skeleton, 13), false);
        bone(kRightFootBone, s.bodyAngles[15], skeletonOffset(data.skeleton, 14), false);
        pop();

        // the upper body: each part's source channel by the channel mix mode
        const int8_t mode = p.channelMix;
        bool trunkUpper, leftArmUpper, rightArmUpper;
        switch (mode) {
        case 1: trunkUpper = true; leftArmUpper = true; rightArmUpper = false; break;
        case 2: trunkUpper = true; leftArmUpper = false; rightArmUpper = true; break;
        case 3: trunkUpper = false; leftArmUpper = true; rightArmUpper = true; break;
        default: trunkUpper = false; leftArmUpper = false; rightArmUpper = false; break;
        }
        const bool carrying = (p.carryFlags & kCarryHolding) != 0;
        auto read = [&](bool upper, size_t i, int16_t a[3]) {
            const LaneVec4 e = angles(upper, i);
            a[0] = e.x;
            a[1] = e.y;
            a[2] = e.z;
        };
        int16_t a[3];
        read(trunkUpper, 1, a); // the trunk
        a[1] = carrying ? int16_t(p.headTurn) : wrap16(a[1] + p.headTurn);
        bone(0, a, root, !(mode == 0 || p.body.anim == p.upper.anim));
        push();
        read(trunkUpper, 2, a); // the head
        a[1] = carrying ? int16_t(p.lean) : wrap16(a[1] + p.lean);
        bone(1, a, skeletonOffset(data.skeleton, 1), false);
        pop();
        push();
        read(leftArmUpper, 3, a); // the left arm (the special weapon's, aiming, unless the Blade Arm)
        if (!(p.armFlags & kArmSpecial) || p.activeSpecial == kBladeArmWeapon) {
            bone(2, a, skeletonOffset(data.skeleton, 2), false);
            bone(3, angles(leftArmUpper, 4), skeletonOffset(data.skeleton, 3), false);
            bone(4, angles(leftArmUpper, 5), skeletonOffset(data.skeleton, 4), false);
        } else {
            a[2] = wrap16(a[2] + p.lean);
            a[0] = wrap16(a[0] + p.rotation.x);
            bone(2, a, skeletonOffset(data.skeleton, 2), true);
            bone(3, angles(leftArmUpper, 4), skeletonOffset(data.skeleton, 3), true);
            bone(4, angles(leftArmUpper, 5), skeletonOffset(data.skeleton, 4), true);
        }
        pop();
        push();
        read(rightArmUpper, 6, a); // the right arm (the buster's, aiming)
        const bool aimArm = (p.armFlags & kArmBuster) != 0;
        if (aimArm) {
            a[2] = wrap16(a[2] + p.lean);
            a[0] = wrap16(a[0] + p.rotation.x);
        }
        bone(5, a, skeletonOffset(data.skeleton, 5), aimArm);
        bone(6, angles(rightArmUpper, 7), skeletonOffset(data.skeleton, 6), aimArm);
        bone(7, angles(rightArmUpper, 8), skeletonOffset(data.skeleton, 7), aimArm);
    }
};

} // namespace

PlayerPoseData playerPoseData(const LoadedFileData& files, const PlayerSnapshot& snapshot, uint8_t coordinateShift) {
    PlayerPoseData data;
    data.coordinateShift = coordinateShift;
    data.frames = {snapshot.animFrame[0], snapshot.animFrame[1]};
    data.skeleton = files.reader(kPlayerSkeleton, 6);
    for (size_t channel = 0; channel < 2; ++channel) {
        const uint8_t anim = snapshot.animId[channel];
        uint32_t base = kPlayerBank, table;
        if (anim < kSpecialArmAnims) table = kPlayerFrames;
        else if (anim < kBusterArmAnims) table = kSpecialArmFrames;
        else if (anim < kStageAnims) table = kBusterArmFrames;
        else {
            base = snapshot.stageAnimBase;
            table = base + kStageFramesOffset;
        }
        const uint32_t offset = files.reader(table + uint32_t(snapshot.animStep[channel]) * 4, 4).read<uint32_t>(0);
        data.frameBanks[channel] = files.reader(base + (offset >> 2) * 4);
    }
    return data;
}

uint32_t playerFrameOffset(uint32_t frameData) { return loadedScalar<uint32_t>(kPlayerFrames + frameData * 4); }

void playerPose(Player& p, const PlayerPoseData& data, bool blend) {
    Pose pose{p, data};
    pose.run(blend);
}

void playerPose(Player& p, bool blend) {
    const auto data = playerPoseData(loadedFiles(), capturePlayerSnapshot(p), stage().coordinateShift);
    playerPose(p, data, blend);
}

} // namespace od::port
