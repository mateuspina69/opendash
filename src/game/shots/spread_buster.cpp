// The spread buster (weapon 14, pool 0x10 type 13, exe 0x56ff30): the first shot (copy 0) spawns 2 more of its class
// (4 with the weapon's extra level), each fanned by (copy - n - 1) * 0x80 of yaw, and pays the energy; each flies 2
// steps a tick drawn as two sprites, bursting into a hit spark on a body or a sparkle (static global type 0x15, exe
// 0x56a110: a disc, and on a wall sparks and rays around its projected point, for 8 ticks) on a wall or at the end of
// its range.
#include "engine/data_source.hpp"
#include "game/shots/spread_buster_state.hpp"
#include "game/actor_state.hpp"
#include "game/shots/player_shots.hpp"

#include "game/actor_movement.hpp"
#include "game/global_class_types.hpp"
#include "game/player/player_weapons.hpp"

#include "game/actor_collision.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/draw_commands.hpp"
#include "game/effect_polygons.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/shots/shot_common.hpp"

namespace od::port {

using namespace spread_buster_state;

namespace {

inline constexpr const char* kSpreadArea = "spread buster";

// [0x8c4a64]
enum class SpreadState : uint8_t { Spawn = 0, Fly = 1 }; // exe 0x56ffd0 / 0x570250
enum class SpreadEnd { Flying = 0, HitBody = 1, HitWall = 2, OutOfRange = 3 };

constexpr uint8_t kSparkleOnWall = 0, kSparkleFaded = 1; // the sparkle's variants
const DataTable<CollisionBoxRecord> spreadBox{TableId::SpreadBusterBox};
constexpr int16_t kSpreadSpeed = -0x600;
constexpr uint16_t kSpreadSound = 0x108;
constexpr int kFanStep = 0x80;
constexpr int kSteps = 2;

SpreadEnd spawn(const PlayerEnv& env, ShotActor& shot, SpreadFields& fields) { // exe 0x56ffd0
    const Player& p = player();
    shot.flags &= uint8_t(~kActorDrawn);
    shot.runFlags |= kActorWarpKept;
    const int n = weaponLevel(p, player().activeSpecial, WeaponStat::Extra) > 0 ? 1 : 0;
    if (uint8_t(fields.copy) == 0) {
        const int copies = n * 2 + 2;
        for (int i = 0; i < copies;) {
            const uint32_t c = allocActor10();
            if (c == 0) break;
            ShotActor& copy = shotActorAt(c);
            copy.flags = kActorLive;
            ++i;
            copy.type = typeByte(GlobalShotType::SpreadBuster);
            copy.loopFlags |= kActorSweepGroup1;
            classState<SpreadFields>(copy).setCopy(uint32_t(i));
        }
        paySpecialEnergy(player());
    }
    PlayerShotFields(shot).hitBox.result = 0;
    if (p.onPlatform == 0) {
        shot.velocity.x = 0;
        shot.velocity.z = kSpreadSpeed;
    } else {
        int x, z;
        platformMove(x, z);
        shot.velocity.x = wrap16(x / 2);
        shot.velocity.z = wrap16(z / 2 + kSpreadSpeed);
    }
    shot.velocity.y = 0;
    placeAtBone(shot, kLeftHandBone, 0);
    shot.rotation.x = p.rotation.x;
    shot.rotation.z = 0;
    shot.rotation.y = wrap16((int(fields.copy) - n - 1) * kFanStep + int(uint16_t(p.rotation.y)));
    moveAlongRotation(shot.prevPos, shot.rotation, 0, 0, -int(shot.velocity.z) * 2);
    fillShotStats(fields.stats);
    shot.collisionBox.pointAt(spreadBox.stateAddress());
    shot.actorHit = 0;
    soundPlayAtPlayer(kSpreadSound);
    ++shot.state;
    if (shotHitWall(env, shot, fields.stats.attack)) return SpreadEnd::HitWall;
    if (uint8_t(fields.copy) == 0) { // exe 0x570360: the muzzle glow at the hand
        const auto& hand = p.bonePoints[kLeftHandBone];
        dc::emitDisc(shot.at, dc::ringColour(0xff60e0), uint32_t(uint16_t(hand.x)) | uint32_t(uint16_t(hand.y)) << 16,
                     uint16_t(hand.z) | dc::ringStep(2), 0x200000u);
    }
    return SpreadEnd::Flying;
}

SpreadEnd fly(const PlayerEnv& env, ShotActor& shot, SpreadFields& fields) { // exe 0x570250
    if (PlayerShotFields(shot).hitBox.result & (kHitAttackSolid | kHitAttackDeflects)) {
        stopHalfway(shot);
        return SpreadEnd::HitBody;
    }
    if (shot.actorHit != 0) return SpreadEnd::HitWall;
    shot.savePreviousPose();
    --fields.stats.range;
    if (fields.stats.range == 0) return SpreadEnd::OutOfRange;
    for (int k = 0; k < kSteps; ++k) {
        moveShot(shot, shot.velocity.x, 0, shot.velocity.z);
        if (shotHitWall(env, shot, fields.stats.attack)) return SpreadEnd::HitWall;
    }
    markFlying(shot);
    // exe 0x5703b0: two sprites (billboards: CLUT 0x7c12, tpage 0x2f additive / 0x4f subtractive), size 0x40, rotated
    // by the frame counter
    dc::Billboard b;
    b.xy = drawXY(shot);
    b.z = dc::zWord(shot.pos.z.whole);
    b.clut = 0x7c12;
    b.uv[0] = dc::texel(0xe0, 0xa0);
    b.uv[1] = dc::texel(0xff, 0xa0);
    b.uv[2] = dc::texel(0xe0, 0xbf);
    b.uv[3] = dc::texel(0xff, 0xbf);
    b.sizeRotation = dc::sizeRotation(0x40, (uint32_t(gameState().frame) & 7) << 8);
    b.colour = dc::billboardColour(0xc0c0c0);
    b.tpage = 0x2f;
    dc::emit(b, shot.at);
    b.colour = dc::billboardColour(0x606060);
    b.tpage = 0x4f;
    dc::emit(b, shot.at);
    return SpreadEnd::Flying;
}

// ---- the sparkle (static global type 0x15) ---------------------------------------------------------------------------
// [0x8c4884]
enum class SparkleState : uint8_t { Start = 0, Fade = 1 }; // exe 0x56a130 / 0x56a240
constexpr uint32_t kSparkleTicks = 8;
// the colours by the ticks left: the disc's, the rays' middle and tip
constexpr uint32_t kSparkleDiscStep = 0x180814, kRayMiddleStep = 0xc040a, kRayTipStep = 0x140712;
constexpr uint32_t kRayHalfWidth = 0x20; // a ray's base: its angle -+ this (12-bit)

// exe 0x56a270(actor, b): at the projected width of (9 - ticks) * 16: a disc (a ring record); on a wall also 4 sparks
// (screen ring records) and 8 rays of two additive G4 (black base, the middle / the tip coloured)
void sparkleDraw(StaticActor& actor, const SparkleFields& f) {
    const int16_t P[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    int16_t sxy[2];
    if (projectOnScreen(P, sxy) < 0) return;
    const uint32_t t = f.ticks;
    const int width = int(9 - t) << 4;
    int16_t depth = 0;
    const int w = projectedWidth(P, int16_t(width), &depth);
    if (w == 0) return;
    const uint32_t c = t * kSparkleDiscStep;
    dc::Ring disc;
    disc.inner = dc::ringColour(c);
    disc.xy = dc::packXY(P[0], P[1]);
    disc.zStep = dc::zWord(P[2]) | dc::ringStep(2);
    disc.radii = dc::ringRadii(0, uint32_t((width * 3) >> 2));
    dc::emit(disc, actor.at);
    if (actor.variant != kSparkleOnWall) return;
    for (uint32_t i = 0; i < kSparkleSparks; ++i) {
        const uint32_t ang = (uint32_t(f.sparkAngles[i]) << 4) & kAngleMask, len = f.sparkSpeeds[i];
        const int d = (int(0x10 - len) * w) >> 5;
        const int16_t Y = wrap16((fixedSin(int(ang)) * d >> 12) + sxy[1]);
        const int16_t X = wrap16((fixedCos(int(ang)) * d >> 12) + sxy[0]);
        dc::Ring spark;
        spark.inner = dc::screenRingColour(c);
        spark.xy = dc::packXY(X, Y);
        spark.zStep = dc::zWord(depth) | dc::ringStep(2);
        spark.radii = dc::ringRadii(0, uint32_t((int(len) * w) >> 6));
        dc::emit(spark, actor.at);
    }
    const ScreenAnchor an = shiftedScreenAnchor(P);
    const uint32_t cMiddle = t * kRayMiddleStep, cTip = t * kRayTipStep;
    auto at = [&](uint32_t ang, int r) {
        ang &= kAngleMask;
        const int16_t Y = wrap16((fixedSin(int(ang)) * r >> 12) + sxy[1]);
        const int16_t X = wrap16((fixedCos(int(ang)) * r >> 12) + sxy[0]);
        return dc::packXY(X, Y);
    };
    for (uint32_t i = 0; i < kSparkleRays; ++i) {
        const uint32_t e = uint32_t(f.rayAngles[i]) << 4;
        const int ln = int(uint32_t(f.rayLengths[i]) * uint32_t(w)) >> 4;
        const int h = ln >> 1;
        const uint32_t L = at(e - kRayHalfWidth, ln), T = at(e, ln), H = at(e + kRayHalfWidth, ln);
        const uint32_t M1 = at(e, ln - h), M2 = at(e, ln + h);
        const uint32_t black = dc::packetColour(dc::kPolyG4Semi, 0);
        const uint32_t col[4] = {black, cMiddle, cTip, black};
        const uint32_t q1[4] = {L, M1, T, H}, q2[4] = {L, M2, T, H};
        anchoredG4(actor.at, an, col, q1);
        anchoredG4(actor.at, an, col, q2);
    }
}
// exe 0x56a130: kept through the warps, the random rays and sparks, kSparkleTicks
void sparkleStart(StaticActor& actor, SparkleFields& f) {
    actor.runFlags |= kActorWarpKept;
    for (uint32_t i = 0; i < kSparkleRays; ++i) {
        f.rayLengths[i] = uint8_t((rngStep() & 7) + 4);
        f.rayAngles[i] = uint8_t(rngStep() & 0xff);
    }
    for (uint32_t i = 0; i < kSparkleSparks; ++i) {
        f.sparkSpeeds[i] = uint8_t(((rngStep() & 7) + (i << 2)) & 0xff);
        f.sparkAngles[i] = uint8_t(rngStep() & 0xff);
    }
    f.ticks = kSparkleTicks;
    sparkleDraw(actor, f);
    actor.state += 1u;
}
// exe 0x56a240: a tick less (none left: freed)
void sparkleFade(StaticActor& actor, SparkleFields& f) {
    --f.ticks;
    if (f.ticks == 0) {
        freeActor(actor);
        return;
    }
    sparkleDraw(actor, f);
}

// ---- the spread buster ------------------------------------------------------------------------------------------------
// exe 0x570470: the hit entry at the shot (radius 0x40 / 4)
void submitSpreadHit(ShotActor& shot, const ShotStats& stats) {
    submitShotHit(shot, hitGroup(kHitGroupPlayerAttacks) | kHitContactPoint | hitRadius(0x40),
                  stats.attack | kHitAttackByPlayer);
}
// exe 0x570550: a sparkle (static global type 0x15) at the shot
void spawnSparkle(const ShotActor& shot, uint8_t variant) {
    const uint32_t address = allocActor60(0);
    if (address == 0) return;
    StaticActor& s = staticActorAt(address);
    s.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    s.flags = kActorLive;
    s.type = typeByte(GlobalStaticType::Sparkle);
    s.variant = variant;
    s.pos = shot.pos;
}

} // namespace

void sparkleUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    auto&& f = classState<SparkleFields>(actor);
    switch (SparkleState(actor.state)) {
    case SparkleState::Start: sparkleStart(actor, f); return;
    case SparkleState::Fade: sparkleFade(actor, f); return;
    default: notPortedAt(kSpreadArea, "0x56a110 state", actor.state);
    }
}

void spreadBusterShotUpdate(const PlayerEnv& env, uint32_t address) {
    ShotActor& shot = shotActorAt(address);
    auto&& fields = classState<SpreadFields>(shot);
    SpreadEnd end;
    switch (SpreadState(shot.state)) {
    case SpreadState::Spawn: end = spawn(env, shot, fields); break;
    case SpreadState::Fly: end = fly(env, shot, fields); break;
    default: notPortedAt(kSpreadArea, "0x56ff30 state", shot.state);
    }
    switch (end) {
    case SpreadEnd::Flying:
        actorCollisionSubmitMover(shot);
        submitSpreadHit(shot, fields.stats);
        return;
    case SpreadEnd::HitBody:
        spawnShotSpark(shot, fields.stats.level, 0); // exe 0x5704f0
        freeActor(shot);                             // exe 0x570590
        return;
    case SpreadEnd::HitWall:
        submitSpreadHit(shot, fields.stats);
        spawnSparkle(shot, kSparkleOnWall);
        freeActor(shot);
        return;
    case SpreadEnd::OutOfRange:
        spawnSparkle(shot, kSparkleFaded);
        freeActor(shot);
        return;
    }
}

} // namespace od::port
