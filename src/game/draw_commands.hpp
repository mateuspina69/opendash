// The draw-command buffer of dash2.exe (the cursor 0x6d0050) and the records the actor updates append to it, the GPU
// packets the ported classes link into the ordering table, and the full-screen tints -- the writers' side
// . The draw pass that reads them back (exe 0x5f0580): game/draw_command_decode.
//
// The frame loop (exe 0x54a47c) sets the cursor to the frame's buffer (0x6b0000 / 0x6b8000 by the draw buffer) before
// the game frame; the actor updates append records (world-space s16 words, nothing projected). Opcode = byte +7 & 0xfc:
//  * kBillboardRecord (0x2c, 10 words): a camera-facing textured quad (Billboard);
//  * kRingRecord (0xf0) / kScreenRingRecord (0xf4) (6 words): a gouraud ring / disc in the camera plane (Ring; 0xf4:
//    the centre already on the screen);
//  * kBandRecord (0xf8, 8 words): a textured gouraud band in the world, two rings tilted by the arc word (Band).
// The ported classes write their records with emit() exactly as the exe: game/ builds them with the typed records
// below (a word whose exe arithmetic carries into a neighbouring byte keeps its expression, commented at its site); the
// content classes not yet converted pass the words.
#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

#include "game/state_array.hpp"

