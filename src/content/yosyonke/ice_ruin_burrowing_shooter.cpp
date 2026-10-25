// The ice ruin's burrowing shooter (exe 0x479130) and its charged shot (exe 0x5c5980), see
// ice_ruin_burrowing_shooter.hpp; one routine per exe routine. Names are hypotheses from the observed behaviour.
//
// The shooter's update: the previous-pose copies, the state [0x890fc4 + state * 4], then (alive) the animation (asked
// for -> exe 0x5e1c30; else stepped only while it animates), the hit entry 0x47a030 and the planar distance to the
// player. States: 0 0x479210 init, 1 0x4792f0 main, 2 0x468fa0 free; 3..12 the modes 0..9 called directly, 13..15 the
// carried subs 0..2 (the tables overlap: [0x890fd0] = entry 3, [0x890ff8] = entry 13); entry 16 is 0 (the exe would
// call address 0). The main state: the damage 0x47a190 while it takes damage, the mode [0x890fd0 + sub * 4] (0..9;
// 10..12 = the carried subs; 13 is 0), then unless dead / carried the lift check (exe 0x573a60 -> mode 3) and the
// collision 0x47a2b0. The modes:
//   0 0x479380 buried: the player within 0xc00 -> dust 0x1e ticks, emerge (sounds 0x163 / 0xa3) -> mode 1
//   1 0x479450 nearer than 0x400 -> mode 5 (on screen or 1 in 4) or mode 2 (shoot); farther than 0xc00 -> mode 4
//     (burrow); else a hop turned toward the player +-0x100 (the side alternating), land, recover
//   2 0x479680 face the player, fire (exe 0x47a4d0: the shot charges while the sub-state is 2), animations 4..6, 0,
//     0x14 ticks -> mode 1
//   3 0x4797b0 carried: 0 0x4797d0 at the player's hands (animation 0xa after 0x78 ticks, then 0x14 more -> sub 2);
//     released -> thrown 0x4798c0: flies, a 0x20000 hit -> mode 9, lands -> mode 5; 2 0x479950 wriggles free until
//     dropped, falls -> mode 4
//   4 0x479c00 burrow (animation 0xc, 0x1e ticks of dust) -> mode 0 (hidden, sound 0xa4)
//   5 0x479cb0 up to three hops away from the player, then turns back toward it over 0x10 ticks -> mode 1 (a wall bump
//     marks "hop again": the next landing hops once more)
//   6 0x479a20 / 7 0x479b40 hurt (knocked up and back, sound 0x8e; 7 = while airborne) -> mode 4
//   8 0x479ed0 dead on the ground (kill count, blast, animations 7 / 8, drops) / 9 0x479fa0 dead while thrown (kill
//     count, hidden, blast, drops at once) -> state 2
// The shadow (exe 0x5f20f0(a, 0x80, 0xc0, 0) while drawn and not carried) is render output and left out.
#include "content/yosyonke/ice_ruin_burrowing_shooter.hpp"
#include "content/yosyonke/ice_ruin_burrowing_shooter_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/draw_commands.hpp"
#include "game/explosions.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/matrix.hpp"
#include "game/pickups.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/visibility.hpp"

namespace od::port {

using namespace ice_ruin_burrowing_shooter_state;

namespace {

// ==== the shooter =====================================================================================================
enum class ShooterState : uint8_t { Init = 0, Main = 1, Free = 2, FirstDirectMode = 3, LastDirect = 15 };
enum class ShooterMode : uint8_t {
    Buried = 0, Hop = 1, Shoot = 2, Carried = 3, Burrow = 4, HopAway = 5, HurtOnGround = 6, HurtInAir = 7,
    DeadOnGround = 8, DeadThrown = 9, FirstCarriedSub = 10, LastCarriedSub = 12,
};
enum class CarriedSub : uint8_t { Held = 0, Thrown = 1, Wriggle = 2 };
// its bits
constexpr uint8_t kShooterAnimates = 0x01;
constexpr uint8_t kShooterDead = 0x02;
constexpr uint8_t kShooterAirborne = 0x04;
constexpr uint8_t kShooterReflects = 0x08;    // the walls turn it (the push-out's reflection)
constexpr uint8_t kShooterTakesDamage = 0x10;
constexpr uint8_t kShooterUntracedBit = 0x20; // set and cleared with kShooterTakesDamage (its reader is not traced)
constexpr uint8_t kShooterCarried = 0x40;
constexpr uint8_t kShooterHopAgain = 0x80;
constexpr uint8_t kShooterActive = kShooterTakesDamage | kShooterUntracedBit;
// the collision's result bits
constexpr uint8_t kContactWall = 0x01;
constexpr uint8_t kContactTurnedOrLanded = 0x02;
constexpr uint8_t kContactAboveGround = 0x04;
constexpr uint8_t kContactMoveFlag = 0x08; // the move test's bit 0x20000000


const DataTable<CollisionBoxRecord> kShooterBox{TableId::IceRuinBurrowingShooterBox};
constexpr int32_t kWakeDistance = 0xc00, kShootDistance = 0x400;
constexpr uint8_t kEmergeTicks = 0x1e, kBurrowTicks = 0x1e, kAimTicks = 0x14, kRecoverTicks = 0x14, kHoldTicks = 0x78,
                  kStruggleTicks = 0x14, kTurnBackTicks = 0x10, kDeadTicks = 0x1e, kGoneTicks = 0xf;
constexpr uint16_t kHopSound = 0x163, kEmergeSound = 0xa3, kBurrowSound = 0xa4, kShootSound = 0x164,
                   kRecoilSound = 0x165, kHurtSound = 0x8e, kFlinchSound = 0x8d;
constexpr int16_t kHopSpeed = int16_t(0xfe88), kHopLift = int16_t(0xfc40), kHopGravity = 0x80, kSkidSpeed = -0x100;
constexpr int16_t kHurtLift = int16_t(0xfe20), kHurtPush = 0x100, kLowGravity = 0x40;
constexpr uint32_t kHitAny = 0x1c0000u, kHitThrownInto = 0x20000u;
constexpr uint32_t kHitStrongKinds = 0x7000000u, kHitCrush = 0x4000000u;

void sound(uint16_t id, const AnimatedActor& actor) { soundPlayAtPos(id, actor.pos); } // exe 0x5d5380(id, +0x10)
// the variant's row of the type's stats (a null type row: the exe reads near address 0)
AnimatedActorStats shooterRow(const AnimatedActor& actor, const char* where) {
    if (!hasAnimatedStats(actor.type)) notPortedAt(kStoryPort, where, actor.type);
    return animatedStats(actor.type, actor.spawnByte);
}
// exe 0x5e3d80(yaw, exe 0x5e3bc0(player, actor)): the turn that faces it away from the player
int faceDiff(const AnimatedActor& actor) {
    return angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleOf(player().pos, actor.pos)));
}
void fly(AnimatedActor& actor) { // exe 0x5e3690(a, 0, vy, vz), then the gravity
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z);
    actor.velocity.y += uint16_t(actor.acceleration.y);
}
void slide(AnimatedActor& actor) { integrateVelocity(actor, 0, 0, actor.velocity.z); } // exe 0x5e3690(a, 0, 0, vz)
void enterMode(AnimatedActor& actor, ShooterMode m) {
    actor.subState = uint8_t(m);
    actor.step = 0;
}

