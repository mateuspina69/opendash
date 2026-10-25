// The ice ruin's critters (pool 0x20 type 0x4a, see ice_ruin_critters.hpp), one routine per exe routine. Names are
// hypotheses from the observed behaviour. The shadows (exe 0x5f20f0(a, 0x38, 0xa0, 0)) are render output and left out.
//
// Both classes share their moves (exe 0x4d5130 the collision and gravity, the walk, the hop, the leap's start) and
// their own bytes (cleared at the init): the hit box (+ the hit's yaw), a timer, the heading they turn to, their bits,
// a coin, the animation's key, the hits taken (and in all), the hurt cool-down and the spawn point (the catchable
// one). The step counter is the hop's "wandered off" mark; the first parameter the scene's cue (sub 1).
#include "content/yosyonke/ice_ruin_critters.hpp"
#include "content/yosyonke/ice_ruin_critters_state.hpp"

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
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/enemy_motion.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace ice_ruin_critters_state;

namespace {

// ---- what the two classes share --------------------------------------------------------------------------------------
enum class CritterState : uint8_t { Init = 0, Run = 1 };
enum class CritterBehaviour : uint8_t {
    Choose = 0, Stand = 1, Walk = 2, Hop = 3, Leap = 4, KnockedBack = 5, Stunned = 6, Startled = 7,
};
// the critter's bits
constexpr uint8_t kCritterLifted = 0x01;
constexpr uint8_t kCritterBounceArmed = 0x02; // airborne: the next wall turns it (cleared every tick)
constexpr uint8_t kCritterThrown = 0x04;
constexpr uint8_t kCritterWatched = 0x08;     // its hit entry is flagged while the player faces it
constexpr uint8_t kCritterStunned = 0x10;
constexpr uint8_t kCritterStrongHit = 0x20;
constexpr uint8_t kCritterLongCooldown = 0x40;
constexpr uint8_t kCritterHurtBits = kCritterStunned | kCritterStrongHit | kCritterLongCooldown;

// the scene critter's cue (its first parameter, set by script 0x49) / the catchable one's index (its caught flag)
struct CritterParams {
    explicit CritterParams(const AnimatedActor& actor) : CritterParams(const_cast<AnimatedActor&>(actor)) {}
    explicit CritterParams(AnimatedActor& actor) : cue(actor.params[0]), index(actor.params[0]) {}
    uint8_t& cue;
    FieldAlias<int8_t, uint8_t> index;
};
constexpr uint8_t kCueLeap = 2;

constexpr uint8_t kCritterGlowType = 6; // the stage effect of the critters' glow (exe 0x4309b0)
constexpr int16_t kCritterGravity = 0x150;
constexpr int16_t kWalkSpeed = -0x40, kHopSpeed = int16_t(0xfc40), kHopBrake = -0x28, kHopRunSpeed = int16_t(0xfd80),
                  kSlowWalkSpeed = -0x50, kLeapLift = int16_t(0xfc10);
constexpr int16_t kBounceLift = int16_t(0xfdbe), kBouncePush = int16_t(0xfb60);
constexpr uint16_t kHopSound = 0x24c, kNoticeSound = 0x24b;
constexpr int kWanderRange = 0xc00;

uint8_t coinOf(uint32_t r) { return uint8_t((0xaaaaaaaau >> (r & 0x1f)) & 1u); } // 0xaaaaaaaa >> (r & 0x1f) & 1
int iabs(int v) { return v < 0 ? -v : v; }
// a timer from a table of signed bytes (movsx ax, byte)
uint16_t tableTicks(const DataTable<ScalarTable<int8_t, 0x10>>& table, uint32_t i) { return uint16_t(int16_t(table[i])); }
void move(AnimatedActor& actor) { integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z); } // 0x5e3690(a, 0, vy, vz)
void enterBehaviour(AnimatedActor& actor, CritterBehaviour b) {
    actor.subState = uint8_t(b);
    actor.step = 0;
}
// the behaviour b with the step and the step counter cleared (the exe's word store)
void restartBehaviour(AnimatedActor& actor, CritterBehaviour b) {
    actor.subState = uint8_t(b);
    actor.restartStep();
}
// the copies at the top of the updates
void savePrevious(AnimatedActor& actor) {
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.tint = kTintNeutral;
    actor.savePreviousContact();
}
// the dust puff (exe 0x5e13b0: the global hit spark, variant 3) at the critter
void puff(const AnimatedActor& actor) {
    const uint32_t e = allocActorA0(0);
    if (!e) return;
    EffectActor& p = effectActorAt(e);
    p.flags = kActorLive;
    p.type = typeByte(GlobalEffectType::HitSpark);
    p.variant = 3;
    p.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    p.pos.x = actor.pos.x.whole;
    p.pos.y = actor.pos.y.whole;
    p.pos.z = actor.pos.z.whole;
    classState<HitSparkFields>(effectActorAt(e)).size = 0x40;
}
// the critters' glow (exe 0x5e13a0: the stage effect 6 variant 1) on the critter (its owner), at the offset (0, 0x36,
// -0x27)
void glow(const AnimatedActor& actor) {
    const uint32_t e = allocActorA0(kActorStageClass);
    if (!e) return;
    EffectActor& g = effectActorAt(e);
    g.flags = kActorLiveDrawn;
    g.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
    g.type = kCritterGlowType;
    g.variant = 1;
    g.step = 0x10;
    g.stepCounter = 0;
    g.setParamsWord(actor.at);
    g.setPositionWords(uint32_t(0x36) << 16, 0xffd9u);
}

