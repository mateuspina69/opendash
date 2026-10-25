// The gauge reaverbots of the Nino ruin (pool 0x20 type 0x76), one routine per exe routine; the names are hypotheses
// from the observed behaviour.
//   0x4fb670  sub 0: the reaverbot (states [0x897a3c]: 0x4fb7c0 init, 0x4fb960 swimming (subs [0x897a50]), 0x4fc100
//             walking (subs [0x897a68]), 0x4fc8b0 standing (subs [0x897a78]), 0x4fccc0 down; 0x4fcfc0 the hit check,
//             0x4fd050 / 0x4fd120 the collision, 0x4fcf90 / 0x4fcc90 the group counts). ST37 sub 6 spawns three (the
//             encounter group 4), their gauges coloured by their second parameter; the last one down with its variant
//             set drops the key (content/nino/pedestal_item).
//   0x4fd180  sub 1: the floating mine it lays (states [0x897ab4]: 0x4fd280 init, 0x4fd360 bob, 0x4fd410 the blast)
//   0x4fd600  sub 2: script 0x5f's cast (by its first parameter [0x897ae0]: 0x4fd6d0 the swimmer, 0x4fd8e0 the one
//             standing), cued by the script's state and step
// Their shot: content/nino/homing_shot. Render output left out as in the other ports: the shadows (exe 0x5f20f0).
#include "content/nino/gauge_reaverbots.hpp"
#include "content/nino/gauge_reaverbots_state.hpp"
#include "content/nino/homing_shot.hpp"
#include "content/nino/pedestal_item.hpp"

#include <cstdlib>

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/collision.hpp"
#include "game/distances.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/explosions.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/territory.hpp"