// 0x47a030: the hit entry (by its kind; none past the table) at (x, y - 0x64, z); the box cleared, its previous x / z
void hitEntry(AnimatedActor& actor, ShooterFields& f) {
    f.hit.previousX = actor.prevPos.x.whole;
    f.hit.previousZ = actor.prevPos.z.whole;
    f.hit.result = 0;
    const uint32_t kind = f.hitKind;
    const int16_t pos[3] = {actor.pos.x.whole, int16_t(uint16_t(uint16_t(actor.pos.y.whole) - 0x64u)),
                            actor.pos.z.whole};
    if (kind > uint32_t(ShooterHitKind::Harmless)) return;
    const AnimatedActorStats r = shooterRow(actor, "0x47a030: no type row, type");
    uint32_t radius = 0x30, hi = 0x400000, lo = 0xa0000, imm = 0x80000000u, l4 = 0, atk = r.attack, b4 = r.hitHeight;
    switch (ShooterHitKind(kind)) {
    case ShooterHitKind::Body: l4 = 0x100000; break;                         // 0x47a0a5
    case ShooterHitKind::Thrown: hi = 0x200000; l4 = 0x80000; b4 = 0; break; // 0x47a0c8
    case ShooterHitKind::Wriggling: radius = 0x100; break;                   // 0x47a0f0
    default: lo = 0x10000; imm = 0xff000000u; atk = 0; break;                // 0x47a10b
    }
    const uint32_t attack = atk | imm | lo;
    const uint32_t shape = (uint32_t(actor.slot) << 24) | (b4 << 9) | (radius >> 2) | l4 | hi;
    hitListSubmit(pos, f.hit, shape, attack); // exe 0x5e4120
}
// 0x47a190: the damage of the hit box. A hit: the white flash; dead -> mode 8 (9 when airborne); alive: below the
// row's flinch threshold the hurt sound, else knocked (mode 7 airborne, 6 on the ground). Then, taking damage, a crush:
// dead -> mode 8 (unless dead / airborne), alive -> mode 6.
void takeDamage(AnimatedActor& actor, ShooterFields& f) {
    uint32_t v = f.hit.result;
    if (v & kHitAny) {
        actor.tint = kTintFlash;
        const uint32_t d = v & 0xfff;
        if (hitPointsAdd(actor, -int(d)) != 0) { // exe 0x5e52f0
            if (!(f.bits & kShooterDead)) {
                enterMode(actor, (f.bits & kShooterAirborne) ? ShooterMode::DeadThrown : ShooterMode::DeadOnGround);
                f.bits |= kShooterDead;
                return;
            }
        } else if (d < shooterRow(actor, "0x47a190: no type row, type").bodyAttack) { // read as the flinch threshold
            sound(kFlinchSound, actor);
        } else {
            enterMode(actor, (f.bits & kShooterAirborne) ? ShooterMode::HurtInAir : ShooterMode::HurtOnGround);
            return;
        }
    }
    if (!(f.bits & kShooterTakesDamage) || !(v & kHitStrongKinds) || !(v & kHitCrush)) return;
    v &= 0xfff;
    if (hitPointsAdd(actor, -int(v)) != 0) {
        if (f.bits & (kShooterDead | kShooterAirborne)) return;
        enterMode(actor, ShooterMode::DeadOnGround);
        f.bits |= kShooterDead;
        return;
    }
    enterMode(actor, ShooterMode::HurtOnGround);
}
// 0x47a2b0: the walls (blocked -> back; a wall push-out, a reflection while it reflects) and the ground: on it, more
// than 0x10 above -> above the ground, 1; else snapped. Airborne: a landing push-out (& 0x3000) unless it reflects ->
// on the ground, snapped, landed.
int collide(const PlayerEnv& env, AnimatedActor& actor, ShooterFields& f) {
    uint32_t pushed = 0;
    f.contact = 0;
    const uint32_t t = actorTestMove(env, actor); // exe 0x40f570
    if (t & 0x20000000) f.contact |= kContactMoveFlag;
    if (int32_t(t) < 0) {
        pushed = actorPushOut(env, actor); // exe 0x410960
        if (int32_t(pushed) < 0) actor.pos = actor.prevPos;
        if (pushed & 0xf00) f.contact = kContactWall;
        if ((f.bits & kShooterReflects) && reflectRotationOffWalls(actor.rotation, pushed) != 0) // exe 0x410740
            f.contact |= kContactTurnedOrLanded;
    }
    const int g = actorGround(env, actor); // exe 0x40f190
    if (actor.airborne == 0) {
        if (g - actor.pos.y.whole > 0x10) {
            f.contact |= kContactAboveGround;
            return 1;
        }
        actor.pos.y.whole = int16_t(g);
        return 0;
    }
    if (!(f.bits & kShooterReflects) && (pushed & 0x3000)) {
        actor.airborne = 0;
        actor.pos.y.whole = int16_t(g);
        f.contact |= kContactTurnedOrLanded;
    }
    return 0;
}
// 0x47a390: dust (exe 0x5e13b0: the global hit spark, variant 3) at a random point around the actor
void dust(const AnimatedActor& actor) {
    const uint32_t e = allocActorA0(0);
    if (!e) return;
    EffectActor& puff = effectActorAt(e);
    puff.flags = kActorLive;
    puff.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    puff.type = typeByte(GlobalEffectType::HitSpark);
    puff.variant = 3;
    const uint32_t dx = rngStep() & 0xff;
    puff.pos.x = int16_t(uint16_t(actor.pos.x.whole) - dx + 0x80u);
    const uint32_t dy = rngStep() & 0x3f;
    puff.pos.y = int16_t(uint16_t(actor.pos.y.whole) - dy);
    const uint32_t dz = rngStep() & 0xff;
    puff.pos.z = int16_t(uint16_t(actor.pos.z.whole) - dz + 0x80u);
    classState<HitSparkFields>(effectActorAt(e)).size = uint16_t((rngStep() & 0xf) + 0xc0u);
}
// 0x47a480: the death blast (exe 0x5e1300: the global explosion) at y - 0x60
void blast(const AnimatedActor& actor) {
    const uint32_t e = allocActor60(0);
    if (!e) return;
    StaticActor& boom = staticActorAt(e);
    boom.flags = kActorLive;
    boom.loopFlags |= kActorSweepGroup2;
    boom.type = typeByte(GlobalStaticType::Explosion);
    boom.variant = 0;
    boom.pos.x.whole = actor.pos.x.whole;
    boom.pos.y.whole = int16_t(uint16_t(uint16_t(actor.pos.y.whole) - 0x60u));
    boom.pos.z.whole = actor.pos.z.whole;
    ExplosionParams params(actorHeaderAt(e));
    params.spread = 0x30;
    params.flashes = 0x3c;
}