// exe 0x4d5130: the mover queue, the wall push-out (a wall turns it while the bounce is armed), the gravity and the
// landing; -1 = it left the ground
int critterMove(const PlayerEnv& env, AnimatedActor& actor, CritterFields& f) {
    actorCollisionSubmitMover(actor); // exe 0x4140b0
    actor.wallHit = 0;
    if (int32_t(actorTestMove(env, actor)) < 0) { // exe 0x40f570
        const uint32_t r = actorPushOut(env, actor); // exe 0x410960
        actor.wallHit = r;
        if (int32_t(r) < 0) actor.pos = actor.prevPos;
        if ((r & 0xf00) && (f.bits & kCritterBounceArmed)) {
            reflectRotationOffWalls(actor.rotation, r); // exe 0x410740
            f.bits &= uint8_t(~kCritterBounceArmed);
            actor.velocity.z = int16_t(actor.velocity.z >> 1);
        }
        if ((actor.wallHit & 0x2000) && actor.velocity.y < 0) actor.velocity.y = int16_t(-int(actor.velocity.y >> 2));
    }
    const int g = actorGround(env, actor); // exe 0x40f190
    if (actor.airborne != 0) {
        const int16_t vy = actor.velocity.y;
        if (vy > 0 && actor.pos.y.whole >= g) {
            actor.velocity.y = 0;
            actor.pos.y.whole = int16_t(g);
            actor.airborne = 0;
            return 0;
        }
        actor.velocity.y = int16_t(uint16_t(uint16_t(actor.acceleration.y) + uint16_t(vy)));
        return 0;
    }
    if (g - actor.pos.y.whole > 0x10) {
        f.bits |= kCritterBounceArmed;
        actor.airborne = 1;
        return -1;
    }
    actor.pos.y = int32_t(uint32_t(g) << 16);
    return 0;
}
// the init of both classes (0x4d47b0 / 0x4d3310 after their own checks): the bytes cleared, the hit points from the
// type's row, the gravity, the glow; the run state in any case
void critterInit(AnimatedActor& actor, CritterFields& f, uint32_t box, bool catchable) {
    actor.clearClassBlock(); // exe 0x40dc40
    actor.anim = 0;
    actor.prevAnim = 0xff;
    f.animKey = 0;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.acceleration.z = 0;
    actor.acceleration.x = 0;
    actor.acceleration.y = kCritterGravity;
    const int16_t hp = hasAnimatedStats(actor.type) ? int16_t(animatedStats(actor.type).hitPoints) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.collisionBox.pointAt(box);
    if (catchable) {
        actor.interactionBox.pointAt(TableId::IceRuinCrittersCritterLiftBox);
        actor.interaction = actor.variant == 1 ? uint8_t(kInteractTalk | kInteractLift | kInteractLiftTalks) : uint8_t(0);
    }
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.wallHit = 0;
    actor.actorHit = 0;
    if (catchable) {
        f.spawnX = actor.pos.x.whole;
        f.spawnZ = actor.pos.z.whole;
    }
    f.bits |= kCritterWatched;
    glow(actor);
    actor.enterState(uint8_t(CritterState::Run));
}
// the walk's speed by the frame (frames 0 / 0xc step, 6 / 0x12 stand; byte tables 0x4d4aac / 0x4d3838)
void walkSpeed(AnimatedActor& actor) {
    const int fr = actor.animKey.frame;
    if (fr == 0 || fr == 0xc) actor.velocity.z = kWalkSpeed;
    else if (fr == 6 || fr == 0x12) actor.velocity.z = 0;
}
void sceneCue(AnimatedActor& actor) { // exe 0x4d5110: the scene's cue 2 -> the leap
    if (CritterParams(actor).cue == kCueLeap) enterBehaviour(actor, CritterBehaviour::Leap);
}
// exe 0x4d44e0: the player within 0xa0 in height: it faces the player within 0x200 -> startled (stunned), within
// 0x600 -> the leap
void notice(AnimatedActor& actor, CritterFields& f) {
    const Player& p = player();
    const int py = p.pos.y.whole, ay = actor.pos.y.whole;
    const int d = py - ay;
    if ((d >= 0 ? d : ay - py) > 0xa0) return;
    if (facingAndRange(actor, p, 0x580, 0x200) == 0) {
        actor.step = 0;
        actor.subState = uint8_t(CritterBehaviour::Startled);
        f.bits |= kCritterStunned;
        return;
    }
    if (facingAndRange(actor, p, 0x580, 0x600) == 0) enterBehaviour(actor, CritterBehaviour::Leap);
}
// the catchable one notices the player, the scene's one waits for its cue
void watch(AnimatedActor& actor, CritterFields& f, bool catchable) {
    if (catchable) notice(actor, f);
    else sceneCue(actor);
}
// 0x4d4920 / 0x4d3610: stand ([table + rng & 0xf] * 2 ticks), then choose again
void stand(AnimatedActor& actor, CritterFields& f, const DataTable<ScalarTable<int8_t, 0x10>>& table, bool catchable) {
    if (actor.step == 0) {
        actor.velocity.y = 0;
        actor.velocity.z = 0;
        actor.anim = 0;
        f.timer = uint16_t(tableTicks(table, rngStep() & 0xf) << 1);
        ++actor.step;
    }
    const uint16_t t = f.timer;
    f.timer = uint16_t(t - 1u);
    if (t == 0) enterBehaviour(actor, CritterBehaviour::Choose);
    watch(actor, f, catchable);
    move(actor);
}
// the walk's tail (0x4d4a12.. / 0x4d379d..): turn to the heading by 0x20, an RNG step every 0x20 ticks, the
// countdown, the walk's speed by the frame
void walkTail(AnimatedActor& actor, CritterFields& f, bool catchable) {
    turnYawTo(actor.rotation.y, uint32_t(int32_t(f.heading)), 0x20); // exe 0x5e3df0
    if ((f.timer & 0x1f) == 0) rngStep();
    const uint16_t t = f.timer;
    f.timer = uint16_t(t - 1u);
    if (t == 0) enterBehaviour(actor, CritterBehaviour::Choose);
    walkSpeed(actor);
    watch(actor, f, catchable);
    move(actor);
}
// 0x4d4ac0 / 0x4d3850 (timers [table + rng & 0xf]): hop forward and pick headings, steer off walls (the three probes at
// +-0x200: all open -> leap back; left / right open -> 0x300 that way; none -> turn back with a slow walk), wander off
// and come back when farther than 0xc00 from the player
enum class HopStep : uint8_t {
    Start = 0, TowardPlayer = 1, Wander = 2, Turn = 4, TurnFast = 5, Run = 7, Brake = 0xa, TurnAway = 0xb,
};
void hop(const PlayerEnv& env, AnimatedActor& actor, CritterFields& f, const DataTable<ScalarTable<int8_t, 0x10>>& table) {
    const Player& p = player();
    switch (HopStep(actor.step)) {
    case HopStep::Start: {
        actor.velocity.y = 0;
        actor.velocity.z = kHopSpeed;
        actor.acceleration.z = kHopBrake;
        actor.anim = 1;
        f.timer = tableTicks(table, rngStep() & 0xf);
        const HopStep s = actor.stepCounter != 0 ? HopStep::Wander : HopStep::TowardPlayer;
        actor.stepCounter = 0;
        actor.step = uint8_t(s);
        break;
    }
    case HopStep::TowardPlayer: {
        int ang = angleOf(p.pos, actor.pos) + 0x800;
        const uint8_t c = uint8_t(rngStep() & 7);
        f.coin = c;
        if (c & 6) ang = (c & 1) ? ang - 0x200 : ang + 0x200;
        f.heading = int16_t(ang);
        actor.step = uint8_t(HopStep::Turn);
        break;
    }
    case HopStep::Wander: {
        const uint8_t c = uint8_t(rngStep() & 7);
        f.coin = c;
        uint16_t y = uint16_t(actor.rotation.y);
        if (c & 6) y = (c & 1) ? uint16_t(y - 0x200) : uint16_t(y + 0x200);
        f.heading = int16_t(y);
        actor.step = uint8_t(HopStep::Turn);
        actor.stepCounter = 1;
        break;
    }
    case HopStep::Turn:
    case HopStep::TurnFast: {
        const int step = actor.step == uint8_t(HopStep::Turn) ? 0x20 : 0x80;
        if (iabs(turnToward(actor.rotation.y, int(f.heading), step)) < step) { // exe 0x5e3db0
            actor.velocity.z = kHopRunSpeed;
            actor.step = uint8_t(HopStep::Run);
        }
        break;
    }
    case HopStep::Run:
        if (actor.wallHit & 0xf00) {
            uint32_t away = uint32_t(angleOf(p.pos, actor.pos)) + 0x800u;
            const uint32_t r = rngStep();
            f.coin = uint8_t(1u << (r & 3));
            actor.step = uint8_t(HopStep::Turn);
            const int yaw = actor.rotation.y;
            uint32_t open = 0;
            if (!(probePushOutAhead(env, actor, yaw + 0x200, 0x30) & 0xf00)) open = 1;
            if (!(probePushOutAhead(env, actor, yaw, 0x30) & 0xf00)) open |= 2;
            if (!(probePushOutAhead(env, actor, yaw - 0x200, 0x30) & 0xf00)) open |= 4;
            if (open == 0) {
                actor.velocity.z = kSlowWalkSpeed;
                actor.step = uint8_t(HopStep::TurnFast);
                f.timer = 1;
                away = ((f.coin & 4) ? 0xe00u : 0u) - 0x700u + uint32_t(int32_t(actor.rotation.y));
                f.heading = int16_t(away & 0xfff);
            } else if (open == 5) {
                // the leap back over the obstacle: behaviour 5 of the run state. The scene critter's table 0x895508 has
                // 5 entries (index 5 = the bytes 0x89551c read as 0x5a3c1e3c: the exe would crash next tick, see its
                // behaviours); the catchable one's has 8 (0x4d3ec0)
                actor.rotation.y += 0x800u;
                actor.velocity.y = kBounceLift;
                actor.velocity.z = kBouncePush;
                actor.airborne = 1;
                f.heading = actor.rotation.y;
                actor.anim = 4;
                actor.state = uint8_t(CritterState::Run);
                actor.subState = uint8_t(CritterBehaviour::KnockedBack);
                actor.step = 1;
                f.heading = int16_t(away & 0xfff);
            } else if (open & 1) {
                f.heading = int16_t((uint32_t(int32_t(actor.rotation.y)) + 0x300u) & 0xfff);
            } else if (open & 4) {
                f.heading = int16_t((uint32_t(int32_t(actor.rotation.y)) - 0x300u) & 0xfff);
            } else {
                f.heading = int16_t(away & 0xfff);
            }
        } else {
            const uint16_t t = f.timer;
            f.timer = uint16_t(t - 1u);
            if (t == 0) {
                f.timer = tableTicks(table, rngStep() & 0xf);
                actor.step = uint8_t(HopStep::Wander);
            }
            if (actor.stepCounter != 0 && planarDistance(actor.pos, p.pos) > kWanderRange)
                actor.step = uint8_t(HopStep::Brake);
        }
        break;
    case HopStep::Brake:
        actor.velocity.z -= uint16_t(actor.acceleration.z);
        if (actor.velocity.z >= kWalkSpeed) {
            actor.velocity.z = kWalkSpeed;
            f.heading = int16_t(rngStep() & 0xfff);
            ++actor.step;
        }
        break;
    case HopStep::TurnAway:
        if (iabs(turnToward(actor.rotation.y, int(f.heading), 0x80)) < 0x80)
            enterBehaviour(actor, CritterBehaviour::Choose);
        break;
    default: break; // 3, 6, 8, 9
    }
    if ((gameState().frame & 0xf) == 0 && (rngStep() & 1)) soundPlayAtPos(kHopSound, actor.pos);
    switch (HopStep(actor.step)) { // byte table 0x4d4f88 / 0x4d3d18: the dust of the hops
    case HopStep::TowardPlayer:
    case HopStep::Wander:
    case HopStep::Turn:
    case HopStep::Brake:
        if (gameState().frame & 1) puff(actor);
        break;
    default: break;
    }
    actor.rotation.y &= 0xfff;
    f.heading &= 0xfff;
    move(actor);
}
// the leap's first steps (shared): face the player (the notice sound), turn by 0x180 (animation 3), the jump at frame
// 4; false past them
bool leapStart(AnimatedActor& actor, CritterFields& f) {
    const uint8_t s = actor.step;
    switch (s) {
    case 0:
        actor.velocity.y = 0;
        actor.velocity.z = 0;
        actor.anim = 0;
        f.heading = int16_t(angleOf(player().pos, actor.pos));
        soundPlayAtPos(kNoticeSound, actor.pos);
        ++actor.step;
        return true;
    case 1:
        turnYawTo(actor.rotation.y, uint32_t(int32_t(f.heading)), 0x180);
        actor.anim = 3;
        actor.step = uint8_t(s + 1u);
        return true;
    case 2:
        if (uint8_t(actor.animKey.frame) == 4) {
            actor.step = uint8_t(s + 1u);
            actor.velocity.y = kLeapLift;
        }
        return true;
    default: return false;
    }
}
// the animation (asked for / stepped) with the critter's key
void animate(AnimatedActor& actor, const CritterFields& f) {
    if (actor.anim == actor.prevAnim) animStepPose(actor); // exe 0x5e1e80
    else animSetPose(actor, actor.anim, f.animKey);        // exe 0x5e1c30
}

