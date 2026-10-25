// The machine gun (weapon 13, pool 0x10 type 12, exe 0x56f8e0): a fast shot (3 steps a tick) with a muzzle flash
// (effect type 10, exe 0x560df0: a random offset of the shot's capsule, a glow at the hand, then a flash at the shot)
// and a shell casing thrown with the player's own motion (static global type 0x11, exe 0x568260: a spinning sprite that
// bounces off the walls and the floor); an impact flash (effect type 11, exe 0x5616e0: rays and a disc) where it hits a
// wall.
//
// The OT polygons of the flash (exe 0x560f00) and the impact rays (exe 0x5617d0 / 0x561c70) go through the presentation
// side channel (game/draw_commands otQuadG4 / otTriG3 / otQuadGT4) with camera-input corners: the exe projects them
// through a local frame (the camera premultiplied by a rotation) or offsets them in screen pixels from a projected
// centre; the port keeps the corners as camera-input points (the centre + (the local matrix * the point) >> 12) or as
// the centre + a camera-plane offset (the pixels brought back to the centre's depth). The GTE loads, the packet cursor
// and the OT links / depths are presentation only and not written.
#include "engine/data_source.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/shots/machine_gun_state.hpp"
#include "game/shots/player_shots.hpp"

#include "game/actor_movement.hpp"
#include "game/global_class_types.hpp"
#include "game/player/player_weapons.hpp"

#include <algorithm>

#include "game/actor_collision.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/draw_commands.hpp"
#include "game/fixed_point.hpp"
#include "game/hit_list.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/shots/shot_common.hpp"
#include "game/stage_state.hpp"