namespace od::port {

using namespace gauge_reaverbots_state;

namespace {

int16_t t16(int v) { return int16_t(uint16_t(uint32_t(v))); }

constexpr uint8_t kGaugeType = 0x76;
enum class GaugeSubtype : uint8_t { Reaverbot = 0, Mine = 1, Cast = 2 };
// The spawn parameters of the type: the others of the group still up (every one down counts the type's actors down),
// the gauge's colour, the angle of the point it circles around the player
struct GaugeParams {
    explicit GaugeParams(const AnimatedActor& actor) : GaugeParams(const_cast<AnimatedActor&>(actor)) {}
    explicit GaugeParams(AnimatedActor& actor) : othersLeft(actor.params[0]), gaugeColour(actor.params[1]), circleAngle(actor.params, 2) {}
    FieldAlias<int8_t, uint8_t> othersLeft;
    FieldAlias<int8_t, uint8_t> gaugeColour;
    BytesField<uint16_t, 4> circleAngle;
};
// The stats row of the type (by its spawn byte): the mines' blast and the spread shots' damage
struct GaugeStats : AnimatedActorStats {
    static constexpr uint32_t kBytes = 0xd;
    uint8_t mineBlastSize = 0; // +0x8: x 4
    uint8_t mineBlastAttack = 0; // +0x9
    uint8_t spreadShotAttack = 0; // +0xa
    template <class R> static GaugeStats load(const R& r) {
        GaugeStats v;
        static_cast<AnimatedActorStats&>(v) = AnimatedActorStats::load(r);
        v.mineBlastSize = r.template read<uint8_t>(0x8);
        v.mineBlastAttack = r.template read<uint8_t>(0x9);
        v.spreadShotAttack = r.template read<uint8_t>(0xa);
        return v;
    }
};
GaugeStats gaugeStats(const AnimatedActor& actor) { return dataRecord<GaugeStats>(animatedStatRow(actor.type, actor.spawnByte), GaugeStats::kBytes); }

// the contact bits of the moves (the walls on x / z, the floor)
constexpr uint8_t kTouchedXWall = 0x01, kTouchedZWall = 0x02, kTouchedFloor = 0x04;
constexpr uint8_t kTouchedWalls = kTouchedXWall | kTouchedZWall;
// the mode bits
constexpr uint8_t kGaugeNoDamage = 0x02;  // the hit check skipped (a hit taken plays)
constexpr uint8_t kGaugePushed = 0x04;    // bumped by a body
constexpr uint8_t kGaugeNoWallTurn = 0x08; // a wall does not start the spin back
constexpr uint8_t kGaugeShotsOnly = 0x10; // only a strong push hurts it; its body deflects the player's shots
constexpr uint8_t kGaugeCharging = 0x20;  // the dash: its body is a thrown attack
constexpr uint8_t kGaugeReflects = 0x40;  // the walls reflect its yaw (no sub-state sets it)
constexpr uint8_t kGaugeDying = 0x80;     // no lock-on any more
constexpr uint32_t kHitSlows = 0x4000;    // the attack bit that slows the player (game/player/player_damage)
constexpr uint8_t kSemiAdditive = 0x22;   // exe 0x5e1a00(actor, 1, 1): drawn semi-transparent, additive
constexpr uint16_t kHurtSound = 0x8d;

// The shared moves of the type (a contact byte and its mode byte)
// exe 0x4fd120 / 0x4fd5a0: the actor collision pass's result into the contact bits (a reflecting mode: the yaw reflected
// off the walls, a floor when it turned)
void contactFromBodies(Actor& actor, uint8_t& contact, uint8_t mode) {
    const uint32_t r = actor.actorHit;
    if (r == 0) return;
    if (r & (kWallOnXPlus | kWallOnXMinus)) contact |= kTouchedXWall;
    if (r & (kWallOnZPlus | kWallOnZMinus)) contact |= kTouchedZWall;
    if (mode & kGaugeReflects) {
        if (reflectRotationOffWalls(actor.rotation, r)) contact |= kTouchedFloor; // exe 0x410740
        return;
    }
    if (r & kWallOnY) contact |= kTouchedFloor;
}
// exe 0x4fd050 / 0x4fd4d0: the move against the room (pushed out, the position back when that fails) or the territory;
// the contact bits from scratch; then a mover of the actor collision pass
void collide(const PlayerEnv& env, Actor& actor, uint8_t& contact, const uint8_t& mode) {
    contact = 0;
    if (int32_t(actorTestMove(env, actor)) < 0) {   // exe 0x40f570
        const uint32_t r = actorPushOut(env, actor); // exe 0x410960
        if (int32_t(r) < 0) actor.pos = actor.prevPos;
        if (r & (kWallOnXPlus | kWallOnXMinus)) contact |= kTouchedXWall;
        if (r & (kWallOnZPlus | kWallOnZMinus)) contact |= kTouchedZWall;
        if ((mode & kGaugeReflects) && reflectRotationOffWalls(actor.rotation, r)) contact |= kTouchedFloor;
    } else if (const int rect = territoryViolated(actor.pos)) { // exe 0x5e6ab0
        const uint32_t r = territoryPushBack(actor, rect);        // exe 0x5e6b40
        if (r & (kWallOnXPlus | kWallOnXMinus)) contact |= kTouchedXWall;
        if (r & (kWallOnZPlus | kWallOnZMinus)) contact |= kTouchedZWall;
    }
    actorCollisionSubmitMover(actor);
}
// the angle of the actor seen from the player / its distance
int angleFromPlayer(const Actor& actor) { return angleOf(player().pos, actor.pos); } // exe 0x5e3bc0(player, actor)
// exe 0x5e3bc0 from a point whose 16.16 fractions the exe leaves unset (taken as 0): the actor seen from (x, z)
int angleFromPoint(int16_t x, int16_t z, const Actor& actor) {
    return angleOf(int32_t(uint32_t(uint16_t(x)) << 16), int32_t(uint32_t(uint16_t(z)) << 16), actor.pos.x, actor.pos.z);
}
void wholePos(const Actor& actor, int16_t out[3]) {
    out[0] = actor.pos.x.whole;
    out[1] = actor.pos.y.whole;
    out[2] = actor.pos.z.whole;
}
// the model of the variant the spawn byte picks (0 -> 0, 1 / 2 -> 1; any other: the exe passes its argument slot, the
// actor's own address)
uint8_t modelVariant(const AnimatedActor& actor) {
    const uint8_t b = actor.spawnByte;
    if (b == 0) return 0;
    if (b == 1 || b == 2) return 1;
    return uint8_t(slotIdentityWord(actor));
}
// the grey-blue glow of the type: the tint 0x4200 | level / 2
uint16_t glowTint(uint8_t level) { return uint16_t(0x4200u | uint32_t(level >> 1)); }

// ==== 0x4fb670: the reaverbot =========================================================================================
enum class GaugeState : uint8_t { Init = 0, Swim = 1, Walk = 2, Stand = 3, Down = 4 };
enum class SwimSub : uint8_t { Circle = 0, Dash = 1, Shoot = 2, KeepAway = 3, Hurt = 4, SpinBack = 5 };
enum class WalkSub : uint8_t { Circle = 0, Dash = 1, Burst = 2, SpinBack = 3 };
enum class StandSub : uint8_t { Wait = 0, Volley = 1, Hurt = 2, HurtPastTable = 3 };
enum class DashPhase : uint8_t { Start = 0, Aim = 1, Charge = 2, Recover = 3 };
enum class ShootPhase : uint8_t { Start = 0, Charge = 1, Cool = 2 };
enum class WalkCirclePhase : uint8_t { Start = 0, TakeAngle = 1, Watch = 2, PlayerMoves = 3 };
enum class BurstPhase : uint8_t { Start = 0, GlowIn = 1, Fire = 2, Cool = 3 };
enum class WaitPhase : uint8_t { Start = 0, Face = 1, Spin = 2 };
enum class VolleyPhase : uint8_t { Start = 0, Fire = 1, Cool = 2, End = 3 };
enum class DownPhase : uint8_t { Start = 0, Fall = 1, Lying = 2 };
const DataTable<CollisionBoxRecord> swimBox{TableId::GaugeReaverbotsSwimBox};
const DataTable<CollisionBoxRecord> standBox{TableId::GaugeReaverbotsStandBox};
constexpr uint16_t kSwimSound = 0x1db, kLaySound = 0x1dc, kShotSound = 0x1da, kVolleySound = 0x1d9,
                   kDownWaterSound = 0x1dd;
constexpr uint8_t kStageGaugeFlagStage = 0x44; // ST44: its last one down sets flag 0x172
constexpr uint32_t kFlagSt44Beaten = 0x172;
constexpr int kMaxLiveAnimated = 10; // the mines are laid while fewer animated actors live
constexpr uint8_t kGlowBase = 0x20, kGlowFull = 0x3e, kGlowStand = 0x22;

// a sub-state's first step
void enterSub(AnimatedActor& actor, uint8_t sub) {
    actor.subState = sub;
    actor.restartStep();
}
// the start of a move: its mode, its animation, standing still but for the z speed
void startMove(AnimatedActor& actor, GaugeFields& fields, uint8_t mode, uint8_t anim, int16_t speed) {
    fields.mode = mode;
    actor.anim = anim;
    fields.startKey = 0;
    actor.velocity.setXY(0, 0);
    actor.velocity.z = speed;
}
// exe 0x4fcfc0: the hit: its damage (a flash and the hurt sound) when the attack is one it takes (only the strong push
// while kGaugeShotsOnly); -1 dead; a body's bump marks it pushed. (It never answers more than 0; its callers test it.)
int gaugeHit(AnimatedActor& actor, GaugeFields& fields) {
    const uint32_t h = fields.hit.result;
    const uint32_t d = h & kHitDamageMask;
    if (d != 0) {
        uint32_t taken = 0;
        const bool hurts = !(fields.mode & kGaugeShotsOnly)
                               ? (h & (kHitAttackByPlayer | kHitAttackThrown | kHitAttackStrong)) != 0
                               : (h & kHitAttackStrongPush) != 0;
        if (hurts) {
            actor.tint = kTintFlash;
            soundPlayAtPos(kHurtSound, actor.pos);
            taken = d;
        }
        if (hitPointsAdd(actor, -int(taken)) != 0) return -1; // exe 0x5e52f0
    }
    if (h & kHitAttackBump) fields.mode |= kGaugePushed;
    return 0;
}
// the point on a circle around the player (radius 1 << shift) at the circle's angle
void circleTarget(GaugeFields& fields, const GaugeParams& params, int shift) {
    const Player& p = player();
    const int a = params.circleAngle & 0xfff;
    fields.circlePoint.x = t16(((int32_t(fixedCos(a)) << shift) >> 12) + p.pos.x.whole);
    fields.circlePoint.y = p.pos.y.whole;
    fields.circlePoint.z = t16(((int32_t(fixedSin(a)) << shift) >> 12) + p.pos.z.whole);
}
void circlePoint(const GaugeFields& fields, int16_t out[3]) {
    out[0] = fields.circlePoint.x;
    out[1] = fields.circlePoint.y;
    out[2] = fields.circlePoint.z;
}
// the glow pulse: the level moved by its step, the step reversed past hi (hiStrict: above it, else at it) or below 0x10
void glowPulse(GaugeFields& fields, uint8_t hi, bool hiStrict) {
    const int8_t step = fields.glowStep;
    const uint8_t v = uint8_t(fields.glow + uint8_t(step));
    fields.glow = v;
    const bool flip = step > 0 ? (hiStrict ? v > hi : v >= hi) : v < 0x10;
    if (flip) fields.glowStep = int8_t(-step);
}
uint8_t glowUp(GaugeFields& fields, uint8_t cap) { // + 1, at most cap
    uint8_t v = uint8_t(fields.glow + 1u);
    if (v > cap) v = cap;
    fields.glow = v;
    return v;
}
uint8_t glowDown(GaugeFields& fields, uint8_t floor) { // - 1, at least floor
    uint8_t v = uint8_t(fields.glow - 1u);
    if (v < floor) v = floor;
    fields.glow = v;
    return v;
}
// exe 0x4fcf90: one more of the group down -- the others-left count of every live actor of the type minus 1
void countGroupDown() {
    AnimatedPool& pool = animatedPool();
    for (uint32_t i = 0; i < kAnimatedFieldSlots; ++i) {
        AnimatedActor& e = pool[i];
        if ((e.flags & kActorLiveDrawn) && e.type == kGaugeType) GaugeParams(e).othersLeft -= 1;
    }
}
// exe 0x4fcc90: the live animated actors of the field
int liveAnimatedActors() {
    AnimatedPool& pool = animatedPool();
    int n = 0;
    for (uint32_t i = kAnimatedFieldSlots; i != 0;) {
        --i;
        if (pool[i].flags & kActorLiveDrawn) ++n;
    }
    return n;
}
// the homing shot from the reaverbot: its turning-back speed, its homing ticks
void spawnShot(const AnimatedActor& actor, int16_t speed, int16_t homingTicks) {
    const uint32_t s = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!s) return;
    soundPlayAtPos(kShotSound, actor.pos);
    ProjectileActor& shot = projectileActorAt(s);
    HomingShotParams params(projectileActorAt(s));
    shot.flags = kActorLive;
    shot.loopFlags |= kActorSweepGroup2;
    shot.type = kHomingShotType;
    shot.variant = uint8_t(HomingShotVariant::Aimed);
    shot.pos = actor.pos;
    shot.velocity.x = 0;
    shot.velocity.z = speed;
    shot.velocity.y = actor.spawnByte == 1 ? int16_t(-0x200) : int16_t(-0x400);
    shot.acceleration.y = 0x20;
    shot.rotation.x = 0;
    shot.rotation.y = actor.rotation.y;
    shot.rotation.z = 0;
    params.homingTicks = homingTicks;
    params.damage = int8_t(gaugeStats(actor).shotAttack);
}
// the death explosion (the global static type 8) at a random point around the reaverbot
void deathExplosion(const AnimatedActor& actor) {
    const uint32_t e = allocActor60(0); // exe 0x5e1300
    if (!e) return;
    StaticActor& boom = staticActorAt(e);
    ExplosionParams params(actorHeaderAt(e));
    boom.flags = kActorLive;
    boom.loopFlags |= kActorSweepGroup2;
    boom.type = typeByte(GlobalStaticType::Explosion);
    boom.variant = 0;
    boom.pos.x.whole = int16_t(uint16_t(uint32_t(uint16_t(actor.pos.x.whole)) - (rngStep() & 0xff) + 0x80u));
    const uint32_t r = rngStep();
    boom.pos.y.whole = int16_t(uint16_t(uint32_t(uint16_t(actor.pos.y.whole)) - ((r >> 2) & 0xff) -
                                        (player().inWater != 0 ? 0u : 0x90u) + 0x80u));
    const uint32_t r2 = rngStep();
    params.spread = 0x30;
    params.flashes = 1;
    boom.pos.z.whole = int16_t(uint16_t(uint32_t(uint16_t(actor.pos.z.whole)) - ((r2 >> 4) & 0xff) + 0x80u));
}
// exe 0x4fccc0: down -- the flash, the fall animation (in water 6 / 7, else 0xd / 0xe), the group counted down (the last
// one with its variant set drops the key), explosions every few ticks, then its drops and it is freed
void gaugeDown(const PlayerEnv& env, AnimatedActor& actor, GaugeFields& fields) {
    GaugeParams params(actor);
    actor.tint = kTintFlash;
    const bool water = player().inWater == 1;
    switch (DownPhase(actor.step)) {
    case DownPhase::Start:
        actor.step = uint8_t(DownPhase::Fall);
        actor.velocity.x = 0;
        actor.velocity.y = 0;
        actor.velocity.z = 0;
        actor.anim = water ? 6 : 0xd;
        fields.ticks = 0x28;
        countDefeat();
        countGroupDown();
        if (actor.variant != 0 && params.othersLeft < 0) spawnPedestalKey(actor.pos.x, actor.pos.z);
        [[fallthrough]];
    case DownPhase::Fall:
        if (actor.animEnded()) {
            actor.anim = water ? 7 : 0xe;
            actor.step = uint8_t(DownPhase::Lying);
        }
        break;
    default: break;
    }
    if (fields.ticks & 3) deathExplosion(actor);
    if (player().inWater == 1 && !(gameState().frame & 0xf)) soundPlayAtPos(kDownWaterSound, actor.pos);
    if (fields.ticks & 1) actor.tint = glowTint(fields.glow);
    const int16_t t = fields.ticks;
    if (t < 0x19) fields.mode |= kGaugeDying;
    fields.ticks = t16(t - 1);
    if (int16_t(t - 1) == 0) {
        if (gameState().stage == kStageGaugeFlagStage && params.othersLeft < 0) setStoryFlag(kFlagSt44Beaten);
        spawnDrops(actor.pos, gaugeStats(actor).dropTable); // exe 0x5f1270
        freeActor(actor);                                   // exe 0x5e1490
        return;
    }
    integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z); // exe 0x5e3690
    if (actor.airborne != 0) actor.velocity.y = int16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y));
    collide(env, actor, fields.contact, fields.mode);
    if (actor.wasAirborne != 0 && actor.airborne == 0) {
        actor.velocity.z = 0;
        actor.velocity.x = 0;
    }
}
// exe 0x4fb7c0: the model, the hit points (its row's x 16), in water it swims, else it stands on the ground; its gauge
void gaugeInit(const PlayerEnv& env, AnimatedActor& actor, GaugeFields& fields) {
    const uint8_t v = modelVariant(actor);
    actor.spawnByte = v;
    if (!bindActorModel(actor, v)) { // exe 0x5e0bc0(a, &v)
        freeActor(actor);            // exe 0x5e1490
        return;
    }
    actor.rotation.x = 0;
    actor.runFlags |= kActorRunMarked;
    actor.rotation.z = 0;
    const int16_t hp = hasAnimatedStats(actor.type) ? int16_t(uint32_t(gaugeStats(actor).hitPoints) << 4) : 0;
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    if (player().inWater == 1) {
        actor.anim = 0;
        actor.semiTransparency = kSemiAdditive;
        actor.collisionBox.pointAt(swimBox.stateAddress());
        actor.enterState(uint8_t(GaugeState::Swim));
    } else {
        actor.anim = 8;
        actor.pos.y.whole = int16_t(actorGround(env, actor)); // exe 0x40f190
        actor.collisionBox.pointAt(standBox.stateAddress());
        actor.enterState(uint8_t(GaugeState::Stand));
    }
    actor.prevAnim = 0xff;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    fields.hit.previousX = actor.pos.x.whole;
    fields.hit.previousZ = actor.pos.z.whole;
    fields.hit.result = 0;
    fields.startY = actor.pos.y.whole;
    fields.startKey = 0;
    fields.glow = kGlowBase;
    registerEnemy(actor.slot, 0);
    resetModelScale(actor); // exe 0x4e5cd0
    fields.homeZ = actor.pos.z.whole;
    fields.homeX = actor.pos.x.whole;
    switch (GaugeParams(actor).gaugeColour) { // exe 0x415d30(a, 1, 4, colour)
    case 1: hudAddBossGauge(actor, 1, 4, 0x66ff); break;
    case 2: hudAddBossGauge(actor, 1, 4, 0xff0066); break;
    case 3: hudAddBossGauge(actor, 1, 4, 0x66ff00); break;
    default: break;
    }
}