// ==== the charged shot ================================================================================================
// States [0x8d1244 + state * 4]: 0 0x5c59a0 init, 1 0x5c59d0 (sub-states [0x8d1250 + sub * 4]), 2 0x5d0bc0 free, 3..7 =
// the sub-states 0..4 directly.
enum class ShotState : uint8_t { Init = 0, Run = 1, Free = 2, FirstDirectSub = 3, LastDirectSub = 7 };
enum class ShotSub : uint8_t { Charge = 0, Fly = 1, Drop = 2, Fall = 3, Burst = 4 };
constexpr uint8_t kChargedShotType = 0xc;
constexpr uint16_t kChargeTicks = 0x14, kFlightTicks = 0x3c, kFlightRings = 0x40, kRingGrowth = 6;
constexpr int16_t kShotSpeed = int16_t(0xf800);
constexpr int16_t kFallTicks = 0x64;
// the hand's offset on the shooter (rotated by its rotation), 0x80 lower
constexpr int kHandY = -0x1000, kHandZ = -0x600;
constexpr uint32_t kHandDrop = 0x800000u;

// exe 0x5c5cc0(actor, lo, hi, w1, w3): a 0xf0 ring record at the shot (word 5 = hi << 16 | lo)
void shotRing(const ProjectileActor& shot, uint32_t lo, uint32_t hi, uint32_t w1, uint32_t w3) {
    dc::emit({0x4000u, w1, (uint32_t(int32_t(shot.pos.y.whole)) << 16) | uint16_t(shot.pos.x.whole),
              w3, uint16_t(shot.pos.z.whole) | 0x10000u, (hi << 16) | lo},
             shot.at);
}
void shotRings(const ProjectileActor& shot, const ShotFields& f) { // exe 0x5c5c40: three rings of the rings' size
    const uint32_t n = f.ringSize;
    shotRing(shot, 0, n - (n >> 1), 0xf0000000u, 0x22007f7fu);
    shotRing(shot, n - (n >> 2), n - (n >> 1), 0xf0000000u, 0x22003f7fu);
    shotRing(shot, n, n - (n >> 2), 0xf0000000u, 0x2200007fu);
}
void shotBillboard(const ProjectileActor& shot, const ShotFields& f) { // exe 0x5c5ee0: the 0x2c billboard, small / big
    const bool big = f.big != 0;
    const uint32_t e = big ? 0x1fu : 0x17u, s = big ? 0x28u : 0x14u;
    dc::emit({0, 0x2c808080u,
              uint32_t(uint16_t(shot.pos.x.whole)) | uint32_t(uint16_t(shot.pos.y.whole)) << 16,
              big ? 0x3c388000u : 0x7cd18000u, uint32_t(uint16_t(shot.pos.z.whole)), e | 0xe8000u, s, (e + 0x80) << 8,
              0u, ((e + 0x80) << 8) | e},
             shot.at, false);
}
// the shot at the shooter's hand
void atHand(ProjectileActor& shot, const AnimatedActor& shooter) {
    int32_t v[3];
    rotateVector(shooter.rotation, 0, kHandY, kHandZ, v); // exe 0x5e3970
    shot.pos.x = int32_t(uint32_t(shooter.pos.x) + uint32_t(v[0]));
    shot.pos.y = int32_t(uint32_t(shooter.pos.y) + uint32_t(v[1]) + kHandDrop);
    shot.pos.z = int32_t(uint32_t(shooter.pos.z) + uint32_t(v[2]));
}
void shotCharge(ProjectileActor& shot, ShotFields& f) { // 0x5c59f0: at the hand until the shooter lets it go
    shotRings(shot, f);
    const AnimatedActor& shooter = *f.shooter;
    f.ringSize += kRingGrowth;
    atHand(shot, shooter);
    if (shooter.subState != uint8_t(ShooterMode::Shoot)) {
        shot.state = uint8_t(ShotState::Free);
        return;
    }
    --f.timer;
    if (f.timer != 0) return;
    if (!bindActorModel(shot, 0)) { // exe 0x5e0ba0
        shot.state = uint8_t(ShotState::Free);
        return;
    }
    shot.collisionBox.pointAt(TableId::IceRuinBurrowingShooterShotFlyBox);
    shot.airborne = 1;
    shot.onPlatform = 0;
    shot.platform = nullptr;
    shot.actorHit = 0;
    shot.semiTransparency = 0x22;
    shot.subState = uint8_t(ShotSub::Fly);
    shot.rotation.y = shooter.rotation.y;
    f.timer = kFlightTicks;
    f.ringSize = kFlightRings;
}
void shotFly(const PlayerEnv& env, ProjectileActor& shot, ShotFields& f) { // 0x5c5b20: spins, forward along its rotation
    shot.rotation.z = int16_t((uint16_t(shot.rotation.z) + 0x100u) & 0xfffu);
    moveAlongRotation(shot.pos, shot.rotation, 0, 0, shot.velocity.z); // exe 0x5e3810
    shotRings(shot, f);
    const int32_t atk = f.attack;
    f.hit.previousX = shot.prevPos.x.whole;
    f.hit.previousZ = shot.prevPos.z.whole;
    f.hit.result = 0;
    const int16_t pos[3] = {shot.pos.x.whole, shot.pos.y.whole, shot.pos.z.whole};
    const uint32_t r = actorTestMove(env, shot); // exe 0x40f570
    if (int32_t(r) < 0 || (f.hit.result & 0x10000)) {
        queueBreakableWallHit(r, atk); // exe 0x58d4e0
        freeActor(shot);               // exe 0x5e1500
        return;
    }
    --f.timer;
    if (f.timer == 0) {
        shot.state = uint8_t(ShotState::Free);
        return;
    }
    shot.savePreviousPose();
    hitListSubmit(pos, f.hit, uint32_t(shot.slot) << 24 | 0x480008u, uint32_t(atk) | 0x180000u); // exe 0x5e4120
}
void shotDrop(ProjectileActor& shot, ShotFields& f) { // 0x5c5d40: the falling form's start
    f.shooter = nullptr;
    f.fallHit.result = 0; // (the same word in the exe)
    shotBillboard(shot, f);
    f.fallTicks = kFallTicks;
    shot.collisionBox.pointAt(TableId::BlobBossShotsFallingShotBox);
    shot.state = uint8_t(ShotState::Run);
}
void shotFall(const PlayerEnv& env, ProjectileActor& shot, ShotFields& f) { // 0x5c5d70: gravity, freed on a hit / wall
    shot.savePreviousPose();
    --f.fallTicks;
    if (f.fallTicks == 0) shot.state = uint8_t(ShotState::Free);
    shot.velocity.y += uint16_t(shot.acceleration.y);
    integrateVelocity(shot, 0, shot.velocity.y, shot.velocity.z); // exe 0x5e3690
    shotBillboard(shot, f);
    const uint32_t h = f.fallHit.result;
    if (f.fallTicks > 0x5f) {
        if (h & 0x50000) shot.state = uint8_t(ShotState::Free);
    } else if (h & 0x70000) {
        shot.state = uint8_t(ShotState::Free);
    }
    if (int32_t(actorTestMove(env, shot)) < 0) shot.state = uint8_t(ShotState::Free);
    f.fallHit.result = 0;
    f.fallHit.previousX = shot.prevPos.x.whole;
    f.fallHit.previousZ = shot.prevPos.z.whole;
    const int16_t pos[3] = {shot.pos.x.whole, shot.pos.y.whole, shot.pos.z.whole};
    const bool big = f.big != 0;
    const uint32_t lo = big ? 0x14u : 0x10u, hi = big ? 0x600000u : 0x400000u;
    hitListSubmit(pos, f.fallHit, ((lo | 0x200000u) >> 2) | uint32_t(shot.slot) << 24 | hi, 0x20180008u);
}
void shotBurst(ProjectileActor& shot) { // 0x5c5e90: the global spark (variant 0) at the shot, freed
    if (const uint32_t e = allocActorA0(0)) { // exe 0x5e13b0
        EffectActor& spark = effectActorAt(e);
        spark.type = typeByte(GlobalEffectType::HitSpark);
        spark.variant = 0;
        spark.flags = kActorLive;
        spark.loopFlags |= kActorSweepGroup2;
        spark.pos.x = shot.pos.x.whole;
        spark.pos.y = shot.pos.y.whole;
        spark.pos.z = shot.pos.z.whole;
        classState<HitSparkFields>(effectActorAt(e)).size = 0x80;
    }
    freeActor(shot); // exe 0x5e1500
}
void shotSub(const PlayerEnv& env, ProjectileActor& shot, ShotFields& f, uint32_t n) {
    switch (ShotSub(n)) {
    case ShotSub::Charge: shotCharge(shot, f); return;
    case ShotSub::Fly: shotFly(env, shot, f); return;
    case ShotSub::Drop: shotDrop(shot, f); return;
    case ShotSub::Fall: shotFall(env, shot, f); return;
    case ShotSub::Burst: shotBurst(shot); return;
    default: notPortedAt(kStoryPort, "0x5c5980 sub-state (table 0x8d1250)", n);
    }
}
void chargedShot(const PlayerEnv& env, ProjectileActor& shot) {
    ShotFields& f = classState<ShotFields>(shot);
    const uint8_t st = shot.state;
    switch (ShotState(st)) {
    case ShotState::Init: // 0x5c59a0
        shot.flags |= kActorDrawn;
        f.timer = kChargeTicks;
        f.ringSize = 0;
        shot.enterStateBytes(uint8_t(ShotState::Run));
        return;
    case ShotState::Run: shotSub(env, shot, f, shot.subState); return; // 0x5c59d0
    case ShotState::Free: freeActor(shot); return;                    // 0x5d0bc0 -> exe 0x5e1500
    default:
        if (st < uint8_t(ShotState::FirstDirectSub) || st > uint8_t(ShotState::LastDirectSub))
            notPortedAt(kStoryPort, "0x5c5980 state (table 0x8d1244)", st);
        shotSub(env, shot, f, st - uint32_t(ShotState::FirstDirectSub));
        return;
    }
}