namespace od::port {

using namespace machine_gun_state;

namespace {

inline constexpr const char* kMachineGunArea = "machine gun";

// [0x8c473c]
enum class CasingState : uint8_t { Start = 0, Fly = 1 }; // exe 0x568280 / 0x568340
const DataTable<CollisionBoxRecord> casingBox{TableId::MachineGunCasingBox};
constexpr int16_t kCasingLife = 0x10;
constexpr int16_t kCasingGravity = 0x40;
constexpr uint16_t kCasingSound = 0x10a;
constexpr int16_t kCasingSoundTicks = 4;
constexpr uint32_t kCasingYawSpread = 0x3ff, kCasingYawBias = 0x200;
// its sprite (a billboard record of tpage 0x2f, CLUT 0x7d90, u 0xc0..0xc8 x v 0xa0..0xa8), size kCasingSize
constexpr uint16_t kCasingTpage = 0x2f, kCasingClut = 0x7d90;
constexpr uint32_t kCasingSize = 6, kCasingRgb = 0x808080;

// exe 0x568420: the sprite, turned by its life and its spin
void casingDraw(StaticActor& actor, const CasingFields& f) {
    dc::Billboard b;
    b.colour = dc::opaqueBillboardColour(kCasingRgb);
    b.xy = dc::packXY(actor.pos.x.whole, actor.pos.y.whole);
    b.z = dc::zWord(actor.pos.z.whole);
    b.clut = kCasingClut;
    b.tpage = kCasingTpage;
    b.uv[0] = dc::texel(0xc0, 0xa0);
    b.uv[1] = dc::texel(0xc8, 0xa0);
    b.uv[2] = dc::texel(0xc0, 0xa8);
    b.uv[3] = dc::texel(0xc8, 0xa8);
    b.sizeRotation = dc::sizeRotation(kCasingSize, uint32_t((f.life * 3 << 9) + f.spin));
    dc::emit(b, actor.at);
}
// exe 0x568280: kept through the warps, at the player's left hand (the special weapon's), its yaw the player's with a
// random spread; its life, a random spin
void casingStart(StaticActor& actor, CasingFields& f) {
    actor.runFlags |= kActorWarpKept;
    const ShortVec3& hand = player().bonePoints[kLeftHandBone];
    actor.pos.x = int32_t(uint32_t(int32_t(hand.x)) << 16);
    actor.pos.y = int32_t(uint32_t(int32_t(hand.y)) << 16);
    actor.pos.z = int32_t(uint32_t(int32_t(hand.z)) << 16);
    actor.rotation.x = 0;
    const uint32_t r = rngStep();
    actor.rotation.z = 0;
    actor.collisionBox.pointAt(casingBox.stateAddress());
    actor.rotation.y = int16_t(uint16_t((r & kCasingYawSpread) + uint32_t(uint16_t(player().rotation.y)) - kCasingYawBias));
    f.life = kCasingLife;
    f.spin = int16_t(rngStep() & kAngleMask);
    f.soundTicks = 0;
    actor.state += 1u;
}
// exe 0x568340: falling along its rotation; off the walls (the side speed reversed) and the floor (the fall speed
// halved, reversed; the sound at most every kCasingSoundTicks); freed when its life runs out
void casingFly(const PlayerEnv& env, StaticActor& actor, CasingFields& f) {
    actor.prevPos = actor.pos;
    --f.life;
    if (f.life == 0) {
        freeActor(actor);
        return;
    }
    actor.velocity.y += uint32_t(kCasingGravity);
    moveAlongRotation(actor.pos, actor.rotation, actor.velocity.x, actor.velocity.y, actor.velocity.z); // exe 0x5e3810
    if (int32_t(actorTestMove(env, actor)) < 0) {                // exe 0x40f570
        const uint32_t r = actorPushOut(env, actor);              // exe 0x410960
        if (r & kWallOnSides) actor.velocity.x = wrap16(-int(actor.velocity.x));
        if (r & kWallOnY) actor.velocity.y = wrap16(-int(actor.velocity.y) >> 1);
        reflectRotationOffWalls(actor.rotation, r); // exe 0x410740
        if (f.soundTicks == 0) {
            soundPlayAtPos(kCasingSound, actor.pos);
            f.soundTicks = kCasingSoundTicks;
        }
    }
    if (f.soundTicks != 0) --f.soundTicks;
    casingDraw(actor, f);
}

// [0x8c4a4c]
enum class MachineGunState : uint8_t { Spawn = 0, Fly = 1 }; // exe 0x56f990 / 0x56fc60
// how a tick ended (the jump table 0x56f978)
enum class MachineGunEnd { Flying = 0, HitBody = 1, HitWall = 2, OutOfRange = 3, Gone = -1 };

const DataTable<CollisionBoxRecord> machineGunBox{TableId::MachineGunBox};
constexpr int16_t kMachineGunSpeed = -0x600;
constexpr uint16_t kMachineGunSound = 0x109;
constexpr int16_t kCasingSide = 0xc0, kCasingUp = -0x100;
constexpr int kSteps = 3;

// x86 idiv-by-3 idiom (the 0x55555556 multiply): truncation toward zero
int div3(int v) {
    const int hi = int((int64_t(v) * 0x55555556LL) >> 32);
    return hi + int(uint32_t(hi) >> 31);
}

// the muzzle's offset of the shot's capsule
void muzzleOffset(const MachineGunFields& fields, int16_t off[3]) {
    const EffectActor& muzzle = effectActorAt(fields.muzzle);
    off[0] = muzzle.pos.x;
    off[1] = muzzle.pos.y;
    off[2] = muzzle.pos.z;
}

MachineGunEnd spawn(const PlayerEnv& env, ShotActor& shot, MachineGunFields& fields) { // exe 0x56f990
    const Player& p = player();
    shot.flags &= uint8_t(~kActorDrawn);
    shot.runFlags |= kActorWarpKept;
    const uint32_t m = allocActorA0(0);
    if (m == 0) {
        freeActor(shot);
        return MachineGunEnd::Gone;
    }
    EffectActor& muzzle = effectActorAt(m);
    auto&& muzzleFields = classState<MuzzleFields>(muzzle);
    muzzle.variant = 0;
    muzzle.clearPositionWords();
    muzzle.flags = kActorLive;
    muzzle.type = typeByte(GlobalEffectType::MuzzleFlash);
    muzzle.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    muzzleFields.shot = shot.at;
    fields.muzzle = m;
    // the player's own last move in its facing frame
    const int yaw = uint16_t(p.rotation.y) & kAngleMask;
    const int dx = int32_t(uint32_t(p.pos.x) - uint32_t(p.prevPos.x)) >> 12;
    const int dz = int32_t(uint32_t(p.pos.z) - uint32_t(p.prevPos.z)) >> 12;
    const int16_t sx = wrap16(((dx * fixedCos(yaw)) >> 12) - ((fixedSin(yaw) * dz) >> 12));
    const int16_t sz = wrap16(((dx * fixedSin(yaw)) >> 12) - ((fixedCos(yaw) * dz) >> 12));
    if (const uint32_t c = allocActor60(0)) { // the shell casing
        StaticActor& casing = staticActorAt(c);
        casing.flags = kActorLive;
        casing.type = typeByte(GlobalStaticType::ShellCasing);
        casing.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
        casing.variant = 0;
        casing.velocity.x = wrap16(sx - kCasingSide);
        casing.velocity.y = kCasingUp;
        casing.velocity.z = sz;
    }
    aimRelativeToTarget(shot);
    PlayerShotFields(shot).hitBox.result = 0;
    if (p.onPlatform == 0) {
        shot.velocity.x = 0;
        shot.velocity.z = kMachineGunSpeed;
    } else {
        shot.velocity.x = wrap16(div3(sx));
        shot.velocity.z = wrap16(div3(sz) + kMachineGunSpeed);
    }
    shot.velocity.y = 0;
    placeAtBone(shot, kLeftHandBone, 0);
    aimAlongPlayer(shot);
    moveAlongRotation(shot.prevPos, shot.rotation, 0, 0, -int(shot.velocity.z) * 2);
    fillShotStats(fields.stats);
    paySpecialEnergy(player());
    shot.collisionBox.pointAt(machineGunBox.stateAddress());
    shot.actorHit = 0;
    soundPlayAtPlayer(kMachineGunSound);
    ++shot.state;
    return shotHitWall(env, shot, fields.stats.attack) ? MachineGunEnd::HitWall : MachineGunEnd::Flying;
}

MachineGunEnd fly(const PlayerEnv& env, ShotActor& shot, MachineGunFields& fields) { // exe 0x56fc60
    auto&& shotFields = PlayerShotFields(shot);
    if (shotFields.hitBox.result & (kHitAttackSolid | kHitAttackDeflects)) {
        stopHalfway(shot);
        return MachineGunEnd::HitBody;
    }
    if (shot.actorHit != 0) return MachineGunEnd::HitWall;
    shot.savePreviousPose();
    if (shotFields.target != nullptr) homeOnTarget(shot, slotIdentityWord(shotFields.target), 2, 0);
    --fields.stats.range;
    if (fields.stats.range == 0) return MachineGunEnd::OutOfRange;
    for (int k = 0; k < kSteps; ++k) {
        moveShot(shot, shot.velocity.x, 0, shot.velocity.z);
        if (shotHitWall(env, shot, fields.stats.attack)) return MachineGunEnd::HitWall;
    }
    markFlying(shot);
    return MachineGunEnd::Flying;
}

// exe 0x56fd80: the capsule (radius 8) offset by the muzzle's jitter
void submitMachineGunHit(ShotActor& shot, const MachineGunFields& fields) {
    int16_t off[3];
    muzzleOffset(fields, off);
    submitShotCapsule(shot, 8, hitGroup(kHitGroupPlayerAttacks) | kHitContactPoint | hitCapsule(1),
                      fields.stats.attack | kHitAttackByPlayer, off);
}
// exe 0x56feb0: the impact flash (effect type 0xb) at the shot + the jitter
void spawnImpact(const ShotActor& shot, const MachineGunFields& fields) {
    const uint32_t address = allocActorA0(0);
    if (address == 0) return;
    int16_t off[3];
    muzzleOffset(fields, off);
    EffectActor& e = effectActorAt(address);
    e.flags = kActorLive | kActorDrawn;
    e.type = typeByte(GlobalEffectType::MachineGunImpact);
    e.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    e.variant = 0;
    e.pos.x = wrap16(off[0] + shot.pos.x.whole);
    e.pos.y = wrap16(off[1] + shot.pos.y.whole);
    e.pos.z = wrap16(off[2] + shot.pos.z.whole);
}
// exe 0x56ff10: the shot and its muzzle freed
void freeShot(ShotActor& shot, const MachineGunFields& fields) {
    freeActor(shot);
    freeActor(effectActorAt(fields.muzzle));
}

// ---- the OT polygons' helpers ---------------------------------------------------------------------------------------
// the camera-input point c + (r * (x, y, z)) >> 12
void framePoint(const Matrix3& r, const int16_t c[3], int x, int y, int z, int16_t out[3]) {
    for (int k = 0; k < 3; ++k)
        out[k] = wrap16(c[k] +
                        ((int32_t(r.e[k * 3]) * x + int32_t(r.e[k * 3 + 1]) * y + int32_t(r.e[k * 3 + 2]) * z) >> 12));
}
// the GTE NCLIP of three projected points (exe 0x40c7b0 -> 0x40cac0), over the truncated screen points of the game
// camera
int64_t nclip(const int16_t p0[3], const int16_t p1[3], const int16_t p2[3]) {
    const CameraPoint a = cameraPoint(p0), b = cameraPoint(p1), c = cameraPoint(p2);
    return int64_t(a.sx) * (b.sy - c.sy) + int64_t(b.sx) * (c.sy - a.sy) + int64_t(c.sx) * (a.sy - b.sy);
}
// the screen-pixel offset (px, py) of a projected centre as a camera-plane offset: `units` per pixel
void pixelOffset(int px, int py, float units, float out[2]) {
    out[0] = float(px) * units;
    out[1] = float(py) * units;
}

// exe 0x560f00(effect, block): the flash at the shot. Gated as the effects (the shot's position << shift within the
// effects' depth and on the screen); the local frame = the camera * exe 0x40c070 of the shot's rotation about that
// point (no scale). With the effect's offset o and the ring point R(i, s, z) = (o.x + (cos64 [i] >> s), o.y + (sin64
// [i] >> s), z) (16-bit sums):
//  * 4 sides of a tube (POLY_G4 0x38, opaque): (R(i - 0x10, 9, z - 0x60), R(i - 0x10, 9, z + 0x60), R(i, 9, z - 0x60),
//    R(i, 9, z + 0x60)) for i = 0x10, 0x20, 0x30, 0, white / 0x0090ff alternating, linked only when the NCLIP of the
//    first three is >= 0;
//  * 4 spikes (DR_MODE 0xe1000220 + POLY_G3 0x32): (o, R(i, 8, z - 0xc0), R(i, 8, z + 0xc0)), 0x0090ff / black / black;
//  * 4 flares (DR_MODE 0xe1000220 + POLY_G4 0x3a): (R(i, 8, zz), o, E, R(i - 0x20, 8, zz)), zz = z - 0xc0 / E = (o.x,
//  o.y,
//    z - 0x140) for the first two, z + 0xc0 / z + 0x140 for the others; black / 0x0090ff / black / black;
//  * the cap (DR_MODE 0xe1000220 + POLY_G4 0x38): (R(0, 9, z + 0x60), R(0x10, ..), R(0x30, ..), R(0x20, ..)) 0x0090ff.
void drawMuzzleFlash(const EffectActor& muzzle) {
    const ShotActor& shot = shotActorAt(classState<MuzzleFields>(muzzle).shot);
    const uint32_t sh = stage().coordinateShift & 0x1f;
    const int16_t c[3] = {wrap16(shot.pos.x.whole << sh), wrap16(shot.pos.y.whole << sh),
                          wrap16(shot.pos.z.whole << sh)};
    const CameraPoint cp = cameraPoint(c);
    if (!effectDepthVisible(cp) || !effectOnScreen(cp)) return;
    const Matrix3 r = rotationZYX(shot.rotation.x, shot.rotation.y, shot.rotation.z); // exe 0x40c070
    const int ox = muzzle.pos.x, oy = muzzle.pos.y, oz = muzzle.pos.z;
    auto ring = [&](uint32_t i, int s, int z, int16_t out[3]) {
        framePoint(r, c, wrap16((cos64(i) >> s) + ox), wrap16((sin64(i) >> s) + oy), wrap16(z), out);
    };
    auto point = [&](int x, int y, int z, int16_t out[3]) { framePoint(r, c, wrap16(x), wrap16(y), wrap16(z), out); };
    const float kNone[4][2] = {};
    uint32_t i = 0;
    int16_t ap[3], bp[3];
    ring(0, 9, oz - 0x60, ap);
    ring(0, 9, oz + 0x60, bp);
    for (int n = 0; n < 4; ++n) {
        i = (i + 0x10) & 0x3f;
        int16_t an[3], bn[3];
        ring(i, 9, oz - 0x60, an);
        ring(i, 9, oz + 0x60, bn);
        if (nclip(ap, bp, an) >= 0) {
            const uint32_t col[4] = {dc::packetColour(dc::kPolyG4, 0xffffff), 0x90ffu,
                                     dc::packetColour(dc::kPolyG4, 0xffffff), 0x90ffu};
            const int16_t q[4][3] = {
                {ap[0], ap[1], ap[2]}, {bp[0], bp[1], bp[2]}, {an[0], an[1], an[2]}, {bn[0], bn[1], bn[2]}};
            dc::otQuadG4(muzzle.at, col, 1, q, kNone);
        }
        std::copy(an, an + 3, ap);
        std::copy(bn, bn + 3, bp);
    }
    int16_t o[3];
    point(ox, oy, oz, o);
    for (int n = 0; n < 4; ++n, i = (i + 0x10) & 0x3f) {
        int16_t p0[3], p1[3];
        ring(i, 8, oz - 0xc0, p0);
        ring(i, 8, oz + 0xc0, p1);
        const uint32_t col[3] = {dc::packetColour(dc::kPolyG3Semi, 0x0090ff), 0, 0};
        const int16_t t[3][3] = {{o[0], o[1], o[2]}, {p0[0], p0[1], p0[2]}, {p1[0], p1[1], p1[2]}};
        const float off[3][2] = {};
        dc::otTriG3(muzzle.at, col, 1, t, off);
    }
    int16_t e[2][3];
    point(ox, oy, oz - 0x140, e[0]);
    point(ox, oy, oz + 0x140, e[1]);
    for (uint32_t n = 0; n < 4; ++n, i = (i + 0x10) & 0x3f) {
        const uint32_t far = (n >> 1) & 1;
        const int zz = far ? oz + 0xc0 : oz - 0xc0;
        int16_t p0[3], p1[3];
        ring(i, 8, zz, p0);
        ring((i - 0x20) & 0x3f, 8, zz, p1);
        const uint32_t col[4] = {dc::packetColour(dc::kPolyG4Semi, 0), 0x90ffu, 0, 0};
        const int16_t q[4][3] = {
            {p0[0], p0[1], p0[2]}, {o[0], o[1], o[2]}, {e[far][0], e[far][1], e[far][2]}, {p1[0], p1[1], p1[2]}};
        dc::otQuadG4(muzzle.at, col, 1, q, kNone);
    }
    int16_t q[4][3];
    ring(0, 9, oz + 0x60, q[0]);
    ring(0x10, 9, oz + 0x60, q[1]);
    ring(0x30, 9, oz + 0x60, q[2]);
    ring(0x20, 9, oz + 0x60, q[3]);
    const uint32_t col[4] = {dc::packetColour(dc::kPolyG4, 0x0090ff), dc::packetColour(dc::kPolyG4, 0x0090ff), 0x90ffu,
                             0x90ffu};
    dc::otQuadG4(muzzle.at, col, 1, q, kNone);
}

// ---- the impact -----------------------------------------------------------------------------------------------------
// The impact's fields: its ticks (the rays) and its bytes in the effect's trail -- the rays' angles and lengths (states 0
// / 1) or the disc's tick counter and its 6 rays (states 2 / 3)
struct ImpactFields {
    explicit ImpactFields(const EffectActor& actor) : ImpactFields(const_cast<EffectActor&>(actor)) {}
    explicit ImpactFields(EffectActor& actor)
        : ticks(actor.params[0]), rayAngles(actor.trail, 0), rayLengths(actor.trail, 8), discTicks(actor.trail, 0),
          discAngles(actor.trail, 4), discLengths(actor.trail, 10) {}
    uint8_t& ticks;
    ArrayFields<uint8_t, 8, int16_t, 8> rayAngles;
    ArrayFields<uint8_t, 8, int16_t, 8> rayLengths;
    ArrayField<uint32_t, int16_t, 8> discTicks;
    ArrayFields<uint8_t, 6, int16_t, 8> discAngles;
    ArrayFields<uint8_t, 6, int16_t, 8> discLengths;
};

// [0x8c3fe4]
enum class ImpactState : uint8_t {
    RaysStart = 0,
    Rays = 1,
    DiscStart = 2,
    Disc = 3
}; // exe 0x561700 / 0x5617a0 / 0x561bb0 / 0x561c40
constexpr uint8_t kRayTicks = 5;
constexpr uint32_t kDiscTicks = 8;

// exe 0x5617d0(effect, block): the rays around the projected centre S (exe 0x5f2dd0), w = exe 0x5f2f20(the position,
// (8 - ticks) * 0x80) (nothing when 0); per ray k (angle b[k] * 0x40, l = length * w * 3) two POLY_G4 0x3a in screen
// pixels from S: (A(a - 0x20, l >> 9), A(a, l >> 8), A(a, l >> 9), A(a + 0x20, l >> 9)) and the same with A(a, l >> 10)
// second (A(a, r) = (cos [a] * r >> 12, sin [a] * r >> 12) of the 4096-step table), black / k1 / k2 / black (t = ticks:
// k1 = {3t, 6t, 2t}, k2 = {16t, 24t, 8t}); then 16 POLY_FT3 0x26 (grey t << 4, tpage 0x2f, clut 0x7d10, uv (0xe0, 0x20)
// (0xef, 0x20) (0xe8, 0x3f)): (T(4m, r), T(4m + 4, r), S), r = ((b[m] & 0xf) * w * 3 >> 9) + 1, T(i, r) = (cos64 [i] *
// r
// >> 12, sin64 [i] * r >> 12)
void drawImpactRays(EffectActor& impact) {
    const ImpactFields fields(impact);
    const int16_t p[3] = {impact.pos.x, impact.pos.y, impact.pos.z};
    int16_t sxy[2];
    if (projectOnScreen(p, sxy) < 0) return; // exe 0x5f2dd0
    const int width = (8 - int(int8_t(fields.ticks))) * 0x80;
    int16_t depth;
    const int w = projectedWidth(p, int16_t(width), &depth); // exe 0x5f2f20
    if (w == 0) return;
    const uint32_t sh = stage().coordinateShift & 0x1f;
    const int16_t c[3] = {wrap16(p[0] << sh), wrap16(p[1] << sh), wrap16(p[2] << sh)};
    const float units = float(int64_t(width) << sh) / float(w);
    const int t = int8_t(fields.ticks);
    const uint32_t k1 =
        (uint32_t(uint8_t(t * 0x80 >> 6)) << 16 | uint32_t(uint8_t(t * 0x180 >> 6)) << 8) | uint32_t(t * 0xc0 >> 6);
    const uint32_t k2 =
        (uint32_t(uint8_t((t << 9) >> 6)) << 16 | uint32_t(uint8_t(t * 0x600 >> 6)) << 8) | uint32_t((t << 10) >> 6);
    auto A = [&](uint32_t an, int32_t rr, float out[2]) {
        pixelOffset(int16_t(fixedCos(int(an)) * rr >> 12), int16_t(fixedSin(int(an)) * rr >> 12), units, out);
    };
    const int16_t q[4][3] = {{c[0], c[1], c[2]}, {c[0], c[1], c[2]}, {c[0], c[1], c[2]}, {c[0], c[1], c[2]}};
    const uint32_t col[4] = {dc::packetColour(dc::kPolyG4Semi, 0), k1, k2, dc::packetColour(dc::kPolyG4Semi, 0)};
    for (uint32_t k = 0; k < 8; ++k) {
        const int32_t l = int32_t(uint32_t(fields.rayLengths[k]) * uint32_t(w) * 3);
        const uint32_t an = uint32_t(fields.rayAngles[k]) * 0x40;
        float off[4][2];
        A(an - 0x20, l >> 9, off[0]);
        A(an, l >> 8, off[1]);
        A(an, l >> 9, off[2]);
        A(an + 0x20, l >> 9, off[3]);
        dc::otQuadG4(impact.at, col, 1, q, off);
        A(an, l >> 10, off[1]);
        dc::otQuadG4(impact.at, col, 1, q, off);
    }
    const uint32_t g = uint32_t(t) << 4;
    const uint32_t grey = ((g | 0x2600u) << 8 | g) << 8 | g;
    for (uint32_t m = 0; m < 0x10; ++m) {
        const int32_t rr = ((int32_t(fields.rayAngles[m] & 0xf) * w * 3) >> 9) + 1;
        float off[4][2] = {};
        pixelOffset(int16_t(cos64(m * 4) * rr >> 12), int16_t(sin64(m * 4) * rr >> 12), units, off[0]);
        pixelOffset(int16_t(cos64(m * 4 + 4) * rr >> 12), int16_t(sin64(m * 4 + 4) * rr >> 12), units, off[1]);
        // the POLY_FT3 as a degenerate GT4 (one colour, the 4th corner = the 3rd)
        // (the first colour word as the exe computes it: the code byte 0x26, POLY_FT3 semi-transparent, ORed in above
        // g, which may carry into it; CLUT 0x7d10, tpage 0x2f additive)
        dc::GouraudTexturedQuad ray;
        ray.colour[0] = grey;
        ray.colour[1] = ray.colour[2] = ray.colour[3] = grey & dc::kRgbMask;
        ray.clut = 0x7d10;
        ray.tpage = 0x2f;
        ray.uv[0] = dc::texel(0xe0, 0x20);
        ray.uv[1] = dc::texel(0xef, 0x20);
        ray.uv[2] = ray.uv[3] = dc::texel(0xe8, 0x3f);
        dc::otQuadGT4(impact.at, ray, q, off);
    }
}

// exe 0x561c70's draw: a disc 0xf0 (its colour by the tick n, its size by 9 - n); variant 0 also 6 rays (angle i = the
// block's angle k, length l: r = l * w >> 4, h = l * w >> 5), two POLY_G4 0x3a each in screen pixels from the projected
// centre: (T(i - 1, r), T(i, r - h), T(i, r), T(i + 1, r)) and the same with T(i, r + h) second, black / n * 0x0c040a /
// n * 0x140712 / black
void drawImpactDisc(EffectActor& impact) {
    const ImpactFields fields(impact);
    const int16_t p[3] = {impact.pos.x, impact.pos.y, impact.pos.z};
    int16_t sxy[2];
    if (projectOnScreen(p, sxy) < 0) return; // exe 0x5f2dd0
    const uint32_t n = fields.discTicks;
    int16_t depth;
    const int w = projectedWidth(p, int16_t((9u - n) * 0x10), &depth); // exe 0x5f2f20
    if (w == 0) return;
    const uint32_t xy = uint32_t(uint16_t(impact.pos.x)) | uint32_t(uint16_t(impact.pos.y)) << 16;
    dc::emitDisc(impact.at, n * 0x180814u | dc::ringColour(0x000000), xy, uint16_t(impact.pos.z) | dc::ringStep(2),
                 (((9u - n) * 0x30) >> 2) << 16);
    if (impact.variant != 0) return;
    const uint32_t sh = stage().coordinateShift & 0x1f;
    const int16_t c[3] = {wrap16(p[0] << sh), wrap16(p[1] << sh), wrap16(p[2] << sh)};
    const float units = float(int64_t((9u - n) * 0x10 & 0xffff) << sh) / float(w);
    const int16_t q[4][3] = {{c[0], c[1], c[2]}, {c[0], c[1], c[2]}, {c[0], c[1], c[2]}, {c[0], c[1], c[2]}};
    const uint32_t col[4] = {dc::packetColour(dc::kPolyG4Semi, 0), n * 0xc040au, n * 0x140712u,
                             dc::packetColour(dc::kPolyG4Semi, 0)};
    auto T = [&](uint32_t i, int32_t r, float out[2]) {
        pixelOffset(int16_t(cos64(i) * r >> 12), int16_t(sin64(i) * r >> 12), units, out);
    };
    for (uint32_t k = 0; k < 6; ++k) {
        const uint32_t i = fields.discAngles[k];
        const int32_t r = int32_t(uint32_t(fields.discLengths[k]) * uint32_t(w)) >> 4;
        const int32_t h = int32_t(uint32_t(fields.discLengths[k]) * uint32_t(w)) >> 5;
        float off[4][2];
        T((i - 1) & 0x3f, r, off[0]);
        T(i, r - h, off[1]);
        T(i, r, off[2]);
        T((i + 1) & 0x3f, r, off[3]);
        dc::otQuadG4(impact.at, col, 1, q, off);
        T(i, r + h, off[1]);
        dc::otQuadG4(impact.at, col, 1, q, off);
    }
}

} // namespace

void shellCasingUpdate(const PlayerEnv& env, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    auto&& f = classState<CasingFields>(actor);
    switch (CasingState(actor.state)) {
    case CasingState::Start: casingStart(actor, f); return;
    case CasingState::Fly: casingFly(env, actor, f); return;
    default: notPortedAt(kMachineGunArea, "0x568260 state", actor.state);
    }
}

void machineGunShotUpdate(const PlayerEnv& env, uint32_t address) {
    ShotActor& shot = shotActorAt(address);
    auto&& fields = classState<MachineGunFields>(shot);
    dropDeadTarget(shot);
    MachineGunEnd end;
    switch (MachineGunState(shot.state)) {
    case MachineGunState::Spawn: end = spawn(env, shot, fields); break;
    case MachineGunState::Fly: end = fly(env, shot, fields); break;
    default: notPortedAt(kMachineGunArea, "0x56f8e0 state", shot.state);
    }
    switch (end) {
    case MachineGunEnd::Flying:
        actorCollisionSubmitMover(shot);
        submitMachineGunHit(shot, fields);
        return;
    case MachineGunEnd::HitBody: {
        int16_t off[3];
        muzzleOffset(fields, off);
        spawnShotSpark(shot, fields.stats.level, 0, off); // exe 0x56fe40
        freeShot(shot, fields);
        return;
    }
    case MachineGunEnd::HitWall:
        submitMachineGunHit(shot, fields);
        spawnImpact(shot, fields);
        freeShot(shot, fields);
        return;
    case MachineGunEnd::OutOfRange: freeShot(shot, fields); return;
    default: return;
    }
}

void machineGunMuzzleUpdate(const PlayerEnv&, uint32_t address) {
    EffectActor& muzzle = effectActorAt(address);
    switch (muzzle.state) { // [0x8c3fdc]
    case 0: {               // exe 0x560e10: the offset, a glow at the hand (exe 0x560eb0)
        muzzle.runFlags |= kActorWarpKept;
        muzzle.pos.x = int16_t((rngStep() & 0x1f) - 0x10u);
        muzzle.pos.z = 0;
        muzzle.pos.y = int16_t((rngStep() & 0x1f) - 0x10u);
        const auto& hand = player().bonePoints[kLeftHandBone];
        dc::emitDisc(muzzle.at, dc::ringColour(0x0090ff), uint32_t(uint16_t(hand.x)) | uint32_t(uint16_t(hand.y)) << 16,
                     uint16_t(hand.z) | dc::ringStep(2), 0x200000u);
        ++muzzle.state;
        return;
    }
    case 1: drawMuzzleFlash(muzzle); return; // exe 0x560e90 -> exe 0x560f00: the flash at the shot
    default: notPortedAt(kMachineGunArea, "0x560df0 state", muzzle.state);
    }
}

void machineGunImpactUpdate(const PlayerEnv&, uint32_t address) {
    EffectActor& impact = effectActorAt(address);
    ImpactFields fields(impact);
    switch (ImpactState(impact.state)) {
    case ImpactState::RaysStart:
        impact.runFlags |= kActorWarpKept;
        for (uint32_t k = 0; k < 8; ++k) {
            fields.rayAngles[k] = uint8_t((rngStep() & 7) + k * 8);
            fields.rayLengths[k] = uint8_t(rngStep() & 0xf);
        }
        fields.ticks = kRayTicks;
        drawImpactRays(impact); // exe 0x5617d0
        ++impact.state;
        return;
    case ImpactState::Rays:
        --fields.ticks;
        if (fields.ticks == 0)
            freeActor(impact);
        else
            drawImpactRays(impact);
        return;
    case ImpactState::DiscStart:
        impact.runFlags |= kActorWarpKept;
        for (uint32_t k = 0; k < 6; ++k) {
            fields.discLengths[k] = uint8_t((rngStep() & 7) + 8);
            fields.discAngles[k] = uint8_t(rngStep() & 0x3f);
        }
        fields.discTicks = kDiscTicks;
        drawImpactDisc(impact);
        ++impact.state;
        return;
    case ImpactState::Disc:
        --fields.discTicks;
        if (fields.discTicks == 0) {
            freeActor(impact);
            return;
        }
        drawImpactDisc(impact);
        return;
    default: notPortedAt(kMachineGunArea, "0x5616e0 state", impact.state);
    }
}

} // namespace od::port
