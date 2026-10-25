// The skeletal animation of the animated actors: the key steps, the frame decoding and the pose (see
// actor_animation.hpp).
#include "game/actor_animation.hpp"

#include <array>

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/animation_data.hpp"
#include "game/fixed_point.hpp"
#include "game/matrix.hpp"
#include "game/player/precise_pose.hpp"
#include "game/port_gap.hpp"
#include "game/stage_state.hpp"
#include "game/state_array.hpp"
#include "game/vectors.hpp"

namespace od::port {

namespace {

constexpr const char* kAnimationPort = "actor animation"; // the area of its port-gap errors

constexpr uint32_t kKeySize = 4;
AnimationKeyData keyAt(uint32_t address) { return animationKey(address); }

// lane k of a packed entry, scaled down by (6 - the entry's 2-bit exponent)
int16_t unpackLane(uint32_t w, int k) {
    const int e = 6 - int(w >> 30);
    int16_t v;
    if (k == 0) v = wrap16(int(int16_t(w)) << 6);
    else if (k == 1) v = int16_t(w >> 4);
    else v = int16_t(w >> 14);
    return int16_t(int(v) >> e);
}

// the packed frame `frame` of the actor's current animation (n entries a frame)
FileDataReader packedFrame(const AnimatedActor& actor, int frame, uint32_t n) {
    const uint32_t first = loadedScalar<uint32_t>(actor.frameTable.value() + uint32_t(actor.frameData) * 4) >> 2;
    return loadedFile(stage().modelBank + (first + uint32_t(frame) * n) * 4, n * 4);
}

// exe 0x5e2350: the current key's frame into the decode buffer
void decodeFrame(const AnimatedActor& actor) {
    DecodedFrame& f = decodedFrame();
    const uint32_t n = uint32_t(actor.partCount) + 1;
    if (n > DecodedFrame::kEntries) notPortedAt(kAnimationPort, "exe 0x5e2350: a frame of more entries than the part table", n);
    const FileDataReader src = packedFrame(actor, animationKey(actor.animKeyAt.value()).frame, n);
    for (uint32_t i = 0; i < n; ++i) f.packed[i] = src.read<uint32_t>(i * 4);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t w = f.packed[i];
        f.entries[i].assign(unpackLane(w, 0), unpackLane(w, 1), unpackLane(w, 2));
    }
}

// exe 0x5e2400: the current key's frame blended toward `frame2` by t / 16
void decodeBlendedFrame(const AnimatedActor& actor, int frame2, int t) {
    DecodedFrame& f = decodedFrame();
    const uint32_t n = uint32_t(actor.partCount) + 1;
    if (2 * n > DecodedFrame::kPackedWords) // the exe's words would run past the scratch (+0x140) into other globals
        notPortedAt(kAnimationPort, "exe 0x5e2400: a blend of more packed words than the scratch holds", n);
    const FileDataReader src = packedFrame(actor, animationKey(actor.animKeyAt.value()).frame, n), src2 = packedFrame(actor, frame2, n);
    for (uint32_t i = 0; i < n; ++i) f.packed[i] = src.read<uint32_t>(i * 4);
    for (uint32_t i = 0; i < n; ++i) f.packed[n + i] = src2.read<uint32_t>(i * 4);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t w = f.packed[i], w2 = f.packed[n + i];
        int16_t lane[3];
        for (int k = 0; k < 3; ++k) {
            const int16_t c = unpackLane(w, k);
            lane[k] = wrap16(((int(unpackLane(w2, k)) - int(c)) * t >> 4) + c);
        }
        f.entries[i].assign(lane[0], lane[1], lane[2]);
    }
}

// lane i (0 x, 1 y, 2 z) of a short vector
int16_t laneOf(const ShortVec3& v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; }

