#include "room.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

#include "dat.hpp"

namespace od {

namespace {

int16_t s16(int v) { return int16_t(uint16_t(v & 0xFFFF)); }
int s8(uint32_t v) { return int(int8_t(uint8_t(v & 0xFF))); }

struct Reader {
    std::span<const uint8_t> b;
    uint32_t u32(size_t o) const { return rd32(b.data() + o); }
    uint16_t u16(size_t o) const { return rd16(b.data() + o); }
    uint8_t u8(size_t o) const { return b[o]; }
};

} // namespace

std::span<const uint8_t> roomBlock(const GameFile& st) { return st.require(FilePart::Kind::Geometry).bytes; }

std::vector<RoomGrid> roomGrids(std::span<const uint8_t> block) {
    const Reader r{block};
    const uint32_t first = r.u32(8);
    const size_t pool = size_t(r.u32(0)) * 4;
    std::vector<RoomGrid> out;
    for (uint32_t i = 0; i + 2 < first; ++i) {
        const size_t ofs = size_t(r.u32(8 + 4 * i)) * 4;
        if (ofs + 4 > pool) break;
        const RoomGrid g{r.u8(ofs), r.u8(ofs + 1), r.u8(ofs + 2), r.u8(ofs + 3), ofs + 4};
        // trailing header entries can point at non-grid data (ST2D: 13 entries, 10 grids)
        if (g.x0 > g.x1 || g.z0 > g.z1 ||
            g.cellsOffset + size_t(g.width()) * size_t(g.z1 - g.z0 + 1) * 12 > pool)
            break;
        out.push_back(g);
    }
    return out;
}

std::map<int, int> segmentOwners(std::span<const uint8_t> block) {
    const Reader r{block};
    std::map<int, int> owners;
    const auto grids = roomGrids(block);
    for (int gi = 0; gi < int(grids.size()); ++gi) {
        const RoomGrid& g = grids[gi];
        const size_t cells = size_t(g.width()) * size_t(g.z1 - g.z0 + 1);
        for (size_t i = 0; i < cells; ++i) {
            const size_t c = g.cellsOffset + i * 12;
            if (c + 2 > block.size()) continue;
            const uint16_t w = r.u16(c);
            if ((w & 0x8000) && !(w & 0x4000)) owners.try_emplace(w & 0x7FF, gi);
        }
    }
    return owners;
}

std::vector<RoomSegment> roomSegments(std::span<const uint8_t> block) {
    const Reader r{block};
    const size_t base = size_t(r.u32(4)) * 4;
    const int count = r.u16(base);
    std::vector<RoomSegment> out;
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        const size_t p = base + size_t(i) * 8;
        out.push_back({i, r.u8(p + 7), r.u8(p + 6), r.u16(p + 8), r.u8(p + 10), r.u8(p + 11)});
    }
    return out;
}

