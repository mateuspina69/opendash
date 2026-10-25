// The buster cannon (weapon 4, pool 0x10 type 3, exe 0x56cad0): a fast shot from the hand (8 steps a tick) homing on
// its target, its glow and its ring of sparks drawn every tick (draw commands 0xf0), a flash burst (static global type
// 0xb, exe 0x564e90) where it ends -- rings, spikes and flying sparks for 16 ticks (variant 1: a flash and the sparks).
#include "engine/data_source.hpp"
#include "game/patched_data.hpp"
#include "game/shots/buster_cannon_state.hpp"
#include "game/actor_state.hpp"
#include "game/shots/player_shots.hpp"

#include "game/actor_movement.hpp"
#include "game/global_class_types.hpp"
#include "game/player/player_weapons.hpp"

#include "game/actor_collision.hpp"
#include "game/actor_pools.hpp"
#include "game/audio_logic.hpp"
#include "game/draw_commands.hpp"
#include "game/fixed_point.hpp"
#include "game/projection.hpp"
#include "game/stage_state.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/shots/shot_common.hpp"

namespace od::port {

using namespace buster_cannon_state;

namespace {

inline constexpr const char* kCannonArea = "buster cannon";

// [0x8c4964]
enum class CannonState : uint8_t { Spawn = 0, Fly = 1, Free = 2 }; // exe 0x56cb70 / 0x56ce10 / 0x56dea0
// how a tick ended (the jump table 0x56cb60)
enum class CannonEnd { Flying = 0, HitBody = 1, HitWall = 2, OutOfRange = 3, Gone = -1 };

// the wall-test box (exe data the shot resizes by its level: a patched record)
PatchedRecord<BoxExtents> cannonBox{TableId::BusterCannonBox};
constexpr int16_t kCannonSpeed = -0x400;
constexpr int kHandDrop = -0x20; // the shot starts this far below the hand
constexpr uint16_t kCannonSound = 0xf5;
constexpr int kHomingStep = 0x10;
constexpr int kSteps = 8;

// x86 idiv-by-8 idiom (cdq / and 7 / add / sar 3): truncation toward zero
int div8(int v) { return (v + ((v >> 31) & 7)) >> 3; }

CannonEnd spawn(const PlayerEnv& env, ShotActor& shot, CannonFields& fields) { // exe 0x56cb70
    shot.flags &= uint8_t(~kActorDrawn);
    shot.runFlags |= kActorWarpKept;
    aimRelativeToTarget(shot);
    PlayerShotFields(shot).hitBox.result = 0;
    if (player().onPlatform == 0) {
        shot.velocity.x = 0;
        shot.velocity.z = kCannonSpeed;
    } else {
        int x, z;
        platformMove(x, z);
        shot.velocity.x = wrap16(div8(x));
        shot.velocity.z = wrap16(div8(z) + kCannonSpeed);
    }
    shot.velocity.y = 0;
    placeAtBone(shot, kLeftHandBone, kHandDrop);
    aimAlongPlayer(shot);
    moveAlongRotation(shot.prevPos, shot.rotation, 0, 0, -int(shot.velocity.z) * 4);
    fillShotStats(fields.stats);
    paySpecialEnergy(player());
    const uint32_t reach = uint32_t(uint16_t(fields.stats.level)) * 2 + 10;
    BoxExtents& box = *cannonBox;
    box.maxZ = int16_t(reach);
    box.minZ = int16_t(0u - reach);
    box.minY = int16_t(0u - reach);
    box.minX = int16_t(0u - reach);
    box.maxY = int16_t(reach);
    box.maxX = int16_t(reach);
    shot.collisionBox.pointTo(box);
    shot.actorHit = 0;
    soundPlayAtPos(kCannonSound, shot.pos);
    ++shot.state;
    return shotHitWall(env, shot, fields.stats.attack) ? CannonEnd::HitWall : CannonEnd::Flying;
}

CannonEnd fly(const PlayerEnv& env, ShotActor& shot, CannonFields& fields) { // exe 0x56ce10
    auto&& shotFields = PlayerShotFields(shot);
    if (shot.actorHit != 0 || (shotFields.hitBox.result & (kHitAttackSolid | kHitAttackDeflects))) {
        stopHalfway(shot);
        return CannonEnd::HitBody;
    }
    shot.savePreviousPose();
    if (shotFields.target != nullptr) homeOnTarget(shot, slotIdentityWord(shotFields.target), kHomingStep, 0);
    --fields.stats.range;
    if (fields.stats.range == 0) return CannonEnd::OutOfRange;
    for (int k = 0; k < kSteps; ++k) {
        moveShot(shot, shot.velocity.x, 0, shot.velocity.z);
        if (shotHitWall(env, shot, fields.stats.attack)) return CannonEnd::HitWall;
    }
    // the glow: two discs, then a ring of sparks of random widths until 0x40
    const uint32_t xy = drawXY(shot), z = uint16_t(shot.pos.z.whole), level = uint16_t(fields.stats.level);
    dc::emitDisc(shot.at, dc::ringColour(0xffffff), xy, z | dc::ringStep(2), (level + 4) << 0x13);
    dc::emitDisc(shot.at, dc::ringColour(0x7f7f00), xy, z | dc::ringStep(2), (level + 4) << 0x14);
    uint32_t b = 0;
    for (;;) {
        const uint32_t r = (rngStep() & 7) * 2 + 2;
        const uint32_t c = level * 8 + 0x30 - r;
        const uint32_t e = r + b;
        dc::Ring ring;
        ring.arc = dc::ringArc(int(b), int(e));
        ring.xy = xy;
        ring.zStep = z | dc::ringStep(1);
        ring.inner = dc::ringColour(0xff4000);
        ring.outer = dc::ringRim(0xff0000);
        ring.radii = dc::ringRadii(0, ((c * 2) & ~1u) >> 1);
        dc::emit(ring, shot.at);
        ring.inner = dc::ringColour(0xff0000);
        ring.outer = dc::kRingFadedRim;
        ring.radii = dc::ringRadii(uint32_t(int32_t(c * 2) >> 1), c * 2);
        dc::emit(ring, shot.at);
        b = e - 2;
        if (int32_t(b) >= 0x40) break;
    }
    markFlying(shot);
    return CannonEnd::Flying;
}

// ---- the flash burst (static global type 0xb) -----------------------------------------------------------------------
// [0x8c43b0]
enum class FlashBurstState : uint8_t {
    Start = 0, // exe 0x564eb0
    Burst = 1, // exe 0x565010: the rings, the spikes and the sparks (variant 0)
    Flash = 2, // exe 0x565060: the flash and the sparks (variant 1)
};
constexpr uint8_t kBurstWithRings = 0; // the variant drawn with rings and spikes
constexpr int16_t kBurstTicks = 0x10;
constexpr uint16_t kBurstSound = 0xf6;
constexpr int16_t kSparksWidth = 0x100, kFlashWidth = 0xc0; // the projected widths the sparks / the flash scale by
constexpr uint32_t kGreyScale = 0x10101;                    // a grey level as an RGB colour

// a phase's colour level: (e * scale) >> 8, at most 0xff
int phaseLevel(int e, int scale) {
    int c = (e * scale) >> 8;
    if (c > 0xff) c = 0xff;
    return c;
}
// exe 0x565090(actor, b, k): at the burst (<< the stage shift; in depth, in front, on the screen): ring records -- a blue
// ring k..3k with its disc (ticks > 6), a cyan disc 3 (k / 2) (ticks > 6), a smaller one (ticks > 5) -- and (ticks > 6)
// 8 spikes (additive POLY_G4 in the camera plane: a black base -+ 0x10 at k / 2, the tip at its angle and length
// (length + k) * 4 + 0x20, the centre grey) and the other 8 spikes as ring arcs
void burstRings(StaticActor& actor, const FlashBurstFields& f, int k) {
    const int sh = stage().coordinateShift;
    const int16_t P[3] = {wrap16(int(actor.pos.x.whole) << (sh & 0x1f)), wrap16(int(actor.pos.y.whole) << (sh & 0x1f)),
                          wrap16(int(actor.pos.z.whole) << (sh & 0x1f))};
    const CameraPoint c = cameraPoint(P);
    if (!effectDepthVisible(c)) return;
    if (c.clipped) return; // exe 0x40cae0 < 0
    if (!effectOnScreen(c)) return;
    const uint32_t xy = dc::packXY(actor.pos.x.whole, actor.pos.y.whole), z = dc::zWord(actor.pos.z.whole);
    const int t = f.ticks;
    int e = (t - 6) << 5;
    if (e > 0) {
        const int cc = phaseLevel(e, 0xff);
        dc::Ring r;
        r.inner = dc::ringColour(uint32_t(cc) << 16);
        r.xy = xy;
        r.zStep = z | dc::ringStep(2);
        r.radii = dc::ringRadii(uint32_t(k), uint32_t(k * 3));
        dc::emit(r, actor.at);
        r.outer = dc::ringRim(uint32_t(cc) << 16);
        r.radii = dc::ringRadii(0, uint32_t(k));
        dc::emit(r, actor.at);
    }
    e = (t - 6) << 6;
    if (e > 0) {
        const int g = phaseLevel(e, 0x7f), rd = phaseLevel(e, 0xff);
        dc::Ring r;
        r.inner = dc::ringColour(uint32_t(rd) << 16 | uint32_t(rd) << 8 | uint32_t(g));
        r.xy = xy;
        r.zStep = z | dc::ringStep(2);
        r.radii = dc::ringRadii(0, uint32_t((k >> 1) * 3));
        dc::emit(r, actor.at);
    }
    e = (t - 5) << 6;
    if (e > 0) {
        const int g = phaseLevel(e, 0x7f), rd = phaseLevel(e, 0xff);
        dc::Ring r;
        r.inner = dc::ringColour(uint32_t(rd) << 16 | uint32_t(rd) << 8 | uint32_t(g));
        r.xy = xy;
        r.zStep = z | dc::ringStep(2);
        r.radii = dc::ringRadii(0, uint32_t(k >> 1) << 1);
        dc::emit(r, actor.at);
    }
    e = (t - 6) << 5;
    if (e <= 0) return;
    const int cc = phaseLevel(e, 0x7f);
    const uint32_t grey = uint32_t(cc) * kGreyScale;
    const int s = (0xc - sh) & 0x1f, kh = k >> 1;
    const int16_t vx = wrap16(c.view[0]), vy = wrap16(c.view[1]);
    for (uint32_t i = 0; i < 8; ++i) {
        const int ang = f.spikeAngles[i];
        const uint32_t hi = uint32_t(ang + 0x10) & 0x3f, lo = uint32_t(ang - 0x10) & 0x3f;
        const int len = (f.spikeLengths[i] + k) * 4 + 0x20;
        float off[4][2];
        auto pt = [&](uint32_t an, int r, float o[2]) {
            o[0] = float(wrap16(wrap16((fixedCos64(an) * r >> s) + vx) - vx));
            o[1] = float(wrap16(wrap16((fixedSin64(an) * r >> s) + vy) - vy));
        };
        pt(lo, kh, off[0]);
        pt(uint32_t(ang), len, off[1]);
        off[2][0] = off[2][1] = 0.0f;
        pt(hi, kh, off[3]);
        const uint32_t col[4] = {dc::packetColour(dc::kPolyG4Semi, 0), 0, grey, 0};
        const int16_t v[4][3] = {{P[0], P[1], P[2]}, {P[0], P[1], P[2]}, {P[0], P[1], P[2]}, {P[0], P[1], P[2]}};
        dc::otQuadG4(actor.at, col, dc::kAdditiveBlend, v, off); // DR_MODE 0xe1000220 (abr 1)
    }
    const int hc = cc >> 1;
    const uint32_t c1 = dc::ringColour(uint32_t(hc) << 16 | uint32_t(hc) << 8), c2 = dc::ringColour(uint32_t(hc) << 16);
    const int base = k * 2 + 0x18;
    for (uint32_t j = 8; j < kBurstSpikes; ++j) {
        const int r2 = (f.spikeLengths[j] + base) * 2;
        const int ang = f.spikeAngles[j];
        dc::Ring arc;
        arc.arc = dc::ringArc(ang + 1, ang + 2);
        arc.inner = c1;
        arc.xy = xy;
        arc.zStep = z;
        arc.radii = dc::ringRadii(0, uint32_t(r2));
        dc::emit(arc, actor.at);
        arc.arc = dc::ringArc(ang, ang + 3);
        arc.inner = c2;
        dc::emit(arc, actor.at);
    }
}
// the 32 sparks flying out: screen ring records at the projected point (depth - d / 4, size (the angle byte >> 6) +
// w >> widthShift, the grey halved when it is the smallest), d = ((0x10 - ticks) * speed * w) >> spread
void burstSparkDiscs(StaticActor& actor, const FlashBurstFields& f, const int16_t sxy[2], int w, int16_t depth,
                     uint32_t grey, int widthShift, int spread, uint32_t step) {
    const int t = f.ticks;
    const int wq = w >> widthShift;
    for (uint32_t i = 0; i < kBurstSparks; ++i) {
        const int bt = f.sparkAngles[i];
        int size = (bt >> 6) + wq;
        uint32_t cc = grey;
        if (size < 1) {
            size = 1;
            cc >>= 1;
        }
        const int d = ((0x10 - t) * f.sparkSpeeds[i] * w) >> spread;
        int16_t zz = wrap16(depth - (d >> 2));
        if (zz < 1) zz = 1;
        const uint32_t ang = uint32_t(bt) & 0x3f;
        const int16_t X = wrap16((fixedCos64(ang) * d >> 12) + sxy[0]), Y = wrap16((fixedSin64(ang) * d >> 12) + sxy[1]);
        dc::Ring r;
        r.inner = dc::screenRingColour(cc * kGreyScale);
        r.xy = dc::packXY(X, Y);
        r.zStep = dc::zWord(zz) | dc::ringStep(step);
        r.radii = dc::ringRadii(0, uint32_t(size));
        dc::emit(r, actor.at);
    }
}
// exe 0x565700(actor, b): the 32 sparks at the projected width of kSparksWidth
void burstSparks(StaticActor& actor, const FlashBurstFields& f) {
    const int16_t P[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    int16_t sxy[2];
    if (projectOnScreen(P, sxy) < 0) return; // exe 0x5f2dd0
    int16_t depth = 0;
    const int w = projectedWidth(P, kSparksWidth, &depth); // exe 0x5f2f20
    if (w == 0) return;
    uint32_t c = uint32_t((int(f.ticks) * 0x7f8) >> 8);
    if (c > 0xff) c = 0xff;
    burstSparkDiscs(actor, f, sxy, w, depth, c, 5, 8, 3);
}
// exe 0x5658a0(actor, b): the flash variant -- two screen discs of the projected width of kFlashWidth * (0x18 - ticks)
// / 16 and the 32 sparks (half their speed)
void burstFlash(StaticActor& actor, const FlashBurstFields& f) {
    const int16_t P[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    int16_t sxy[2];
    if (projectOnScreen(P, sxy) < 0) return;
    int16_t depth = 0;
    const int w = projectedWidth(P, kFlashWidth, &depth);
    if (w == 0) return;
    const int t = f.ticks;
    uint32_t c = uint32_t((t * 0x7f8) >> 8);
    if (c > 0xff) c = 0xff;
    const int bb = ((0x18 - t) * w) >> 4;
    dc::Ring r;
    r.inner = dc::screenRingColour(c << 16);
    r.xy = dc::packXY(sxy[0], sxy[1]);
    r.zStep = dc::zWord(depth) | dc::ringStep(2);
    r.radii = dc::ringRadii(0, uint32_t((bb * 3) >> 1));
    dc::emit(r, actor.at);
    r.inner = dc::screenRingColour(c * kGreyScale);
    r.radii = dc::ringRadii(0, uint32_t(bb));
    dc::emit(r, actor.at);
    uint32_t c2 = uint32_t((t * 0x3fc) >> 8);
    if (c2 > 0xff) c2 = 0xff;
    burstSparkDiscs(actor, f, sxy, bb, depth, c2, 5, 9, 4);
}
// exe 0x564eb0: kept through the warps, the sound, the random spikes / sparks, kBurstTicks; variant kBurstWithRings ->
// Burst (with the first rings), else Flash
void flashBurstStart(StaticActor& actor, FlashBurstFields& f) {
    actor.runFlags |= kActorWarpKept;
    soundPlayAtPos(kBurstSound, actor.pos);
    for (uint32_t i = 0; i < kBurstSpikes; ++i) {
        f.spikeAngles[i] = int8_t(((rngStep() & 7) + (i << 3) - 4) & 0x3f);
        f.spikeLengths[i] = int8_t(rngStep() & 0x3f);
    }
    for (uint32_t i = 0; i < kBurstSparks; ++i) {
        rngStep();
        const uint32_t r = rngStep();
        const uint32_t lo = ((r & 7) + ((i << 3) & 0xff) - 4) & 0xff, cl = r & 0xff;
        f.sparkAngles[i] = int8_t(((lo ^ cl) & 0x3f) ^ cl);
        f.sparkSpeeds[i] = int8_t((rngStep() & 0x3f) + 8);
    }
    f.ticks = kBurstTicks;
    if (actor.variant == kBurstWithRings) {
        burstRings(actor, f, int(uint32_t(f.size) * 2) >> 4);
        actor.state += 1u;
        return;
    }
    actor.state += 2u;
}
// exe 0x565010: a tick less (none left: freed); the rings growing with the time, the sparks
void flashBurstTick(StaticActor& actor, FlashBurstFields& f) {
    if (f.ticks == 0) {
        freeActor(actor);
        return;
    }
    const int t = wrap16(f.ticks - 1);
    f.ticks = int16_t(t);
    burstRings(actor, f, ((0x12 - t) * int(f.size)) >> 4);
    burstSparks(actor, f);
}
// exe 0x565060: a tick less (none left: freed); the flash
void flashBurstFlashTick(StaticActor& actor, FlashBurstFields& f) {
    if (f.ticks == 0) {
        freeActor(actor);
        return;
    }
    --f.ticks;
    burstFlash(actor, f);
}

// ---- the cannon ------------------------------------------------------------------------------------------------------
// exe 0x56d150(shot, stats, variant): a flash burst (static global type 0xb) at the shot, its size by the level
void spawnFlash(const ShotActor& shot, const ShotStats& stats, uint8_t variant) {
    const uint32_t address = allocActor60(0);
    if (address == 0) return;
    StaticActor& s = staticActorAt(address);
    s.variant = variant;
    s.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    s.flags = kActorLive;
    s.type = typeByte(GlobalStaticType::FlashBurst);
    s.pos = shot.pos;
    classState<FlashBurstFields>(s).size = uint16_t((uint32_t(uint16_t(stats.level)) + 8) << 4);
}

// exe 0x56d0a0: the capsule from the shot's previous position (radius (level * 8 + 0x28) / 4)
void submitCannonHit(ShotActor& shot, const ShotStats& stats) {
    submitShotCapsule(shot, uint16_t(int32_t(uint32_t(uint16_t(stats.level)) * 8 + 0x28) >> 2),
                      hitGroup(kHitGroupPlayerAttacks) | kHitContactMotion | hitCapsule(2),
                      stats.attack | kHitAttackByPlayer | kHitAttackStrongPush);
}

} // namespace

void flashBurstUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    auto&& f = classState<FlashBurstFields>(actor);
    switch (FlashBurstState(actor.state)) {
    case FlashBurstState::Start: flashBurstStart(actor, f); return;
    case FlashBurstState::Burst: flashBurstTick(actor, f); return;
    case FlashBurstState::Flash: flashBurstFlashTick(actor, f); return;
    default: notPortedAt(kCannonArea, "0x564e90 state", actor.state);
    }
}

void busterCannonShotUpdate(const PlayerEnv& env, uint32_t address) {
    ShotActor& shot = shotActorAt(address);
    auto&& fields = classState<CannonFields>(shot);
    dropDeadTarget(shot);
    CannonEnd end;
    switch (CannonState(shot.state)) {
    case CannonState::Spawn: end = spawn(env, shot, fields); break;
    case CannonState::Fly: end = fly(env, shot, fields); break;
    case CannonState::Free: freeActor(shot); return;
    default: notPortedAt(kCannonArea, "0x56cad0 state", shot.state);
    }
    switch (end) {
    case CannonEnd::Flying:
        actorCollisionSubmitMover(shot);
        submitCannonHit(shot, fields.stats);
        return;
    case CannonEnd::HitBody: spawnFlash(shot, fields.stats, 0); break;
    case CannonEnd::HitWall:
        spawnFlash(shot, fields.stats, 0);
        submitCannonHit(shot, fields.stats);
        break;
    case CannonEnd::OutOfRange: spawnFlash(shot, fields.stats, 1); break;
    default: return;
    }
    ++shot.state;
}

} // namespace od::port
