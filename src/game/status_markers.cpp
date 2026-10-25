// The player's status markers (see status_markers.hpp). The drain / burn marker's sprites and particles are draw
// commands; the slow marker's lightning and sparks are ordering-table polygons around the projected bone points
// (game/effect_polygons). Their RNG steps only happen on screen, so the camera is read (read-only, as the exe's
// geometry-unit sequence 0x40c3c0 / 0x40c3e0 / 0x40c6a0 / 0x40c800 / 0x40c860).
#include "game/actor_state.hpp"
#include "game/status_markers_state.hpp"
#include "game/status_markers.hpp"

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/draw_commands.hpp"
#include "game/effect_polygons.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"

namespace od::port {

using namespace status_markers_state;

namespace {

inline constexpr const char* kMarkersArea = "status markers";

// ---- what both markers share ----------------------------------------------------------------------------------------
// the markers' states ([0x8c4844] / [0x8c4868])
enum class MarkerState : uint8_t { Start = 0, Hold = 1, Fade = 2 };
constexpr uint8_t kMarkerFace = 4; // the player's idle faces while a marker is drawn (0 when it ends)
constexpr uint32_t kNeutralAmbient = 0x808080; // the player model's light colour with no status tint

// the player is drawn (the markers draw only then)
bool playerDrawn() { return (player().flags & kActorDrawn) != 0; }
// the player shows the markers' face
void markerFaces(uint8_t face) {
    player().idleFaces[0] = int8_t(face);
    player().idleFaces[1] = int8_t(face);
}
// the player can lose the status: alive and not knocked back / down
bool playerSettled() {
    const Player& p = player();
    return p.hitPoints >= 0 && p.sub() != PlayerSub::KnockBack && p.sub() != PlayerSub::KnockDown;
}
// the marker ended: its marker bit and the status's top nibble cleared (the word store), the faces back, freed
void endMarker(StaticActor& actor, uint16_t statusEffect) {
    player().status &= uint16_t(kStatusTickMask & ~uint16_t(statusEffect << kStatusMarkerShift));
    markerFaces(0);
    freeActor(actor);
}

// ---- the drain / burn marker (static type 0x12) ---------------------------------------------------------------------
constexpr int16_t kParticleLife = 0xb;
constexpr int16_t kMarkerSoundTicks = 0x18;
constexpr uint16_t kDrainBurnSound = 0x144;
// the bones the sprites sit on by level >> 1 (exe data 0x8c4744: 16 s8 bone indices a row, a negative one ends it)
constexpr size_t kBoneRowSize = 0x10;
const DataTable<ScalarTable<int8_t, 0x10 * kBoneRowSize>> drainBoneRows{TableId::StatusDrainBoneRows};
constexpr uint32_t kSpritesPerRow = 0xf;
// the leg bones dropped when the player's box is low (crouching: the box's bottom above these heights)
constexpr int16_t kShinsHiddenBelow = -0x30, kFeetHiddenBelow = -0x18;
// the sprites (draw command billboards): a 32-texel cell of the animated strip of tpage 0x2f, by the frame
constexpr uint16_t kMarkerTpage = 0x2f;
constexpr uint16_t kDrainClut = 0x7dd1, kBurnClut = 0x7cd2, kDrainParticleClut = 0x7c11;
constexpr uint32_t kDrainSpriteRgb = 0x282828, kBurnSpriteRgb = 0x383838;
constexpr uint32_t kParticleStep = 0x30303;       // a particle sprite's grey by its life
constexpr uint32_t kParticleRingStep = 0x1010;    // a burn particle's disc: inner colour by its life ...
constexpr uint32_t kParticleRimStep = 0x40c, kParticleRimBase = 0x2060; // ... and its rim's
constexpr uint32_t kMarkerSpriteSize = 0x30;
constexpr uint32_t kCellTop = 0x40, kCellBottom = 0x5f, kCellWidth = 0x1f;
// the player tint by the level: the drain's darkens toward kDrainTintBase, the burn's reddens from kBurnTintBase
constexpr uint32_t kDrainTintBase = 0x837e7e, kDrainTintStep = 0x3fdfe;
constexpr uint32_t kBurnTintBase = 0x787887, kBurnTintStep = 0x807f8;

// the status bits the marker shows: the drain and its floor form, or the burn's
uint16_t markedStatus(bool drain) {
    return drain ? uint16_t(kStatusDrain | kStatusDrainFloor) : uint16_t(kStatusBurn | kStatusBurnFloor);
}
// the 32-texel cell of the strip by the frame counter (+ i)
uint32_t stripCell(uint32_t i) { return ((uint32_t(gameState().frame) + i) << 5) & 0xe0; }
// a particle reborn around the player with its full life
void rebornAroundPlayer(DrainParticle& p) {
    const Player& pl = player();
    p.point.x = int16_t(uint16_t((rngStep() & 0x3f) + uint32_t(uint16_t(pl.pos.x.whole)) - 0x20u));
    p.point.y = int16_t(uint16_t(uint32_t(uint16_t(pl.pos.y.whole)) - (rngStep() & 0x7f) - 0x20u));
    p.point.z = int16_t(uint16_t((rngStep() & 0x3f) + uint32_t(uint16_t(pl.pos.z.whole)) - 0x20u));
}

// the player tint by the level n (exe 0x5684c0 / 0x568630)
void drainBurnTint(bool drain, int n) {
    if (drain) player().ambientColour = uint32_t(n) * kDrainTintStep + kDrainTintBase;
    else player().ambientColour = kBurnTintBase - uint32_t(n) * kBurnTintStep;
}
// exe 0x5687f0(actor, b, drain): (while the player is drawn) the faces; one sprite on each bone of the level's row
// (stopping at a negative entry, or at the shins / the feet when the player's box is low); then the particles rise and
// are reborn around the player with their full life, each a sprite (the drain) or a yellow disc (the burn) coloured by
// its life before the step; every kMarkerSoundTicks calls the marker's sound at the player
void drainBurnDraw(StaticActor& actor, DrainBurnMarkerFields& m, bool drain) {
    if (!playerDrawn()) return;
    markerFaces(kMarkerFace);
    const Player& pl = player();
    const uint32_t rgb = drain ? kDrainSpriteRgb : kBurnSpriteRgb;
    const uint16_t clut = drain ? kDrainClut : kBurnClut;
    const size_t row = size_t(m.level >> 1) * kBoneRowSize;
    for (uint32_t i = 0; i < kSpritesPerRow; ++i) {
        const int bone = drainBoneRows[row + i];
        if (bone < 0) break;
        const int16_t boxBottom = pl.collisionBox->maxY;
        if (boxBottom < kShinsHiddenBelow && (bone == int(kLeftShinBone) || bone == int(kRightShinBone))) break;
        if (boxBottom < kFeetHiddenBelow && (bone == int(kLeftFootBone) || bone == int(kRightFootBone))) break;
        const uint32_t u = stripCell(i);
        const ShortVec3& point = pl.bonePoints[size_t(bone)];
        dc::Billboard b;
        b.colour = dc::billboardColour(rgb);
        b.xy = dc::packXY(point.x, point.y);
        b.z = dc::zWord(point.z);
        b.clut = clut;
        b.tpage = kMarkerTpage;
        b.uv[0] = dc::texel(u, kCellTop);
        b.uv[1] = dc::texel(u + kCellWidth, kCellTop);
        b.uv[2] = dc::texel(u, kCellBottom);
        b.uv[3] = dc::texel(u + kCellWidth, kCellBottom);
        b.sizeRotation = kMarkerSpriteSize;
        dc::emit(b, actor.at);
    }
    for (int k = 0; k < m.particlesAlive; ++k) {
        auto&& p = m.particles[size_t(k)];
        const int life = p.life;
        if (life != 0) {
            p.point.x += uint32_t(int(rngStep() & 0xf) - 8);
            p.point.y += uint32_t(-8 - int(rngStep() & 7));
            p.point.z += uint32_t(int(rngStep() & 0xf) - 8);
            --p.life;
        } else {
            rebornAroundPlayer(p);
            p.life = kParticleLife;
        }
        const uint32_t t = uint32_t(life);
        if (drain) {
            const uint32_t u = stripCell(uint32_t(k));
            dc::Billboard b;
            b.colour = dc::billboardColour(t * kParticleStep);
            b.xy = dc::packXY(p.point.x, p.point.y);
            b.z = dc::zWord(p.point.z);
            b.clut = kDrainParticleClut;
            b.tpage = kMarkerTpage;
            b.uv[0] = dc::texel(u, kCellTop);
            b.uv[1] = dc::texel(u + kCellWidth, kCellTop);
            b.uv[2] = dc::texel(u, kCellBottom);
            b.uv[3] = dc::texel(u + kCellWidth, kCellBottom);
            b.sizeRotation = kMarkerSpriteSize;
            dc::emit(b, actor.at);
        } else {
            dc::Ring r;
            r.arc = dc::kRingFullArc | dc::ringAngle((t & 0xf) << 2);
            r.inner = dc::ringColour(t * kParticleRingStep);
            r.xy = dc::packXY(p.point.x, p.point.y);
            r.outer = dc::ringRim(t * kParticleRimStep | kParticleRimBase);
            r.zStep = dc::zWord(p.point.z) | dc::ringStep(4);
            r.radii = dc::ringRadii(0, 1);
            dc::emit(r, actor.at);
        }
    }
    --m.soundTicks;
    if (m.soundTicks == 0) {
        m.soundTicks = kMarkerSoundTicks;
        soundPlayAtPlayer(kDrainBurnSound); // exe 0x5d53e0
    }
}
// exe 0x5684c0: the particles around the player (lives 0..0xb), the level full (variant kMarkerStartsFull) or 0, the
// sound's ticks; the status already gone -> fading at once (state Fade, level kFadeStart - level)
void drainBurnStart(StaticActor& actor, DrainBurnMarkerFields& m) {
    for (uint32_t k = 0; k < kDrainParticles; ++k) {
        auto&& p = m.particles[k];
        rebornAroundPlayer(p);
        p.life = int16_t(k);
    }
    if (actor.variant & kMarkerStartsFull) {
        m.level = kFullLevel;
        m.particlesAlive = int16_t(kDrainParticles);
    } else {
        m.level = 0;
        m.particlesAlive = 1;
    }
    m.soundTicks = kMarkerSoundTicks;
    const bool drain = (actor.variant & kMarkerOfDrain) != 0;
    drainBurnTint(drain, m.level);
    drainBurnDraw(actor, m, drain);
    if (!(player().status & markedStatus(drain))) {
        m.level = int16_t(kFadeStart - m.level);
        actor.state += 2u;
        return;
    }
    actor.state += 1u;
}
// exe 0x568630: the level and the particles grow to full; the status gone (the player settled) -> fading
void drainBurnHold(StaticActor& actor, DrainBurnMarkerFields& m) {
    ++m.level;
    if (m.level > kFullLevel) m.level = kFullLevel;
    ++m.particlesAlive;
    if (m.particlesAlive > int16_t(kDrainParticles)) m.particlesAlive = int16_t(kDrainParticles);
    const bool drain = (actor.variant & kMarkerOfDrain) != 0;
    drainBurnTint(drain, m.level);
    drainBurnDraw(actor, m, drain);
    if (!(player().status & markedStatus(drain)) && playerSettled()) {
        m.level = int16_t(kFadeStart - m.level);
        actor.state += 1u;
    }
}
// exe 0x568700: the level fades by kFadeStep up to kFadeEnd (the tint back to neutral), then (or when the status came
// back) the marker ends
void drainBurnFade(StaticActor& actor, DrainBurnMarkerFields& m) {
    const bool drain = (actor.variant & kMarkerOfDrain) != 0;
    m.level += uint32_t(kFadeStep);
    const int n = m.level;
    if (n <= kFadeEnd && !(player().status & markedStatus(drain))) {
        const int r = kFadeEnd - n;
        if (drain) player().ambientColour = uint32_t(r) * kDrainTintStep + kNeutralAmbient;
        else player().ambientColour = kNeutralAmbient - uint32_t(r) * kBurnTintStep;
        drainBurnDraw(actor, m, drain);
        return;
    }
    endMarker(actor, drain ? kStatusDrain : kStatusBurn);
}

// ---- the slow marker (static type 0x13) -----------------------------------------------------------------------------
// the bones a bolt joins (exe data 0x8c4850: two bone indices a bolt) and the bones a spark may sit on (0x8c4860)
const DataTable<ScalarTable<uint8_t, kBoltEnds>> boltBones{TableId::StatusMarkersBoltBones};
const DataTable<ScalarTable<uint8_t, 8>> sparkBones{TableId::StatusMarkersSparkBones};
// the other statuses' bits (the drain, the burn, the orbit and their floor forms): while any is up, the slow marker
// leaves the player's tint to its marker
constexpr uint16_t kOtherStatuses = kStatusDrain | kStatusBurn | kStatusOrbit | kStatusDrainFloor | kStatusBurnFloor |
                                    kStatusOrbitFloor;
constexpr uint16_t kSlowStatus = kStatusSlowed | kStatusSlowedFloor;
// the bolts' polygons: tpage 0x2f, CLUT 0x7d90, the lightning strip u 0xa8..0xb8 x v 0xa0..0xbf
constexpr uint16_t kBoltClut = 0x7d90;
constexpr uint32_t kBoltU0 = 0xa8, kBoltU1 = 0xb8, kBoltV0 = 0xa0, kBoltV1 = 0xbf;
constexpr uint32_t kBoltRgb = 0xe0e0e0;
// the spark's polygons: a gouraud star from white at its centre (draw mode abr 1)
constexpr uint32_t kSparkRayRgb = 0x2040, kSparkCentreRgb = 0xffffff;
constexpr uint16_t kSlowSound = 0x143;
// the tints by the bolts drawn (shockBoltsDrawn)
constexpr uint32_t kSlowTintSome = 0xa0c0, kSlowTintMany = 0xc0ff;
enum class BoltsDrawn { Few = 0, Some = 1, Many = 2 };

// the player's bone point k (camera-input units)
void bonePoint(uint32_t k, int16_t out[3]) {
    const ShortVec3& p = player().bonePoints[k];
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
}

// one bolt quad: its corners = the bone points (camera-input) plus the screen corners' offsets at their depth
struct BoltCorner {
    const int16_t* pt;    // the bone point
    const CameraPoint* c; // its projection
    int16_t sx, sy;       // the corner on the screen
};
void boltQuad(StaticActor& actor, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3, const BoltCorner (&k)[4]) {
    dc::GouraudTexturedQuad q;
    q.colour[0] = c0;
    q.colour[1] = c1;
    q.colour[2] = c2;
    q.colour[3] = c3;
    q.clut = kBoltClut;
    q.tpage = kMarkerTpage;
    q.uv[0] = dc::texel(kBoltU0, kBoltV1);
    q.uv[1] = dc::texel(kBoltU0, kBoltV0);
    q.uv[2] = dc::texel(kBoltU1, kBoltV1);
    q.uv[3] = dc::texel(kBoltU1, kBoltV0);
    int16_t v[4][3];
    float off[4][2];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 3; ++j) v[i][j] = k[i].pt[j];
        const float s = pixelUnitsAt(*k[i].c);
        off[i][0] = float(wrap16(k[i].sx - k[i].c->sx)) * s;
        off[i][1] = float(wrap16(k[i].sy - k[i].c->sy)) * s;
    }
    dc::otQuadGT4(actor.at, q, v, off);
}
// exe 0x568d00(actor, b): (player drawn) the faces; with the body's bone point in depth and on the screen, each bolt i
// drawn one time in 4 (RNG & 3 == 0): its end angles drift by (RNG & 0xf) - 8, and between the projected bone points
// SA / SB (h = |SB - SA| / 2) four quads: a GT4 fading in from black (SA + r3 / 32 at angle a1 -+ 8) to Q0 / Q1
// (SA + r1 at a1 -+ 2), FT4 Q0 Q1 -> C0 C1 (the jittered midpoint M of Q0..Q2 + h / 4 at a1 -+ 0x10), FT4 C0 C1 ->
// Q2 Q3 (SB + r2 at a2 -+ 2), a GT4 fading out to SB + r3 / 32 at a2 -+ 8 (r = ((RNG & 0xf) + 4) * h / 16); linked at
// OT[(the body's depth >> 2) - 0xc]. Returns how many bolts were drawn (Many: more than 3, Some: 3)
BoltsDrawn slowBolts(StaticActor& actor, SlowMarkerFields& m) {
    if (!playerDrawn()) return BoltsDrawn::Few;
    markerFaces(kMarkerFace);
    int16_t p0[3];
    bonePoint(kBodyBone, p0);
    const CameraPoint c0 = cameraPoint(p0);
    if (!effectDepthVisible(c0) || !effectOnScreen(c0)) return BoltsDrawn::Few;
    if (m.bolts <= 0) return BoltsDrawn::Few;
    int count = 0;
    for (int i = 0; i < m.bolts; ++i) {
        if (rngStep() & 3) continue;
        ++count;
        int16_t pa[3], pb[3];
        bonePoint(boltBones[size_t(i) * 2], pa);
        const CameraPoint ca = cameraPoint(pa);
        auto&& endA = m.boltEnds[size_t(i) * 2];
        auto&& endB = m.boltEnds[size_t(i) * 2 + 1];
        endA += uint32_t(int(rngStep() & 0xf) - 8);
        const uint32_t a1 = endA;
        endB += uint32_t(int(rngStep() & 0xf) - 8);
        const uint32_t a2 = endB;
        bonePoint(boltBones[size_t(i) * 2 + 1], pb);
        const CameraPoint cb = cameraPoint(pb);
        const int dx = wrap16(cb.sx - ca.sx), dy = wrap16(cb.sy - ca.sy);
        const int h = isqrt(int32_t(dx * dx + dy * dy)) >> 1; // exe 0x5e3b80 (inlined)
        auto at = [](int16_t base, int v) { return wrap16(base + v); };
        const int r1 = int(uint32_t(int((rngStep() & 0xf) + 4) * h) >> 4);
        const int16_t q0x = at(ca.sx, fixedCos64(a1 - 2) * r1 >> 12), q0y = at(ca.sy, fixedSin64(a1 - 2) * r1 >> 12);
        const int16_t q1x = at(ca.sx, fixedCos64(a1 + 2) * r1 >> 12), q1y = at(ca.sy, fixedSin64(a1 + 2) * r1 >> 12);
        const int r2 = int(uint32_t(int((rngStep() & 0xf) + 4) * h) >> 4);
        const int16_t q2x = at(cb.sx, fixedCos64(a2 - 2) * r2 >> 12), q2y = at(cb.sy, fixedSin64(a2 - 2) * r2 >> 12);
        const int16_t q3x = at(cb.sx, fixedCos64(a2 + 2) * r2 >> 12), q3y = at(cb.sy, fixedSin64(a2 + 2) * r2 >> 12);
        const int r3 = int(uint32_t(int((rngStep() & 0xf) + 4) * h) >> 4);
        const int16_t s0x = at(ca.sx, fixedCos64(a1 - 8) * r3 >> 0x11),
                      s0y = at(ca.sy, fixedSin64(a1 - 8) * r3 >> 0x11);
        const int16_t s1x = at(ca.sx, fixedCos64(a1 + 8) * r3 >> 0x11),
                      s1y = at(ca.sy, fixedSin64(a1 + 8) * r3 >> 0x11);
        const int fx = int(rngStep() & 3) + 2;
        const int mx = q0x + ((q2x - q0x) * fx >> 3);
        const int fy = int(rngStep() & 3) + 2;
        const int my = q0y + ((q2y - q0y) * fy >> 3);
        const int16_t m0x = wrap16(mx + (fixedCos64(a1 - 0x10) * h >> 0xe)),
                      m0y = wrap16(my + (fixedSin64(a1 - 0x10) * h >> 0xe));
        const int16_t m1x = wrap16(mx + (fixedCos64(a1 + 0x10) * h >> 0xe)),
                      m1y = wrap16(my + (fixedSin64(a1 + 0x10) * h >> 0xe));
        const int16_t e0x = at(cb.sx, fixedCos64(a2 - 8) * r3 >> 0x11),
                      e0y = at(cb.sy, fixedSin64(a2 - 8) * r3 >> 0x11);
        const int16_t e1x = at(cb.sx, fixedCos64(a2 + 8) * r3 >> 0x11),
                      e1y = at(cb.sy, fixedSin64(a2 + 8) * r3 >> 0x11);
        const BoltCorner S0{pa, &ca, s0x, s0y}, S1{pa, &ca, s1x, s1y}, Q0{pa, &ca, q0x, q0y}, Q1{pa, &ca, q1x, q1y};
        const BoltCorner C0{pa, &ca, m0x, m0y}, C1{pa, &ca, m1x, m1y};
        const BoltCorner Q2{pb, &cb, q2x, q2y}, Q3{pb, &cb, q3x, q3y}, E0{pb, &cb, e0x, e0y}, E1{pb, &cb, e1x, e1y};
        const uint32_t fadeIn = dc::packetColour(dc::kPolyGT4Semi, 0);
        const uint32_t flat = dc::packetColour(dc::kPolyGT4Semi, kBoltRgb);
        boltQuad(actor, fadeIn, 0, kBoltRgb, kBoltRgb, {S0, S1, Q0, Q1});
        // the two FT4 of one colour (0x2ee0e0e0): as GT4s with that colour on every corner
        boltQuad(actor, flat, kBoltRgb, kBoltRgb, kBoltRgb, {Q0, Q1, C0, C1});
        boltQuad(actor, flat, kBoltRgb, kBoltRgb, kBoltRgb, {C0, C1, Q2, Q3});
        boltQuad(actor, flat, kBoltRgb, 0, 0, {Q2, Q3, E0, E1});
    }
    if (count > 3) return BoltsDrawn::Many;
    if (count > 2) return BoltsDrawn::Some;
    return BoltsDrawn::Few;
}
// exe 0x5694a0(actor, b): a spark at a random bone of the spark bones, jittered by (RNG & 0x1f) - 0x10 (<< the stage
// shift); in depth, in front and on the screen: size s = (RNG & 0xf) * 4 + 0x20, in the camera plane (the identity
// over the view point V) two POLY_G4 (an additive draw mode before each): (V + s / 32 at angle j * 32 - 0x10: black,
// V + s at j * 32: kSparkRayRgb, V: white, V + s / 32 at j * 32 + 0x10: black) for j = 0 / 1 (64 steps, >> 12 - shift)
void slowSpark(StaticActor& actor) {
    const uint32_t bone = sparkBones[rngStep() & 7];
    const int sh = stage().coordinateShift;
    const ShortVec3& b = player().bonePoints[bone];
    int16_t p[3];
    p[0] = wrap16(int(int16_t(uint16_t(uint16_t(b.x) + (rngStep() & 0x1f) - 0x10))) << (sh & 0x1f));
    p[1] = wrap16(int(int16_t(uint16_t(uint16_t(b.y) + (rngStep() & 0x1f) - 0x10))) << (sh & 0x1f));
    p[2] = wrap16(int(int16_t(uint16_t(uint16_t(b.z) + (rngStep() & 0x1f) - 0x10))) << (sh & 0x1f));
    const CameraPoint c = cameraPoint(p);
    if (!effectDepthVisible(c)) return;
    const uint32_t size = (rngStep() & 0xf) * 4 + 0x20, small = size >> 5;
    if (c.clipped) return; // exe 0x40cae0 < 0
    if (!effectOnScreen(c)) return;
    const int s = (0xc - sh) & 0x1f;
    const int16_t vx = wrap16(c.view[0]), vy = wrap16(c.view[1]);
    for (uint32_t j = 0; j < 2; ++j) {
        const uint32_t ang = j << 5;
        auto pt = [&](uint32_t an, uint32_t r, float o[2]) {
            const int16_t x = wrap16(int(uint32_t(fixedCos64(an) * int(r)) >> s) + vx);
            const int16_t y = wrap16(int(uint32_t(fixedSin64(an) * int(r)) >> s) + vy);
            o[0] = float(wrap16(x - vx));
            o[1] = float(wrap16(y - vy));
        };
        float off[4][2];
        pt((ang - 0x10) & 0x3f, small, off[0]);
        pt(ang, size, off[1]);
        off[2][0] = off[2][1] = 0.0f;
        pt((ang + 0x10) & 0x3f, small, off[3]);
        const uint32_t col[4] = {dc::packetColour(dc::kPolyG4Semi, 0), kSparkRayRgb, kSparkCentreRgb, 0};
        const int16_t v[4][3] = {{p[0], p[1], p[2]}, {p[0], p[1], p[2]}, {p[0], p[1], p[2]}, {p[0], p[1], p[2]}};
        dc::otQuadG4(actor.at, col, dc::kAdditiveBlend, v, off);
    }
}
// the player tint by the bolts drawn (exe 0x568b60 / 0x568c10): neutral, kSlowTintSome, kSlowTintMany + the sound
void slowTint(BoltsDrawn r) {
    if (r == BoltsDrawn::Few) player().ambientColour = kNeutralAmbient;
    else if (r == BoltsDrawn::Some) player().ambientColour = kSlowTintSome;
    else {
        player().ambientColour = kSlowTintMany;
        soundPlayAtPlayer(kSlowSound); // exe 0x5d53e0
    }
}
// another status's marker owns the tint
bool otherStatusUp() { return (uint8_t(player().status) & kOtherStatuses) != 0; }
bool slowStatusUp() { return (uint8_t(player().status) & kSlowStatus) != 0; }
// exe 0x568b60: the end angles at random, kBolts bolts; the tint; the status already gone -> Fade
void slowStart(StaticActor& actor, SlowMarkerFields& m) {
    for (uint32_t k = 0; k < kBoltEnds; ++k) m.boltEnds[k] = uint8_t(rngStep() & 0x3f);
    m.bolts = kBolts;
    const BoltsDrawn r = slowBolts(actor, m);
    if (!otherStatusUp()) slowTint(r);
    actor.state += slowStatusUp() ? 1u : 2u;
}
// exe 0x568c10: the bolts (and the tint), a spark when many; the status gone and the player settled -> Fade (the tint
// neutral)
void slowHold(StaticActor& actor, SlowMarkerFields& m) {
    const BoltsDrawn r = slowBolts(actor, m);
    if (!otherStatusUp()) slowTint(r);
    if (r == BoltsDrawn::Many) slowSpark(actor);
    if (!slowStatusUp() && playerSettled()) {
        if (!otherStatusUp()) player().ambientColour = kNeutralAmbient;
        actor.state += 1u;
    }
}
// exe 0x568cb0: the bolts fade by kBoltFade a tick; none left (or the status back) -> the marker ends
void slowFade(StaticActor& actor, SlowMarkerFields& m) {
    m.bolts -= uint32_t(kBoltFade);
    if (m.bolts >= 0 && !slowStatusUp()) {
        slowBolts(actor, m);
        return;
    }
    endMarker(actor, kStatusSlowed);
}

} // namespace

void drainBurnMarkerUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    DrainBurnMarkerFields& m = classState<DrainBurnMarkerFields>(actor);
    switch (MarkerState(actor.state)) {
    case MarkerState::Start: drainBurnStart(actor, m); return;
    case MarkerState::Hold: drainBurnHold(actor, m); return;
    case MarkerState::Fade: drainBurnFade(actor, m); return;
    default: notPortedAt(kMarkersArea, "0x5684a0 state", actor.state);
    }
}

void slowMarkerUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    SlowMarkerFields& m = classState<SlowMarkerFields>(actor);
    switch (MarkerState(actor.state)) {
    case MarkerState::Start: slowStart(actor, m); return;
    case MarkerState::Hold: slowHold(actor, m); return;
    case MarkerState::Fade: slowFade(actor, m); return;
    default: notPortedAt(kMarkersArea, "0x568b40 state", actor.state);
    }
}

} // namespace od::port
