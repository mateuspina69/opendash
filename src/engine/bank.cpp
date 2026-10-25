#include "bank.hpp"

#include <stdexcept>

#include "dat.hpp"

namespace od {

namespace {

constexpr size_t kStaticPartStride = 0x18, kAnimatedPartStride = 0x10, kFaceSize = 12;

int16_t sext16Shift(uint32_t v16, int s) { return int16_t(int16_t(uint16_t(v16 & 0xFFFF)) >> s); }

void require(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(what);
}

} // namespace

ModelPart decodeSubmesh(std::span<const uint8_t> b, size_t hdr) {
    require(hdr + 16 <= b.size(), "part header out of bounds");
    const uint8_t* h = b.data() + hdr;
    const uint32_t packed = rd32(h), triOfs = rd32(h + 4), quadOfs = rd32(h + 8), vertOfs = rd32(h + 12);
    const int tris = int(packed & 0xFF), quads = int((packed >> 8) & 0xFF), nv = int((packed >> 16) & 0xFF);
    const int s = 6 - int(packed >> 24);
    require(s >= 0 && s <= 15 && nv > 0, "bad part header");
    require(size_t(vertOfs) + size_t(nv) * 4 <= b.size(), "vertex span out of bounds");
    ModelPart p;
    for (int i = 0; i < nv; ++i) {
        const uint32_t u = rd32(b.data() + vertOfs + size_t(i) * 4);
        p.verts.push_back({sext16Shift(u << 6, s), sext16Shift(u >> 4, s), sext16Shift(u >> 14, s)});
    }
    auto faces = [&](size_t ofs, int n, int count) {
        require(size_t(ofs) + size_t(n) * kFaceSize <= b.size(), "face span out of bounds");
        for (int j = 0; j < n; ++j) {
            const size_t f = ofs + size_t(j) * kFaceSize;
            const uint32_t w = rd32(b.data() + f + 8);
            ModelFace face{count, {}, {}, uint8_t((w >> 28) & 3), count == 4 && (w & 0x40000000u) != 0,
                           (w & 0x80000000u) != 0, (w & 0x40000000u) != 0};
            for (int k = 0; k < 4; ++k) {
                face.idx[k] = uint8_t((w >> (7 * k)) & 0x7F);
                face.uvs[k] = {b[f + 2 * k], b[f + 2 * k + 1]};
            }
            for (int k = 0; k < count; ++k) require(face.idx[k] < nv, "face index out of range");
            p.faces.push_back(face);
        }
    };
    faces(triOfs, tris, 3);
    faces(quadOfs, quads, 4);
    return p;
}

std::span<const uint8_t> modelBank(const GameFile& st) {
    // FUN_005f3200: leading records (header bit31/bit30, size = header & 0x3fffffff) come
    // first; the bank proper starts at the first plain header (entry offsets are relative to it)
    const BankSegment s = modelBankSegment(st);
    return s.seg.subspan(s.bankOffset);
}

bool hasModelBank(const GameFile& st) { return st.has(FilePart::Kind::Models); }

BankSegment modelBankSegment(const GameFile& st) {
    BankSegment r;
    r.seg = st.require(FilePart::Kind::Models).bytes;
    size_t p = 0;
    while (p + 4 <= r.seg.size()) {
        const uint32_t h = rd32(r.seg.data() + p);
        if (!(h & 0xC0000000u)) {
            r.bankOffset = p;
            return r;
        }
        if (h & 0x80000000u) r.playerAnims = long(p);
        else r.extra = long(p);
        if (!(h & 0x3FFFFFFFu)) break;
        p += h & 0x3FFFFFFFu;
    }
    throw std::runtime_error(st.name + ": model bank chain runs past the part");
}

std::vector<BankEntry> bankEntries(std::span<const uint8_t> b) {
    require(b.size() >= 4, "bank too small");
    const uint32_t count = rd32(b.data());
    require(4 + size_t(count) * 16 <= b.size(), "bank entry table out of bounds");
    std::vector<BankEntry> out;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t e = 4 + size_t(i) * 16;
        out.push_back({int(i), b[e], b[e + 1], b[e + 2], rd32(b.data() + e + 4)});
    }
    return out;
}

BankModel decodeModel(std::span<const uint8_t> b, const BankEntry& e, int lod) {
    require(lod >= 0 && lod < 3 && size_t(e.modelOfs) + 28 <= b.size(), "model header out of bounds");
    const uint32_t counts = rd32(b.data() + e.modelOfs);
    const uint32_t partTable = rd32(b.data() + e.modelOfs + 4 + size_t(lod) * 4);
    const int parts = int((counts >> (8 * lod)) & 0xFF);
    const size_t stride = (e.cls & 0xF0) == 0x20 ? kAnimatedPartStride : kStaticPartStride;
    BankModel m;
    for (int p = 0; p < parts; ++p) m.parts.push_back(decodeSubmesh(b, partTable + size_t(p) * stride));
    // texture table (header word 6, FUN_005f45a0 passes it as actor+0x98): ends at 0x1, max 4
    const uint32_t tex = rd32(b.data() + e.modelOfs + 24);
    for (int i = 0; i < 4 && size_t(tex) + size_t(i) * 4 + 4 <= b.size(); ++i) {
        const uint32_t t = rd32(b.data() + tex + size_t(i) * 4);
        if (t == 1) break;
        m.textures.push_back(t);
    }
    return m;
}

} // namespace od