namespace od::dc {

constexpr uint32_t kCursor = 0x6d0050; // the cursor (the address the next record goes to)
constexpr uint32_t kBufferBase = 0x6b0000, kBufferSize = 0x10000; // both buffers
constexpr uint32_t kBufferShift = 15;          // a buffer's address: (the draw buffer + kBufferIndexBias) << 15
constexpr uint32_t kBufferIndexBias = 0xd6;

// The two buffers (exe 0x6b0000 / 0x6b8000) and the cursor (exe 0x6d0050: the exe address the next record goes to)
struct DrawCommandState {
    uint32_t cursor = 0; // 0x6d0050
    port::StateArray<uint32_t, kBufferSize / 4> words{}; // 0x6b0000: both buffers' words
    // the buffers hold [address, address + n words)
    static bool holds(uint32_t address, uint32_t n) {
        return address >= kBufferBase && address % 4 == 0 && uint64_t(address) + n * 4 <= uint64_t(kBufferBase) + kBufferSize;
    }
    // the word at a buffer address
    uint32_t& word(uint32_t address) { return words[(address - kBufferBase) / 4]; }
};
inline DrawCommandState& drawCommands() {
    static DrawCommandState s;
    return s;
}

// The current frame's buffer: (the draw buffer + 0xd6) << 15 (exe 0x54a47c)
uint32_t buffer();
// The frame loop's reset (exe 0x54a489): cursor = buffer(); maps the buffers when absent; the side channel restarted.
void resetCursor();

// ---- the records' words --------------------------------------------------------------------------------------------
// word 1's top byte (the opcode; the decoder takes byte +7 & 0xfc)
constexpr uint32_t kBillboardOpaque = 0x2c000000; // a billboard drawn opaque
constexpr uint32_t kBillboardSemi = 0x2e000000;   // ... semi-transparent through its texture page's blend
constexpr uint32_t kRingOpcode = 0xf0000000;      // a ring around a world point
constexpr uint32_t kScreenRingOpcode = 0xf4000000; // a ring around a screen point
constexpr uint32_t kBandOpcode = 0xf8000000;
constexpr uint32_t kRecordOpcodeMask = 0xfc; // byte +7 & this: the record kind
constexpr uint32_t kBillboardRecord = 0x2c, kRingRecord = 0xf0, kScreenRingRecord = 0xf4, kBandRecord = 0xf8;
constexpr uint32_t kBillboardWords = 10, kRingWords = 6, kBandWords = 8;
// word 3's top byte of a ring / band: the outer colour semi-transparent (additive)
constexpr uint32_t kSemiAdditive = 0x22000000;
constexpr uint32_t billboardColour(uint32_t rgb) { return kBillboardSemi | rgb; }
constexpr uint32_t opaqueBillboardColour(uint32_t rgb) { return kBillboardOpaque | rgb; }
constexpr uint32_t ringColour(uint32_t rgb) { return kRingOpcode | rgb; }
constexpr uint32_t screenRingColour(uint32_t rgb) { return kScreenRingOpcode | rgb; }
constexpr uint32_t bandColour(uint32_t rgb) { return kBandOpcode | rgb; }
constexpr uint32_t ringRim(uint32_t rgb) { return kSemiAdditive | rgb; }
constexpr uint32_t kRingFadedRim = ringRim(0); // a black rim: the disc fades out
// word 0 of a ring: the segment range start .. end of the 64-step circle (bytes 0 / 1), the angle offset (byte 2) and
// the ellipse (byte 3, s8); kRingFullArc the whole circle, untilted. ringArc keeps the int arithmetic of the exe's
// emitters (a negative start sign-extends over the word)
constexpr uint32_t kRingFullArc = 0x4000;
constexpr uint32_t ringArc(int start, int end) { return uint32_t(end << 8 | start); }
constexpr uint32_t ringAngle(uint32_t offset) { return offset << 16; }
// word 4: the s16 z | ringStep(s) (segments 1 << s steps of the 64-step circle apart) | kRingFront
constexpr uint32_t ringStep(uint32_t s) { return s << 16; }
constexpr uint32_t kRingFront = 0x1000000; // word 4 bit 24: linked at OT entry 1 (a band: drawn double sided)
// word 5: the inner radius | the outer radius << 16
constexpr uint32_t ringRadii(uint32_t inner, uint32_t outer) { return inner | outer << 16; }
// two shorts as one word (x | y << 16: a record's xy word, a screen point)
constexpr uint32_t packXY(int16_t x, int16_t y) { return uint32_t(uint16_t(x)) | uint32_t(uint16_t(y)) << 16; }
// a texel (u | v << 8) of a corner
constexpr uint16_t texel(uint32_t u, uint32_t v) { return uint16_t(u | v << 8); }
// a z word as the emitters write it: zero-extended (a u16 read) / sign-extended (an s16 read)
constexpr uint32_t zWord(int16_t z) { return uint16_t(z); }
constexpr uint32_t signedZWord(int16_t z) { return uint32_t(int32_t(z)); }
// word 6 of a billboard: the half diagonal | the rotation << 16
constexpr uint32_t sizeRotation(uint32_t size, uint32_t rotation) { return size | rotation << 16; }
// a band's texture words (words 6 / 7): the CLUT / the tpage in the high half, a texel in the low one
constexpr uint32_t pageTexel(uint16_t page, uint32_t texel) { return uint32_t(page) << 16 | texel; }
// a band's arc word byte 3: the texture's repeat along the band
constexpr uint32_t bandRepeat(uint32_t n) { return n << 24; }

// The billboard record (0x2c): corners C - (a, b), C + (-b, a), C + (b, -a), C + (a, b) in the camera plane around the
// centre (not shifted), a / b = the 64-step tables at (rotation + 0x600) >> 6 times the size; word 0 is never written
struct Billboard {
    uint32_t colour = 0;       // word 1: billboardColour / opaqueBillboardColour
    uint32_t xy = 0;           // word 2: packXY of the centre
    uint32_t z = 0;            // word 4: zWord / signedZWord of the centre
    uint16_t clut = 0;         // word 3's high half
    uint16_t tpage = 0;        // word 5's high half
    uint16_t uv[4] = {};       // the corners' texels (words 3 / 5 / 7 / 9)
    uint32_t sizeRotation = 0; // word 6
};
// The ring record (0xf0 / 0xf4): the segments of the arc between the inner radius (inner colour) and the outer radius
// (outer colour; its top byte the draw mode) in the camera plane
struct Ring {
    uint32_t arc = kRingFullArc;    // word 0
    uint32_t inner = 0;             // word 1: ringColour / screenRingColour
    uint32_t xy = 0;                // word 2
    uint32_t outer = kRingFadedRim; // word 3: ringRim
    uint32_t zStep = 0;             // word 4: z | ringStep | kRingFront
    uint32_t radii = 0;             // word 5: ringRadii
};
// The band record (0xf8): a ring's words (the arc word's bytes 0 / 1 tilt the inner / outer ring, byte 3 the texture
// repeat) and the texture: CLUT | uv of the inner edge, tpage | uv of the outer edge
struct Band {
    Ring ring;
    uint32_t clutTexel = 0;  // word 6
    uint32_t tpageTexel = 0; // word 7
};

// The records' words as the emitters write them (a billboard's word 0, which they never write: 0)
using BillboardWords = std::array<uint32_t, kBillboardWords>;
using RingWords = std::array<uint32_t, kRingWords>;
using BandWords = std::array<uint32_t, kBandWords>;
BillboardWords recordWords(const Billboard& b);
RingWords recordWords(const Ring& r);
BandWords recordWords(const Band& b);

// Appends one record at the cursor as the exe's emitters do (words [first, n) written, word 0 left as it was when
// writeWord0 is false: the billboard emitters never write it) and advances the cursor by the record size. `owner` = the
// emitting actor (presentation only: groups the decoded prims per actor). Without a mapped cursor nothing happens;
// without a mapped buffer (logic tests) only the cursor advances.
// `particle` (optional, >= 0): the emitter's identity of this record's particle for its whole life (e.g. slot << 8 |
// birth tick): the record becomes its own presentation object, so it blends with itself tick to tick and a particle
// reborn in the slot is not dragged from the old one's place (presentation only).
void emit(std::initializer_list<uint32_t> words, uint32_t owner, bool writeWord0 = true, int32_t particle = -1);
// the typed records
void emit(const Billboard& b, uint32_t owner, int32_t particle = -1);
void emit(const Ring& r, uint32_t owner);
void emit(const Band& b, uint32_t owner);
// a disc: the full ring from the colour at the centre to a faded rim
void emitDisc(uint32_t owner, uint32_t colour, uint32_t xy, uint32_t z, uint32_t radii);

// ---- the GPU packets of the ordering table -------------------------------------------------------------------------
// The packets the ported classes link into the ordering table (OT [0x6d0048], packet cursor 0x6d004c): the exe
// projects the corners on the CPU and stores screen x / y in the packet; the port keeps the corners' camera-input
// points instead (s16, the units the camera 0x6d0014 takes: game units << the stage shift) with the packet's other
// words, and draws them through its own camera. Presentation side channel (not game state), restarted by
// resetCursor(), drawn by game/draw_command_decode.
// the packets' codes (the top byte of their first colour word; bit 1 semi-transparent)
constexpr uint32_t kPolyF3Semi = 0x22, kPolyFT4 = 0x2c, kPolyFT4Semi = 0x2e, kPolyG3Semi = 0x32, kPolyG4 = 0x38,
                   kPolyG4Semi = 0x3a, kPolyGT4Semi = 0x3e, kLineG2Semi = 0x52, kTileSemi = 0x62;
constexpr uint32_t kGouraudCodeBit = 0x10; // a flat packet's code | this: its gouraud form (FT4 -> GT4)
constexpr uint32_t packetColour(uint32_t code, uint32_t rgb) { return code << 24 | rgb; }
constexpr uint32_t kRgbMask = 0xffffff; // a colour word's colour (below its code byte)
// the blend of a draw mode linked before a gouraud packet (abr 1: additive)
constexpr uint8_t kAdditiveBlend = 1;
// A POLY_FT4's words (word 1 code / colour, word 3 CLUT / uv0, word 5 tpage / uv1, words 7 / 9 uv2 / uv3; the xy words
// are the side channel's corners)
struct TexturedQuad {
    uint32_t colour = 0;
    uint16_t clut = 0, tpage = 0;
    uint16_t uv[4] = {};
};
// A POLY_GT4's words (a colour per corner: words 1 / 4 / 7 / 10; word 3 CLUT / uv0, word 6 tpage / uv1, words 9 / 12
// uv2 / uv3)
struct GouraudTexturedQuad {
    uint32_t colour[4] = {};
    uint16_t clut = 0, tpage = 0;
    uint16_t uv[4] = {};
};
// a POLY_FT4 drawn as its gouraud form (the one colour at every corner; its code | kGouraudCodeBit)
GouraudTexturedQuad gouraudForm(const TexturedQuad& q);

// w = the POLY_FT4 packet words 0..9 (the xy words are ignored), v = the corners in PS1 order.
void otQuadFT4(uint32_t owner, const uint32_t w[10], const int16_t v[4][3]);
void otQuadFT4(uint32_t owner, const TexturedQuad& q, const int16_t v[4][3]);
// A POLY_G4 whose corners the exe offsets in screen pixels from projected points (the trail ribbon of 0x5636b0):
// c = the packet's colour words (c[0] carries the code: bit 1 semi-transparent), abr = the draw mode's blend (the
// DR_MODE linked before it), v = the corners' camera-input points, off = their offsets in the camera plane (x right,
// y down) in camera-input units (the pixel offsets brought back to the point's depth). Drawn as v + (camera rows 0 / 1
// of 0x6d0014) * off, gouraud untextured.
void otQuadG4(uint32_t owner, const uint32_t c[4], uint8_t abr, const int16_t v[4][3], const float off[4][2]);
// A textured gouraud POLY_GT4 whose corners the exe offsets in screen pixels from projected points (the status
// markers' lightning 0x568d00): w = the packet words 0..12 (w1 code / colour 0: bit 1 semi-transparent through the
// tpage's abr, bit 0 raw texture; w3 CLUT / uv0, w4 colour 1, w6 tpage / uv1, w7 colour 2, w9 uv2, w10 colour 3,
// w12 uv3; the xy words are ignored), v / off as otQuadG4.
void otQuadGT4(uint32_t owner, const uint32_t w[13], const int16_t v[4][3], const float off[4][2]);
void otQuadGT4(uint32_t owner, const GouraudTexturedQuad& q, const int16_t v[4][3], const float off[4][2]);
// A POLY_G3 (code 0x30, 0x32 semi; a POLY_F3 0x20 / 0x22 passes its one colour three times): c / abr / v / off as
// otQuadG4 for three corners. Drawn as a degenerate G4 (the 4th corner repeats the 3rd).
void otTriG3(uint32_t owner, const uint32_t c[3], uint8_t abr, const int16_t v[3][3], const float off[3][2]);
// A LINE_G2 (code 0x50, 0x52 semi): the two ends as otQuadG4 corners (c[0] carries the code). The PS1 line is one
// pixel wide: drawn as a thin gouraud quad whose sides are the ends +- half a PS1 pixel (view z / focal 0xa59d10
// camera-input units at each end) along the screen-space perpendicular of the projected ends (game camera 0x6d0014 /
// 0x6d0028), computed by the decoder.
void otLineG2(uint32_t owner, const uint32_t c[2], uint8_t abr, const int16_t v[2][3], const float off[2][2]);

// exe 0x5f3150(arg): a full-screen TILE linked at the head of the OT (drawn last): draw mode 0xe1000200 | (arg >> 24
// & 0xf0) (its abr bits 5..6 = arg >> 29 & 3: 0x22.. adds, 0x42.. subtracts), packet 0x60000000 | (arg & 0x0fffffff)
// (bit 25 = semi-transparent), size by the display mode 0xa7172c; nothing while the frame draws nothing. Presentation
// side channel like the OT quads (restarted by resetCursor); the app draws screenTints() over the scene.
void screenTint(uint32_t arg);
const std::vector<uint32_t>& screenTints();

// ---- the side channel the decoder reads (presentation only, not game state) ---------------------------------------
struct OtQuadFT4Packet {
    uint32_t owner;
    uint32_t w[10];
    int16_t v[4][3];
};
struct OtQuadG4Packet {
    uint32_t owner;
    uint32_t c[4];
    uint8_t abr;
    int16_t v[4][3];
    float off[4][2];
};
struct OtQuadGT4Packet {
    uint32_t owner;
    uint32_t w[13];
    int16_t v[4][3];
    float off[4][2];
};
struct OtLineG2Packet {
    uint32_t owner;
    uint32_t c[2];
    uint8_t abr;
    int16_t v[2][3];
    float off[2][2];
};
struct SideChannel {
    std::vector<std::pair<uint32_t, uint32_t>> owners;   // record address -> emitting actor
    std::vector<std::pair<uint32_t, int32_t>> particles; // record address -> particle identity (emit)
    std::vector<OtQuadFT4Packet> ft4;
    std::vector<OtQuadG4Packet> g4;
    std::vector<OtQuadGT4Packet> gt4;
    std::vector<OtLineG2Packet> lines;
    std::vector<uint32_t> tints; // exe 0x5f3150's arguments of the frame
};
const SideChannel& sideChannel();

} // namespace od::dc
