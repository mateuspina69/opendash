// The draw-command buffer, its records and the ordering-table packets' side channel (see draw_commands.hpp).
#include "game/draw_commands.hpp"

#include <algorithm>

#include "game/display.hpp"

namespace od::dc {
namespace {

constexpr size_t kMaxSideEntries = 4096; // each side-channel list keeps at most this many entries a frame
constexpr size_t kMaxTints = 64;

SideChannel& side() {
    static SideChannel s;
    return s;
}

} // namespace

uint32_t buffer() { return (uint32_t(port::display().drawBuffer) + kBufferIndexBias) << kBufferShift; }

const SideChannel& sideChannel() { return side(); }

GouraudTexturedQuad gouraudForm(const TexturedQuad& q) {
    GouraudTexturedQuad g;
    const uint32_t rgb = q.colour & kRgbMask;
    g.colour[0] = q.colour | kGouraudCodeBit << 24;
    g.colour[1] = g.colour[2] = g.colour[3] = rgb;
    g.clut = q.clut;
    g.tpage = q.tpage;
    std::copy(q.uv, q.uv + 4, g.uv);
    return g;
}

void emitDisc(uint32_t owner, uint32_t colour, uint32_t xy, uint32_t z, uint32_t radii) {
    emit({kRingFullArc, colour, xy, kRingFadedRim, z, radii}, owner);
}

void resetCursor() {
    drawCommands().cursor = buffer();
    SideChannel& s = side();
    s.owners.clear();
    s.particles.clear();
    s.ft4.clear();
    s.g4.clear();
    s.gt4.clear();
    s.lines.clear();
    s.tints.clear();
}

void screenTint(uint32_t arg) {
    if (port::display().drawingOff != 0 || side().tints.size() >= kMaxTints) return;
    side().tints.push_back(arg);
}
const std::vector<uint32_t>& screenTints() { return side().tints; }

void otQuadFT4(uint32_t owner, const uint32_t w[10], const int16_t v[4][3]) {
    OtQuadFT4Packet q{};
    q.owner = owner;
    std::copy(w, w + 10, q.w);
    for (int i = 0; i < 4; ++i) std::copy(v[i], v[i] + 3, q.v[i]);
    side().ft4.push_back(q);
}

void otQuadFT4(uint32_t owner, const TexturedQuad& t, const int16_t v[4][3]) {
    const uint32_t w[10] = {0, t.colour, 0, t.uv[0] | uint32_t(t.clut) << 16, 0, t.uv[1] | uint32_t(t.tpage) << 16,
                            0, t.uv[2], 0, t.uv[3]};
    otQuadFT4(owner, w, v);
}

void otQuadG4(uint32_t owner, const uint32_t c[4], uint8_t abr, const int16_t v[4][3], const float off[4][2]) {
    if (side().g4.size() >= kMaxSideEntries) return;
    OtQuadG4Packet q{};
    q.owner = owner;
    q.abr = abr;
    for (int i = 0; i < 4; ++i) {
        q.c[i] = c[i];
        std::copy(v[i], v[i] + 3, q.v[i]);
        q.off[i][0] = off[i][0];
        q.off[i][1] = off[i][1];
    }
    side().g4.push_back(q);
}

void otTriG3(uint32_t owner, const uint32_t c[3], uint8_t abr, const int16_t v[3][3], const float off[3][2]) {
    const uint32_t c4[4] = {c[0], c[1], c[2], c[2]};
    int16_t v4[4][3];
    float o4[4][2];
    for (int i = 0; i < 4; ++i) {
        const int s = i < 3 ? i : 2;
        std::copy(v[s], v[s] + 3, v4[i]);
        o4[i][0] = off[s][0];
        o4[i][1] = off[s][1];
    }
    otQuadG4(owner, c4, abr, v4, o4);
}

void otLineG2(uint32_t owner, const uint32_t c[2], uint8_t abr, const int16_t v[2][3], const float off[2][2]) {
    if (side().lines.size() >= kMaxSideEntries) return;
    OtLineG2Packet q{};
    q.owner = owner;
    q.abr = abr;
    for (int i = 0; i < 2; ++i) {
        q.c[i] = c[i];
        std::copy(v[i], v[i] + 3, q.v[i]);
        q.off[i][0] = off[i][0];
        q.off[i][1] = off[i][1];
    }
    side().lines.push_back(q);
}

void otQuadGT4(uint32_t owner, const uint32_t w[13], const int16_t v[4][3], const float off[4][2]) {
    if (side().gt4.size() >= kMaxSideEntries) return;
    OtQuadGT4Packet q{};
    q.owner = owner;
    std::copy(w, w + 13, q.w);
    for (int i = 0; i < 4; ++i) {
        std::copy(v[i], v[i] + 3, q.v[i]);
        q.off[i][0] = off[i][0];
        q.off[i][1] = off[i][1];
    }
    side().gt4.push_back(q);
}

void otQuadGT4(uint32_t owner, const GouraudTexturedQuad& t, const int16_t v[4][3], const float off[4][2]) {
    const uint32_t w[13] = {0, t.colour[0], 0, t.uv[0] | uint32_t(t.clut) << 16, t.colour[1], 0,
                            t.uv[1] | uint32_t(t.tpage) << 16, t.colour[2], 0, t.uv[2], t.colour[3], 0, t.uv[3]};
    otQuadGT4(owner, w, v, off);
}

namespace {

// the n words at the cursor (word 0 skipped unless writeWord0), the cursor advanced (see emit)
void emitWords(const uint32_t* words, uint32_t n, uint32_t owner, bool writeWord0, int32_t particle) {
    DrawCommandState& dcs = drawCommands();
    const uint32_t at = dcs.cursor;
    if (DrawCommandState::holds(at, n)) {
        for (uint32_t i = 0; i < n; ++i)
            if (i != 0 || writeWord0) dcs.word(at + i * 4) = words[i];
        if (side().owners.size() < kMaxSideEntries) side().owners.emplace_back(at, owner);
        if (particle >= 0 && side().particles.size() < kMaxSideEntries) side().particles.emplace_back(at, particle);
    }
    dcs.cursor = at + n * 4;
}

} // namespace

BillboardWords recordWords(const Billboard& b) {
    return {0, b.colour, b.xy, b.uv[0] | uint32_t(b.clut) << 16, b.z, b.uv[1] | uint32_t(b.tpage) << 16, b.sizeRotation,
            b.uv[2], 0, b.uv[3]};
}
RingWords recordWords(const Ring& r) { return {r.arc, r.inner, r.xy, r.outer, r.zStep, r.radii}; }
BandWords recordWords(const Band& b) {
    return {b.ring.arc, b.ring.inner, b.ring.xy, b.ring.outer, b.ring.zStep, b.ring.radii, b.clutTexel, b.tpageTexel};
}

void emit(std::initializer_list<uint32_t> words, uint32_t owner, bool writeWord0, int32_t particle) {
    emitWords(words.begin(), uint32_t(words.size()), owner, writeWord0, particle);
}

void emit(const Billboard& b, uint32_t owner, int32_t particle) {
    emitWords(recordWords(b).data(), kBillboardWords, owner, false, particle);
}

void emit(const Ring& r, uint32_t owner) { emitWords(recordWords(r).data(), kRingWords, owner, true, -1); }

void emit(const Band& b, uint32_t owner) { emitWords(recordWords(b).data(), kBandWords, owner, true, -1); }

} // namespace od::dc
