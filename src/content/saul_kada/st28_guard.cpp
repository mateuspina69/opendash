// The fire ruin's guard (ST28 pool 0x20 type 0x6c sub 0, exe 0x4f1870; hypothesis names): a reaverbot guarding its spawn
// point. States [0x896b8c]: 0 bind 0x4f1aa0 (the hit points of its row x 16, two glow trails on its head), 1 run 0x4f1c00
// (its sub-state [0x896ba8], snapped to the ground, its damage unless dying), 2 death 0x4f21b0 (flashing, an
// explosion, the row's drops), 3 freed. Sub-states: 0 wait 0x4f1c40 (the player within 0x708 of its home -> charge,
// within 0x1964 -> watch), 1 watch 0x4f1d20 (0x23 ticks turning its head to him), 2 shoot 0x4f1da0 (a torpedo -- or,
// the player 0x600..0xe00 away and CRT rand() & 0xff >= 0x55, a lobbed shot -- from between bones 3 / 4, with a
// spark), 3 / 4 charge 0x4f2060 / rush 0x4f2120 (at him at 0x140 / 0x230 while he is within 0x708 of its home; the
// rush once nearer than 0x3e8). Every tick its head's yaw eases (at most 0x20) toward its target and turns the pose's
// head. Its animations: the byte list [0x896b84 + variant * 4]. The shadow is render output.
#include "content/saul_kada/st28_guard.hpp"
#include "content/saul_kada/st28_guard_state.hpp"

#include "content/saul_kada/st28_guard_shot.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/vectors.hpp"
#include "game/global_class_types.hpp"
#include "game/glow_trails.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st28_guard_state;

