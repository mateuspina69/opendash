// The missile trail (see missile_trail.hpp).
#include "game/missile_trail.hpp"

#include <algorithm>

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/draw_commands.hpp"
#include "game/port_gap.hpp"
#include "game/stage_state.hpp"

namespace od::port {

namespace {

constexpr const char* kTrailArea = "missile trail"; // the area of its port-gap errors

// [0x8c420c]
enum class TrailState : uint8_t { Start = 0, Follow = 1 }; // exe 0x5636e0 / 0x563730

constexpr uint8_t kTrailWidthMask = 3;         // the variant's bits: the ribbon's width
constexpr int kHeadWidth = 8, kWidthStep = 16; // the head's half width: width * 16 + 8
constexpr uint32_t kRibbonCode = 0x3a000000;   // POLY_G4, semi-transparent; the corners black
constexpr uint32_t kHeadShade = 0x3a343434;    // the screen points' grey at the head
constexpr uint32_t kShadeStep = 0x20202;       // one step fainter further back
constexpr uint8_t kAdditive = 1;               // the DR_MODE 0xe1000220's blend

// exe 0x5636e0: the ring started at the actor's position, the warp offset cleared
void start(StaticActor& trail, MissileTrailFields& f) {
    const Actor& parent = actorLayoutAt(f.parent);
    trail.runFlags |= kActorWarpKept;
    trail.pos.x = 0;
    trail.pos.y = 0;
    trail.pos.z = 0;
    f.head = 0;
    f.length = 0;
    f.xs[0] = parent.pos.x.whole;
    f.ys[0] = parent.pos.y.whole;
    f.zs[0] = parent.pos.z.whole;
    ++trail.state;
}

// The rest of exe 0x563730: the ribbon from the head back along the ring. The exe projects each point (<< the
// coordinate shift; exe 0x5f2dd0) and gives it a screen half width w = exe 0x5f2f20(point, width) (head: the variant's
// width * 16 + 8; then (0x10 - (n >> 1)) * (width * 2 + 1) for the remaining count n), its corners C0 (sx - w, sy -
// w), C1 (sx + w + 1, sy - w), C2 (sx + w + 1, sy + w + 1), C3 (sx - w, sy + w + 1). Between a point t and the newer
// one o, unless w(t) == 0: a DR_MODE 0xe1000220 (abr 1, additive) then 4 POLY_G4 0x3a (C_t.0 S_t C_o.0 S_o), (S_t
// C_t.2 S_o C_o.2), (C_t.1 S_t C_o.1 S_o), (S_t C_t.3 S_o C_o.3), S coloured k (t) / k + 0x020202 (o), the corners
// black, k = 0x343434 - 0x020202 per step. The exe also skips a quad behind or off ITS camera (a point clipped, or
// nearer than 0x30 at either end); OpenDash draws the ribbon in world space, for the modern camera: seen from it the
// trail vanished whenever Megaman faced the viewer (the PS1 camera then looks the other way), so those tests are not
// made. The corners are kept as the point + a camera-plane offset (dc::otQuadG4): p pixels at the point = p * (width
// << shift) / w camera-input units, which does not depend on the projection.
void ribbon(const StaticActor& trail, const MissileTrailFields& f, int h) {
    const int sh = stage().coordinateShift & 0x1f;
    struct Set {
        int16_t pt[3]; // camera-input point
        int w;         // half width (pixels)
        float scale;   // camera-input units per pixel at the point
    } set[2]{};
    auto load = [&](Set& e, int i, int width) {
        const int16_t p[3] = {f.xs[size_t(i)], f.ys[size_t(i)], f.zs[size_t(i)]};
        e.w = width > 0 ? width : 0;
        for (int k = 0; k < 3; ++k) e.pt[k] = int16_t(uint16_t(p[k] << sh));
        e.scale = float(1 << sh);
    };
    // corner k's pixel offset from the screen point (C0..C3)
    auto corner = [](const Set& e, int k, float off[2]) {
        static constexpr int kx[4] = {0, 1, 1, 0}, ky[4] = {0, 0, 1, 1};
        off[0] = float(kx[k] ? e.w + 1 : -e.w) * e.scale;
        off[1] = float(ky[k] ? e.w + 1 : -e.w) * e.scale;
    };
    const int width = trail.variant & kTrailWidthMask;
    int t = 0;
    load(set[0], h, width * kWidthStep + kHeadWidth);
    uint32_t k = kHeadShade;
    int idx = h;
    for (int n = f.length; n > 1; --n) {
        idx = (idx + int(kTrailPoints) - 1) % int(kTrailPoints);
        t ^= 1;
        Set& cur = set[t];
        const Set& old = set[t ^ 1];
        load(cur, idx, (0x10 - (n >> 1)) * (width * 2 + 1));
        if (cur.w != 0) {
            const uint32_t kn = k + kShadeStep;
            // per quad: a corner of a set (its index) or the set's screen point (c < 0)
            struct V {
                bool newer;
                int c;
            };
            static constexpr V kQuads[4][4] = {{{false, 0}, {false, -1}, {true, 0}, {true, -1}},
                                               {{false, -1}, {false, 2}, {true, -1}, {true, 2}},
                                               {{false, 1}, {false, -1}, {true, 1}, {true, -1}},
                                               {{false, -1}, {false, 3}, {true, -1}, {true, 3}}};
            for (int q = 0; q < 4; ++q) {
                uint32_t c[4];
                int16_t v[4][3];
                float off[4][2];
                for (int j = 0; j < 4; ++j) {
                    const V& vv = kQuads[q][j];
                    const Set& e = vv.newer ? old : cur;
                    std::copy(e.pt, e.pt + 3, v[j]);
                    if (vv.c < 0) {
                        off[j][0] = off[j][1] = 0.0f;
                        c[j] = vv.newer ? kn : k;
                    } else {
                        corner(e, vv.c, off[j]);
                        c[j] = kRibbonCode;
                    }
                }
                dc::otQuadG4(trail.at, c, kAdditive, v, off);
            }
        }
        k -= kShadeStep;
    }
}

// exe 0x563730: the ring grown (up to kTrailPoints) and moved by the warp offset (then cleared), the actor's position
// at the new head; the ribbon
void follow(StaticActor& trail, MissileTrailFields& f) {
    f.length = int16_t(f.length + 1);
    if (f.length > int16_t(kTrailPoints)) f.length = int16_t(kTrailPoints);
    if (trail.pos.x != 0 || trail.pos.z != 0) {
        for (int i = 0; i < f.length; ++i) {
            f.xs[size_t(i)] = int16_t(f.xs[size_t(i)] + trail.pos.x.whole);
            f.zs[size_t(i)] = int16_t(f.zs[size_t(i)] + trail.pos.z.whole);
        }
        trail.pos.x = 0;
        trail.pos.z = 0;
    }
    const int h = (int(f.head) + 1) % int(kTrailPoints);
    f.head = int16_t(h);
    const Actor& parent = actorLayoutAt(f.parent);
    f.xs[size_t(h)] = parent.pos.x.whole;
    f.ys[size_t(h)] = parent.pos.y.whole;
    f.zs[size_t(h)] = parent.pos.z.whole;
    ribbon(trail, f, h);
}

} // namespace

void missileTrailUpdate(const PlayerEnv&, uint32_t actor) {
    StaticActor& trail = staticActorAt(actor);
    auto&& f = classState<MissileTrailFields>(trail);
    switch (TrailState(trail.state)) {
    case TrailState::Start: start(trail, f); break;
    case TrailState::Follow: follow(trail, f); break;
    default: notPortedAt(kTrailArea, "exe 0x5636b0 state (table 0x8c420c has 2 entries)", trail.state);
    }
    if (actorLayoutAt(f.parent).flags == 0) freeActor(trail); // exe 0x5e1540
}

} // namespace od::port
