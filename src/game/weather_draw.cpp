// Weather overlays (see weather_draw.hpp for the evidence and the packet layouts).
#include "game/actor_state.hpp"
#include "game/weather_draw.hpp"

#include "game/actor_pools.hpp"
#include "game/actors.hpp"
#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/display.hpp"
#include "game/snow_state.hpp"

namespace od::weather {
namespace {

constexpr RoutineId kBlizzardLayer = RoutineId::BlizzardLayerUpdate; // the classes' update routines
constexpr RoutineId kSnow = RoutineId::SnowUpdate;
// the blizzard's grid: 9 rows x 11 columns of 32-pixel quads, cells of a 128 x 128 texture (v from 0x80), tpage 0x2e
// (additive) CLUT 0x7c92
constexpr int kRows = 9, kColumns = 11, kCell = 32, kCellMask = 0x60, kCellEdge = 0x1f, kTextureTop = 0x80;
constexpr uint16_t kBlizzardTpage = 0x2e, kBlizzardClut = 0x7c92;
constexpr int kPhaseStep = 0x20; // a vertex's phase step (a corner: -+ this)
// the snow: sprites of the strip u / v 0x20..0x3f, tpage 0x2f (additive) CLUT 0x7c50, drawn from depth kSnowNear on,
// at (x + kSnowLeft, y - kSnowTop) in a box kSnowBox pixels wide
constexpr uint16_t kSnowTpage = 0x2f, kSnowClut = 0x7c50;
constexpr int kSnowNear = 100, kSnowLeft = 0xa0, kSnowTop = 0x14, kSnowBox = 360;
constexpr uint8_t kSnowU0 = 0x20, kSnowU1 = 0x3f;

RoutineId handler(const port::ActorHeader& a, port::ActorPool p1, port::ActorPool p2) {
    const RoutineRef h = port::actorHandler(a, p1);
    return h ? h.id() : port::actorHandler(a, p2).id();
}

// effect 0x431af0's draw (the tail of the same function, after the scroll): 9 x 11 POLY_GT4 of 32 x 32
void layer(const port::EffectActor& a, std::vector<Quad>& out, int extra) {
    if (!(a.flags & port::kActorDrawn) || port::display().drawingOff != 0) return;
    const port::BlizzardLayerFields& f = port::classState<port::BlizzardLayerFields>(a);
    if (!f.speed.targetMapped(8)) return;
    const uint8_t v6 = a.variant;
    const port::CameraState& cam = port::cameraState();
    const int32_t X = (int32_t(f.scrollX) >> 4) + cam.viewAngles.y;
    const int32_t Y = (int32_t(f.scrollY) >> 4) + cam.viewAngles.x;
    const int uOff0 = (v6 & port::kBlizzardMirrorU) ? kCellEdge : 0, uOff1 = (v6 & port::kBlizzardMirrorU) ? 0 : kCellEdge;
    const int vOff0 = (v6 & port::kBlizzardMirrorV) ? kCellEdge : 0, vOff1 = (v6 & port::kBlizzardMirrorV) ? 0 : kCellEdge;
    const int sx = X & kCellEdge, sy = Y & kCellEdge;
    const uint32_t base = f.speed->grey, amp = f.speed->swing;
    auto grey = [&](int32_t phase) {
        return uint8_t(base + uint32_t((int32_t(amp) * port::fixedSin(phase)) >> 12));
    };
    const int more = (extra + kCell - 1) / kCell;
    for (int row = 0; row < kRows; ++row) {
        const int32_t yTot = Y + row * kCell;
        const int32_t phaseRow = X + Y + kPhaseStep + row * kCell;
        const int ytop = row * kCell - sy;
        const int vb = (v6 & port::kBlizzardMirrorV) ? 0xe0 - (yTot & kCellMask) : (yTot & kCellMask) + kTextureTop;
        for (int col = -more; col < kColumns + more; ++col) {
            const int32_t phase = phaseRow + col * kCell;
            const int32_t xTot = X + col * kCell;
            const int ub = (v6 & port::kBlizzardMirrorU) ? kCellMask - (xTot & kCellMask) : xTot & kCellMask;
            const int xl = col * kCell - sx;
            Quad q;
            q.x[0] = q.x[2] = int16_t(xl);
            q.x[1] = q.x[3] = int16_t(xl + kCell);
            q.y[0] = q.y[1] = int16_t(ytop);
            q.y[2] = q.y[3] = int16_t(ytop + kCell);
            q.u[0] = q.u[2] = uint8_t(ub + uOff0);
            q.u[1] = q.u[3] = uint8_t(ub + uOff1);
            q.v[0] = q.v[1] = uint8_t(vb + vOff0);
            q.v[2] = q.v[3] = uint8_t(vb + vOff1);
            q.grey[0] = grey(phase - kPhaseStep);
            q.grey[1] = q.grey[2] = grey(phase);
            q.grey[3] = grey(phase + kPhaseStep);
            q.tpage = kBlizzardTpage;
            q.clut = kBlizzardClut;
            q.ot = f.otSlot;
            q.source = kBlizzardLayer;
            q.original = col >= 0 && col < kColumns;
            out.push_back(q);
        }
    }
}

// exe 0x591c10 for every particle the move loop 0x5916d0 drew, with the wrapped s16 position it passed (kept by the
// logic port in the snow's draw block: the packed slot +0x2c loses the sign of a wrapped y)
void snow(uint32_t slotIndex, std::vector<Quad>& out, int extra) {
    const port::SnowDrawBlock& blk = port::snowDrawBlock(slotIndex);
    const uint32_t mask = blk.drawn;
    for (int k = int(port::kSnowParticles) - 1; k >= 0; --k) {
        if (!((mask >> k) & 1)) continue;
        const port::ShortVec3& p = blk.particles[size_t(k)];
        const int x = p.x, y = p.y, z = p.z;
        if (z < kSnowNear) continue;
        const int s = int16_t((0x400 - z) >> 8) + 1;
        Quad q;
        q.grey[0] = q.grey[1] = q.grey[2] = q.grey[3] = uint8_t((z >> 3) + 0x40);
        q.u[0] = q.u[2] = kSnowU0;
        q.u[1] = q.u[3] = kSnowU1;
        q.v[0] = q.v[1] = kSnowU0;
        q.v[2] = q.v[3] = kSnowU1;
        q.tpage = kSnowTpage;
        q.clut = kSnowClut;
        q.ot = uint16_t(z >> 1);
        q.source = RoutineId::SnowParticleDraw;
        // the particle box is kSnowBox pixels wide (x + 0xa0 in -20..340): wider windows get its copies at +-kSnowBox
        for (int c = -2; c <= 2; ++c) {
            const int x0 = int16_t(x + kSnowLeft) + c * kSnowBox, y0 = int16_t(y - kSnowTop);
            if (c != 0 && (x0 + s < -extra || x0 > 320 + extra)) continue;
            q.x[0] = q.x[2] = int16_t(x0);
            q.x[1] = q.x[3] = int16_t(x0 + s);
            q.y[0] = q.y[1] = int16_t(y0);
            q.y[2] = q.y[3] = int16_t(y0 + s);
            q.original = c == 0;
            out.push_back(q);
        }
    }
}

} // namespace

void collect(std::vector<Quad>& out, int extra) {
    out.clear();
    port::EffectPool& effects = port::effectPool();
    port::StaticPool& statics = port::staticPool();
    for (uint32_t i = 0; i < effects.size(); ++i) { // pool 0xa0
        const port::EffectActor& a = effects[i];
        if (!a.live()) continue;
        if (handler(a, port::ActorPool::Effects, port::ActorPool::EffectsLate) == kBlizzardLayer) layer(a, out, extra);
    }
    for (uint32_t i = 0; i < statics.size(); ++i) { // pool 0x60
        const port::StaticActor& a = statics[i];
        if (!a.live()) continue;
        if (handler(a, port::ActorPool::Statics, port::ActorPool::StaticsLate) == kSnow) snow(i, out, extra);
    }
}

Conditions conditions() {
    Conditions c;
    port::EffectPool& effects = port::effectPool();
    port::StaticPool& statics = port::staticPool();
    for (uint32_t i = 0; i < effects.size(); ++i) {
        const port::EffectActor& a = effects[i];
        if (!a.live() || handler(a, port::ActorPool::Effects, port::ActorPool::EffectsLate) != kBlizzardLayer) continue;
        if (!(a.flags & port::kActorDrawn) || port::display().drawingOff != 0) continue; // as the layer's draw
        const port::BlizzardLayerFields& f = port::classState<port::BlizzardLayerFields>(a);
        if (f.otSlot >= kBackSlot || !f.speed.targetMapped(8)) continue;
        if (c.blizzards++ == 0) c.speed = *f.speed;
    }
    for (uint32_t i = 0; i < statics.size(); ++i) {
        const port::StaticActor& a = statics[i];
        if (!a.live() || handler(a, port::ActorPool::Statics, port::ActorPool::StaticsLate) != kSnow) continue;
        const port::SnowDrawBlock& blk = port::snowDrawBlock(i);
        if (c.snowStatics++ == 0) {
            const port::snow_state::SnowFields& f = port::classState<port::snow_state::SnowFields>(a);
            c.wind[0] = f.windX.get();
            c.wind[1] = f.windY.get();
            c.wind[2] = f.windZ.get();
        }
        for (uint32_t m = blk.drawn; m; m &= m - 1) ++c.flakes;
    }
    return c;
}

} // namespace od::weather