// ---- swimming ([0x897a50]) ----
// exe 0x4fb9f0: around the player on a circle of 0x400 facing it; a wall -> 0x80 ticks backing off; now and then the
// shot or the dash; the player slowed -> keep away; the last of the group -> it walks
void swimCircle(AnimatedActor& actor, GaugeFields& fields) {
    GaugeParams params(actor);
    if (actor.step == 0) {
        startMove(actor, fields, 0, 0, -0x80);
        const uint32_t r = rngStep();
        fields.counter = 0;
        fields.ticks = int16_t(0x3c - (r & 0x1f));
        actor.step = 1;
    }
    if (!(uint8_t(actor.animKey.frame) & 0xf)) soundPlayAtPos(kSwimSound, actor.pos);
    circleTarget(fields, params, 10);
    int16_t at[3], target[3];
    wholePos(actor, at);
    turnYawTo(actor.rotation.y, uint32_t(angleFromPlayer(actor)), 0x40);
    circlePoint(fields, target);
    if (fields.counter != 0) {
        fields.counter -= 1;
        moveAlongYaw(actor.pos, angleOf(target, at) - 0x400, 0, 0, actor.velocity.z); // exe 0x5e36f0
    } else if (fields.contact & kTouchedWalls) {
        fields.counter = 0x80;
    } else {
        const int d = planarDistance16(target, at);
        const int off = d > 0x200 ? -0x80 : (d >= 0x180 ? 0 : 0x80);
        if (fields.counter < 0) fields.counter = 0;
        else fields.counter -= 0x40;
        moveAlongYaw(actor.pos, angleOf(target, at) + off + 0x400, 0, 0, actor.velocity.z);
    }
    if (fields.ticks == 0) {
        if (actor.animEnded()) {
            const uint32_t k = rngStep() & 7;
            if (k < 4) {
                enterSub(actor, uint8_t(SwimSub::Shoot));
            } else if (k < 6) {
                enterSub(actor, uint8_t(SwimSub::Dash));
            } else {
                fields.ticks = int16_t(0x3c - (rngStep() & 0x1f));
                return;
            }
        }
    } else {
        fields.ticks -= 1;
    }
    if (player().status & (kStatusSlowed | kStatusSlowedFloor)) enterSub(actor, uint8_t(SwimSub::KeepAway));
    if (params.othersLeft == 0) actor.enterState(uint8_t(GaugeState::Walk));
}
// exe 0x4fbc60: it turns to the player, dashes forward (0x40 ticks or a wall), recovers
void swimDash(AnimatedActor& actor, GaugeFields& fields) {
    switch (DashPhase(actor.step)) {
    case DashPhase::Start:
        startMove(actor, fields, kGaugeNoWallTurn | kGaugeCharging, 2, 0);
        actor.step += 1u;
        [[fallthrough]];
    case DashPhase::Aim:
        if (actor.animEnded()) {
            actor.velocity.z = -0x200;
            actor.anim = 3;
            actor.step += 1u;
            fields.ticks = 0x40;
        }
        turnYawTo(actor.rotation.y, uint32_t(angleFromPlayer(actor)), 0x40);
        return;
    case DashPhase::Charge:
        integrateVelocity(actor, 0, 0, actor.velocity.z); // exe 0x5e3690
        if (fields.ticks != 0 && !(fields.contact & kTouchedWalls)) {
            fields.ticks -= 1;
            return;
        }
        if (actor.animEnded()) {
            actor.anim = 4;
            actor.step += 1u;
        }
        return;
    case DashPhase::Recover:
        if (actor.animEnded()) enterSub(actor, uint8_t(SwimSub::Circle));
        return;
    default: return;
    }
}
// exe 0x4fbd60: the glow charges, the shot at frame 0xa, the glow back down
void swimShoot(AnimatedActor& actor, GaugeFields& fields) {
    switch (ShootPhase(actor.step)) {
    case ShootPhase::Start:
        startMove(actor, fields, 0, 1, 0);
        actor.step = uint8_t(ShootPhase::Charge);
        [[fallthrough]];
    case ShootPhase::Charge:
        glowUp(fields, kGlowFull);
        if (actor.animKey.frame == 0xa) {
            spawnShot(actor, -0x100, 0x200);
            actor.step = uint8_t(ShootPhase::Cool);
        }
        return;
    case ShootPhase::Cool:
        if (glowDown(fields, kGlowBase) == kGlowBase && actor.animEnded()) enterSub(actor, uint8_t(SwimSub::Circle));
        return;
    default: return;
    }
}
// exe 0x4fbeb0: the player slowed: it keeps on a circle of 0x200 around it, facing it, until the player is not slowed
void swimKeepAway(AnimatedActor& actor, GaugeFields& fields) {
    GaugeParams params(actor);
    if (actor.step == 0) {
        startMove(actor, fields, 0, 0, -0x100);
        actor.step = 1;
    }
    circleTarget(fields, params, 9);
    int16_t at[3], target[3];
    wholePos(actor, at);
    circlePoint(fields, target);
    int off;
    if (fields.contact & kTouchedWalls) {
        off = -0x200;
    } else {
        const int d = planarDistance16(target, at);
        off = d > 0x180 ? -0x40 : (d >= 0x80 ? 0 : 0x40);
    }
    turnYawTo(actor.rotation.y, uint32_t(angleFromPlayer(actor)), 0x40);
    moveAlongYaw(actor.pos, angleOf(target, at) + off + 0x400, 0, 0, actor.velocity.z);
    if (!(player().status & (kStatusSlowed | kStatusSlowedFloor))) enterSub(actor, uint8_t(SwimSub::Circle));
}
// exe 0x4fbfe0 / 0x4fcc50: a hit taken: no damage while the animation plays
void hurt(AnimatedActor& actor, GaugeFields& fields, uint8_t anim) {
    if (actor.step == 0) {
        startMove(actor, fields, kGaugeNoDamage, anim, 0);
        actor.step = 1;
    }
    if (actor.animEnded()) enterSub(actor, 0);
}
// exe 0x4fc020 / 0x4fc7d0: a wall hit -> it spins backwards (+z) along the angle FUN_005e3bc0(point, actor) taken on
// the first tick, 0x80 ticks. That point is a stack local the exe reads BEFORE writing it (the home words are stored
// into it only afterwards), so it holds whatever the earlier calls at that depth left there. Taking the home itself made
// the actor back away from the home into the corner it hit, forever (a stuck boss in ST37 sub 6); the angle is taken
// from the actor toward the home instead, so the spin carries it back into the room, which is what the move is for.
void spinBack(AnimatedActor& actor, GaugeFields& fields, int16_t speed, uint8_t hi, bool hiStrict) {
    if (actor.step == 0) {
        startMove(actor, fields, kGaugeNoWallTurn | kGaugeShotsOnly, 5, speed);
        actor.step = 1;
        fields.ticks = 0x80;
        fields.counter = int16_t((uint32_t(angleFromPoint(fields.homeX, fields.homeZ, actor)) + 0x800u) & 0xfffu);
    }
    moveAlongYaw(actor.pos, fields.counter, 0, 0, actor.velocity.z);
    actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + 0x80u);
    glowPulse(fields, hi, hiStrict);
    fields.ticks -= 1;
    if (fields.ticks == 0) enterSub(actor, 0);
}
// exe 0x4fb960
void gaugeSwim(const PlayerEnv& env, AnimatedActor& actor, GaugeFields& fields) {
    actor.tint = glowTint(fields.glow);
    if (!(fields.mode & kGaugeNoDamage)) {
        const int r = gaugeHit(actor, fields);
        if (r < 0) {
            actor.enterState(uint8_t(GaugeState::Down));
            gaugeDown(env, actor, fields);
            return;
        }
        if (r > 0 && actor.subState == 0) enterSub(actor, uint8_t(SwimSub::Hurt));
    }
    switch (SwimSub(actor.subState)) {
    case SwimSub::Circle: swimCircle(actor, fields); break;
    case SwimSub::Dash: swimDash(actor, fields); break;
    case SwimSub::Shoot: swimShoot(actor, fields); break;
    case SwimSub::KeepAway: swimKeepAway(actor, fields); break;
    case SwimSub::Hurt: hurt(actor, fields, 5); break;
    case SwimSub::SpinBack: spinBack(actor, fields, 0x80, kGlowBase, true); break;
    default: notPortedAt(kStoryPort, "0x4fb670 state 1 sub (table 0x897a50)", actor.subState);
    }
    collide(env, actor, fields.contact, fields.mode); // exe 0x4fd050
    if ((fields.contact & kTouchedWalls) && !(fields.mode & kGaugeNoWallTurn))
        enterSub(actor, uint8_t(SwimSub::SpinBack));
}