// exe 0x5e2de0: the bone matrices and points of the decoded frame into the actor's bone blocks. Part k (table +0xec):
// its matrix = the parent slot's matrix * Rx Ry Rz(its decoded angles), its point = the parent's point + the parent's
// matrix * its skeleton offset. froot: the root matrix in float (the draw-only precise pose, game/player/precise_pose);
// null = the fixed root.
void poseBones(AnimatedActor& actor, const Matrix3& root, const precise::F3* froot) {
    // +0x74: the root matrix; the game copies 0x20 bytes from its stack frame (9 shorts + stack garbage)
    actor.rootMatrix = root;
    DecodedFrame& frame = decodedFrame();
    // the float twin of every slot matrix (unscaled, 1 = 4096) and point, recorded next to the fixed values
    const bool fp = precise::enabled();
    std::array<precise::F3, 32> fmat{};
    std::array<bool, 32> fhave{};
    std::array<std::array<float, 3>, 32> fpt{};
    std::array<bool, 32> fpthave{};
    auto anglesOf = [&](uint32_t entry) {
        const LaneVec4 e = frame.entries[entry];
        return std::array<int16_t, 3>{e.x, e.y, e.z};
    };
    auto fang = [&](uint32_t entry) { return precise::frotXYZ(anglesOf(entry).data()); };
    auto rotationOf = [&](uint32_t entry) {
        const auto a = anglesOf(entry);
        return rotationXYZ(a[0], a[1], a[2]);
    };
    const FileDataReader skeleton = loadedFile(actor.skeleton.value());
    std::array<BoneBlock*, 8> blk{};
    const int nb = ((int(actor.boneSlotCount) - 1) >> 2) + 1;
    for (int k = 0; k < nb && k < 8; ++k) blk[size_t(k)] = boneBlockOf(actor, k);
    auto block = [&](int slot) -> BoneBlock& {
        BoneBlock* b = blk[size_t((slot >> 2) & 7)];
        if (!b) notPorted(kAnimationPort, "exe 0x5e1780 returned null (bone slot past the mask)");
        return *b;
    };
    auto slotMatrix = [&](int slot) -> Matrix3& { return block(slot).matrices[size_t(slot & 3)]; };
    auto slotPoint = [&](int slot) -> ShortVec4& { return block(slot).points[size_t(slot & 3)]; };
    const int n = actor.partCount;
    slotMatrix(0) = mulMatrix3x3(root, rotationOf(1));
    if (fp) {
        fmat[0] = precise::fmul(froot ? *froot : precise::fromFixed(root.e), fang(1));
        fhave[0] = true;
    }
    for (int k = 1; k < n; ++k) {
        const PartEntry part = actor.parts[size_t(k)];
        const int child = part.boneSlot, parent = part.parentSlot;
        if (child == 0) continue;
        slotMatrix(child) = mulMatrix3x3(slotMatrix(parent), rotationOf(uint32_t(child) + 1));
        if (fp && child < 32 && parent >= 0 && parent < 32 && fhave[size_t(parent)]) {
            fmat[size_t(child)] = precise::fmul(fmat[size_t(parent)], fang(uint32_t(child) + 1));
            fhave[size_t(child)] = true;
        }
    }
    // the scaled float matrices in the fixed units (columns * s, like scaleColumns' * s >> 12 of 4096 = 1)
    float froot74[9]{};
    auto fscale = [&](int k, const int32_t* s) {
        if (k < 32 && fhave[size_t(k)])
            for (int i = 0; i < 9; ++i) fmat[size_t(k)].e[i] *= float(s[i % 3]);
    };
    auto frootScale = [&](const int32_t* s) {
        const precise::F3 r = froot ? *froot : precise::fromFixed(root.e);
        for (int i = 0; i < 9; ++i) froot74[i] = r.e[i] * float(s[i % 3]);
    };
    const int sh = stage().coordinateShift & 0x1f;
    if (actor.runFlags & kActorBoneScale) { // per-bone scale bytes times the scale +0xe0..
        auto scaleOf = [&](const auto& bytes) {
            std::array<int32_t, 3> s{};
            for (int i = 0; i < 3; ++i)
                s[size_t(i)] =
                    int32_t(uint32_t(bytes[size_t(i)]) * uint32_t(int32_t(laneOf(actor.scale, i))) << sh) >> 6;
            return s;
        };
        {
            const auto s = scaleOf(actor.rootScaleBytes);
            actor.rootMatrix = scaleColumns(actor.rootMatrix, s.data());
            frootScale(s.data());
        }
        for (int k = 0; k < n; ++k) {
            BoneBlock& b = block(k);
            const auto s = scaleOf(b.scaleBytes[size_t(k & 3)]);
            auto&& m = b.matrices[size_t(k & 3)];
            m = scaleColumns(m, s.data());
            fscale(k, s.data());
        }
    } else {
        int32_t s[3];
        for (int i = 0; i < 3; ++i) s[i] = int32_t(uint32_t(int32_t(laneOf(actor.scale, i))) << sh);
        actor.rootMatrix = scaleColumns(actor.rootMatrix, s);
        frootScale(s);
        for (int k = 0; k < n; ++k) {
            auto&& m = slotMatrix(k);
            m = scaleColumns(m, s);
            fscale(k, s);
        }
    }
    // root translation: decoded root offset + skeleton offset 0, through the (scaled) root matrix
    auto&& rootOffset = frame.entries[0]; // the decode buffer's root entry, offset in place as the exe does
    const LoadedVector skeletonRoot = skeletonOffset(skeleton, 0);
    rootOffset.x += int(skeletonRoot.x);
    rootOffset.y += int(skeletonRoot.y);
    rootOffset.z += int(skeletonRoot.z);
    int32_t t[3];
    {
        const int16_t v[3] = {rootOffset.x, rootOffset.y, rootOffset.z};
        transformVector(actor.rootMatrix, v, t);
    }
    BoneBlock* const b0 = blk[0];
    if (!b0) notPorted(kAnimationPort, "exe 0x5e1780 returned null (no bone block)");
    auto&& rootPoint = b0->points[0];
    auto shifted = [&](const Fixed16& c, int32_t d) {
        return int16_t(uint16_t(uint16_t(uint32_t(uint16_t(c.whole)) << sh) + uint32_t(d)));
    };
    rootPoint.x = shifted(actor.pos.x, t[0]);
    rootPoint.y = shifted(actor.pos.y, t[1]);
    rootPoint.z = shifted(actor.pos.z, t[2]);
    if (fp) { // the 16.16 position (not its integer part) + the unrounded root offset
        const float v[3] = {float(rootOffset.x), float(rootOffset.y), float(rootOffset.z)};
        const float p[3] = {float(actor.pos.x.value()), float(actor.pos.y.value()), float(actor.pos.z.value())};
        for (int i = 0; i < 3; ++i)
            fpt[0][size_t(i)] =
                p[i] / 65536.0f * float(1 << sh) +
                (froot74[i * 3] * v[0] + froot74[i * 3 + 1] * v[1] + froot74[i * 3 + 2] * v[2]) / 4096.0f;
        fpthave[0] = true;
        precise::putPoint(&b0->points[0].x, fpt[0].data());
    }
    for (int k = 1; k < n; ++k) {
        const PartEntry part = actor.parts[size_t(k)];
        const int child = part.boneSlot, parent = part.parentSlot, dst = part.pointSlot;
        auto&& d = slotPoint(dst);
        if (child == 0) {
            d = rootPoint;
            if (fp && fpthave[0] && dst < 32) {
                fpt[size_t(dst)] = fpt[0];
                fpthave[size_t(dst)] = true;
                precise::putPoint(&slotPoint(dst).x, fpt[0].data());
            }
            continue;
        }
        const LoadedVector off = skeletonOffset(skeleton, size_t(child));
        const int16_t v[3] = {off.x, off.y, off.z};
        int32_t r[3];
        transformVector(slotMatrix(parent), v, r);
        const ShortVec4& pt = slotPoint(parent);
        d.x = int16_t(uint16_t(uint16_t(pt.x) + uint32_t(r[0])));
        d.y = int16_t(uint16_t(uint16_t(pt.y) + uint32_t(r[1])));
        d.z = int16_t(uint16_t(uint16_t(pt.z) + uint32_t(r[2])));
        if (fp && dst < 32 && parent >= 0 && parent < 32 && fpthave[size_t(parent)] && fhave[size_t(parent)]) {
            const precise::F3& pm = fmat[size_t(parent)];
            for (int i = 0; i < 3; ++i)
                fpt[size_t(dst)][size_t(i)] =
                    fpt[size_t(parent)][size_t(i)] +
                    (pm.e[i * 3] * float(v[0]) + pm.e[i * 3 + 1] * float(v[1]) + pm.e[i * 3 + 2] * float(v[2])) /
                        4096.0f;
            fpthave[size_t(dst)] = true;
            precise::putPoint(&slotPoint(dst).x, fpt[size_t(dst)].data());
        }
    }
    if (fp)
        for (int k = 0; k < n && k < 32; ++k)
            if (fhave[size_t(k)]) precise::putMatrix(slotMatrix(k).e, fmat[size_t(k)].e);
}

} // namespace

