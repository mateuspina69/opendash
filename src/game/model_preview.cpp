// The sub-screens' 3D model previews (see model_preview.hpp): the capture of the actors' polygons in view space.
#include "game/model_preview.hpp"

#include <algorithm>
#include <array>

#include "game/actor_animation.hpp"
#include "game/actor_state.hpp"
#include "game/vectors.hpp"
#include "game/loaded_data.hpp"
#include "game/matrix.hpp"
#include "game/menu_draw.hpp"
#include "game/model_colour.hpp"
#include "game/projection_state.hpp"
#include "game/stage_state.hpp"

namespace od::port {

namespace {

ModelPreview gPreviews[2]; // kPreviewEquipment, kPreviewDevRoom

// ---- the model data the walk reads (the stage's model bank) ----
// A model header's LOD part tables (offsets into the bank, by LOD)
struct ModelLods {
    explicit ModelLods(uint32_t pointer) : at(pointer) {}
    uint32_t at;
    FileDataReader data{loadedFile(at)};
    LoadedArray<uint32_t> parts{at + 0x4};
};
// A part's header (0x10 bytes): its triangle / quad / vertex counts, its scale (the vertices' shift: 6 - scale), the
// offsets of its triangle and quad records and of its vertex words
struct ModelPartHeader {
    explicit ModelPartHeader(uint32_t pointer) : at(pointer) {}
    uint32_t at;
    FileDataReader data{loadedFile(at)};
    uint8_t triangles{data.read<uint8_t>(0x0)};
    uint8_t quads{data.read<uint8_t>(0x1)};
    uint8_t vertices{data.read<uint8_t>(0x2)};
    int8_t scale{data.read<int8_t>(0x3)};
    uint32_t triangleRecords{data.read<uint32_t>(0x4)};
    uint32_t quadRecords{data.read<uint32_t>(0x8)};
    int32_t vertexWords{data.read<int32_t>(0xc)};
};
constexpr uint32_t kPartHeaderSize = 0x10;
// a part's vertices: three packed in three words (10-bit x / y / z)
struct VertexWords {
    explicit VertexWords(uint32_t pointer) : at(pointer) {}
    uint32_t at;
    FileDataReader data{loadedFile(at)};
    LoadedArray<uint32_t> words{at + 0};
};
// a polygon record (12 bytes): its corners' uv words, its index word (the corners' vertex indices by 7 bits; bits
// 28..29 the texture word, bit 30 both sides, bit 31 the part colour)
struct PolygonRecord {
    explicit PolygonRecord(uint32_t pointer) : at(pointer) {}
    uint32_t at;
    FileDataReader data{loadedFile(at)};
    LoadedArray<uint16_t> uv{at + 0x0};
    uint32_t word{data.read<uint32_t>(0x8)};
};
constexpr uint32_t kPolygonRecordSize = 0xc;
constexpr uint32_t kBothSides = 0x40000000u, kPartColour = 0x80000000u;
// the texture words of a model (AnimatedActor::extraGeometry: tpage low half, clut high half)
struct TextureWords {
    explicit TextureWords(uint32_t pointer) : at(pointer) {}
    uint32_t at;
    FileDataReader data{loadedFile(at)};
    LoadedArray<uint32_t> words{at + 0};
};

// m * v >> 12 of a short vector (exe 0x40bab0: game/matrix transformVector; the walk's vectors are shorts)
std::array<int32_t, 3> matVec(const Matrix3& m, int32_t x, int32_t y, int32_t z) {
    const int16_t v[3] = {int16_t(x), int16_t(y), int16_t(z)};
    std::array<int32_t, 3> out{};
    transformVector(m, v, out.data());
    return out;
}

// The capture of one preview (the exe's walk of exe 0x5f4160 / 0x5f4980, the camera of the sub-screen: the identity
// rotation, the translations of exe 0x40de70({0, 0, 0}, 0x400, 0, 0, 0): (0, 0, 0x400 << shift) for the parts,
// (0, 0, 0x400) for the actor's centre; OpenDash does not write those globals: the host's field view keeps its camera)
struct ActorCapture {
    const Matrix3 cam{{0x1000, 0, 0, 0, 0x1000, 0, 0, 0, 0x1000}};
    int32_t camT[3]{0, 0, 0};
    int32_t camTr[3]{0, 0, 0x400};
    int ofx = 160, ofy = 120, h = 0x180, shift = 0;
    uint32_t bank = 0;
    std::vector<ModelTri>* capture = nullptr;
    std::array<std::array<int32_t, 3>, 64> shared{}; // exe 0x5f4980's shared-vertex slots