// ---- 0x4d46f0: the scene critter (sub 1). States [0x8954fc + state * 4]: 0 0x4d47b0 init, 1 0x4d4890 (the
// behaviours [0x895508 + sub * 4], then exe 0x4d5130), 2 0x468fa0 free, 3..7 = the behaviours 0..4 directly.
enum class SceneCritterState : uint8_t { Init = 0, Run = 1, Free = 2, FirstDirect = 3, LastDirect = 7 };
const DataTable<ScalarTable<int8_t, 0x10>> kSceneStandTicks{TableId::IceRuinCrittersSceneStandTicks};
const DataTable<ScalarTable<int8_t, 0x10>> kSceneHopTicks{TableId::IceRuinCrittersSceneHopTicks};
constexpr uint16_t kSceneWalkTicks = 0x1e;

void sceneChoose(AnimatedActor& actor, CritterFields& f) { // 0x4d48c0: no cue -> stand, else walk
    const uint32_t r = rngStep();
    actor.step = 0;
    actor.stepCounter = 0;
    if (CritterParams(actor).cue == 0) {
        actor.subState = uint8_t(CritterBehaviour::Stand);
        return;
    }
    f.coin = coinOf(r >> 3);
    actor.subState = uint8_t(CritterBehaviour::Walk);
}
void sceneWalk(AnimatedActor& actor, CritterFields& f) { // 0x4d49b0: walk 0x1e ticks
    if (actor.step == 0) {
        actor.velocity.y = 0;
        actor.velocity.z = kWalkSpeed;
        actor.anim = 0;
        f.timer = kSceneWalkTicks;
        f.coin = coinOf(rngStep());
        f.heading = actor.rotation.y;
        ++actor.step;
    }
    walkTail(actor, f, false);
}
void sceneLeap(AnimatedActor& actor, CritterFields& f) { // 0x4d4fa0: leap away from the player, then hop
    if (!leapStart(actor, f)) switch (actor.step) {
        case 3:
            if (actor.airborne != 0) break;
            ++actor.step;
            [[fallthrough]];
        case 4:
            if (actor.animEnded()) {
                actor.anim = 0;
                f.coin = coinOf(rngStep());
                const uint32_t rr = crtRand();
                f.heading = int16_t((rr & 0xff) + (uint16_t(actor.rotation.y) ^ 0x800u) - 0x80u);
                ++actor.step;
            }
            break;
        case 5:
            if (turnYawTo(actor.rotation.y, uint32_t(int32_t(f.heading)), 0x180) == 0)
                restartBehaviour(actor, CritterBehaviour::Hop);
            break;
        default: break;
        }
    move(actor);
}
void sceneBehaviour(const PlayerEnv& env, AnimatedActor& actor, CritterFields& f, uint32_t n) {
    switch (CritterBehaviour(n)) {
    case CritterBehaviour::Choose: sceneChoose(actor, f); return;
    case CritterBehaviour::Stand: stand(actor, f, kSceneStandTicks, false); return; // 0x4d4920
    case CritterBehaviour::Walk: sceneWalk(actor, f); return;
    case CritterBehaviour::Hop: hop(env, actor, f, kSceneHopTicks); return; // 0x4d4ac0
    case CritterBehaviour::Leap: sceneLeap(actor, f); return;
    default: notPortedAt(kStoryPort, "0x4d46f0 behaviour (table 0x895508 has 5 entries; the exe calls garbage)", n);
    }
}
void sceneCritter(const PlayerEnv& env, AnimatedActor& actor) { // 0x4d46f0
    CritterFields& f = classState<CritterFields>(actor);
    savePrevious(actor);
    f.bits &= uint8_t(~kCritterBounceArmed);
    const uint8_t st = actor.state;
    switch (SceneCritterState(st)) {
    case SceneCritterState::Init: // 0x4d47b0
        if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
            freeActor(actor);            // exe 0x5e1490
            return;
        }
        critterInit(actor, f, stateAddress(TableId::IceRuinCrittersSceneCritterBox), false);
        break;
    case SceneCritterState::Run: // 0x4d4890
        sceneBehaviour(env, actor, f, actor.subState);
        critterMove(env, actor, f);
        break;
    case SceneCritterState::Free: freeActor(actor); break; // 0x468fa0 -> exe 0x5e1490
    default:
        if (st < uint8_t(SceneCritterState::FirstDirect) || st > uint8_t(SceneCritterState::LastDirect))
            notPortedAt(kStoryPort, "0x4d46f0 state (table 0x8954fc)", st);
        sceneBehaviour(env, actor, f, st - uint32_t(SceneCritterState::FirstDirect));
        break;
    }
    if (!(actor.flags & kActorLive)) return;
    animate(actor, f);
}