// ---- walking ([0x897a68]) ----
// The mine's spawn parameters (written by the reaverbot that lays it): its blast's size and attack
struct MineParams {
    explicit MineParams(const AnimatedActor& actor) : MineParams(const_cast<AnimatedActor&>(actor)) {}
    explicit MineParams(AnimatedActor& actor) : blastSize(actor.params, 0), blastAttack(actor.params, 2) {}
    BytesField<uint16_t, 4> blastSize;
    BytesField<uint16_t, 4> blastAttack;
};
// a mine (sub 1) laid at the reaverbot, 0x80 below it; its blast from the row
void layMine(const AnimatedActor& actor) {
    const uint32_t e = allocActor20(); // exe 0x5e1160
    if (!e) return;
    soundPlayAtPos(kLaySound, actor.pos);
    AnimatedActor& mine = animatedActorAt(e);
    const GaugeStats row = gaugeStats(actor);
    mine.flags = kActorLive;
    mine.type = kGaugeType;
    mine.subtype = uint8_t(GaugeSubtype::Mine);
    mine.variant = 0;
    mine.spawnByte = actor.spawnByte;
    mine.loopFlags |= kActorSweepGroup2;
    mine.pos.x.whole = actor.pos.x.whole;
    mine.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + 0x80u);
    mine.pos.z.whole = actor.pos.z.whole;
    MineParams params(mine);
    params.blastSize = uint16_t(uint32_t(row.mineBlastSize) << 2);
    params.blastAttack = row.mineBlastAttack;
}
// exe 0x4fc180: it circles the player (0x200) on the angle it took while the player stood still, spinning and glowing;
// after 0x3c ticks it lays a mine (fewer than 10 animated actors live), then a burst, a dash or more circling
void walkCircle(AnimatedActor& actor, GaugeFields& fields) {
    GaugeParams params(actor);
    switch (WalkCirclePhase(actor.step)) {
    case WalkCirclePhase::Start:
        startMove(actor, fields, kGaugeShotsOnly, 0, -0x100);
        fields.ticks = 0x3c;
        fields.glowStep = -1;
        actor.step = uint8_t(WalkCirclePhase::TakeAngle);
        [[fallthrough]];
    case WalkCirclePhase::TakeAngle:
        params.circleAngle = uint16_t(angleFromPlayer(actor));
        actor.step = uint8_t(WalkCirclePhase::Watch);
        [[fallthrough]];
    case WalkCirclePhase::Watch:
        if (player().velocity.z != 0) actor.step = uint8_t(WalkCirclePhase::PlayerMoves);
        break;
    case WalkCirclePhase::PlayerMoves:
        if (player().velocity.z == 0) actor.step = uint8_t(WalkCirclePhase::TakeAngle);
        break;
    default: break;
    }
    if (!(uint8_t(actor.animKey.frame) & 0xf)) soundPlayAtPos(kSwimSound, actor.pos);
    circleTarget(fields, params, 9);
    int16_t at[3], target[3];
    wholePos(actor, at);
    circlePoint(fields, target);
    if (fields.counter != 0) {
        fields.counter -= 1;
        moveAlongYaw(actor.pos, angleOf(target, at) - 0x400, 0, 0, actor.velocity.z);
    } else if (fields.contact & kTouchedWalls) {
        fields.counter = 0x80;
    } else {
        const int d = planarDistance16(target, at);
        const int off = d > 0x280 ? -0x80 : (d >= 0x180 ? 0 : 0x80);
        moveAlongYaw(actor.pos, angleOf(target, at) + off + 0x400, 0, 0, actor.velocity.z);
    }
    actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + 0x80u);
    glowPulse(fields, 0x1f, false);
    if (fields.ticks != 0) {
        fields.ticks -= 1;
        return;
    }
    if (!actor.animEnded()) return;
    if (liveAnimatedActors() < kMaxLiveAnimated) layMine(actor);
    const uint32_t k = rngStep() & 7;
    if (k < 3) enterSub(actor, uint8_t(WalkSub::Burst));
    else if (k < 6) enterSub(actor, uint8_t(WalkSub::Dash));
    else fields.ticks = 0x3c;
}
// exe 0x4fc4a0: it turns (0x80 a tick) until it faces the player, dashes 0x40 ticks, recovers
void walkDash(AnimatedActor& actor, GaugeFields& fields) {
    switch (DashPhase(actor.step)) {
    case DashPhase::Start:
        startMove(actor, fields, kGaugeNoWallTurn | kGaugeShotsOnly, 2, 0);
        actor.step += 1u;
        [[fallthrough]];
    case DashPhase::Aim: {
        const int angle = angleFromPlayer(actor) & 0xfff;
        const uint16_t yaw = uint16_t(actor.rotation.y);
        const int y = int(yaw & 0xfff);
        const int d = y - angle >= 0 ? y - angle : angle - y;
        if (d < 0x80) {
            if (actor.animEnded()) {
                actor.velocity.z = -0x300;
                actor.anim = 3;
                actor.step += 1u;
                fields.ticks = 0x40;
                fields.mode = uint8_t((fields.mode & ~kGaugeShotsOnly) | kGaugeCharging);
            }
        } else {
            actor.rotation.y = int16_t(uint16_t(yaw + 0x80u));
        }
        glowUp(fields, kGlowBase);
        return;
    }
    case DashPhase::Charge:
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        if (fields.ticks != 0) {
            fields.ticks -= 1;
            return;
        }
        if (actor.animEnded()) {
            actor.anim = 4;
            actor.step += 1u;
        }
        return;
    case DashPhase::Recover:
        if (actor.animEnded()) enterSub(actor, uint8_t(WalkSub::Circle));
        return;
    default: return;
    }
}
// exe 0x4fc5f0: a burst of 3..6 homing shots (their turning-back speed 0x400), the glow charged before each
void walkBurst(AnimatedActor& actor, GaugeFields& fields) {
    switch (BurstPhase(actor.step)) {
    case BurstPhase::Start:
        fields.mode = 0;
        fields.startKey = 0;
        actor.velocity.setXY(0, 0);
        actor.velocity.z = 0;
        actor.step = uint8_t(BurstPhase::GlowIn);
        fields.counter = -0x400;
        fields.ticks = int16_t((rngStep() & 3) + 3u);
        [[fallthrough]];
    case BurstPhase::GlowIn: {
        const uint8_t v = uint8_t(fields.glow + 1u);
        fields.glow = v;
        if (v > kGlowBase) {
            actor.anim = 1;
            fields.glow = kGlowBase;
            actor.step = uint8_t(BurstPhase::Fire);
        }
        return;
    }
    case BurstPhase::Fire:
        glowUp(fields, kGlowFull);
        if (actor.animKey.frame == 0xa) {
            spawnShot(actor, fields.counter, 0x100);
            actor.step = uint8_t(BurstPhase::Cool);
        }
        return;
    case BurstPhase::Cool:
        if (glowDown(fields, kGlowBase) != kGlowBase || !actor.animEnded()) return;
        fields.ticks -= 1;
        if (fields.ticks != 0) {
            actor.prevAnim = 0xff;
            actor.step = uint8_t(BurstPhase::Fire);
        } else {
            enterSub(actor, uint8_t(WalkSub::Circle));
        }
        return;
    default: return;
    }
}
// exe 0x4fc100
void gaugeWalk(const PlayerEnv& env, AnimatedActor& actor, GaugeFields& fields) {
    actor.tint = glowTint(fields.glow);
    if (!(fields.mode & kGaugeNoDamage) && gaugeHit(actor, fields) < 0) {
        actor.enterState(uint8_t(GaugeState::Down));
        gaugeDown(env, actor, fields);
        return;
    }
    switch (WalkSub(actor.subState)) {
    case WalkSub::Circle: walkCircle(actor, fields); break;
    case WalkSub::Dash: walkDash(actor, fields); break;
    case WalkSub::Burst: walkBurst(actor, fields); break;
    case WalkSub::SpinBack: spinBack(actor, fields, 0x100, 0x1f, false); break;
    default: notPortedAt(kStoryPort, "0x4fb670 state 2 sub (table 0x897a68)", actor.subState);
    }
    collide(env, actor, fields.contact, fields.mode);
    if ((fields.contact & kTouchedWalls) && !(fields.mode & kGaugeNoWallTurn))
        enterSub(actor, uint8_t(WalkSub::SpinBack));
}