// exe 0x47a4d0: the charged shot (exe 0x5e1280: pool 0x40 stage type 0xc) at the shooter's hand, pitched from the
// player (y - 0x5a) toward it; its attack the type row's strong attack
void fireShot(const AnimatedActor& shooter) {
    int32_t v[3];
    rotateVector(shooter.rotation, 0, kHandY, kHandZ, v); // exe 0x5e3970
    const Player& p = player();
    const int16_t pl[3] = {p.pos.x.whole, int16_t(uint16_t(uint16_t(p.pos.y.whole) - 0x5au)), p.pos.z.whole};
    const uint32_t e = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!e) return;
    ProjectileActor& shot = projectileActorAt(e);
    ShotFields& f = classState<ShotFields>(shot);
    shot.flags = kActorLive;
    shot.loopFlags |= kActorSweepGroup2;
    shot.type = kChargedShotType;
    shot.variant = 0;
    shot.pos.x = int32_t(uint32_t(shooter.pos.x) + uint32_t(v[0]));
    shot.pos.y = int32_t(uint32_t(shooter.pos.y) + uint32_t(v[1]) + kHandDrop);
    shot.pos.z = int32_t(uint32_t(shooter.pos.z) + uint32_t(v[2]));
    const int16_t sp[3] = {shot.pos.x.whole, shot.pos.y.whole, shot.pos.z.whole};
    const int pitch = pitchOf(pl, sp);
    shot.rotation.x = int16_t(angleDiff(uint32_t(int32_t(shooter.rotation.x)), uint32_t(0x800 - pitch))); // 0x5e3d80
    shot.rotation.y = shooter.rotation.y;
    shot.rotation.z = 0;
    shot.velocity.x = 0;
    shot.velocity.y = 0;
    shot.velocity.z = kShotSpeed;
    if (!hasAnimatedStats(shooter.type))
        notPortedAt(kStoryPort, "FUN_0047a4d0: no type row (the exe reads address 2), type", shooter.type);
    f.attack = int16_t(animatedStats(shooter.type).strongAttack);
    f.shooter = &shooter;
}