std::vector<RoomQuad> parseRoom(std::span<const uint8_t> block, int shift, int sub,
                                RoomStats* statsOut, int lod, bool switchable) {
    RoomStats st;
    const Reader r{block};
    const size_t end = block.size();
    const size_t pool = size_t(r.u32(0)) * 4;
    const auto grids = roomGrids(block);
    const auto owners = segmentOwners(block);
    std::vector<RoomQuad> quads;
    // the segments the requested sub-grid's cells reference: the game draws whatever the active grid references, and
    // a sub-room can reuse segments another grid references first (ST25 sub 6, Pokte Village during the Tron fight,
    // shares its houses / walls with the village grid: they were dropped as "owned" by the other grid -> square
    // holes and missing houses)
    std::set<int> subRefs;
    if (sub >= 0 && sub < int(grids.size())) {
        const RoomGrid& g = grids[size_t(sub)];
        const size_t cells = size_t(g.width()) * size_t(g.z1 - g.z0 + 1);
        for (size_t i = 0; i < cells; ++i) {
            const size_t c = g.cellsOffset + i * 12;
            if (c + 2 > end) continue;
            const uint16_t w = r.u16(c);
            if ((w & 0x8000) && !(w & 0x4000)) subRefs.insert(w & 0x7FF);
        }
    }

    for (const RoomSegment& s : roomSegments(block)) {
        const RoomGrid* g = nullptr;
        if (subRefs.count(s.index)) {
            g = &grids[size_t(sub)];
        } else if (auto it = owners.find(s.index); it != owners.end()) {
            g = &grids[it->second];
        } else { // unreferenced: fall back to the first grid containing it
            ++st.ambiguousGrid;
            // a sub-room draws only what its grid references (Maybe_CullSegmentsMode0 0x5f6080 flags the active
            // grid's segments, the rest is never drawn): ST0D.DAT also carries the segments only ST0D01's grid
            // references, near-copies of its trees 16 units higher -> z-fighting, intact trees over the stumps
            if (sub >= 0) continue;
            for (const RoomGrid& cand : grids)
                if (cand.contains(s.gx, s.gz)) {
                    g = &cand;
                    break;
                }
            if (!g) {
                ++st.noGrid;
                continue;
            }
        }
        if (sub >= 0 && (sub >= int(grids.size()) || g != &grids[sub])) continue;
        const size_t cell = pool + 4 + size_t(s.cellIdx) * 12;
        const uint32_t cflags = r.u32(cell + 4);
        const uint32_t varOfs = r.u32(cell + 8) & 0xFFFF;
        if (!g->contains(s.gx, s.gz)) {
            ++st.noGrid;
            continue;
        }
        const long long gcell = (long long)g->cellsOffset +
                                ((long long)(s.gz - g->z0) * g->width() - g->x0 + s.gx) * 12;
        if (!(gcell + 6 >= 0 && size_t(gcell + 6) < end)) {
            ++st.noGrid;
            continue;
        }

        const int16_t ax = s16((((s.gx - 0x40) * 0x200) + ((cflags & 0x10000000) ? 0x100 : 0) + 0x100) << shift);
        const int16_t az = s16((((s.gz - 0x40) * 0x200) + ((cflags & 0x20000000) ? 0x100 : 0) + 0x100) << shift);
        const int16_t ay = s16((((0x40 - int(r.u8(size_t(gcell) + 6))) * 0x10) - ((s.height >> 4) & 0x7F0)) << shift);

        const int slot = lod >= 0 ? lod : ((s.flags & 0x20) ? 0 : (2 - ((s.flags & 0x40) ? 1 : 0)));
        // run-time switches (Maybe_RenderSegments 0x5fb050): the variant is flags & 3 (bit 0 = the alternate set,
        // FUN_00600f10, allowed when the pool entry's byte 7 bit 0 is set); variant word 1 bit (28 + slot) marks
        // door strip lists (list 0 = closed door, drawn while flags bit 2 is clear; list 1 = open doorway),
        // bit (24 + slot) camera-facing lists 0..3 (the door lists move to 4 / 5)
        const bool altAllowed = (r.u8(cell + 7) & 1) != 0;
        auto doorBits = [&](int v) { return r.u32(pool + (size_t(varOfs) + size_t(v) * 3) * 4 + 4); };
        // damage variants (Maybe_SwitchCellSegmentVariant 0x58d5f0: flags & 3 = 0 .. the pool entry's +0xb & 3, set
        // by scripts / attacks at run time, e.g. the Pokte houses in the Tron fight): every variant is built
        const int damage = r.u8(cell + 7) & 3; // the game's 0x6d0084 entry +0xb (cell = entry + 4)
        const bool dynamic = switchable && (altAllowed || damage != 0 || (doorBits(s.flags & 3) & (0x10000000u << slot)) ||
                                            (doorBits((s.flags & 3) | 1) & (0x10000000u << slot)));
        int vFirst = dynamic && altAllowed ? (s.flags & 2) : (s.flags & 3);
        int vLast = dynamic && altAllowed ? (s.flags & 2) | 1 : (s.flags & 3);
        if (dynamic && damage != 0) {
            vFirst = 0;
            vLast = std::max(vLast, damage);
        }
        for (int v = vFirst; v <= vLast; ++v) {
        const size_t variant = pool + (size_t(varOfs) + size_t(v) * 3) * 4;
        const uint32_t stripOfs = r.u32(variant + size_t(slot) * 4) & 0xFFFF;
        if (!stripOfs || pool + size_t(stripOfs) * 4 >= end) {
            ++st.noStrip;
            continue;
        }
        ++st.segments;
        const uint32_t vbits = r.u32(variant + 4);

        const size_t sb = pool + size_t(stripOfs) * 4;
        const uint32_t hdr = r.u32(sb);
        const int outer = int(hdr & 0xFF);
        const uint32_t listOfs = (hdr >> 8) & 0xFF;
        if (((hdr >> 16) & 0xFF) > 8) {
            ++st.badHeader;
            continue;
        }
        const int sh = int((((hdr >> 16) & 0xFF) + uint32_t(shift)) & 0x1F);
        const size_t tpage = sb + 4;
        size_t lst = tpage + size_t(listOfs) * 4;

        for (int o = 0; o < outer; ++o, lst += 8) {
            const uint32_t w0 = r.u32(lst), w1 = r.u32(lst + 4);
            size_t vp = pool + size_t(w0 & 0xFFFF) * 4;
            size_t uvp = pool + size_t(w1 & 0xFFFF) * 4;
            size_t shp = pool + size_t(w1 >> 16) * 4; // per-face gouraud dword (FUN_005fbba0 @0x6d0194)
            int it = int(w0 >> 16);
            if (it <= 0 || it > 16384) continue;
            int16_t cx[4]{}, cy[4]{}, cz[4]{};
            uint32_t run = 0;
            while (it-- > 0) {
                if (vp + 16 > end || uvp + 8 > end) break;
                const uint32_t d0 = r.u32(vp), d1 = r.u32(vp + 4);
                auto setVert = [&](int k, uint32_t d) {
                    cx[k] = s16((s8(d) << sh) + ax);
                    cy[k] = s16((s8(d >> 8) << sh) + ay);
                    cz[k] = s16((s8(d >> 16) << sh) + az);
                };
                if (run == 0) {
                    setVert(0, d0);
                    setVert(1, d1);
                    setVert(2, r.u32(vp + 8));
                    setVert(3, r.u32(vp + 12));
                } else {
                    setVert(2, d0);
                    setVert(3, d1);
                }
                if ((d0 & 0xFF000000u) != 0xFF000000u) {
                    RoomQuad q;
                    q.tex = r.u32(tpage + size_t((d1 >> 24) & 0x3F) * 4);
                    const uint32_t u0 = r.u32(uvp), u1 = r.u32(uvp + 4);
                    const uint16_t uv16[4] = {uint16_t(u0 & 0xFFFF), uint16_t(u0 >> 16),
                                              uint16_t(u1 & 0xFFFF), uint16_t(u1 >> 16)};
                    for (int k = 0; k < 4; ++k) {
                        q.verts[k] = {cx[k], cy[k], cz[k]};
                        q.uvs[k] = {uint8_t(uv16[k] & 0xFF), uint8_t(uv16[k] >> 8)};
                    }
                    q.segment = s.index;
                    q.uvOfs = uint32_t(uvp);
                    const uint32_t sd = shp + 4 <= end ? r.u32(shp) : 0x80808080u;
                    for (int k = 0; k < 4; ++k) q.shades[k] = uint8_t(sd >> (8 * k));
                    // FUN_005fbba0 draws when (d0 & 0x20000000) or the projected winding is front
                    q.doubleSided = (d0 & 0x20000000u) != 0;
                    q.otMode = uint8_t(d0 >> 30);
                    q.semi = uint8_t((d0 >> 24) & 3);
                    if (dynamic) {
                        q.variant = int8_t(v);
                        q.list = uint8_t(o);
                        q.doorList = (vbits & (0x10000000u << slot)) != 0;
                        q.cameraLists = (vbits & (0x1000000u << slot)) != 0;
                    }
                    quads.push_back(q);
                    ++st.quads;
                } else {
                    ++st.skippedFaces;
                }
                if (run == 0) {
                    run = r.u32(vp + 8) >> 24;
                    vp += 16;
                } else {
                    --run;
                    vp += 8;
                }
                if (run != 0) {
                    cx[0] = cx[2]; cy[0] = cy[2]; cz[0] = cz[2];
                    cx[1] = cx[3]; cy[1] = cy[3]; cz[1] = cz[3];
                }
                uvp += 8;
                shp += 4;
            }
        }
        } // variants
    }
    if (statsOut) *statsOut = st;
    return quads;
}