// ---- standing ([0x897a78]) ----
// exe 0x4fc920: it waits (spawn byte 0: 0x30 ticks facing the player; 1 / 2: 0x30.. ticks spinning, only a strong push
// hurting it), then the volley
void standWait(AnimatedActor& actor, GaugeFields& fields) {
    switch (WaitPhase(actor.step)) {
    case WaitPhase::Start:
        startMove(actor, fields, 0, 8, 0);
        if (actor.spawnByte == 0) {
            fields.ticks = 0x30;
            actor.step = uint8_t(WaitPhase::Face);
        } else if (actor.spawnByte <= 2) {
            fields.ticks = int16_t((gameState().frame & 0x1fu) + 0x30u);
            fields.mode |= kGaugeShotsOnly;
            actor.step = uint8_t(WaitPhase::Spin);
        }
        break;
    case WaitPhase::Face: turnYawTo(actor.rotation.y, uint32_t(angleFromPlayer(actor)), 0x40); break;
    case WaitPhase::Spin: actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + 0x80u); break;
    default: break;
    }
    fields.ticks -= 1;
    if (fields.ticks == 0) {
        fields.mode &= uint8_t(~kGaugeShotsOnly);
        enterSub(actor, uint8_t(StandSub::Volley));
    }
}
// exe 0x4fc9e0: 1..5 shots as the animation ends (spawn byte 0: aimed, else spread ones), the glow charged before each
void standVolley(AnimatedActor& actor, GaugeFields& fields) {
    switch (VolleyPhase(actor.step)) {
    case VolleyPhase::Start:
        startMove(actor, fields, 0, 8, 0);
        switch (actor.spawnByte) {
        case 0: fields.ticks = int16_t((rngStep() & 1) + 1u); break;
        case 1: fields.ticks = int16_t((rngStep() & 3) + 2u); break;
        case 2: fields.ticks = 2; break;
        default: break;
        }
        actor.step = uint8_t(VolleyPhase::Fire);
        [[fallthrough]];
    case VolleyPhase::Fire: {
        glowUp(fields, kGlowFull);
        if (!actor.animEnded()) return;
        const uint32_t s = allocActor40(kActorStageClass); // exe 0x5e1280
        if (s) {
            soundPlayAtPos(kVolleySound, actor.pos);
            ProjectileActor& shot = projectileActorAt(s);
            HomingShotParams params(projectileActorAt(s));
            const GaugeStats row = gaugeStats(actor);
            shot.flags = kActorLive;
            shot.loopFlags |= kActorSweepGroup2;
            shot.type = kHomingShotType;
            if (actor.spawnByte == 0) {
                shot.variant = uint8_t(HomingShotVariant::Aimed);
                shot.velocity.z = -0x200;
                params.homingTicks = 0x100;
                params.damage = int8_t(row.shotAttack);
            } else {
                shot.variant = uint8_t(HomingShotVariant::Spread);
                const uint32_t r = rngStep();
                params.homingTicks = 0x100;
                shot.velocity.z = t16(-0x180 - int((r >> 8) & 0xff));
                params.damage = int8_t(row.spreadShotAttack);
            }
            shot.pos = actor.pos;
            shot.velocity.x = 0;
            shot.velocity.y = -0x400;
            shot.pos.y.whole = int16_t(uint16_t(shot.pos.y.whole) - 0x40u);
            shot.acceleration.y = 0x20;
            shot.rotation.x = 0;
            const uint32_t r = rngStep();
            shot.rotation.z = 0;
            shot.rotation.y = int16_t(uint16_t((r & 0x7ff) + uint16_t(actor.rotation.y) - 0x400u));
        }
        actor.step = uint8_t(VolleyPhase::Cool);
        return;
    }
    case VolleyPhase::Cool:
        if (glowDown(fields, kGlowStand) != kGlowStand || !actor.animEnded()) return;
        fields.ticks -= 1;
        if (fields.ticks != 0) {
            actor.anim = 0xa;
            actor.prevAnim = 0xff;
            actor.step = uint8_t(VolleyPhase::Fire);
        } else {
            actor.anim = 0xb;
            actor.step = uint8_t(VolleyPhase::End);
        }
        return;
    case VolleyPhase::End:
        if (actor.animEnded()) {
            fields.glow = kGlowBase;
            enterSub(actor, uint8_t(StandSub::Wait));
        }
        return;
    default: return;
    }
}
// exe 0x4fc8b0
void gaugeStand(const PlayerEnv& env, AnimatedActor& actor, GaugeFields& fields) {
    actor.tint = glowTint(fields.glow);
    if (!(fields.mode & kGaugeNoDamage)) {
        const int r = gaugeHit(actor, fields);
        if (r < 0) {
            actor.enterState(uint8_t(GaugeState::Down));
            gaugeDown(env, actor, fields);
            return;
        }
        if (r > 0) enterSub(actor, uint8_t(StandSub::HurtPastTable));
    }
    switch (StandSub(actor.subState)) {
    case StandSub::Wait: standWait(actor, fields); break;
    case StandSub::Volley: standVolley(actor, fields); break;
    case StandSub::Hurt: hurt(actor, fields, 0xc); break;
    default: notPortedAt(kStoryPort, "0x4fb670 state 3 sub (table 0x897a78)", actor.subState);
    }
    collide(env, actor, fields.contact, fields.mode);
}
// exe 0x4fb670
void gaugeReaverbot(const PlayerEnv& env, AnimatedActor& actor) {
    GaugeFields& fields = classState<GaugeFields>(actor);
    contactFromBodies(actor, fields.contact, fields.mode);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (GaugeState(actor.state)) {
    case GaugeState::Init: gaugeInit(env, actor, fields); break;
    case GaugeState::Swim: gaugeSwim(env, actor, fields); break;
    case GaugeState::Walk: gaugeWalk(env, actor, fields); break;
    case GaugeState::Stand: gaugeStand(env, actor, fields); break;
    case GaugeState::Down: gaugeDown(env, actor, fields); break;
    default: notPortedAt(kStoryPort, "0x4fb670 state (table 0x897a3c)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStep(actor);  // exe 0x5e1ea0
    else animSet(actor, actor.anim, fields.startKey);   // exe 0x5e1c50
    animPose(actor);                                    // exe 0x5e2d90
    const GaugeStats row = gaugeStats(actor);
    const uint8_t mode = fields.mode;
    const ShortVec4& body = bonePoint(actor, 0, kStoryPort); // exe 0x5e1810
    const int16_t at[3] = {body.x, body.y, body.z};
    fields.hit.result = 0;
    fields.hit.previousX = actor.prevPos.x.whole;
    fields.hit.previousZ = actor.prevPos.z.whole;
    uint32_t attack = (mode & kGaugeShotsOnly) ? kHitAttackDeflects : kHitAttackSolid;
    if (mode & kGaugeCharging) attack |= kHitAttackThrown | row.strongAttack;
    else attack |= row.attack;
    attack |= kHitSlows;
    const uint32_t owner = hitOwner(actor.slot) | ((mode & kGaugeDying) ? kHitNoLockOn : 0u) |
                           hitGroup(kHitGroupBodies) | hitRadius(0x80);
    hitListSubmit(at, fields.hit, owner, attack); // exe 0x5e4120
    // exe 0x5f20f0(a, 0xc8, 0x100, 0): the shadow (render output)
}

// ==== 0x4fd180: the floating mine =====================================================================================
enum class MineState : uint8_t { Init = 0, Bob = 1, Blast = 2 };
const DataTable<CollisionBoxRecord> mineBox{TableId::GaugeReaverbotsMineBox};
constexpr int16_t kMineScale = 0x48;
constexpr uint32_t kMineTriggers = kHitAttackBump | kHitAttackByPlayer | kHitAttackThrown | kHitAttackStrong;

// exe 0x4fd280: the model, facing the player, no hit points, the box; drawn semi-transparent, small (exe 0x4fd470)
void mineInit(AnimatedActor& mine, MineFields& fields) {
    mine.flags |= kActorDrawn;
    if (!bindActorModel(mine, modelVariant(mine))) { // exe 0x5e0bc0(a, &v)
        freeActor(mine);                             // exe 0x5e1490
        return;
    }
    mine.anim = 0;
    mine.runFlags |= kActorRunMarked;
    mine.prevAnim = 0xff;
    mine.rotation.x = 0;
    mine.rotation.y = int16_t(angleFromPlayer(mine));
    mine.rotation.z = 0;
    mine.maxHitPoints = 0;
    mine.hitPoints = 0;
    mine.collisionBox.pointAt(mineBox.stateAddress());
    mine.interactionBox = nullptr;
    mine.interaction = 0;
    mine.airborne = 0;
    mine.onPlatform = 0;
    mine.clearClassBlock(); // exe 0x40dc40
    fields.hit.previousX = mine.pos.x.whole;
    fields.hit.previousZ = mine.pos.z.whole;
    fields.hit.result = 0;
    mine.semiTransparency = kSemiAdditive; // exe 0x5e1a00(a, 1, 1)
    mine.enterState(uint8_t(MineState::Bob));
    mine.scale.x = kMineScale;
    mine.scale.y = kMineScale;
    mine.scale.z = kMineScale;
}
// exe 0x4fd490: hit by a body or an attack: a flash and the hurt sound
bool mineHit(AnimatedActor& mine, const MineFields& fields) {
    if (!(fields.hit.result & kMineTriggers)) return false;
    mine.tint = kTintFlash;
    soundPlayAtPos(kHurtSound, mine.pos);
    return true;
}
// exe 0x4fd360: it bobs (its vertical speed swinging within +-4) facing the player, 0x300 ticks or until hit
void mineBob(const PlayerEnv& env, AnimatedActor& mine, MineFields& fields) {
    mine.tint = kTintNeutral;
    const bool hit = mineHit(mine, fields);
    if (mine.step == 0) {
        fields.ticks = 0x300;
        mine.acceleration.y = 1;
        mine.velocity.y = 0;
        mine.step = 1;
    }
    turnYawTo(mine.rotation.y, uint32_t(angleFromPlayer(mine)), 0x40);
    if (std::abs(int(mine.velocity.y)) > 4) mine.acceleration.y = t16(-mine.acceleration.y);
    mine.velocity.y = int16_t(uint16_t(mine.velocity.y) + uint16_t(mine.acceleration.y));
    mine.pos.y.whole = int16_t(uint16_t(mine.pos.y.whole) - uint16_t(mine.velocity.y));
    fields.ticks -= 1;
    if (hit || fields.ticks == 0) mine.enterState(uint8_t(MineState::Blast));
    collide(env, mine, fields.contact, fields.mode); // exe 0x4fd4d0
}
// exe 0x4fd410: a blast (the global static type 7) of the mine's parameters, the mine freed
void mineBlast(AnimatedActor& mine) {
    mine.tint = kTintFlash;
    const uint32_t e = allocActor60(0); // exe 0x5e1300
    if (e) {
        StaticActor& blast = staticActorAt(e);
        BlastParams params(actorHeaderAt(e));
        const MineParams mineParams(mine);
        blast.flags = kActorLive;
        blast.loopFlags |= kActorSweepGroup2;
        blast.type = typeByte(GlobalStaticType::Blast);
        blast.variant = 0;
        blast.pos.x.whole = mine.pos.x.whole;
        blast.pos.y.whole = mine.pos.y.whole;
        blast.pos.z.whole = mine.pos.z.whole;
        params.size = mineParams.blastSize;
        params.attack = mineParams.blastAttack;
    }
    freeActor(mine); // exe 0x5e1490
}
// exe 0x4fd180
void floatingMine(const PlayerEnv& env, AnimatedActor& mine) {
    MineFields& fields = classState<MineFields>(mine);
    contactFromBodies(mine, fields.contact, fields.mode); // exe 0x4fd5a0
    mine.prevAnim = mine.anim;
    mine.savePreviousPose();
    mine.savePreviousContact();
    switch (MineState(mine.state)) {
    case MineState::Init: mineInit(mine, fields); break;
    case MineState::Bob: mineBob(env, mine, fields); break;
    case MineState::Blast: mineBlast(mine); break;
    default: notPortedAt(kStoryPort, "0x4fd180 state (table 0x897ab4)", mine.state);
    }
    if (!(mine.flags & kActorLive)) return;
    if (mine.anim == mine.prevAnim) animStepPose(mine); // exe 0x5e1e80
    else animSetPose(mine, mine.anim, 0);               // exe 0x5e1c30
    const ShortVec4& body = bonePoint(mine, 0, kStoryPort);
    const int16_t at[3] = {body.x, body.y, body.z};
    fields.hit.result = 0;
    fields.hit.previousX = mine.prevPos.x.whole;
    fields.hit.previousZ = mine.prevPos.z.whole;
    hitListSubmit(at, fields.hit, hitOwner(mine.slot) | hitGroup(kHitGroupBodies) | hitRadius(0x20),
                  gaugeStats(mine).shotAttack | kHitAttackHarmless | kHitAttackSolid | kHitSlows);
    // exe 0x5f20f0(a, 0x50, 0x100, 0): the shadow (render output)
}

// ==== 0x4fd600: script 0x5f's cast ====================================================================================
enum class CastVariant : int8_t { Swimmer = 0, Stander = 1 };
enum class CastState : uint8_t { Init = 0, Wait = 1, Act = 2, Glow = 3, Done = 4 };
enum class GlowPhase : uint8_t { Up = 0, Down = 1, Done = 2 };
// The cast's spawn parameters (its variant; its last one set: it stays afloat, else it keeps to the ground) and its
// class block (its animation's restart flag and start key, its glow)
struct CastParams {
    explicit CastParams(const AnimatedActor& actor) : CastParams(const_cast<AnimatedActor&>(actor)) {}
    explicit CastParams(AnimatedActor& actor) : variant(actor.params[0]), afloat(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> variant;
    uint8_t& afloat;
};
const DataTable<CollisionBoxRecord> castBox{TableId::GaugeReaverbotsCastBox};
// the script's cues: its stepSub
constexpr uint8_t kCueAppear = 1, kCueGlow = 2, kCueEnd = 3;
constexpr int32_t kSwimInTicks = 0xc8;

// the glow of the cast's state 3 (its sub-state: up to 0x3e, down to 0x20, held)
void castGlow(AnimatedActor& cast, CastFields& fields) {
    switch (GlowPhase(cast.subState)) {
    case GlowPhase::Up: {
        const uint8_t v = uint8_t(fields.glow + 1u);
        fields.glow = v;
        if (v > kGlowFull) {
            fields.glow = kGlowFull;
            cast.subState = uint8_t(GlowPhase::Down);
        }
        break;
    }
    case GlowPhase::Down: {
        const uint8_t v = uint8_t(fields.glow - 1u);
        fields.glow = v;
        if (v < kGlowBase) {
            fields.glow = kGlowBase;
            cast.subState = uint8_t(GlowPhase::Done);
        }
        break;
    }
    default: break;
    }
    cast.tint = glowTint(fields.glow);
}
// the common init of both variants: the model of its variant, the box, its class block cleared
bool castInit(AnimatedActor& cast, CastFields& fields, uint8_t anim) {
    if (!bindActorModel(cast, cast.variant)) { // exe 0x5e0bc0(a, a + 6)
        freeActor(cast);
        return false;
    }
    cast.collisionBox.pointAt(castBox.stateAddress());
    cast.clearClassBlock();
    cast.enterState(uint8_t(CastState::Wait));
    cast.anim = anim;
    cast.prevAnim = 0xff;
    fields.animFlags &= uint8_t(~1u);
    return true;
}
// exe 0x4fd6d0: the swimmer -- once the script runs it swims around the room's centre facing the player (until the
// tick 0xc8), then glows at the cue
void castSwimmer(AnimatedActor& cast, CastFields& fields) {
    ScriptState& s = scriptState();
    switch (CastState(cast.state)) {
    case CastState::Init:
        if (!castInit(cast, fields, 0)) return;
        fields.accelerationSpare = 0;
        fields.velocitySpare = 0;
        CastParams(cast).afloat = 1;
        cast.velocity.z = -0x80;
        if (player().inWater == 1) cast.semiTransparency = kSemiAdditive; // exe 0x5e1a00(a, 1, 1)
        return;
    case CastState::Wait:
        if (s.state != 0) cast.state = uint8_t(CastState::Act);
        return;
    case CastState::Act:
        if (cast.subState == 0) {
            turnYawTo(cast.rotation.y, uint32_t(angleFromPlayer(cast)), 0x40);
            // exe 0x5e3b00 / 0x5e3bc0 from a stack point whose integer parts are 0 (its fractions left unset by the
            // exe: taken as 0): the room's origin
            const int dx = cast.pos.x.whole, dz = cast.pos.z.whole;
            const int dist = isqrt(int32_t(uint32_t(dx * dx) + uint32_t(dz * dz)));
            const int off = dist > 0x100 ? -0x100 : (dist >= 0x80 ? 0 : 0x100);
            moveAlongYaw(cast.pos, angleFromPoint(0, 0, cast) + off + 0x400, 0, 0, cast.velocity.z);
            if (s.stepTick == kSwimInTicks) cast.subState = 1;
        }
        if (s.stepSub == kCueGlow) {
            fields.glow = kGlowBase;
            cast.enterState(uint8_t(CastState::Glow));
        }
        return;
    case CastState::Glow:
        castGlow(cast, fields);
        if (s.stepSub == kCueEnd) cast.enterState(uint8_t(CastState::Done));
        return;
    default: return;
    }
}
// exe 0x4fd8e0: the one standing -- it appears at the first cue and glows at the second
void castStander(AnimatedActor& cast, CastFields& fields) {
    ScriptState& s = scriptState();
    switch (CastState(cast.state)) {
    case CastState::Init: castInit(cast, fields, 8); return;
    case CastState::Wait:
        if (s.stepSub == kCueAppear) cast.enterState(uint8_t(CastState::Act));
        return;
    case CastState::Act:
        if (s.stepSub == kCueGlow) {
            fields.glow = kGlowBase;
            cast.enterState(uint8_t(CastState::Glow));
        }
        return;
    case CastState::Glow:
        castGlow(cast, fields);
        if (s.stepSub == kCueEnd) cast.enterState(uint8_t(CastState::Done));
        return;
    default: return;
    }
}
// exe 0x4fd600
void gaugeCast(const PlayerEnv& env, AnimatedActor& cast) {
    CastFields& fields = classState<CastFields>(cast);
    const CastParams params(cast);
    cast.savePreviousPose();
    cast.prevAnim = cast.anim;
    switch (CastVariant(params.variant.get())) { // [0x897ae0]
    case CastVariant::Swimmer: castSwimmer(cast, fields); break;
    case CastVariant::Stander: castStander(cast, fields); break;
    default: notPortedAt(kStoryPort, "0x4fd600 variant (table 0x897ae0)", uint8_t(params.variant));
    }
    if (!(cast.flags & kActorLive)) return;
    if (params.afloat == 0) cast.pos.y.whole = int16_t(actorGround(env, cast)); // exe 0x40f190
    if (cast.anim == cast.prevAnim && !(fields.animFlags & 1)) animStepPose(cast); // exe 0x5e1e80
    else animSetPose(cast, cast.anim, fields.startKey);                             // exe 0x5e1c30
    // exe 0x5f20f0(a, 0x30, 0x160, 0): the shadow (render output)
}

} // namespace

void gaugeReaverbotUpdate(const PlayerEnv& env, uint32_t actor) { gaugeReaverbot(env, animatedActorAt(actor)); }
void floatingMineUpdate(const PlayerEnv& env, uint32_t actor) { floatingMine(env, animatedActorAt(actor)); }
void gaugeCastUpdate(const PlayerEnv& env, uint32_t actor) { gaugeCast(env, animatedActorAt(actor)); }

} // namespace od::port