// ---- 0x4d3240: the critter to catch (sub 0). States [0x895474 + state * 4]: 0 0x4d3310 init, 1 0x4d3450 (the hurt
// check, the behaviours [0x895490 + sub * 4], exe 0x4d5130, lift / talk), 2 0x4d41d0 (carried [0x8954c0 + sub * 4]),
// 3 0x4d4430 caught, 4 0x468fa0 free.
enum class CatchableState : uint8_t { Init = 0, Run = 1, Carried = 2, Caught = 3, Free = 4 };
enum class CarriedSub : uint8_t { Held = 0, Flying = 1 };
const DataTable<ScalarTable<int8_t, 0x10>> kWalkTicks{TableId::IceRuinCrittersWalkTicks};
const DataTable<ScalarTable<int8_t, 0x10>> kCatchableHopTicks{TableId::IceRuinCrittersCatchableHopTicks}; // also the stun's
const DataTable<ScalarTable<int8_t, 0x10>> kCatchableStandTicks{TableId::IceRuinCrittersCatchableStandTicks};
const DataTable<ScalarTable<uint16_t, 3>> kCaughtFlags{TableId::IceRuinCrittersCaughtFlags}; // by the critter's index (variant 1)
constexpr uint32_t kFlagCaughtFirst = 0x3f1, kFlagCaughtSecond = 0x3f2, kFlagCaughtThird = 0x3f3;
constexpr uint32_t kCaughtEventParam = 0x30000;
constexpr uint16_t kCaughtMessage = 0x25;
constexpr uint32_t kHitStrong = 0x400000u, kHitShoved = 0x1000000u, kHitThrownInto = 0x20000u;