// ==== the shooter's modes =============================================================================================
void buried(AnimatedActor& actor, ShooterFields& f) { // 0x479380
    switch (actor.step) {
    case 0:
        if (f.playerDistance <= kWakeDistance) {
            actor.step = 1;
            f.ticks = kEmergeTicks;
        }
        break;
    case 1:
        if (gameState().frame & 4) dust(actor); // 0x47a390
        --f.ticks;
        if (f.ticks != 0) break;
        actor.flags |= kActorDrawn;
        f.bits |= kShooterActive | kShooterAnimates;
        f.setHitKind(ShooterHitKind::Body);
        sound(kHopSound, actor);
        sound(kEmergeSound, actor);
        actor.step = 2;
        break;
    case 2:
        if (!actor.animEnded()) break;
        enterMode(actor, ShooterMode::Hop);
        f.ticks = 0;
        f.bits &= uint8_t(~kShooterAirborne);
        break;
    default: break;
    }
}
void hop(AnimatedActor& actor, ShooterFields& f) { // 0x479450 ([0x47965c] on the step, 6 entries; past them nothing)
    switch (actor.step) {
    case 0: { // 0x47946a
        const int32_t d = f.playerDistance;
        if (d < kShootDistance) {
            if (pointVisibleOrNear(actor.pos, 0) != 0) { // exe 0x5e59a0
                enterMode(actor, ShooterMode::HopAway);
                return;
            }
            enterMode(actor, (rngStep() & 3) == 0 ? ShooterMode::HopAway : ShooterMode::Shoot);
            return;
        }
        if (d > kWakeDistance) {
            enterMode(actor, ShooterMode::Burrow);
            return;
        }
        actor.velocity.z = kHopSpeed;
        int t = faceDiff(actor);
        t = f.hopSide > 0 ? t + 0x100 : t - 0x100;
        t >>= 1;
        f.turnStep = int16_t(t);
        actor.rotation.y += uint16_t(t);
        actor.step = 1;
        actor.stepCounter = 0;
        return;
    }
    case 1: // 0x479523: take off
        actor.rotation.y += uint16_t(f.turnStep);
        actor.anim = 1;
        sound(kHopSound, actor);
        actor.acceleration.y = kHopGravity;
        actor.velocity.y = kHopLift;
        f.bits |= kShooterAirborne;
        f.hopSide = int8_t(~uint8_t(f.hopSide));
        f.ticks = 3;
        actor.step = 2;
        actor.stepCounter = 0;
        return;
    case 2: // 0x479580
        --f.ticks;
        if (f.ticks != 0) return;
        fly(actor);
        actor.step = 3;
        return;
    case 3: // 0x4795b7: rising
        if (!(f.contact & kContactAboveGround)) {
            actor.step = 0;
            f.bits &= uint8_t(~kShooterAirborne);
            return;
        }
        if (actor.stepCounter == 0 && actor.velocity.y > 0) {
            actor.anim = 2;
            actor.step = 4;
        }
        fly(actor);
        return;
    case 4: // 0x47960e: falling until landed
        if (f.contact & kContactAboveGround) {
            fly(actor);
            return;
        }
        actor.step = 5;
        actor.anim = 0xd;
        actor.velocity.z = kSkidSpeed;
        f.bits &= uint8_t(~kShooterAirborne);
        return;
    case 5: // 0x479638: skid until the animation ends
        if (actor.animEnded()) {
            actor.step = 0;
            return;
        }
        slide(actor);
        return;
    default: return;
    }
}
void shoot(AnimatedActor& actor, ShooterFields& f) { // 0x479680 ([0x479798] on the step, 6 entries; past them nothing)
    if (actor.step < 2) actor.rotation.y += uint16_t(faceDiff(actor));
    switch (actor.step) {
    case 0: // 0x4796c4
        fireShot(actor); // exe 0x47a4d0
        f.ticks = kAimTicks;
        actor.anim = 4;
        sound(kShootSound, actor);
        actor.step = 1;
        return;
    case 1: // 0x4796ef
        --f.ticks;
        if (f.ticks != 0) return;
        actor.anim = 5;
        actor.step = 2;
        f.ticks = 1;
        return;
    case 2: // 0x479716
        --f.ticks;
        if (f.ticks != 0) return;
        sound(kRecoilSound, actor);
        actor.step = 3;
        f.ticks = 0;
        return;
    case 3: // 0x479744
        if (!actor.animEnded()) return;
        actor.anim = 6;
        actor.step = 4;
        return;
    case 4: // 0x47975a
        if (!actor.animEnded()) return;
        actor.anim = 0;
        actor.step = 5;
        f.ticks = kRecoverTicks;
        return;
    case 5: // 0x479776
        --f.ticks;
        if (f.ticks != 0) return;
        enterMode(actor, ShooterMode::Hop);
        f.ticks = 0;
        return;
    default: return;
    }
}
void carriedThrown(const PlayerEnv& env, AnimatedActor& actor, ShooterFields& f) { // 0x4798c0
    actor.velocity.y += uint16_t(actor.acceleration.y);
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e3690
    if (f.hit.result & kHitThrownInto) {
        enterMode(actor, ShooterMode::DeadThrown);
        f.bits |= kShooterDead;
        return;
    }
    collide(env, actor, f); // 0x47a2b0
    if (f.contact & kContactAboveGround) return;
    enterMode(actor, ShooterMode::HopAway);
    f.setHitKind(ShooterHitKind::Body);
    f.bits = uint8_t((f.bits & uint8_t(~kShooterCarried)) | kShooterActive);
}
void carriedHeld(const PlayerEnv& env, AnimatedActor& actor, ShooterFields& f) { // 0x4797d0: released -> thrown
    carryAtHands(actor, 0, 0, 0); // exe 0x573ab0
    if (actor.stepCounter != 0) {
        --f.ticks;
        if (f.ticks == 0) {
            actor.step = uint8_t(CarriedSub::Wriggle);
            actor.stepCounter = 0;
            return;
        }
    } else {
        --f.ticks;
        if (f.ticks == 0) {
            actor.anim = 0xa;
            ++actor.stepCounter;
            f.ticks = kStruggleTicks;
        }
    }
    if (carryReleased() == CarryRelease::Held) return; // exe 0x573950
    int32_t v[3];
    playerAimVector(0, -0x100, -0x400, v); // exe 0x5e3e50
    actor.velocity.x = 0;
    actor.velocity.y = int16_t(v[1]);
    actor.velocity.z = int16_t(v[2]);
    actor.acceleration.y = kLowGravity;
    actor.rotation.y = player().rotation.y;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    carryDropFromPlayer(actor); // exe 0x573990
    actor.step = uint8_t(CarriedSub::Thrown);
    f.setHitKind(ShooterHitKind::Thrown);
    carriedThrown(env, actor, f);
}
void carriedWriggle(const PlayerEnv& env, AnimatedActor& actor, ShooterFields& f) { // 0x479950: until dropped
    switch (actor.stepCounter) {
    case 0:
        actor.velocity.x = 0;
        actor.velocity.y = int16_t(0xff80);
        actor.velocity.z = 0x80;
        actor.acceleration.y = kLowGravity;
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        actor.airborne = 0;
        ++actor.stepCounter;
        f.setHitKind(ShooterHitKind::Wriggling);
        carryAtHands(actor, 0, 0, 0); // exe 0x573ab0
        return;
    case 1:
        if (carryReleased() == CarryRelease::Dropped) ++actor.stepCounter; // exe 0x573950
        return;
    case 2:
        fly(actor);
        collide(env, actor, f); // 0x47a2b0
        if (f.contact & kContactAboveGround) return;
        enterMode(actor, ShooterMode::Burrow);
        actor.stepCounter = 0;
        f.bits = uint8_t((f.bits & uint8_t(~kShooterCarried)) | kShooterActive);
        return;
    default: return;
    }
}
void carried(const PlayerEnv& env, AnimatedActor& actor, ShooterFields& f, uint32_t n) {
    switch (CarriedSub(n)) {
    case CarriedSub::Held: carriedHeld(env, actor, f); return;
    case CarriedSub::Thrown: carriedThrown(env, actor, f); return;
    case CarriedSub::Wriggle: carriedWriggle(env, actor, f); return;
    default: notPortedAt(kStoryPort, "0x4797b0 sub (table 0x890ff8 has 3 entries; the exe calls address 0)", n);
    }
}
void burrow(AnimatedActor& actor, ShooterFields& f) { // 0x479c00
    switch (actor.step) {
    case 0:
        f.bits &= uint8_t(~kShooterActive);
        actor.anim = 0xc;
        actor.step = 1;
        f.setHitKind(ShooterHitKind::Harmless);
        f.ticks = kBurrowTicks;
        return;
    case 1:
        if (gameState().frame & 4) dust(actor); // 0x47a390
        --f.ticks;
        if (f.ticks != 0) return;
        enterMode(actor, ShooterMode::Buried);
        actor.anim = 3;
        actor.setDrawn(false);
        f.setHitKind(ShooterHitKind::None);
        f.ticks = 0;
        f.bits &= uint8_t(~kShooterAnimates);
        sound(kBurrowSound, actor);
        return;
    default: return;
    }
}
void hopAway(AnimatedActor& actor, ShooterFields& f) { // 0x479cb0 ([0x479ea8] on the step, 7 entries; past them nothing)
    const uint8_t c = f.contact;
    if (c & kContactWall) f.bits |= kShooterHopAgain;
    switch (actor.step) {
    case 0: { // 0x479cdb
        const uint8_t n = uint8_t(f.hopCount + 1u);
        f.hopCount = n;
        if (n > 2) {
            actor.step = 5;
            f.hopCount = 0;
            return;
        }
        actor.rotation.y += uint16_t(faceDiff(actor) + 0x800);
        actor.velocity.z = kHopSpeed;
        actor.anim = 1;
        sound(kHopSound, actor);
        actor.acceleration.y = kHopGravity;
        actor.velocity.y = kHopLift;
        f.ticks = 3;
        actor.step = 1;
        actor.stepCounter = 0;
        f.bits |= kShooterAirborne;
        return;
    }
    case 1: // 0x479d66
        --f.ticks;
        if (f.ticks != 0) return;
        fly(actor);
        actor.step = 2;
        return;
    case 2: // 0x479d9d
        if (!(c & kContactAboveGround)) {
            actor.step = 0;
            f.bits &= uint8_t(~kShooterAirborne);
            return;
        }
        if (actor.stepCounter == 0 && actor.velocity.y > 0) {
            actor.anim = 2;
            actor.step = 3;
        }
        fly(actor);
        return;
    case 3: // 0x479ded
        if (c & kContactAboveGround) {
            fly(actor);
            return;
        }
        actor.step = 4;
        actor.anim = 0xd;
        actor.velocity.z = kSkidSpeed;
        f.bits &= uint8_t(~kShooterAirborne);
        return;
    case 4: // 0x479e12
        if (!actor.animEnded()) {
            slide(actor);
            return;
        }
        if (f.bits & kShooterHopAgain) {
            f.bits &= uint8_t(~kShooterHopAgain);
            actor.step = 5;
            return;
        }
        actor.step = 0;
        return;
    case 5: { // 0x479e47: turn toward the player over 0x10 ticks
        const int d = faceDiff(actor);
        f.ticks = kTurnBackTicks;
        f.turnStep = int16_t(d >> 4);
        actor.anim = 0xe;
        actor.step = 6;
        return;
    }
    case 6: // 0x479e81
        --f.ticks;
        if (f.ticks == 0) {
            enterMode(actor, ShooterMode::Hop);
            return;
        }
        actor.rotation.y += uint16_t(f.turnStep);
        return;
    default: return;
    }
}
// the knock of a hurt: up and back, the hurt sound, the flash; harmless while it lasts
void knocked(AnimatedActor& actor, ShooterFields& f, uint8_t anim) {
    actor.anim = anim;
    sound(kHurtSound, actor);
    actor.velocity.y = kHurtLift;
    actor.velocity.z = kHurtPush;
    actor.acceleration.y = kLowGravity;
    f.bits &= uint8_t(~kShooterActive);
}
void hurtOnGround(AnimatedActor& actor, ShooterFields& f) { // 0x479a20 ([0x479b24] on the step, 4 entries)
    switch (actor.step) {
    case 0:
        actor.anim = 5;
        sound(kHurtSound, actor);
        ++actor.step;
        actor.velocity.y = kHurtLift;
        actor.velocity.z = kHurtPush;
        actor.acceleration.y = kLowGravity;
        f.bits &= uint8_t(~kShooterActive);
        actor.tint = kTintFlash;
        f.setHitKind(ShooterHitKind::Harmless);
        return;
    case 1:
        fly(actor);
        if (actor.animEnded()) ++actor.step;
        return;
    case 2:
        fly(actor);
        if (f.contact & kContactAboveGround) return;
        actor.anim = 6;
        ++actor.step;
        return;
    case 3:
        if (!actor.animEnded()) return;
        enterMode(actor, ShooterMode::Burrow);
        actor.stepCounter = 0;
        f.bits = uint8_t((f.bits & uint8_t(~kShooterCarried)) | kShooterActive);
        return;
    default: return;
    }
}
void hurtInAir(AnimatedActor& actor, ShooterFields& f) { // 0x479b40
    switch (actor.step) {
    case 0:
        knocked(actor, f, 6);
        ++actor.step;
        actor.tint = kTintFlash;
        f.setHitKind(ShooterHitKind::Harmless);
        return;
    case 1:
        fly(actor);
        if (f.contact & kContactAboveGround) return;
        enterMode(actor, ShooterMode::Burrow);
        actor.stepCounter = 0;
        f.bits = uint8_t((f.bits & uint8_t(~kShooterAirborne)) | kShooterActive);
        return;
    default: return;
    }
}
void drops(const AnimatedActor& actor) { // exe 0x5f1270(+0x10, the row's drop table)
    spawnDrops(actor.pos, shooterRow(actor, "0x479130 drops: no type row, type").dropTable);
}
void deadOnGround(AnimatedActor& actor, ShooterFields& f) { // 0x479ed0
    switch (actor.step) {
    case 0:
        countDefeat(); // exe 0x5e53e0
        blast(actor);  // 0x47a480
        actor.anim = 7;
        f.setHitKind(ShooterHitKind::Harmless);
        actor.step = 1;
        return;
    case 1:
        if (!actor.animEnded()) return;
        actor.anim = 8;
        actor.step = 2;
        f.ticks = kDeadTicks;
        return;
    case 2: {
        const uint8_t t = uint8_t(f.ticks - 1u);
        f.ticks = t;
        if (t == kGoneTicks) {
            f.setHitKind(ShooterHitKind::None);
            return;
        }
        if (int8_t(t) > 0) return;
        drops(actor);
        actor.state = uint8_t(ShooterState::Free);
        actor.subState = 0;
        actor.step = 0;
        f.ticks = 0;
        return;
    }
    default: return;
    }
}
void deadThrown(AnimatedActor& actor, ShooterFields& f) { // 0x479fa0
    switch (actor.step) {
    case 0:
        countDefeat(); // exe 0x5e53e0
        actor.setDrawn(false);
        blast(actor); // 0x47a480
        drops(actor);
        f.ticks = kGoneTicks;
        ++actor.step;
        f.setHitKind(ShooterHitKind::Harmless);
        return;
    case 1:
        --f.ticks;
        if (f.ticks != 0) return;
        actor.state = uint8_t(ShooterState::Free);
        actor.subState = 0;
        actor.step = 0;
        return;
    default: return;
    }
}
void runMode(const PlayerEnv& env, AnimatedActor& actor, ShooterFields& f, uint32_t n) { // [0x890fd0 + n * 4]
    switch (ShooterMode(n)) {
    case ShooterMode::Buried: buried(actor, f); return;
    case ShooterMode::Hop: hop(actor, f); return;
    case ShooterMode::Shoot: shoot(actor, f); return;
    case ShooterMode::Carried: carried(env, actor, f, actor.step); return; // 0x4797b0: [0x890ff8 + step * 4]
    case ShooterMode::Burrow: burrow(actor, f); return;
    case ShooterMode::HopAway: hopAway(actor, f); return;
    case ShooterMode::HurtOnGround: hurtOnGround(actor, f); return;
    case ShooterMode::HurtInAir: hurtInAir(actor, f); return;
    case ShooterMode::DeadOnGround: deadOnGround(actor, f); return;
    case ShooterMode::DeadThrown: deadThrown(actor, f); return;
    default:
        if (n >= uint32_t(ShooterMode::FirstCarriedSub) && n <= uint32_t(ShooterMode::LastCarriedSub)) {
            carried(env, actor, f, n - uint32_t(ShooterMode::FirstCarriedSub)); // the table runs into [0x890ff8]
            return;
        }
        notPortedAt(kStoryPort, "0x479130 mode (table 0x890fd0; entry 13 is 0, the exe calls address 0)", n);
    }
}