namespace {

enum class GuardState : uint8_t { Bind = 0, Run = 1, Death = 2, Free = 3 };
enum class GuardSub : uint8_t { Wait = 0, Watch = 1, Shoot = 2, Charge = 3, Rush = 4 };
// the entries of its animation list
enum class GuardAnim : uint8_t { Idle = 0, Charge = 9, Rush = 0xa, Fall = 7, Down = 8 };
constexpr uint8_t kGuardNoHitEntries = 0x01, kGuardNoDamage = 0x02, kGuardRushing = 0x20, kGuardDeathMark = 0x80;
// the hits whose damage counts: the player's, thrown, strong attacks and any compared height (the others: none)
constexpr uint32_t kHurtBy = kHitLevelAbove | kHitLevelWithin | kHitLevelBelow | kHitAttackByPlayer | kHitAttackThrown |
                             kHitAttackStrong;
constexpr uint8_t kGlowSize = 0xc, kGlowMode = 7;
constexpr uint16_t kGlowBone = 2;
constexpr uint8_t kSparkVariant = 3;
constexpr uint16_t kTorpedoSound = 0x20b, kLobSound = 0x20a, kChargeStepSound = 0x248, kRushSound = 0x249;
constexpr uint8_t kTorpedoType = 0x1e;
constexpr int16_t kTorpedoAnim = 0, kLobAnim = 3;
// the shot's launch: who fired it
// the shot's shooter (its identity word, the hit box's first word)
struct ShotLaunch {
    explicit ShotLaunch(const ProjectileActor& actor) : ShotLaunch(const_cast<ProjectileActor&>(actor)) {}
    explicit ShotLaunch(ProjectileActor& actor) : shooter(actor.hitBox.result) {}
    uint32_t& shooter;
};

AnimatedActorStats guardStats(const AnimatedActor& actor, const char* who) {
    if (!hasAnimatedStats(actor.type)) notPorted(kStoryPort, who);
    return animatedStats(actor.type, actor.variant);
}
// the animation k of its variant's list
uint8_t listAnim(const AnimatedActor& actor, uint32_t k) {
    const uint32_t list = dataScalar<uint32_t>(stateAddress(TableId::St28GuardAnimLists) + uint32_t(actor.variant) * 4);
    return dataScalar<uint8_t>(list + k);
}
void setAnim(AnimatedActor& actor, uint32_t k) { actor.anim = listAnim(actor, k); }
void enterSub(AnimatedActor& actor, GuardSub sub) {
    actor.subState = uint8_t(sub);
    actor.restartStep();
}
uint16_t angleToPlayer(const AnimatedActor& actor) { return uint16_t(angleOf(player().pos, actor.pos)); }
int playerDistance(const AnimatedActor& actor) { return planarDistance(actor.pos, player().pos); }
// exe 0x4f1cd0: the player within 0x708 of its home
bool playerNearHome(const GuardFields& guard) {
    const int32_t home[3] = {int32_t(uint32_t(uint16_t(guard.homeX)) << 16), 0,
                             int32_t(uint32_t(uint16_t(guard.homeZ)) << 16)};
    return planarDistance(player().pos, home) < 0x708;
}
// exe 0x4f2350: a hit; its damage counts only from the attacks kHurtBy (then flashed); dead -> its death
void takeHit(AnimatedActor& actor, GuardFields& guard) {
    const uint32_t hit = guard.hitBox.result;
    const uint32_t damage = hit & kHitDamageMask;
    if (!damage) return;
    int amount = 0;
    if (hit & kHurtBy) {
        actor.tint = kTintFlash;
        amount = int(damage);
    }
    if (hitPointsAdd(actor, -amount)) actor.enterState(uint8_t(GuardState::Death)); // exe 0x5e52f0
}

// ---- the sub-states [0x896ba8] -------------------------------------------------------------------------------------
// exe 0x4f1c40: waiting
void wait(AnimatedActor& actor, GuardFields& guard) {
    if (actor.step == 0) {
        guard.flags = 0;
        ++actor.step;
        setAnim(actor, uint8_t(GuardAnim::Idle));
        guard.headTarget = angleToPlayer(actor);
    }
    if (actor.variant == 0 && playerNearHome(guard)) {
        enterSub(actor, GuardSub::Charge);
        return;
    }
    if (playerDistance(actor) < 0x1964) enterSub(actor, GuardSub::Watch);
}
// exe 0x4f1d20: watching (0x23 ticks)
void watch(AnimatedActor& actor, GuardFields& guard) {
    switch (actor.step) {
    case 0:
        guard.flags = 0;
        ++actor.step;
        setAnim(actor, uint8_t(GuardAnim::Idle));
        guard.timer = 0x23;
        [[fallthrough]];
    case 1:
        guard.headTarget = angleToPlayer(actor);
        guard.timer -= 1u;
        if (guard.timer == 0) enterSub(actor, GuardSub::Shoot);
        break;
    default: break;
    }
}
// the shoot's step 1: the shot (its type) between bones 3 and 4, its sound, a spark
void fireShot(const AnimatedActor& actor, const GuardFields& guard) {
    const ShortVec4& p3 = bonePoint(actor, 3, "0x4f1da0: bone 3 past its blocks"); // exe 0x5e1810
    const ShortVec4& p4 = bonePoint(actor, 4, "0x4f1da0: bone 4 past its blocks");
    const int16_t mid[3] = {int16_t(uint16_t((int(p4.x) + int(p3.x)) >> 1)),
                            int16_t(uint16_t((int(p4.y) + int(p3.y)) >> 1)),
                            int16_t(uint16_t((int(p4.z) + int(p3.z)) >> 1))};
    const uint32_t slot = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!slot) notPorted(kStoryPort, "0x4f1da0: pool 0x40 full (the exe plays the sound at address 0x10)");
    ProjectileActor& shot = projectileActorAt(slot);
    shot.flags = kActorLive;
    shot.loopFlags |= kActorSweepGroup2;
    shot.type = guard.shotType;
    shot.subtype = 0;
    shot.variant = 0;
    shot.pos.x.whole = mid[0];
    shot.pos.y.whole = mid[1];
    shot.rotation.x = 0;
    shot.pos.z.whole = mid[2];
    shot.rotation.z = 0;
    ShotLaunch(projectileActorAt(slot)).shooter = slotIdentityWord(actor);
    shot.rotation.y = int16_t(uint16_t(uint16_t(actor.rotation.y) + guard.headYaw));
    soundPlayAtPos(guard.shotSound, shot.pos); // exe 0x5d5380
    const uint32_t sparkSlot = allocActorA0(0); // exe 0x5e13b0
    if (!sparkSlot) return;
    EffectActor& spark = effectActorAt(sparkSlot);
    spark.type = typeByte(GlobalEffectType::HitSpark);
    spark.flags = kActorLive;
    spark.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    spark.variant = kSparkVariant;
    spark.pos.x = mid[0];
    spark.pos.y = mid[1];
    spark.pos.z = mid[2];
    classState<HitSparkFields>(effectActorAt(sparkSlot)).size = uint16_t((rngStep() & 0xf) + 0xa0);
}
// the shot's next animation
void nextShotAnim(AnimatedActor& actor, GuardFields& guard) {
    setAnim(actor, dataScalar<uint8_t>(stateAddress(TableId::St28GuardShotAnims) + uint32_t(int32_t(guard.shotAnim))));
    guard.shotAnim += 1u;
}
// exe 0x4f1da0: shooting
void shoot(AnimatedActor& actor, GuardFields& guard) {
    switch (actor.step) {
    case 0: {
        guard.flags = 0;
        ++actor.step;
        const int d = playerDistance(actor);
        if (d <= 0xe00 && d >= 0x600 && (crtRand() & 0xff) >= 0x55) { // CRT rand() 0x611443
            guard.shotAnim = kLobAnim;
            guard.shotSound = kLobSound;
            guard.shotType = kGuardShotType;
        } else {
            guard.shotAnim = kTorpedoAnim;
            guard.shotSound = kTorpedoSound;
            guard.shotType = kTorpedoType;
        }
        nextShotAnim(actor, guard);
        guard.timer = 0xa;
        break;
    }
    case 1:
        guard.timer -= 1u;
        if (guard.timer != 0) break;
        ++actor.step;
        fireShot(actor, guard);
        break;
    case 2:
    case 3:
        if (!actor.animEnded()) break;
        ++actor.step;
        nextShotAnim(actor, guard);
        break;
    case 4:
        if (actor.animEnded()) enterSub(actor, GuardSub::Wait);
        break;
    default: break;
    }
}
// exe 0x4f2060 / 0x4f2120: at the player (its animation, its flags on entry, its speed); its head straight
void runAt(AnimatedActor& actor, GuardFields& guard, GuardAnim anim, uint8_t flags, int speed) {
    if (actor.step == 0) {
        guard.flags = flags;
        ++actor.step;
        setAnim(actor, uint8_t(anim));
    }
    turnYawTo(actor.rotation.y, angleToPlayer(actor), 0x20); // exe 0x5e3df0
    integrateVelocity(actor, 0, 0, speed);                   // exe 0x5e3690
    guard.headTarget = uint16_t(actor.rotation.y);
}
// exe 0x4f2060: charging
void charge(AnimatedActor& actor, GuardFields& guard) {
    runAt(actor, guard, GuardAnim::Charge, 0, -0x140);
    if (!playerNearHome(guard)) {
        enterSub(actor, GuardSub::Wait);
        return;
    }
    if (playerDistance(actor) < 0x3e8) enterSub(actor, GuardSub::Rush);
    const int8_t frame = actor.animKey.frame;
    if (frame == 9 || frame == 0x17) soundPlayAtPos(kChargeStepSound, actor.pos);
}
// exe 0x4f2120: rushing
void rush(AnimatedActor& actor, GuardFields& guard) {
    runAt(actor, guard, GuardAnim::Rush, kGuardRushing, -0x230);
    if (!playerNearHome(guard)) enterSub(actor, GuardSub::Wait);
    if (!(uint8_t(actor.animKey.frame) & 3)) soundPlayAtPos(kRushSound, actor.pos);
}
// exe 0x4f1aa0: its model, on the ground, the hit points of its row x 16, its box, its home, two glow trails
void bind(const PlayerEnv& env, AnimatedActor& actor, GuardFields& guard) {
    if (!bindActorModel(actor, kAnyModelVariant)) { // exe 0x5e0bc0(actor, 0)
        freeActor(actor);                           // exe 0x5e1490
        return;
    }
    actor.prevAnim = 0xff;
    setAnim(actor, uint8_t(GuardAnim::Idle));
    actor.pos.y.whole = int16_t(actorGround(env, actor)); // exe 0x40f190
    const uint16_t hp =
        hasAnimatedStats(actor.type) ? uint16_t(animatedStats(actor.type, actor.variant).hitPoints << 4) : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(TableId::St28GuardBox);
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.cullWidth = 0x400;
    actor.cullHeight = 0x2000;
    actor.clearClassBlock(); // exe 0x40dc40
    guard.hitBox.previousX = actor.pos.x.whole;
    guard.hitBox.previousZ = actor.pos.z.whole;
    guard.hitBox.result = 0;
    guard.homeX = actor.pos.x.whole;
    guard.homeZ = actor.pos.z.whole;
    if (actor.variant == 1) {
        actor.scale.x = 0x100;
        actor.scale.y = 0x100;
        actor.scale.z = 0x100;
    }
    for (uint32_t k = 0; k < 2; ++k) {
        const ShortVec3 point = dataShortVec3(stateAddress(TableId::St28GuardGlowPoints) + k * 6);
        const uint32_t slot = allocActorA0(kActorStageClass); // exe 0x5e13a0
        if (!slot) notPorted(kStoryPort, "0x4f1aa0: effect pool full (the exe retries forever)");
        EffectActor& glow = effectActorAt(slot);
        GlowTrailFields& trail = classState<GlowTrailFields>(effectActorAt(slot));
        glow.flags = kActorLiveDrawn;
        glow.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
        glow.type = kGlowTrailStageType;
        glow.variant = uint8_t(GlowTrailVariant::Ring);
        trail.size = kGlowSize;
        trail.mode = kGlowMode;
        trail.parent = &actor;
        glow.pos.x = point.x;
        glow.pos.y = point.y;
        glow.pos.z = point.z;
        trail.bone = kGlowBone;
    }
    actor.enterState(uint8_t(GuardState::Run));
}
// exe 0x4f1c00
void run(const PlayerEnv& env, AnimatedActor& actor, GuardFields& guard) {
    actor.tint = kTintNeutral;
    switch (GuardSub(actor.subState)) { // [0x896ba8]
    case GuardSub::Wait: wait(actor, guard); break;
    case GuardSub::Watch: watch(actor, guard); break;
    case GuardSub::Shoot: shoot(actor, guard); break;
    case GuardSub::Charge: charge(actor, guard); break;
    case GuardSub::Rush: rush(actor, guard); break;
    default: notPortedAt(kStoryPort, "0x4f1870 sub-state past [0x896ba8]", actor.subState);
    }
    actor.pos.y.whole = int16_t(actorGround(env, actor)); // exe 0x40f190
    if (!(guard.flags & kGuardNoDamage)) takeHit(actor, guard);
}
// the death's flashing: white / grey by the timer's parity; true when the timer runs out
bool deathFlash(AnimatedActor& actor, GuardFields& guard) {
    actor.tint = kTintFlash;
    if (guard.timer & 1) actor.tint = 0x4214;
    guard.timer -= 1u;
    return guard.timer == 0;
}
// exe 0x4f21b0: its death (its steps in the sub-state byte)
void death(AnimatedActor& actor, GuardFields& guard) {
    actor.tint = kTintNeutral;
    switch (actor.subState) {
    case 0:
        guard.flags = kGuardNoDamage;
        ++actor.subState;
        setAnim(actor, uint8_t(GuardAnim::Fall));
        break;
    case 1:
        if (!actor.animEnded()) break;
        ++actor.subState;
        guard.timer = 0x14;
        break;
    case 2: {
        if (!deathFlash(actor, guard)) break;
        ++actor.subState;
        setAnim(actor, uint8_t(GuardAnim::Down));
        guard.flags &= uint8_t(~kGuardDeathMark);
        guard.timer = 0x1e;
        const uint32_t slot = allocActor60(0); // exe 0x5e1300
        if (!slot) break;
        StaticActor& explosion = staticActorAt(slot);
        explosion.flags = kActorLive;
        explosion.loopFlags |= kActorSweepGroup2;
        explosion.type = typeByte(GlobalStaticType::Explosion);
        explosion.variant = 0;
        explosion.pos.x.whole = actor.pos.x.whole;
        explosion.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) - 0x60u);
        explosion.pos.z.whole = actor.pos.z.whole;
        ExplosionParams params(actorHeaderAt(slot));
        params.spread = 0xc0;
        params.flashes = 0x1e;
        break;
    }
    case 3:
        if (!deathFlash(actor, guard)) break;
        spawnDrops(actor.pos, guardStats(actor, "0x4f21b0: null type row (the exe faults)").dropTable); // 0x5f1270
        countDefeat(); // exe 0x5e53e0
        freeActor(actor);
        break;
    default: break;
    }
}
// its hit entries: a capsule along its head (bone 2), a sphere at its feet, a sphere at bone 2
void submitHits(const AnimatedActor& actor, GuardFields& guard) {
    const AnimatedActorStats stats = guardStats(actor, "0x4f1870: null type row (the exe faults)");
    int16_t front[3], back[3];
    const int16_t frontOffset[3] = {0, -0x30, 0x160}, backOffset[3] = {0, -0x18, -0x220};
    bonePointOffset(actor, 2, frontOffset, front); // exe 0x5e1970
    bonePointOffset(actor, 2, backOffset, back);
    const uint16_t radius = uint16_t(stats.hitSize >> 2);
    hitListSubmitCapsule(front, radius, back, radius, guard.hitBox,
                         hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | kHitNoLockOn | hitCapsule(3),
                         kHitAttackSolid | 0x1e); // exe 0x5e4190
    const int16_t feet[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    // the row's +8 as the sphere's radius / 4 ... << 2
    const uint32_t feetShape = hitOwner(actor.slot) | hitHeightLevel(stats.hitHeight) |
                               (uint32_t(stats.bodyAttack) << 2) | hitGroup(kHitGroupBodies) |
                               kHitCompareHeights | kHitNoLockOn;
    hitListSubmit(feet, guard.hitBox, feetShape, uint32_t(stats.attack) | kHitAttackSolid); // exe 0x5e4120
    const ShortVec4& head = bonePoint(actor, 2, "0x4f1870: bone 2 past its blocks"); // exe 0x5e1810
    const int16_t headPoint[3] = {head.x, head.y, head.z};
    hitListSubmit(headPoint, guard.hitBox, hitOwner(actor.slot) | hitGroup(kHitGroupBodies) | hitRadius(0x14),
                  kHitAttackSolid);
}
void guardUpdate(const PlayerEnv& env, AnimatedActor& actor) { // exe 0x4f1870
    GuardFields& guard = classState<GuardFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (GuardState(actor.state)) { // [0x896b8c]
    case GuardState::Bind: bind(env, actor, guard); break;
    case GuardState::Run: run(env, actor, guard); break;
    case GuardState::Death: death(actor, guard); break;
    case GuardState::Free: freeActor(actor); break; // 0x468fa0
    default: notPortedAt(kStoryPort, "0x4f1870 state past [0x896b8c]", actor.state);
    }
    // its head's yaw eases toward its target by at most 0x20
    int d = angleDiff(uint32_t(int(actor.rotation.y) + int(guard.headYaw)), guard.headTarget);
    if (d > 0x20) d = 0x20;
    if (d < -0x20) d = -0x20;
    guard.headYaw = uint16_t(guard.headYaw + uint32_t(d));
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStep(actor); // exe 0x5e1ea0
    else animSet(actor, actor.anim, 0);               // exe 0x5e1c50
    LaneVec4 head = decodedFrame().entries[2];       // part 1's angles: its head
    head.y = int16_t(uint16_t(guard.headYaw + uint16_t(head.y)));
    animPose(actor); // exe 0x5e2d90
    guard.hitBox.result = 0;
    guard.hitBox.previousX = actor.prevPos.x.whole;
    guard.hitBox.previousZ = actor.prevPos.z.whole;
    if (!(guard.flags & kGuardNoHitEntries)) submitHits(actor, guard);
    // drawn: the shadow (exe 0x5f20f0(actor, 0x180, 0x100, 0)) -- render output
}

} // namespace

void st28GuardUpdate(const PlayerEnv& env, uint32_t actor) { guardUpdate(env, animatedActorAt(actor)); }

} // namespace od::port