DecodedFrame& decodedFrame() {
    static DecodedFrame f;
    return f;
}

BoneBlock* boneBlockOf(const AnimatedActor& actor, int n) {
    int left = n + 1;
    const uint32_t mask = actor.boneBlocks;
    for (int i = 0, bit = 31; bit >= 0; ++i, --bit)
        if ((mask & (1u << bit)) && --left == 0) return &boneBlockPool()[size_t(i)];
    return nullptr;
}

ShortVec4& bonePoint(const AnimatedActor& actor, uint32_t bone, const char* area) {
    BoneBlock* b = boneBlockOf(actor, int(bone >> 2));
    if (!b) notPorted(area, "exe 0x5e1810 returned null");
    return b->points[bone & 3];
}

uint32_t boneBlockAddress(const AnimatedActor& actor, int n) {
    const BoneBlock* b = boneBlockOf(actor, n);
    return b ? b->at : 0;
}

uint32_t bonePointAddress(const AnimatedActor& actor, uint32_t bone) {
    const BoneBlock* b = boneBlockOf(actor, int(bone >> 2));
    return b ? b->pointAddress(bone & 3) : 0;
}

ShortVec4* bonePointOrNull(const AnimatedActor& actor, uint32_t bone) {
    BoneBlock* b = boneBlockOf(actor, int(bone >> 2));
    return b ? &b->points[bone & 3] : nullptr;
}

