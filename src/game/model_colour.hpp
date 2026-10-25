// The actor model colour of exe 0x5f4160 / 0x5f4980 (shared by the sub-screen model draw, game/model_preview, and the
// host's live actors, app/scene_builder).
//   * the centre's depth (modelOtz): exe 0x40c200 leaves SZ 0xa59d34 = the view z (0 when <= 0), exe 0x40d010 stores
//     SZ >> 2 -- the value exe 0x5f4160 tests against the depth range / 0x10, the LOD limits +0xd4 / +0xd6 and the cue;
//   * the draw flags 0x6d00e0 before the per-part bits (modelDrawFlags): actor byte 0 & 0x30 = 0x20: 1; else the
//     depth cue (StageState::depthCue) by that depth >> 7 (mode 0x10 as is, else (e - 1 when e != 0) >> 1) << 16, bit 0
//     unless mode 0x30 with a cue in bits 16..18;
//   * the part colour 0x6d0100 = rgb15(+0xe6), or when +0xe6 == 0 rgb15 of the bone block's +0x86 + (ref & 3) * 8;
//     flags bit 1 = that colour != 0x808080;
//   * exe 0x5f4980: with flags & 3 == 1 a face takes its vertices' colours (the table +0xbc.., & 0xffffff) unless its
//     index word has bit 31; otherwise every corner takes the part colour.
#pragma once

#include <cstdint>

#include "game/loaded_data.hpp"
#include "game/stage_state.hpp"

namespace od::port {

// exe 0x5f4160's 15-bit colour -> 0xBBGGRR
inline uint32_t rgb15(uint32_t v) {
    return ((((v & 0x7c00) << 3 | (v & 0x3e0)) << 3 | (v & 0x1f)) << 3);
}

// One word of a model's vertex colour table (the table +0xbc.., a word a vertex): its colour (& 0xffffff) and its top
// byte, the shared-vertex op of exe 0x5f4980 in a part with flags 0x40 (0x80 | s: the posed vertex kept
// in slot s, 0xc0 | s: slot s's taken instead)
struct VertexColourWord {
    explicit VertexColourWord(uint32_t pointer) : word(loadedScalar<uint32_t>(pointer)), op(uint8_t(word >> 24)) {}
    uint32_t word;
    uint8_t op;
};
constexpr uint32_t kVertexColourSize = 4;

// the depth the model's centre is ordered by (the view z's SZ >> 2)

inline int32_t modelOtz(int32_t viewZ) { return (viewZ > 0 ? viewZ : 0) >> 2; }

// the actor flags byte's depth-cue mode (its bits 4..5)
constexpr uint8_t kCueModeMask = 0x30;
constexpr uint8_t kCueFull = 0x10;     // the cue entry as it is
constexpr uint8_t kCueNone = 0x20;     // no cue: the flags kDrawLit only
constexpr uint8_t kCueOnly = 0x30;     // the cue halved, kDrawLit only without a cue in kCueBits
constexpr uint32_t kDrawLit = 1;       // the draw flags' bit 0
constexpr uint32_t kCueShift = 16;     // the cue's place in the draw flags
constexpr uint32_t kCueBits = 0x70000; // the cue's low three bits there
constexpr int32_t kCueDepthShift = 7;  // the cue's entry: the depth >> this

// the draw flags of an actor with flags byte b0 at the depth z = modelOtz (an entry of the cue not mapped: 0)
inline uint32_t modelDrawFlags(uint8_t b0, int32_t z) {
    const uint32_t mode = b0 & kCueModeMask;
    if (mode == kCueNone) return kDrawLit;
    const uint32_t entry = stage().depthCue.value() + uint32_t(z >> kCueDepthShift) * 4;
    uint32_t e0 = loadedFileMapped(entry, 4) ? loadedScalar<uint32_t>(entry) : 0;
    if (mode != kCueFull) {
        if (e0) e0 -= 1;
        e0 = uint32_t(int32_t(e0) >> 1);
    }
    e0 <<= kCueShift;
    if (mode != kCueOnly || !(e0 & kCueBits)) e0 |= kDrawLit;
    return e0;
}

} // namespace od::port