bool segmentQuadDrawn(const RoomQuad& q, uint8_t flags) {
    if (q.variant < 0) return true;
    if (q.variant != (flags & 3)) return false;
    uint32_t mask = q.doorList ? ((flags & 4) ? 1u : 2u) : 0u;
    if (q.cameraLists) mask <<= 4; // the camera-facing lists 0..3 themselves are always drawn here
    return !((mask >> q.list) & 1);
}

std::vector<RoomQuad> parseFloor(std::span<const uint8_t> block, const FloorUvTable& uvTable,
                                 const FloorOrientTable& orientTable, int shift, int sub) {
    const Reader r{block};
    const RoomGrid g = roomGrids(block).at(size_t(sub));
    const auto segs = roomSegments(block);
    const int unit = 0x200 << shift;
    std::vector<RoomQuad> out;
    for (int gz = g.z0; gz <= g.z1; ++gz) {
        for (int gx = g.x0; gx <= g.x1; ++gx) {
            const size_t cell = g.cellsOffset + (size_t(gz - g.z0) * g.width() + size_t(gx - g.x0)) * 12;
            if (cell + 12 > block.size()) continue;
            uint16_t word = r.u16(cell);
            if (!(word & 0x8000)) continue;
            if (!(word & 0x4000)) {
                // segment cell: Maybe_DispatchGridCell also draws a floor tile under the segment when
                // its entry s16 (.height) is negative: word = (height & 0xff) | (word & 0xf000) | 0xc000
                const size_t idx = word & 0x7FF;
                if (idx >= segs.size() || !(segs[idx].height & 0x8000)) continue;
                word = uint16_t((segs[idx].height & 0xFF) | (word & 0xF000) | 0xC000);
            }
            const int16_t x0 = s16((gx - 0x40) * 0x200 << shift);
            const int16_t z0 = s16((gz - 0x40) * 0x200 << shift);
            const int16_t x1 = s16(x0 + unit), z1 = s16(z0 + unit);
            // corner k -> (x, z): 0=(x0,z1) 1=(x1,z1) 2=(x0,z0) 3=(x1,z0); height/shade index = k
            const int16_t corner[4][2] = {{x0, z1}, {x1, z1}, {x0, z0}, {x1, z0}};
            const bool diag = (word & 0x2000) != 0;
            static constexpr int kOrderStraight[4] = {0, 1, 2, 3};
            static constexpr int kOrderDiag[4] = {1, 3, 0, 2};
            const int* order = diag ? kOrderDiag : kOrderStraight;
            const int orient = ((word >> 8) & 3) + (diag ? 8 : 0);
            const uint16_t lo = uvTable[word & 0x1F][0], hi = uvTable[word & 0x1F][1];
            const uint16_t halves[4] = {lo, hi, uint16_t(lo + 0x1F00), uint16_t(hi + 0x1F00)};
            RoomQuad q;
            for (int slot = 0; slot < 4; ++slot) {
                const int k = order[slot];
                q.verts[slot] = {corner[k][0], s16(-int(r.u8(cell + 4 + k)) * 0x10 << shift), corner[k][1]};
                const uint16_t uv = halves[orientTable[orient][slot]];
                q.uvs[slot] = {uint8_t(uv & 0xFF), uint8_t(uv >> 8)};
                q.shades[slot] = r.u8(cell + 8 + k);
            }
            const uint32_t clut = ((((word >> 7) & 1) + 0x1F0) * 0x40) | ((word >> 3) & 8);
            q.tex = (uint32_t(kFloorTpage) << 16) | clut;
            q.segment = -1;
            q.farBothSides = (word & 0x800) != 0; // (a segment cell's word has it clear)
            q.lodPoint = {s16((gx << 9) - 0x7f00), 0x190, s16((gz << 9) - 0x7f00)}; // Maybe_WalkVisibleGridCellsA
            out.push_back(q);
        }
    }
    return out;
}

} // namespace od