ShortVec4& bonePointAtAddress(uint32_t address) {
    const uint32_t point = address - 0x80;
    const uint32_t k = (point - kBoneBlocks) % 0xb0 / 8;
    BoneBlock& b = boneBlockPool().at(point - k * 8);
    if (b.pointAddress(k) != address) notAnActorSlot(address, kBoneBlocks);
    return b.points[k];
}

namespace {

// the bone block of bone `bone` (block bone / 4), the port gap when the actor has fewer blocks
const BoneBlock& blockOfBone(const AnimatedActor& actor, uint32_t bone, const char* what) {
    const BoneBlock* b = boneBlockOf(actor, int(bone >> 2));
    if (!b) notPorted(kAnimationPort, what);
    return *b;
}

} // namespace

void bonePointOffset(const AnimatedActor& actor, uint32_t bone, const int16_t v[3], int16_t out[3]) {
    const BoneBlock& b = blockOfBone(actor, bone, "exe 0x5e1780 returned null (exe 0x5e1970)");
    const int16_t shifted[3] = {wrap16(v[0] << 3), wrap16(v[1] << 3), wrap16(v[2] << 3)};
    int16_t turned[3];
    transformShortVector(b.matrices[bone & 3], shifted, turned);
    const ShortVec4& point = b.points[bone & 3];
    out[0] = wrap16(turned[0] + point.x);
    out[1] = wrap16(turned[1] + point.y);
    out[2] = wrap16(turned[2] + point.z);
}

void bonePointOffsetShorts(const AnimatedActor& actor, uint32_t bone, int16_t in[3], int16_t out[3]) {
    const BoneBlock& b = blockOfBone(actor, bone, "exe 0x5e1780 returned null (exe 0x5e1970)");
    for (int i = 0; i < 3; ++i) in[i] = wrap16(uint16_t(in[i]) << 3);
    const int16_t v[3] = {in[0], in[1], in[2]};
    int16_t turned[3];
    transformShortVector(b.matrices[bone & 3], v, turned); // exe 0x40bb30
    const ShortVec4& point = b.points[bone & 3];
    out[0] = wrap16(turned[0] + point.x);
    out[1] = wrap16(turned[1] + point.y);
    out[2] = wrap16(turned[2] + point.z);
}

void bonePointAhead(const AnimatedActor& actor, uint32_t bone, int d, int16_t out[3]) {
    const BoneBlock& b = blockOfBone(actor, bone, "exe 0x5e1780 returned null (bone past the mask)");
    const Matrix3 m = b.matrices[bone & 3];
    const ShortVec4& point = b.points[bone & 3];
    const int ahead = int(wrap16(-d * 8));
    const int16_t p[3] = {point.x, point.y, point.z};
    for (int i = 0; i < 3; ++i) out[i] = wrap16(wrap16((int(m.e[i * 3 + 2]) * ahead) >> 12) + p[i]);
}

void animStep(AnimatedActor& actor) {
    const uint8_t ticks = uint8_t(actor.animKey.ticks - 1);
    actor.animKey.ticks = ticks;
    if (ticks == 0) {
        const AnimationKeyData key = animationKey(actor.animKeyAt.value());
        const uint8_t flags = key.flags;
        if (flags & kKeyJump) {
            if (flags == kKeyHold) {
                actor.animKey.ticks = key.ticks;
                animDecodeCurrent(actor);
                return;
            }
            const AnimationKeyData target = keyAt(actor.animScript.value() + uint32_t(flags & 0x7f) * kKeySize);
            actor.animKeyAt.pointAt(target.at);
            actor.keyIndex = 0;
            copyAnimationKey(actor.animKey, target);
            animDecodeCurrent(actor);
            return;
        }
        const AnimationKeyData next = keyAt(key.at + kKeySize);
        actor.animKeyAt.pointAt(next.at);
        copyAnimationKey(actor.animKey, next);
        ++actor.keyIndex;
    }
    animDecodeCurrent(actor);
}

