// The crumbling of a broken wall (see wall_crumble.hpp).
#include "game/wall_crumble.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/draw_commands.hpp"
#include "game/explosions.hpp"
#include "game/global_class_types.hpp"
#include "game/port_gap.hpp"
#include "game/room_grid.hpp"
#include "game/wall_debris.hpp"

namespace od::port {

namespace {

inline constexpr const char* kWallCrumbleArea = "wall crumble";

// the variants [0x88a408 + variant * 4]
enum class CrumbleVariant : uint8_t {
    Crumble = 0,     // exe 0x419a40: the debris, the blasts of the wall's pattern, the falling grid across x and z
    CrumbleSide = 1, // exe 0x419e70: the debris, two blasts by the side, the falling grid along the side
    OneBlast = 2,    // exe 0x41a2c0
    TwoBlastsX = 3,  // exe 0x41a310
    TwoBlastsZ = 4,  // exe 0x41a380
    FourBlasts = 5,  // exe 0x41a3f0
};
// the blast pattern of variant 0 (the pool entry's break flags >> kBreakBlastsShift & 3: [0x88a410 + k * 4])
enum class BlastPattern : uint8_t { One = 0, TwoX = 1, TwoZ = 2, Four = 3 };

constexpr uint32_t kCrumbleTicks = 0x10; // the crumble's ticks (the state byte counts them)
constexpr int16_t kBlastSize = 0x100;    // the pattern blasts' size and their spacing
constexpr int16_t kSideBlastSize = 0xc0; // variant 1's
constexpr uint16_t kBlastFlashes = 8;

// The crumble's texture cell (the exe's scratch past the shared geometry scratch: the crumble's own): the four uv
// corners of the quads' 0x30-texel cell, each {u, v} (a corner is also written as one word)
struct UvCorner {
    uint8_t u = 0;
    uint8_t v = 0;
    uint16_t word() const { return uint16_t(u | v << 8); }
    void setWord(uint16_t uv) {
        u = uint8_t(uv);
        v = uint8_t(uv >> 8);
    }
};
struct CrumbleCell {
    UvCorner corner0, corner1, corner2, corner3;
};
// the texture cell's one instance
CrumbleCell& crumbleCell() {
    static CrumbleCell c;
    return c;
}
constexpr uint8_t kCellTexels = 0x2f; // a cell's last texel (0x30 wide)

// a blast (static type 8) at the wall's point + (dx, dz), its size given (exe 0x41a2c0 .. 0x41a470)
void blastAt(const WallCrumbleFields& wall, int dx, int dz, uint16_t size) {
    const uint32_t address = allocActor60(0);
    if (!address) return;
    StaticActor& s = staticActorAt(address);
    s.flags = kActorLive;
    s.type = typeByte(GlobalStaticType::Explosion);
    s.variant = 0;
    s.loopFlags |= kActorSweepGroup2;
    s.pos.x.whole = int16_t(wall.point.x + dx);
    s.pos.y.whole = wall.point.y;
    s.pos.z.whole = int16_t(wall.point.z + dz);
    auto&& params = ExplosionParams(s);
    params.spread = size;
    params.flashes = kBlastFlashes;
}
void oneBlast(const WallCrumbleFields& wall) { blastAt(wall, 0, 0, kBlastSize); } // exe 0x41a2c0
void twoBlastsX(const WallCrumbleFields& wall) {                                  // exe 0x41a310
    for (int k = 0; k < 2; ++k) blastAt(wall, (k & 1) ? -kBlastSize : kBlastSize, 0, kBlastSize);
}
void twoBlastsZ(const WallCrumbleFields& wall) { // exe 0x41a380
    for (int k = 0; k < 2; ++k) blastAt(wall, 0, (k & 1) ? -kBlastSize : kBlastSize, kBlastSize);
}
void fourBlasts(const WallCrumbleFields& wall) { // exe 0x41a3f0
    for (int k = 0; k < 4; ++k)
        blastAt(wall, (k & 1) ? -kBlastSize : kBlastSize, k >= 2 ? kBlastSize : -kBlastSize, kBlastSize);
}
// exe 0x41a470: two blasts kSideBlastSize either side of the point (side bit 0: along z, else along the x / z swap)
void sideBlasts(const WallCrumbleFields& wall) {
    for (int k = 0; k < 2; ++k) {
        const uint32_t address = allocActor60(0);
        if (!address) continue;
        StaticActor& s = staticActorAt(address);
        s.flags = kActorLive;
        s.type = typeByte(GlobalStaticType::Explosion);
        s.variant = 0;
        s.loopFlags |= kActorSweepGroup2;
        s.pos.y.whole = wall.point.y;
        const int off = k != 0 ? -kSideBlastSize : kSideBlastSize;
        if (wall.side & 1) {
            s.pos.x.whole = wall.point.x;
            s.pos.z.whole = int16_t(wall.point.z + off);
        } else {
            s.pos.x.whole = int16_t(wall.point.z + off);
            s.pos.z.whole = wall.point.x;
        }
        auto&& params = ExplosionParams(s);
        params.spread = uint16_t(kSideBlastSize);
        params.flashes = kBlastFlashes;
    }
}
// the wall's debris (static global type 0xc of the segment's pool entry) at the point (exe 0x419a40 / 0x419e70)
void spawnDebris(const WallCrumbleFields& wall) {
    const SegmentPoolEntry pe(wall.poolEntry);
    const uint32_t address = allocActor60(0);
    if (!address) return;
    placeWallDebris(address, pe, wall.point.x, wall.point.y, wall.point.z);
}

// the falling quads' texture: CLUT 0x7c91, tpage 0x2e (additive)
constexpr uint16_t kQuadClut = 0x7c91, kQuadTpage = 0x2e;

} // namespace

// the texture cell of tick t (inline in 0x419a40 / 0x419e70): while the sub-state is 0 the cells of a 4 x 2 sheet by
// tick (then the sub-state is set), after that two cells alternating with the tick's parity
void wallCrumbleSelectCell(ControllerActor& crumble) {
    CrumbleCell& cell = crumbleCell();
    const uint8_t t = crumble.state;
    if (crumble.subState == 0) {
        cell.corner0.u = uint8_t((t & 3) * 0x30);
        cell.corner0.v = uint8_t(((t >> 2) & 1) * 0x30);
        if (t > 2) ++crumble.subState;
    } else if (t & 1) {
        cell.corner0.u = 0x90;
        cell.corner0.v = 0;
    } else {
        cell.corner0.u = 0;
        cell.corner0.v = 0x30;
    }
    const uint16_t uv = cell.corner0.word();
    cell.corner3.setWord(uv);
    cell.corner2.setWord(uv);
    cell.corner1.setWord(uv);
    cell.corner1.u = uint8_t(uv + kCellTexels);
    cell.corner3.u = uint8_t(cell.corner3.u + kCellTexels);
    cell.corner2.v = uint8_t(cell.corner2.v + kCellTexels);
    cell.corner3.v = uint8_t(cell.corner3.v + kCellTexels);
}
// the grey of tick t: ((0x10 - t) >> 1) * 10 in the three colour bytes
uint32_t wallCrumbleGrey(uint32_t t) {
    const uint32_t c = uint32_t(int32_t(int32_t(kCrumbleTicks) - int32_t(t)) >> 1) * 10;
    return c | ((c << 8 | c) << 8);
}
// one falling quad (a billboard) of the cell at (x, y, z)
void wallCrumbleQuad(const ControllerActor& crumble, uint32_t grey, int16_t x, int16_t y, int16_t z, uint32_t sizeWord) {
    if (grey == 0) return;
    const CrumbleCell& cell = crumbleCell();
    dc::Billboard b;
    b.colour = dc::billboardColour(grey);
    b.xy = dc::packXY(x, y);
    b.z = dc::signedZWord(z);
    b.clut = kQuadClut;
    b.tpage = kQuadTpage;
    b.uv[0] = cell.corner0.word();
    b.uv[1] = cell.corner1.word();
    b.uv[2] = cell.corner2.word();
    b.uv[3] = cell.corner3.word();
    b.sizeRotation = sizeWord;
    dc::emit(b, crumble.at);
}

namespace {

// exe 0x419a40: variant 0
void crumbleAcross(ControllerActor& crumble) {
    const WallCrumbleFields& wall = classState<WallCrumbleFields>(crumble);
    const SegmentPoolEntry pe(wall.poolEntry);
    if (crumble.state == 0) {
        spawnDebris(wall);
        switch (BlastPattern((pe.breakFlags() >> kBreakBlastsShift) & 3)) {
        case BlastPattern::One: oneBlast(wall); break;
        case BlastPattern::TwoX: twoBlastsX(wall); break;
        case BlastPattern::TwoZ: twoBlastsZ(wall); break;
        default: fourBlasts(wall); break;
        }
    }
    const uint32_t t = crumble.state;
    if (t >= kCrumbleTicks) {
        freeActor(crumble); // exe 0x5e15c0
        return;
    }
    wallCrumbleSelectCell(crumble);
    const uint32_t grey = wallCrumbleGrey(t);
    const uint32_t sizeWord = ((t << 0x16) & 0xfff0000u) | ((t >> 1) + 0xe0);
    const int xa = (pe.breakFlags() & 0x10) ? -0x168 : -0xb4, xb = -xa;
    const int za = (pe.breakFlags() & 0x20) ? -0x168 : -0xb4, zb = -za;
    const int bottom = int(wall.height) * 2;
    for (int j = 0; j <= bottom; j += 0x100) {
        const int16_t y = int16_t(uint16_t(wall.point.y - uint16_t(t * 12) - j));
        for (int x = xa; x <= xb; x += 0x100) {
            wallCrumbleQuad(crumble, grey, int16_t(x + wall.point.x), y, int16_t(wall.point.z + za), sizeWord);
            wallCrumbleQuad(crumble, grey, int16_t(x + wall.point.x), y, int16_t(wall.point.z + zb), sizeWord);
        }
        for (int z = za; z <= zb; z += 0x100) {
            wallCrumbleQuad(crumble, grey, int16_t(wall.point.x + xa), y, int16_t(z + wall.point.z), sizeWord);
            wallCrumbleQuad(crumble, grey, int16_t(wall.point.x + xb), y, int16_t(z + wall.point.z), sizeWord);
        }
    }
    crumble.state = uint8_t(t + 1);
}

// exe 0x419e70: variant 1
void crumbleAlongSide(ControllerActor& crumble) {
    auto&& wall = classState<WallCrumbleFields>(crumble);
    if (crumble.state == 0) {
        const SegmentPoolEntry pe(wall.poolEntry);
        spawnDebris(wall);
        if (uint8_t(pe.offsetX()) == 0)
            wall.side = wall.point.z >= 0 ? 2 : 0;
        else
            wall.side = wall.point.x >= 0 ? 3 : 1;
        sideBlasts(wall);
    }
    const uint32_t t = crumble.state;
    if (t >= kCrumbleTicks) {
        freeActor(crumble); // exe 0x5e15c0
        return;
    }
    wallCrumbleSelectCell(crumble);
    const uint32_t grey = wallCrumbleGrey(t);
    const uint32_t sizeWord = ((t << 0x16) & 0xfff0000u) | ((t >> 1) + 0x100);
    int xa, xb, za, zb;
    if (wall.side & 1) {
        xa = -0x60, xb = 0x60, za = -0x190, zb = 0x190;
    } else {
        xa = -0x190, xb = 0x190, za = -0x60, zb = 0x60;
    }
    const int bottom = int(wall.height) * 2;
    for (int j = 0; j <= bottom; j += kSideBlastSize) {
        const int16_t y = int16_t(uint16_t(wall.height - uint16_t(t << 4) - j + wall.point.y));
        for (int x = xa; x <= xb; x += kSideBlastSize) {
            wallCrumbleQuad(crumble, grey, int16_t(x + wall.point.x), y, int16_t(wall.point.z + za), sizeWord);
            wallCrumbleQuad(crumble, grey, int16_t(x + wall.point.x), y, int16_t(wall.point.z + zb), sizeWord);
        }
        for (int z = za; z <= zb; z += kSideBlastSize) {
            wallCrumbleQuad(crumble, grey, int16_t(wall.point.x + xa), y, int16_t(z + wall.point.z), sizeWord);
            wallCrumbleQuad(crumble, grey, int16_t(wall.point.x + xb), y, int16_t(z + wall.point.z), sizeWord);
        }
    }
    crumble.state = uint8_t(t + 1);
}

} // namespace

void wallCrumbleUpdate(const PlayerEnv&, uint32_t actor) {
    ControllerActor& crumble = controllerActorAt(actor);
    const WallCrumbleFields& wall = classState<WallCrumbleFields>(crumble);
    switch (CrumbleVariant(crumble.variant)) { // [0x88a408 + variant * 4]
    case CrumbleVariant::Crumble: crumbleAcross(crumble); return;
    case CrumbleVariant::CrumbleSide: crumbleAlongSide(crumble); return;
    case CrumbleVariant::OneBlast: oneBlast(wall); return;
    case CrumbleVariant::TwoBlastsX: twoBlastsX(wall); return;
    case CrumbleVariant::TwoBlastsZ: twoBlastsZ(wall); return;
    case CrumbleVariant::FourBlasts: fourBlasts(wall); return;
    default: notPortedAt(kWallCrumbleArea, "pool e0 0x419a20 variant", crumble.variant);
    }
}

} // namespace od::port