    // exe 0x5f4980(part header, bank, texture words): one part -- its vertices (<< the part's scale) through the
    // part's matrix, the shared-vertex slots (flags & 4), then the triangles / quads as view-space triangles (a quad
    // split (0, 1, 2) + (1, 3, 2)): the texture word's page + the cell's page offset, the blend bits (flags 21..22),
    // the clut; the uv + the cell offset; the colours: the vertex colours when flags & 3 == 1 and the index word has no
    // kPartColour, else the part's
    void part(const ModelPartHeader& hdr, const TextureWords& tex, const Matrix3& r, const int32_t t[3], uint32_t flags,
              uint32_t colour, uint32_t colours, uint16_t uvAdd, uint16_t pageAdd) {
        const uint32_t sh = uint32_t(6 - int32_t(hdr.scale)) & 0x1f;
        const uint32_t nv = hdr.vertices, ntri = hdr.triangles, nquad = hdr.quads;
        std::array<std::array<int32_t, 3>, 129> v{};
        const VertexWords vertexWords(bank + uint32_t(hdr.vertexWords >> 2) * 4);
        for (uint32_t k = 0; k < (nv + 2) / 3 && k * 3 < 128; ++k) {
            for (uint32_t j = 0; j < 3; ++j) {
                const uint32_t d = vertexWords.words[k * 3 + j];
                const int32_t x = int16_t(int32_t(int16_t(d << 6)) >> sh);
                const int32_t y = int16_t(int32_t(int16_t(d >> 4)) >> sh);
                const int32_t z = int16_t(int32_t(int16_t(d >> 14)) >> sh);
                const auto q = matVec(r, x, y, z);
                v[k * 3 + j] = {q[0] + t[0], q[1] + t[1], q[2] + t[2]};
            }
        }
        const bool cols = loadedFileMapped(colours, nv * 4);
        const auto colourWords = cols ? loadedFile(colours, nv * 4) : FileDataReader{};
        if ((flags & 4) && cols)
            for (uint32_t k = 0; k < nv && k < 128; ++k) {
                const uint8_t op = uint8_t(colourWords.read<uint32_t>((k) * 4) >> 24);
                if ((op & 0xc0) == 0x80) shared[op & 0x3f] = v[k];
                else if ((op & 0xc0) == 0xc0) v[k] = shared[op & 0x3f];
            }
        auto emit = [&](int n, const uint32_t* idx, const PolygonRecord& rec, uint32_t word) {
            const uint32_t tw = tex.words[(word >> 28) & 3];
            const uint16_t tp = uint16_t((uint16_t(tw + pageAdd + 0x2000u) & ~0x60u) | ((flags >> 16) & 0x60u));
            static constexpr int kSplit[2][3] = {{0, 1, 2}, {1, 3, 2}};
            for (int s = 0; s < (n == 4 ? 2 : 1); ++s) {
                ModelTri mt{};
                mt.tpage = tp;
                mt.clut = uint16_t(tw >> 16);
                for (int k = 0; k < 3; ++k) {
                    const int corner = kSplit[s][k];
                    for (int c = 0; c < 3; ++c) mt.p[k][c] = float(v[idx[corner]][c]);
                    const uint16_t uv = uint16_t(rec.uv[size_t(corner)] + uvAdd);
                    mt.u[k] = uint8_t(uv);
                    mt.v[k] = uint8_t(uv >> 8);
                    mt.colour[k] = ((flags & 3) != 1 || (word & kPartColour) || !cols)
                                       ? colour : (colourWords.read<uint32_t>(idx[corner] * 4) & 0xffffff);
                }
                capture->push_back(mt);
            }
        };
        uint32_t rec = bank + hdr.triangleRecords;
        for (uint32_t i = 0; i < ntri; ++i, rec += kPolygonRecordSize) {
            const PolygonRecord record(rec);
            const uint32_t word = record.word;
            const uint32_t idx[3] = {word & 0x7f, (word >> 7) & 0x7f, (word >> 14) & 0x7f};
            emit(3, idx, record, word);
        }
        rec = bank + hdr.quadRecords;
        for (uint32_t i = 0; i < nquad; ++i, rec += kPolygonRecordSize) {
            const PolygonRecord record(rec);
            const uint32_t word = record.word;
            const uint32_t idx[4] = {word & 0x7f, (word >> 7) & 0x7f, (word >> 14) & 0x7f, (word >> 21) & 0x7f};
            emit(4, idx, record, word);
        }
    }

