// The skeletal animation of the animated actors (pool 0x20): the animation scripts of the stage's bank (a key list
// per animation: {s8 frame, u8 ticks, u8, u8 flags}), the decoding of a key's packed frame into the decode buffer
// (the root offset + one angle triple per part, blended toward the next frame when the key asks), and the pose: the
// part matrices and points written into the actor's bone blocks (exe 0x5e1c50 / 0x5e1ea0 / 0x5e1f30 / 0x5e2350 /
// 0x5e2400 / 0x5e2d90 / 0x5e2de0). The renderer draws part p from its bone-block slot: world = point + M * v / 4096.
#pragma once

#include <array>
#include <cstdint>

#include "game/actor_state.hpp"
#include "game/geometry_scratch.hpp"
#include "game/vectors.hpp"

namespace od::port {

// exe 0x5e1c50: starts animation `anim` at key `key` (the script of the model's animation table) and decodes it
void animSet(AnimatedActor& actor, uint32_t anim, int key);
// exe 0x5e1ea0: one tick of the current key; at its end the next key (or the jump / hold of the last one); decodes
void animStep(AnimatedActor& actor);
// exe 0x5e1f30: decodes the current key's frame (blended toward the next different frame when the key asks)
void animDecodeCurrent(AnimatedActor& actor);
// exe 0x5e2d90 (+ the hierarchy 0x5e2de0): the pose of the decoded frame -- the root matrix (Ry Rx Rz of the
// rotation, times the previous root with kActorPoseOnRoot), scaled, then every part's matrix and point
void animPose(AnimatedActor& actor);
// exe 0x5e1c30: animSet + animPose
void animSetPose(AnimatedActor& actor, uint32_t anim, int key);
// exe 0x5e1e80: animStep + animPose
void animStepPose(AnimatedActor& actor);
// the animation tail of most classes: the animation asked for started at `key` and posed when it changed since the
// last tick (exe 0x5e1c30), else the current one stepped and posed (exe 0x5e1e80)
inline void animateAsked(AnimatedActor& actor, int key) {
    if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, key);
    else animStepPose(actor);
}
// the classes' animation request: the animation asked for and the class's start-key byte cleared (the next animation
// starts at key 0)
inline void requestAnim(AnimatedActor& actor, uint8_t& startKey, uint8_t anim) {
    actor.anim = anim;
    startKey = 0;
}

// The decode buffer of the frame being posed (the geometry scratch's +0x20.., game/geometry_scratch: the same lanes
// are the collision queries' state during the actor collision pass): entry 0 the root offset, entry 1 + k part k's
// angles; the key's packed words (+0x84..; then the blend target's). The exe does not bound the entries: past the
// twelfth they run over the packed words (lanes the decode has read by then) -- the views below are the same lanes, so
// a model of more parts overlaps them as in the exe.
struct DecodedFrame {
    static constexpr uint32_t kEntries = 25;     // the root and the part table's 24 parts
    static constexpr uint32_t kPackedWords = 47; // +0x84 .. the scratch's end (+0x140)
    std::array<LaneVec4, kEntries> entries = geometryScratch().vec4s<kEntries>(0x20);
    std::array<Lane32, kPackedWords> packed = geometryScratch().words<kPackedWords>(0x84);
};
DecodedFrame& decodedFrame();

// exe 0x5e1780: the actor's n-th bone block (the n-th set bit of +0xcc from bit 31); nullptr when it has fewer
BoneBlock* boneBlockOf(const AnimatedActor& actor, int n);
// exe 0x5e1810: the point of bone `bone` (block bone / 4, slot bone & 3); a bone past the actor's blocks (the exe
// returns null and its callers read address 0x80..) throws the port gap of `area`
ShortVec4& bonePoint(const AnimatedActor& actor, uint32_t bone, const char* area);
// The same two as the exe returns them, for the callers that still work on addresses: the block's / the bone point's
// address, 0 when the actor has fewer blocks
uint32_t boneBlockAddress(const AnimatedActor& actor, int n);
uint32_t bonePointAddress(const AnimatedActor& actor, uint32_t bone);
// ... the point itself (nullptr when the actor has fewer blocks: the exe's null, its callers' port gap)
ShortVec4* bonePointOrNull(const AnimatedActor& actor, uint32_t bone);
// the bone point whose exe address a class handed on (a parameter word: bonePointAddress); an address that is no bone
// point of the pool's blocks: notAnActorSlot naming it
ShortVec4& bonePointAtAddress(uint32_t address);
// exe 0x5e1970(actor, bone, v, out): the offset v (<< 3) through bone `bone`'s matrix (exe 0x40bb30) plus its point
// (s16 world coordinates << the stage shift)
void bonePointOffset(const AnimatedActor& actor, uint32_t bone, const int16_t v[3], int16_t out[3]);
// ... on vectors of the game state as the exe does it: the input shifted (<< 3) in place first (an exe table it reads
// drifts on every call), then out = bone matrix * in >> 12 + the bone's point (in and out may be the same)
// ... on three shorts (`in` shifted in place, as the exe)
void bonePointOffsetShorts(const AnimatedActor& actor, uint32_t bone, int16_t in[3], int16_t out[3]);
// ... on two s16 vectors (`in` shifted in place: a table of the game's data, a native point)
template <class In, class Out> void bonePointOffsetInPlace(const AnimatedActor& actor, uint32_t bone, In& in, Out& out) {
    int16_t i[3] = {in.x, in.y, in.z}, o[3];
    bonePointOffsetShorts(actor, bone, i, o);
    in.x = i[0];
    in.y = i[1];
    in.z = i[2];
    out.x = o[0];
    out.y = o[1];
    out.z = o[2];
}
// exe 0x5e1890(actor, bone): the index (in the bone-block pool) of the block holding the bone, -1 when the actor has
// fewer blocks
int boneBlockIndex(const AnimatedActor& actor, uint32_t bone);
// exe 0x5e1850(actor, bone, word): the bone's scale bytes and semi-transparency bits = the word (nothing when the actor
// has fewer blocks)
void setBoneScaleWord(AnimatedActor& actor, uint32_t bone, uint32_t word);
// exe 0x5e1a20(actor, bone, semi, mode): the actor drawn with per-part semi-transparency (+0xeb = 1), the part on the
// bone with semi (& 1) and the blend mode (& 3); a bone past the actor's blocks (the exe writes through null) is the
// port gap
void setBoneSemiTransparency(AnimatedActor& actor, uint32_t bone, uint8_t semi, uint8_t mode);
// exe 0x5e3220: every part's bone scaled 1.0 on the three axes with no semi-transparency (the dword 0x404040 per
// bone), kActorBoneScale set; a part past the actor's bone blocks (the exe writes through null) is the port gap
void resetBoneScales(AnimatedActor& actor);
// exe 0x5e18d0(actor, bone, d, out): the point d * 8 ahead of bone `bone` (along its matrix's z column, -z forward)
// plus its point
void bonePointAhead(const AnimatedActor& actor, uint32_t bone, int d, int16_t out[3]);

} // namespace od::port