// exe 0x4d4660: the box's damage -> hit points, the stun and its cool-down; -1 hurt
int critterHurt(AnimatedActor& actor, CritterFields& f) {
    uint8_t bits = f.bits;
    if (bits & kCritterStunned) return 0;
    if (f.hurtCooldown != 0) {
        --f.hurtCooldown;
        return 0;
    }
    const uint32_t box = f.hit.result;
    const uint32_t dmg = box & 0xfff;
    if (dmg != 0) {
        bits |= (box & kHitStrong) ? uint8_t(kCritterStunned | kCritterStrongHit) : kCritterStunned;
        f.bits = bits;
        hitPointsAdd(actor, -int(dmg)); // exe 0x5e52f0
        f.hurtCooldown = (f.bits & kCritterLongCooldown) ? 0x14 : 0xc;
        if (actor.variant != 2) ++f.hits;
        return -1;
    }
    if (box & kHitShoved) enterBehaviour(actor, CritterBehaviour::Leap);
    return 0;
}
// exe 0x4d4590: the hit entry at bone 0 (the type row's strong attack / hit height or, thrown, its body attack);
// "watched" while the player faces it within 0x200 / 0xc00
void critterHitEntry(AnimatedActor& actor, CritterFields& f) {
    uint32_t watched = 0;
    f.hit.previousX = actor.prevPos.x.whole;
    f.hit.result = 0;
    f.hit.previousZ = actor.prevPos.z.whole;
    if (f.bits & kCritterWatched) {
        if (facingAndRange(player(), actor, 0x200, 0xc00) == 0) f.bits &= uint8_t(~kCritterWatched);
        else watched = 0x80000;
    }
    if (f.bits & kCritterLifted) return;
    if (!hasAnimatedStats(actor.type)) notPorted(kStoryPort, "FUN_004d4590: no type row (the exe reads address 2)");
    const AnimatedActorStats row = animatedStats(actor.type);
    uint32_t shape, attack;
    if (!(f.bits & kCritterThrown)) {
        attack = row.strongAttack | 0x20000u;
        shape = ((((uint32_t(actor.slot) << 15) | row.hitHeight) << 9) | watched) | 0x50800au;
    } else {
        attack = row.bodyAttack | 0x40000u;
        shape = (uint32_t(actor.slot) << 24) | 0x28800au;
    }
    const ShortVec4& bp = bonePoint(actor, 0, kStoryPort); // exe 0x5e1810
    const int16_t pos[3] = {bp.x, bp.y, bp.z};
    hitListSubmit(pos, f.hit, shape, attack); // exe 0x5e4120
}
// 0x4d3520: stand (1 in 8), walk, or a long walk on a random half-turn
void catchableChoose(AnimatedActor& actor, CritterFields& f) {
    const uint32_t r = rngStep();
    actor.step = 0;
    actor.stepCounter = 0;
    if ((r & 7) == 0) {
        actor.subState = uint8_t(CritterBehaviour::Stand);
        return;
    }
    f.coin = coinOf(r >> 3);
    actor.subState = uint8_t(CritterBehaviour::Walk);
    if ((r & 0x7f80) != 0) return;
    actor.subState = uint8_t(CritterBehaviour::Walk);
    actor.step = 1;
    actor.velocity.z = 0;
    actor.velocity.y = 0;
    actor.velocity.x = 0;
    actor.anim = 0;
    f.timer = tableTicks(kWalkTicks, rngStep() & 0xf);
    const uint8_t c = coinOf(rngStep());
    f.coin = c;
    const uint32_t v = c ? 0x7ffu : 0xfffff801u;
    f.heading = int16_t(v);
    f.heading = int16_t(uint16_t(actor.rotation.y) + v);
}
// 0x4d36a0: walk ([0x895454] ticks, a heading +-0x1ff); a wall -> a half-turn
void catchableWalk(AnimatedActor& actor, CritterFields& f) {
    switch (actor.step) {
    case 0: {
        actor.velocity.y = 0;
        actor.velocity.z = kWalkSpeed;
        actor.anim = 0;
        f.timer = tableTicks(kWalkTicks, rngStep() & 0xf);
        f.coin = coinOf(rngStep());
        int v = int(rngStep() & 0x1ff);
        f.heading = int16_t(v);
        if (f.coin == 0) v = -v;
        f.heading = int16_t(v);
        f.heading = int16_t(uint16_t(actor.rotation.y) + uint32_t(v));
        ++actor.step;
        break;
    }
    case 1:
        if (actor.wallHit & 0xf00) {
            const uint32_t v = f.coin ? 0x7ffu : 0xfffff801u;
            f.heading = int16_t(v);
            const uint32_t c = (uint16_t(actor.rotation.y) + v) & 0xfff;
            f.timer += 0x40u;
            f.heading = int16_t(c);
            ++actor.step;
        }
        break;
    default: break;
    }
    walkTail(actor, f, true);
}
// 0x4d3d30: noticed -- leap away, land facing off the player, hop
void catchableLeap(AnimatedActor& actor, CritterFields& f) {
    if (!leapStart(actor, f)) switch (actor.step) {
        case 3:
            if (actor.airborne != 0) break;
            ++actor.step;
            [[fallthrough]];
        case 4:
            if (actor.animEnded()) {
                actor.anim = 0;
                const uint8_t c = coinOf(rngStep());
                f.coin = c;
                const uint32_t ang = uint32_t(angleOf(player().pos, actor.pos));
                f.heading = int16_t(ang + (c ? 0x7ffu : 0x801u));
                ++actor.step;
            }
            break;
        case 5:
            turnYawTo(actor.rotation.y, uint32_t(int32_t(f.heading)), 0x180);
            restartBehaviour(actor, CritterBehaviour::Hop);
            break;
        default: break;
        }
    move(actor);
}
// 0x4d3ec0: knocked back along the hit's yaw, then stunned
void knockedBack(AnimatedActor& actor, CritterFields& f) {
    switch (actor.step) {
    case 0:
        actor.velocity.y = kBounceLift;
        actor.velocity.z = kBouncePush;
        actor.anim = 4;
        f.heading = int16_t(f.hitYaw & 0xfff);
        ++actor.step;
        break;
    case 1:
        if (actor.airborne == 0 && actor.animEnded()) {
            actor.anim = 5;
            actor.velocity.y = 0;
            actor.velocity.z = int16_t(0xfdf4);
            ++actor.step;
        }
        break;
    case 2:
        if (actor.animEnded()) restartBehaviour(actor, CritterBehaviour::Stunned);
        break;
    default: break;
    }
    moveAlongYaw(actor.pos, f.heading, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e36f0
}
// 0x4d3f70: stunned ([0x895464] >> 2 / >> 3 ticks; out of hit points: + 0x1e and the hit points back), get up, then
// hop or (the player near) startled
void stunned(AnimatedActor& actor, CritterFields& f) {
    switch (actor.step) {
    case 0: {
        const int16_t hp = actor.hitPoints;
        actor.anim = 6;
        actor.velocity.z = 0;
        if (hp < 0) {
            f.timer = uint16_t(int16_t(kCatchableHopTicks[rngStep() & 0xf]) + 0x1e);
            actor.hitPoints = actor.maxHitPoints;
        } else {
            const int8_t t = kCatchableHopTicks[rngStep() & 0xf];
            f.timer = uint16_t(int16_t((f.bits & kCritterStrongHit) ? int8_t(t >> 2) : int8_t(t >> 3)));
        }
        ++actor.step;
        break;
    }
    case 1: {
        const uint16_t t = f.timer;
        f.timer = uint16_t(t - 1u);
        if (t == 0) {
            actor.anim = 7;
            actor.velocity.z = int16_t(0xfff5);
            ++actor.step;
        }
        break;
    }
    case 2:
        if (actor.animEnded()) {
            f.hits = 0;
            f.bits &= uint8_t(~kCritterHurtBits);
            restartBehaviour(actor, CritterBehaviour::Hop);
            if (planarDistance(actor.pos, player().pos) < 0x200) // exe 0x4d4560
                enterBehaviour(actor, CritterBehaviour::Startled);
        }
        break;
    default: break;
    }
    move(actor);
}
// 0x4d40b0: startled -- face the player, jump back, land, hop
void startled(AnimatedActor& actor, CritterFields& f) {
    switch (actor.step) {
    case 0:
        actor.anim = 0;
        actor.velocity.y = 0;
        actor.velocity.z = 0;
        f.heading = int16_t(angleOf(player().pos, actor.pos));
        ++actor.step;
        break;
    case 1:
        if (iabs(turnYawTo(actor.rotation.y, uint32_t(int32_t(f.heading)), 0x180)) < 0x20) {
            actor.anim = 8;
            ++actor.step;
        }
        break;
    case 2:
        if (actor.animEnded()) {
            actor.anim = 9;
            actor.velocity.y = int16_t(0xfd00);
            actor.velocity.z = int16_t(0xfc48);
            ++actor.step;
        }
        break;
    case 3:
        if (actor.airborne == 0 && actor.animEnded()) {
            actor.anim = 0xa;
            actor.velocity.y = 0;
            actor.velocity.z = 0;
            ++actor.step;
        }
        break;
    case 4:
        if (actor.animEnded()) {
            f.bits &= uint8_t(~kCritterHurtBits);
            restartBehaviour(actor, CritterBehaviour::Hop);
        }
        break;
    default: break;
    }
    move(actor);
}
void catchableBehaviour(const PlayerEnv& env, AnimatedActor& actor, CritterFields& f, uint32_t n) {
    switch (CritterBehaviour(n)) {
    case CritterBehaviour::Choose: catchableChoose(actor, f); return;
    case CritterBehaviour::Stand: stand(actor, f, kCatchableStandTicks, true); return; // 0x4d3610
    case CritterBehaviour::Walk: catchableWalk(actor, f); return;
    case CritterBehaviour::Hop: hop(env, actor, f, kCatchableHopTicks); return; // 0x4d3850
    case CritterBehaviour::Leap: catchableLeap(actor, f); return;
    case CritterBehaviour::KnockedBack: knockedBack(actor, f); return;
    case CritterBehaviour::Stunned: stunned(actor, f); return;
    case CritterBehaviour::Startled: startled(actor, f); return;
    default: notPortedAt(kStoryPort, "0x4d3240 behaviour (table 0x895490)", n);
    }
}
// 0x4d4320: flying (thrown / dropped): bounces off walls, lands, gets up (stunned)
void carriedFlying(const PlayerEnv& env, AnimatedActor& actor, CritterFields& f) {
    const uint32_t res = actor.actorHit;
    if ((res & 0xf00) && (f.bits & kCritterBounceArmed)) {
        reflectRotationOffWalls(actor.rotation, res); // exe 0x410740
        f.bits &= uint8_t(~kCritterBounceArmed);
        actor.velocity.z = int16_t(actor.velocity.z >> 1);
    }
    switch (actor.step) {
    case 0:
        if (f.hit.result & kHitThrownInto) {
            actor.rotation.y = int16_t((uint16_t(actor.rotation.y) - 0x800u) & 0xfff);
            spawnHitSparkWithoutSize(kActorSweepGroup2, actor.pos.x.whole, actor.pos.y.whole,
                                     actor.pos.z.whole); // exe 0x5e5400
        }
        if (actor.airborne == 0) {
            actor.velocity.y = 0;
            actor.velocity.z = int16_t(actor.velocity.z >> 1);
            ++actor.step;
        }
        break;
    case 1:
        if (actor.animEnded()) {
            actor.anim = 6;
            actor.rotation.y = int16_t((uint16_t(actor.rotation.y) + 0x400u) & 0xfff);
            actor.enterState(uint8_t(CatchableState::Run));
            f.hits = 0;
            f.bits &= uint8_t(~(kCritterHurtBits | kCritterThrown));
            restartBehaviour(actor, CritterBehaviour::Stunned);
        }
        break;
    default: break;
    }
    moveAlongYaw(actor.pos, actor.rotation.y, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e36f0
    f.bits |= kCritterBounceArmed;
    critterMove(env, actor, f);
}
// 0x4d41f0: in the hands (animation 2) until let go: thrown / dropped
void carriedHeld(const PlayerEnv& env, AnimatedActor& actor, CritterFields& f) {
    if (actor.step == 0) {
        actor.anim = 2;
        f.timer = uint16_t((rngStep() & 7) + 0xc);
        ++actor.step;
    }
    carryAtHands(actor, 0, 0, 0); // exe 0x573ab0
    const uint16_t t = f.timer;
    f.timer = uint16_t(t - 1u);
    if (t == 0) f.timer = uint16_t((rngStep() & 7) + 0xc);
    const CarryRelease r = carryReleased(); // exe 0x573950
    if (r == CarryRelease::Held) return;
    f.bits &= uint8_t(~kCritterLifted);
    if (r == CarryRelease::Thrown) {
        int32_t v[3];
        playerAimVector(0, -0x240, -0x400, v); // exe 0x5e3e50
        actor.velocity.y = int16_t(v[1]);
        actor.velocity.z = int16_t(v[2]);
        f.bits |= kCritterThrown;
    } else {
        actor.velocity.y = int16_t(0xfe00);
        actor.velocity.z = int16_t(0xff00);
    }
    actor.rotation.y = player().rotation.y;
    actor.airborne = 1;
    carryDropFromPlayer(actor); // exe 0x573990
    actor.subState = uint8_t(CarriedSub::Flying);
    actor.restartStep();
    carriedFlying(env, actor, f);
}
void catchableCritter(const PlayerEnv& env, AnimatedActor& actor) { // 0x4d3240
    CritterFields& f = classState<CritterFields>(actor);
    savePrevious(actor);
    f.bits &= uint8_t(~kCritterBounceArmed);
    switch (CatchableState(actor.state)) {
    case CatchableState::Init: // 0x4d3310: freed when it was caught already (variant 1: its index's flag)
        if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
            freeActor(actor);            // exe 0x5e1490
            return;
        }
        if (actor.variant == 1 && storyFlag(kCaughtFlags[size_t(int32_t(CritterParams(actor).index))])) {
            freeActor(actor);
            return;
        }
        critterInit(actor, f, stateAddress(TableId::IceRuinCrittersCatchableBox), true);
        break;
    case CatchableState::Run: { // 0x4d3450
        if (critterHurt(actor, f) != 0) {
            if (f.hitsInAll < 0x7fff) ++f.hitsInAll;
            enterBehaviour(actor, CritterBehaviour::KnockedBack);
        }
        catchableBehaviour(env, actor, f, actor.subState);
        critterMove(env, actor, f);
        if (playerLiftTargetCheck(actor, 0)) { // exe 0x573a60
            actor.enterState(uint8_t(CatchableState::Carried));
            f.bits |= kCritterLifted;
        }
        if (player().talksTo(actor) && eventState().active == 0) {
            uint32_t n = storyFlag(kFlagCaughtFirst) ? 1 : 0;
            if (storyFlag(kFlagCaughtSecond)) ++n;
            eventStartAtActor(actor, kCaughtEventParam, 0, uint16_t(n + kCaughtMessage)); // exe 0x543af0
            actor.enterState(uint8_t(CatchableState::Caught));
        }
        break;
    }
    case CatchableState::Carried: // 0x4d41d0
        switch (CarriedSub(actor.subState)) {
        case CarriedSub::Held: carriedHeld(env, actor, f); break;
        case CarriedSub::Flying: carriedFlying(env, actor, f); break;
        default: notPortedAt(kStoryPort, "0x4d3240 carried sub-state (table 0x8954c0)", actor.subState);
        }
        break;
    case CatchableState::Caught: // 0x4d4430: a puff, the notice sound, the first clear caught flag, freed next tick
        puff(actor);
        soundPlayAtPos(kNoticeSound, actor.pos);
        if (!storyFlag(kFlagCaughtFirst)) setStoryFlag(kFlagCaughtFirst);
        else if (!storyFlag(kFlagCaughtSecond)) setStoryFlag(kFlagCaughtSecond);
        else setStoryFlag(kFlagCaughtThird);
        actor.state = uint8_t(CatchableState::Free);
        break;
    case CatchableState::Free: freeActor(actor); break; // 0x468fa0 -> exe 0x5e1490
    default: notPortedAt(kStoryPort, "0x4d3240 state (table 0x895474)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    animate(actor, f);
    // exe 0x5f20f0(a, 0x38, 0xa0, 0) unless carried and held: the shadow (render output, not ported)
    critterHitEntry(actor, f); // exe 0x4d4590
}

} // namespace

void sceneCritterUpdate(const PlayerEnv& env, uint32_t actor) { sceneCritter(env, animatedActorAt(actor)); }
void catchableCritterUpdate(const PlayerEnv& env, uint32_t actor) { catchableCritter(env, animatedActorAt(actor)); }

} // namespace od::port