// ==== the shooter's states ============================================================================================
void shooterInit(AnimatedActor& actor, ShooterFields& f) { // 0x479210
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);            // exe 0x5e1490
        return;
    }
    actor.setDrawn(false);
    actor.anim = 3;
    actor.prevAnim = 0xff;
    const int16_t hp =
        hasAnimatedStats(actor.type) ? int16_t(animatedStats(actor.type, actor.spawnByte).hitPoints) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.velocity.setXY(0, 0);
    actor.velocity.setZSpare(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.runFlags |= kActorWarpKept;
    actor.collisionBox.pointAt(kShooterBox.stateAddress());
    actor.interactionBox.pointAt(TableId::IceRuinBurrowingShooterInteraction);
    actor.interaction = kInteractLift;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    f.clearAll(); // exe 0x40dc40
    f.bits = uint8_t((f.bits & kShooterReflects) | kShooterTakesDamage | kShooterUntracedBit | kShooterAirborne);
    f.playerDistance = planarDistance(actor.pos, player().pos); // exe 0x5e3b00
    f.hopCount = 0;
    f.setHitKind(ShooterHitKind::None);
    f.hopSide = 1;
    actor.state = uint8_t(ShooterState::Main);
}
void shooterMain(const PlayerEnv& env, AnimatedActor& actor, ShooterFields& f) { // 0x4792f0
    if (f.bits & kShooterTakesDamage) takeDamage(actor, f); // 0x47a190
    runMode(env, actor, f, actor.subState);
    if (f.bits & (kShooterDead | kShooterCarried)) return;
    if (playerLiftTargetCheck(actor, 0)) { // exe 0x573a60
        actor.subState = uint8_t(ShooterMode::Carried);
        actor.step = 0;
        actor.stepCounter = 0;
        actor.anim = 9;
        f.ticks = kHoldTicks;
        f.setHitKind(ShooterHitKind::None);
        f.bits = uint8_t((f.bits & uint8_t(~kShooterActive)) | kShooterCarried);
    }
    collide(env, actor, f); // 0x47a2b0
}
void burrowingShooter(const PlayerEnv& env, AnimatedActor& actor) { // 0x479130
    ShooterFields& f = classState<ShooterFields>(actor);
    actor.prevAnim = actor.anim;
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    const uint32_t st = actor.state;
    switch (ShooterState(st)) {
    case ShooterState::Init: shooterInit(actor, f); break;
    case ShooterState::Main: shooterMain(env, actor, f); break;
    case ShooterState::Free: freeActor(actor); break; // 0x468fa0 -> exe 0x5e1490
    default:
        if (st > uint32_t(ShooterState::LastDirect))
            notPortedAt(kStoryPort, "0x479130 state (table 0x890fc4; entry 16 is 0, the exe calls address 0)", st);
        runMode(env, actor, f, st - uint32_t(ShooterState::FirstDirectMode)); // the modes, then the carried subs
        break;
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim != actor.prevAnim) animSetPose(actor, actor.anim, 0); // exe 0x5e1c30(a, anim, 0)
    else if (f.bits & kShooterAnimates) animStepPose(actor);            // exe 0x5e1e80
    hitEntry(actor, f);                                                  // 0x47a030
    f.playerDistance = planarDistance(actor.pos, player().pos);          // exe 0x5e3b00
}

} // namespace

void burrowingShooterUpdate(const PlayerEnv& env, uint32_t actor) { burrowingShooter(env, animatedActorAt(actor)); }
void chargedShotUpdate(const PlayerEnv& env, uint32_t actor) { chargedShot(env, projectileActorAt(actor)); }

} // namespace od::port