void animSet(AnimatedActor& actor, uint32_t anim, int key) {
    const uint32_t offset = loadedScalar<uint32_t>(actor.animTable.value() + anim * 4);
    if (offset & 3)
        notPortedAt(kAnimationPort, "exe 0x5e1c50: misaligned animation offset (the exe writes 0x84555555)", offset);
    actor.anim = uint8_t(anim);
    const uint32_t script = stage().modelBank + 4 + uint32_t(int32_t(offset) >> 2) * 4;
    const AnimationKeyData first = keyAt(script), current = keyAt(script + uint32_t(key) * kKeySize);
    actor.animScript.pointAt(first.at);
    actor.animKeyAt.pointAt(current.at);
    actor.frameData = loadedScalar<uint8_t>(script - 4);
    copyAnimationKey(actor.animKey, current);
    actor.keyIndex = uint8_t(key);
    decodeFrame(actor);
}

void animPose(AnimatedActor& actor) {
    Matrix3 r = rotationYXZ(actor.rotation.x, actor.rotation.y, actor.rotation.z);
    const int16_t ang[3] = {actor.rotation.x, actor.rotation.y, actor.rotation.z};
    precise::F3 fr = precise::frotYXZ(ang);
    if (actor.flags & kActorPoseOnRoot) {
        const Matrix3 previousRoot = actor.rootMatrix;
        fr = precise::fmul(fr, precise::fromFixed(previousRoot.e));
        r = mulMatrix3x3(r, previousRoot);
    }
    poseBones(actor, r, &fr);
}

void animDecodeCurrent(AnimatedActor& actor) {
    const AnimationKeyData key = animationKey(actor.animKeyAt.value());
    const uint8_t flags = key.flags;
    if ((flags & kKeyBlendMask) == kKeyBlend) {
        const int8_t frame = key.frame;
        uint32_t p = key.at;
        do p += kKeySize;
        while (keyAt(p).frame == frame);
        decodeBlendedFrame(actor, keyAt(p).frame, flags & 0xf);
        return;
    }
    decodeFrame(actor);
}

void animSetPose(AnimatedActor& actor, uint32_t anim, int key) {
    animSet(actor, anim, key);
    animPose(actor);
}

void animStepPose(AnimatedActor& actor) {
    animStep(actor);
    animPose(actor);
}

int boneBlockIndex(const AnimatedActor& actor, uint32_t bone) {
    uint32_t left = (bone >> 2) + 1;
    const uint32_t mask = actor.boneBlocks;
    for (uint32_t i = 0; i < 32; ++i)
        if ((mask & (0x80000000u >> i)) && --left == 0) return int(i);
    return -1;
}

void setBoneScaleWord(AnimatedActor& actor, uint32_t bone, uint32_t word) {
    const int i = boneBlockIndex(actor, bone);
    if (i == -1) return;
    boneBlockPool()[size_t(i)].setScaleWord(bone & 3, word);
}

void setBoneSemiTransparency(AnimatedActor& actor, uint32_t bone, uint8_t semi, uint8_t mode) {
    actor.semiTransparency = 1;
    BoneBlock* b = boneBlockOf(actor, int(int32_t(bone) >> 2));
    if (!b) notPorted(kAnimationPort, "exe 0x5e1780 returned null (exe 0x5e1a20)");
    b->setSemiBits(bone & 3, uint8_t((((semi & 1u) << 4) | (mode & 3u)) << 1));
}

void resetBoneScales(AnimatedActor& actor) {
    const uint32_t parts = actor.partCount;
    BoneBlock* block = nullptr;
    for (uint32_t i = 0; i < parts; ++i) {
        if ((i & 3) == 0) block = boneBlockOf(actor, int(i >> 2));
        if (!block) notPorted(kAnimationPort, "exe 0x5e1780 returned null (exe 0x5e3220)");
        block->setScaleWord(i & 3, uint32_t(kScaleByteOne) * 0x10101u);
    }
    actor.runFlags |= kActorBoneScale;
}

} // namespace od::port