    // exe 0x5f4160(actor, bank): the actor's centre through the camera (its depth SZ >> 2) picks the drawing (flags
    // byte & 0x40: nearer than the depth range; else 0x10 <= depth <= the range and on screen) and the LOD (up to
    // cullWidth: 0, cullHeight: 1, else 2: its part table, vertex colours, part count); the draw flags (game/
    // model_colour), the actor's semi-transparency (bit 0: per part, the bone block's); the colour (15-bit; 0: the
    // bone's). Per part (its bone slot and flags: kPartHidden, 0x40 shared vertices, & 3 its texture cell word): the
    // bone block's matrix and point through the camera; the vertex colours advance by the part's vertex count.
    // (exe 0x6d009b & 1 with flags & 0x20 clear selects exe 0x5f4f00 instead -- the sub-screen's actors have 0x20 set.)
    void actor(const AnimatedActor& a) {
        const auto c = matVec(cam, a.pos.x.whole, a.pos.y.whole, a.pos.z.whole);
        const int32_t x = c[0] + camTr[0], y = c[1] + camTr[1], vz = c[2] + camTr[2];
        const int32_t z = modelOtz(vz); // game/model_colour: the OTZ (SZ >> 2) the exe tests
        const int32_t far = int32_t(stage().depthRange);
        const int32_t sx = vz > 0 ? int32_t(int64_t(x) * h / vz) + ofx : 0x7fff;
        const int32_t sy = vz > 0 ? int32_t(int64_t(y) * h / vz) + ofy : 0x7fff;
        const uint8_t b0 = a.flags;
        const bool on = ((b0 & 0x40) && z < far) ||
                        (z <= far && z >= 0x10 && uint16_t(sx + 0x160) <= 0x400 && uint16_t(sy + 0x88) <= 0x400);
        if (!on) return;
        uint32_t e0 = modelDrawFlags(b0, z); // game/model_colour
        const uint32_t lod = z <= a.cullHeight ? (z <= a.cullWidth ? 0 : 1) : 2;
        uint32_t hdr = ModelLods(a.model).parts[lod] + bank;
        uint32_t colours = a.colourTables[lod];
        const uint32_t parts = a.lodParts(lod);
        const int32_t own = int16_t(a.tint);
        uint32_t colour = rgb15(uint32_t(own));
        const uint8_t semi = a.semiTransparency;
        if (!(semi & 1)) e0 |= uint32_t(semi) << 20;
        const TextureWords tex(a.extraGeometry);
        for (uint32_t i = 0; i < parts; ++i, hdr += kPartHeaderSize) {
            const ModelPartHeader header(hdr);
            const uint8_t ref = a.parts[i].boneSlot, fl = a.parts[i].flags;
            const BoneBlock* blk = boneBlockOf(a, int(ref >> 2u));
            const uint32_t k = ref & 3u;
            if (!(fl & kPartHidden) && blk != nullptr) {
                e0 = (fl & 0x40) ? e0 | 4u : e0 & ~4u;
                const Matrix3 r = mulMatrix3x3(cam, blk->matrices[k]);
                const ShortVec4& point = blk->points[k];
                const auto bt = matVec(cam, point.x, point.y, point.z);
                const int32_t t[3] = {bt[0] + camT[0], bt[1] + camT[1], bt[2] + camT[2]};
                if (own == 0) colour = rgb15(uint32_t(int32_t(int16_t(blk->boneColour(k)))));
                e0 = colour != 0x808080 ? e0 | 2u : e0 & ~2u;
                if (semi & 1) e0 = (e0 & 0xfd9fffffu) | uint32_t(blk->semiBits(k)) << 20;
                uint16_t uvAdd = 0, pageAdd = 0;
                if (fl & 3) {
                    const uint32_t cell = a.textureCellWord(fl & 3u);
                    uvAdd = uint16_t(cell);
                    pageAdd = uint16_t((cell >> 30) & 3);
                }
                part(header, tex, r, t, e0, colour, colours, uvAdd, pageAdd);
            }
            colours += uint32_t(header.vertices) * 4;
        }
    }
};

} // namespace

const ModelPreview& modelPreview(int slot) { return gPreviews[slot & 1]; }

void modelPreviewDraw(size_t c, const std::vector<const AnimatedActor*>& actors, int cx, int cy, int slot,
                      const int32_t* centre) {
    ActorCapture d;
    StageState& st = stage();
    d.bank = st.modelBank;
    d.shift = st.coordinateShift & 0x1f;
    d.camT[2] = 0x400 << d.shift;
    if (centre) {
        const int32_t dist = std::clamp(int32_t(st.depthRange) - 0x40, 0x100, 0x400);
        d.camTr[2] = dist;
        d.camT[2] = dist << d.shift;
        for (int i = 0; i < 3; ++i) {
            d.camTr[i] -= centre[i];
            d.camT[i] -= centre[i] << d.shift;
        }
    }
    const int32_t focal = projection().focal;
    d.h = focal > 0 ? focal : 0x180;
    d.ofx = cx;
    d.ofy = cy;
    ModelPreview& mp = gPreviews[slot];
    mp.tris.clear();
    d.capture = &mp.tris;
    for (const AnimatedActor* a : actors) d.actor(*a);
    if (mp.tris.empty()) return;
    ScreenPrim& p = menu_draw::otPut(c, ScreenPrim::Sprite);
    p.x = int16_t(cx - 0x18);
    p.y = int16_t(cy - 0x18);
    p.w = 0x30;
    p.h = 0x30;
    p.model = int8_t(slot);
}

} // namespace od::port
