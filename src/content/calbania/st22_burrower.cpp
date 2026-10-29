// The ST22 burrower class exe 0x457480 (see st22_burrower.hpp), transcribed from dash2.exe one function per exe
// function. An entry past the class's own states (the exe's tables run into the next class's tables / data) throws
// notPortedAt.
//
// exe 0x457480 (ST22 type 8 sub 0): the tint neutral, the previous pose, states [0x88ee40 + +8 * 4]: 0 0x4575f0 bind
//   (model by [0x88ee7c + the spawn byte], box 0x88ee70, hit points from its stat row << 2, 0x200 below the ground,
//   facing away from the player, registered (0x5e5c10)), 1 0x457700 (subs [0x88ee4c + +9 * 4]: 0 0x457740 rise, 1
//   0x457820 walk at the player (variant 0), 2 0x457940 walk around the neighbours (variant 1), 3 0x457b00 wait / back
//   off, 4 0x457bb0 throw (a rock every 0x40 ticks), 5 0x457c70 flinch, 6 0x457cb0 sink, 7 0x457d90 die), 2 0x457720
//   (subs [0x88ee64]: 0 0x457cb0 sink away, 1 0x457d90 die). Then (while live): the ground followed unless kNoGround,
//   the hit 0x4580e0 (capsule bone 1 .. position, flinch at half hp, death sound 0x8e), animation, the carried pieces
//   0x457e60 (kLungeRequest: pool-0xa0 type 4 at bone 0; kThrowRequest: a pool-0x40 type 4 at bone 1, held for 0x20
//   ticks), the step sound 0xd3 on animation frames.
//
// notPorted where the exe would read through a null pointer (bone points, the stat row) or index past a table.
#include "content/calbania/st22_burrower.hpp"
#include "content/calbania/st22_burrower_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/address_forms.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/charge_glow.hpp"
#include "game/charge_glow_state.hpp"
#include "game/distances.hpp"
#include "game/encounter.hpp"
#include "game/explosions.hpp"
#include "game/enemy_neighbours.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/room_grid.hpp"
#include "game/step_dust.hpp"
#include "game/vectors.hpp"

namespace od::port {

using namespace st22_burrower_state;

namespace {

using address_forms::E;

constexpr const char* kActorsPort = "st22 burrower port"; // the area of its port-gap errors

constexpr uint16_t kBurrowerNoGround = 0x01;   // the ground not followed
constexpr uint16_t kBurrowerNoDamage = 0x02;
constexpr uint16_t kBurrowerNoFlinch = 0x08;
constexpr uint16_t kLungeRequest = 0x10;       // the charge glow (game/charge_glow) at bone 0
constexpr uint16_t kLungeRelease = 0x20;       // ... let go
constexpr uint16_t kThrowRequest = 0x40;       // a rock at bone 1
constexpr uint16_t kRockRelease = 0x80;        // the rock held let go
constexpr uint16_t kPieceRequestsCleared = 0xff0f;
constexpr uint8_t kRisingBits = uint8_t(kBurrowerNoGround | kBurrowerNoDamage);
constexpr uint8_t kFlinchBits = uint8_t(kLungeRelease | kRockRelease);
constexpr uint8_t kSinkBits = uint8_t(kRisingBits | kLungeRelease | kRockRelease);
constexpr uint8_t kDyingBits = uint8_t(kBurrowerNoDamage | kLungeRelease | kRockRelease);

enum class BurrowerState : uint8_t { Bind = 0, Active = 1, Leaving = 2 };
enum class ActiveSub : uint8_t { Rise = 0, Chase = 1, Circle = 2, Wait = 3, Throw = 4, Flinch = 5, Sink = 6, Die = 7 };
enum class LeavingSub : uint8_t { Sink = 0, Die = 1 };
enum class CircleStep : uint8_t { Start = 0, Walk = 1, Avoid = 2 };

constexpr uint8_t kAnimIdle = 0, kAnimWalk = 1, kAnimThrow = 2, kAnimWait = 3, kAnimTurn = 4, kAnimSink = 5,
                  kAnimRise = 6, kAnimHurt = 7;
constexpr uint8_t kNoAnim = 0xff;
const DataTable<ScalarTable<uint8_t, 0x100>> burrowerModels{TableId::St22BurrowerModels}; // the model variant by the spawn byte
constexpr uint8_t kRockType = 4, kExplosionType = 8;
constexpr uint16_t kSoundHurt = 0x8d, kSoundHurtHard = 0x8e, kSoundRise = 0xa3, kSoundSink = 0xa4, kSoundStep = 0xd3;
constexpr uint32_t kHitDamageMask = 0xfff, kHitByPlayer = 0x40000;
constexpr uint8_t kRockHoldTicks = 0x20;
constexpr int16_t kRiseDepth = 0x200;

int16_t t16(int64_t v) { return int16_t(uint16_t(uint64_t(v))); }
// the stat row of its type and spawn byte (a null row would be read by the exe)
AnimatedActorStats statsOf(const AnimatedActor& actor) {
    if (!hasAnimatedStats(actor.type))
        notPortedAt(kActorsPort, "stat row [*0xa714c0 + type * 4] is null, type", actor.type);
    return animatedStats(actor.type, actor.spawnByte);
}
// exe 0x5e1810 (a null result would be dereferenced by the exe)
ShortVec4& bone(const AnimatedActor& actor, uint32_t b) { return bonePoint(actor, b, kActorsPort); }
int ground(const AnimatedActor& actor) { return actorGround(*E, actor); } // exe 0x40f190
// exe 0x40b980(dz * dz + dx * dx) of the player's integer x / z against the actor's
int playerDistance(const AnimatedActor& actor) {
    const int dx = int(player().pos.x.whole) - actor.pos.x.whole, dz = int(player().pos.z.whole) - actor.pos.z.whole;
    return isqrt(int32_t(uint32_t(dz * dz) + uint32_t(dx * dx)));
}
int angleFromPlayer(const AnimatedActor& actor) { return angleOf(player().pos, actor.pos); }
// |((yaw - to) & 0xfff) as -0x7ff..0x800|
int absWrap(uint32_t yaw, uint32_t to) {
    int v = int((yaw - to) & 0xfff);
    if (v >= 0x801) v -= 0x1000;
    return v < 0 ? -v : v;
}
// the rock (the stage's pool-0x40 type 4): its damage, its texture word
// the rock (the stage's pool-0x40 type 4): its damage, held, a byte its reader is not traced, its spin; its texture
// word (the hit box's first word)
struct RockFields {
    explicit RockFields(const ProjectileActor& actor) : RockFields(const_cast<ProjectileActor&>(actor)) {}
    explicit RockFields(ProjectileActor& actor)
        : damage(actor.params[0]), held(actor.params[1]), unreadE(actor.params[2]), spin(actor.params[3]),
          textureWord(actor.hitBox.result) {}
    uint8_t& damage;
    uint8_t& held;
    uint8_t& unreadE;
    uint8_t& spin;
    uint32_t& textureWord;
};
enum class RockState : uint8_t { Init = 0, Held = 1 };

// exe 0x4580e0: the hits (unless kBurrowerNoDamage): damage by the player -> the flash, the hit points; dead -> state
// 2 sub 1 (sound 0x8e); at or below half its hit points in state 1 without kBurrowerNoFlinch -> flinch (sub 5, sound
// 0x8e); else sound 0x8d. Then the capsule bone 1 .. the position (0x10 / 0x10), its group, damage (its attack |
// 0x8000) << 2; the box cleared, its previous x / z
void burrowerHit(AnimatedActor& actor, BurrowerFields& f) {
    if (!(f.bits & kBurrowerNoDamage)) {
        const uint32_t r = f.hitBox.result;
        const uint32_t d = r & kHitDamageMask;
        if (d != 0 && (r & kHitByPlayer)) {
            actor.tint = kTintFlash;
            uint16_t snd;
            if (hitPointsAdd(actor, -int(d)) != 0) {
                actor.enterState(uint8_t(BurrowerState::Leaving));
                actor.subState = uint8_t(LeavingSub::Die);
                snd = kSoundHurtHard;
            } else if ((f.bits & kBurrowerNoFlinch) || actor.hitPoints >= int16_t(actor.maxHitPoints >> 1) ||
                       BurrowerState(actor.state) != BurrowerState::Active) {
                snd = kSoundHurt;
            } else {
                actor.subState = uint8_t(ActiveSub::Flinch);
                actor.enterStep(0);
                f.bits = uint16_t((f.bits & 0xffafu) | 0xa8u);
                snd = kSoundHurtHard;
            }
            soundPlayAtPos(snd, actor.pos);
        }
    }
    const int16_t pos[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
    const ShortVec4& b1 = bone(actor, 1);
    const int16_t pb[3] = {b1.x, b1.y, b1.z};
    const uint32_t param = ((uint32_t(actor.slot) << 6 | (f.bits & kBurrowerNoDamage)) << 0x12) | 0x402000u;
    const uint32_t param2 = (uint32_t(statsOf(actor).attack) | 0x8000u) << 2;
    hitListSubmitCapsule(pb, 0x10, pos, 0x10, f.hitBox, param, param2); // exe 0x5e4190
    f.hitBox.result = 0;
    f.hitBox.previousX = pos[0];
    f.hitBox.previousZ = pos[2];
}
// the 16.16 point of a bone's point, y raised by `lift`
void placeAtBone(Fixed16Vec3& pos, const ShortVec4& b, int lift) {
    pos.x = int32_t(uint32_t(int32_t(b.x)) << 16);
    pos.y = int32_t(uint32_t(int32_t(b.y) - lift) << 16);
    pos.z = int32_t(uint32_t(int32_t(b.z)) << 16);
}
// exe 0x457e60: the carried pieces: the charge glow at bone 0, the rock in the hand at bone 1
void burrowerPieces(AnimatedActor& actor, BurrowerFields& f) {
    EncounterState& encounter = encounterState();
    if (f.lungeSlot == 0) {
        if (f.bits & kLungeRequest) {
            if (const uint32_t address = allocActorA0(kActorStageClass)) { // exe 0x5e13a0: the stage's effect type 4
                EffectActor& e = effectActorAt(address);
                charge_glow_state::ChargeGlowFields& lunge = classState<charge_glow_state::ChargeGlowFields>(e);
                const ShortVec4& b0 = bone(actor, 0);
                e.flags = kActorLiveDrawn;
                e.type = kChargeGlowStageType;
                e.loopFlags |= uint8_t(kActorSweepGroup2 | kActorLateLoop);
                e.subtype = 0;
                e.variant = 0;
                lunge.held = 1;
                e.pos.x = b0.x;
                e.pos.y = b0.y;
                e.pos.z = b0.z;
                lunge.damage = int16_t(uint32_t(statsOf(actor).strongAttack) << 2);
                lunge.clearHitBoxWords();
                f.lungeSlot = e.slot;
                encounter.markAttackLock(kAttackLockAttack);
            }
        }
    } else if (f.bits & kLungeRelease) {
        f.lungeSlot = 0;
    } else {
        if (const uint32_t address = actorBySlot(f.lungeSlot)) { // exe 0x5e0fa0
            EffectActor& e = effectActorAt(address);
            const ShortVec4& b0 = bone(actor, 0);
            classState<charge_glow_state::ChargeGlowFields>(e).held = 1;
            e.pos.x = b0.x;
            e.pos.y = b0.y;
            e.pos.z = b0.z;
        }
        encounter.markAttackLock(kAttackLockAttack);
    }
    if (f.rockSlot != 0) {
        const uint32_t address = actorBySlot(f.rockSlot);
        if (address) {
            ProjectileActor& rock = projectileActorAt(address);
            if ((rock.flags & kActorLive) && rock.type == kRockType && RockState(rock.state) == RockState::Held &&
                !(f.bits & kRockRelease)) {
                const ShortVec4& b1 = bone(actor, 1);
                rock.subState = 1;
                placeAtBone(rock.pos, b1, 0x50);
                if (f.rockHold != 0) {
                    f.bits &= kPieceRequestsCleared;
                    --f.rockHold;
                    return;
                }
            }
        }
        f.rockSlot = 0;
        f.rockHold = 0;
    } else if (f.bits & kThrowRequest) {
        f.rockSlot = 0;
        if (const uint32_t address = allocActor40(kActorStageClass)) { // exe 0x5e1280: the stage's pool-0x40 type 4
            ProjectileActor& rock = projectileActorAt(address);
            RockFields r(rock);
            const ShortVec4& b1 = bone(actor, 1);
            rock.flags = kActorLiveDrawn;
            rock.type = kRockType;
            rock.loopFlags |= kActorSweepGroup2;
            rock.subtype = 0;
            rock.variant = f.rockVariant;
            const uint8_t damage = statsOf(actor).shotAttack;
            r.held = 1;
            r.damage = uint8_t(damage << 1);
            r.unreadE = 0;
            r.spin = uint8_t((rngStep() & 1) + 1u);
            placeAtBone(rock.pos, b1, 0x50);
            r.textureWord = loadedScalar<uint32_t>(actor.extraGeometry + 4) | 0x20u; // its model's second texture word
            f.bits &= kPieceRequestsCleared;
            f.rockSlot = rock.slot;
            f.rockHold = kRockHoldTicks;
            return;
        }
    }
    f.bits &= kPieceRequestsCleared;
}
// exe 0x458230: the walls (a blocked push-out: back to the previous position); a wall result sets the heading by its
// side and the turn's ticks = the turn / 128; turned toward the heading (0x20 a tick, snapped within), forward 0xc0
void burrowerWalk(AnimatedActor& actor, BurrowerFields& f) {
    const uint32_t v = f.pushResult;
    if (int32_t(v) < 0) {
        actor.pos = actor.prevPos;
        return;
    }
    if (v != 0 && f.wallTurn == 0) {
        switch (v & 0xf00) {
        case 0x100: f.heading = 0x400; break;
        case 0x200: f.heading = int16_t(0xc00); break;
        case 0x400: f.heading = 0; break;
        case 0x500: f.heading = 0x200; break;
        case 0x600: f.heading = int16_t(0xe00); break;
        case 0x800: f.heading = 0x800; break;
        case 0x900: f.heading = 0x600; break;
        case 0xa00: f.heading = int16_t(0xa00); break;
        default: break;
        }
        const int d = absWrap(uint16_t(actor.rotation.y), uint16_t(f.heading));
        f.wallTurn = int16_t(d >> 7);
    }
    const int16_t h = f.heading;
    if (absWrap(uint16_t(actor.rotation.y), uint32_t(int32_t(h))) >= 0x21) turnToward(actor.rotation.y, int(h), 0x20);
    else actor.rotation.y = h;
    integrateVelocity(actor, 0, 0, -0xc0);
    if (f.wallTurn > 0) --f.wallTurn;
}
// exe 0x4583a0: a throw decision (not frozen, a thrower): 1 in 4, 1 or 2 throws
int burrowerThrows(AnimatedActor& actor, BurrowerFields& f) {
    if (!(actor.runFlags & 0x20) && f.throws != 0) {
        if (!(rngStep() & 3)) {
            f.throwsLeft = uint8_t((rngStep() & 1) + 1u);
            return 1;
        }
    }
    return 0;
}
// the dust of the rise / the sink every 4 ticks at its ground, plus two ground effects on tick 0
void dustAtGround(AnimatedActor& actor, BurrowerFields& f) {
    if (f.timer & 3) return;
    const int32_t kind = groundKindUnder(actor.pos);
    spawnDustSpark(actor.pos.x.whole, f.groundY, actor.pos.z.whole); // exe 0x5e62c0
    if (uint16_t(f.timer) == 0) {
        spawnStepDust(actor.pos.x.whole, f.groundY, actor.pos.z.whole, kind); // exe 0x5e6300
        spawnStepDust(actor.pos.x.whole, f.groundY, actor.pos.z.whole, kind);
    }
}
// the walk sub-state of its variant (variant 0: Chase, else Circle)
uint8_t homeSub(const AnimatedActor& actor) { return actor.variant != 0 ? 2 : 1; }
int nearLimit(const AnimatedActor& actor) { return actor.variant != 0 ? 0xf0 : 0x400; }

// exe 0x457740: rises out of the ground
void burrowerRise(AnimatedActor& actor, BurrowerFields& f) {
    if (actor.step == 0) {
        actor.step = 1;
        f.timer = 0;
        soundPlayAtPos(kSoundRise, actor.pos);
    }
    actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) - 0x20u);
    dustAtGround(actor, f);
    ++f.timer;
    if (actor.pos.y.whole > f.groundY) return;
    actor.enterStep(0);
    actor.subState = homeSub(actor);
    f.bits &= 0xfffcu;
    f.timer = 0;
    f.groundY = 0;
}
// the end of a walk sub-state: the player far (variant v) -> sink away; near and no attack runs -> wait (the lunge);
// every 16 ticks a throw decision
void burrowerWalkEnd(AnimatedActor& actor, BurrowerFields& f, uint8_t sinkVariant, int nearDistance) {
    const int d = playerDistance(actor);
    if (actor.variant == sinkVariant && d >= 0xc00) {
        actor.enterState(uint8_t(BurrowerState::Leaving));
        actor.subState = uint8_t(LeavingSub::Sink);
        return;
    }
    if (d <= nearDistance && !(encounterState().attackLocksRecent() & kAttackLockAttack)) {
        actor.subState = uint8_t(ActiveSub::Wait);
        actor.enterStep(0);
        f.setBitsByte(uint8_t(kLungeRequest));
        return;
    }
    if (!(f.timer & 0xf) && burrowerThrows(actor, f)) {
        actor.subState = uint8_t(ActiveSub::Throw);
        actor.enterStep(0);
    }
}
// exe 0x457820 (variant 0): faces / walks at the player
void burrowerChase(AnimatedActor& actor, BurrowerFields& f) {
    if (actor.step == 0) {
        actor.step = 1;
        actor.anim = kAnimWalk;
        f.timer = 0;
    }
    const int16_t ang = t16(angleFromPlayer(actor));
    f.heading = ang;
    if (absWrap(uint16_t(actor.rotation.y), uint32_t(int32_t(ang))) >= 0x21) {
        turnToward(actor.rotation.y, int(ang), 0x20);
        actor.anim = kAnimTurn;
    } else {
        actor.rotation.y = ang;
        actor.anim = kAnimWalk;
    }
    ++f.timer;
    burrowerWalkEnd(actor, f, 0, 0x3ff);
}
// exe 0x457940 (variant 1): walks around the neighbours
void burrowerCircle(AnimatedActor& actor, BurrowerFields& f) {
    const CircleStep s = CircleStep(actor.step);
    if (s == CircleStep::Avoid) {
        if (actor.stepCounter == 0) {
            neighbourClassify(actor, f.neighbour); // exe 0x5e5f70
            actor.stepCounter = 1;
        }
        Actor& o = neighbourActor(f.neighbour); // exe 0x5e5cb0
        if (f.wallTurn == 0) {
            int h;
            if (f.neighbour & kNeighbourOpposite)
                h = headingFromOther(actor, o, neighbourSide(f.neighbour), 0x140) & 0xfff; // exe 0x5e5d90
            else
                h = angleFromPlayer(actor);
            f.heading = int16_t(h);
        }
        if (o.flags == 0 || ((uint16_t(o.pos.x.whole) ^ uint16_t(actor.pos.x.whole)) & 0xff80) ||
            ((uint16_t(o.pos.z.whole) ^ uint16_t(actor.pos.z.whole)) & 0xff80)) {
            actor.step = uint8_t(CircleStep::Walk);
            actor.stepCounter = 0;
        }
        burrowerWalk(actor, f);
    } else if (s == CircleStep::Start || s == CircleStep::Walk) {
        if (s == CircleStep::Start) {
            actor.step = uint8_t(CircleStep::Walk);
            actor.stepCounter = 0;
            actor.anim = kAnimIdle;
            f.timer = 0;
        }
        if (f.wallTurn == 0) f.heading = int16_t(angleFromPlayer(actor));
        burrowerWalk(actor, f);
        if (neighbourFind(actor, f.neighbour, 1) != 0) { // exe 0x5e5e00
            actor.step = uint8_t(CircleStep::Avoid);
            actor.stepCounter = 0;
        }
    }
    ++f.timer;
    burrowerWalkEnd(actor, f, 1, 0xef);
}
// exe 0x457b00: waits (0x5a / 0x1e ticks), then walks again once the player is out of reach
void burrowerWait(AnimatedActor& actor, BurrowerFields& f) {
    if (actor.step == 0) {
        actor.step = 1;
        actor.anim = kAnimWait;
        f.timer = actor.variant != 0 ? 0x1e : 0x5a;
    }
    if (f.timer == 0) {
        if (playerDistance(actor) < nearLimit(actor)) return;
        actor.enterStep(0);
        actor.subState = homeSub(actor);
        f.bits = uint16_t((f.bits & 0xffefu) | 0x20u);
        return;
    }
    --f.timer;
}
// exe 0x457bb0: throws: a rock every 0x40 ticks for its throws * 0x40 ticks
void burrowerThrow(AnimatedActor& actor, BurrowerFields& f) {
    if (actor.step == 0) {
        actor.step = 1;
        actor.anim = kAnimThrow;
        f.timer = int16_t(uint32_t(f.throwsLeft) << 6);
    }
    if (!(uint16_t(f.timer) & 0x3f)) f.setBitsByte(uint8_t(kThrowRequest));
    --f.timer;
    const int d = playerDistance(actor);
    if (f.timer != 0 && d <= 0xbff && nearLimit(actor) <= d) return;
    actor.enterStep(0);
    actor.subState = homeSub(actor);
    f.setBitsByte(uint8_t(kRockRelease));
    f.throwsLeft = 0;
}
// exe 0x457c70: flinches until the animation's end
void burrowerFlinch(AnimatedActor& actor, BurrowerFields& f) {
    if (actor.step == 0) {
        actor.step = 1;
        actor.anim = kAnimHurt;
        f.setBitsByte(kFlinchBits);
    }
    if (!actor.animEnded()) return;
    actor.enterStep(0);
    actor.subState = homeSub(actor);
}
// exe 0x457cb0: sinks away, freed 0x200 under its start
void burrowerSink(AnimatedActor& actor, BurrowerFields& f) {
    if (actor.step == 0) {
        actor.step = 1;
        actor.anim = kAnimSink;
        f.setBitsByte(kSinkBits);
        f.groundY = actor.pos.y.whole;
        f.timer = 0;
        soundPlayAtPos(kSoundSink, actor.pos);
    }
    actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + 0x20u);
    dustAtGround(actor, f);
    ++f.timer;
    if (int(actor.pos.y.whole) - f.groundY <= 0x1ff) return;
    freeActor(actor); // exe 0x5e1490
}
// exe 0x457d90: dies: an explosion (static global type 8) at bone 0, the drops after 0xf ticks, freed
void burrowerDie(AnimatedActor& actor, BurrowerFields& f) {
    if (actor.step == 0) {
        actor.step = 1;
        actor.anim = kAnimHurt;
        f.setBitsByte(kDyingBits);
        f.timer = 0;
        if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300
            StaticActor& e = staticActorAt(address);
            e.flags = kActorLive;
            e.type = kExplosionType;
            e.variant = 0;
            e.loopFlags |= kActorSweepGroup2;
            const ShortVec4& b0 = bone(actor, 0);
            e.pos.x.whole = b0.x;
            e.pos.y.whole = b0.y;
            e.pos.z.whole = b0.z;
            ExplosionParams explosion(actorHeaderAt(address));
            explosion.spread = 0x80;
            explosion.flashes = 0x10;
        }
    }
    actor.tint = 0x421f;
    if (f.timer == 0xf) {
        spawnDrops(actor.pos, statsOf(actor).dropTable); // exe 0x5f1270
        countDefeat();                                   // exe 0x5e53e0
        freeActor(actor);
        return;
    }
    ++f.timer;
}
// exe 0x4575f0: the model, its hit points, below the ground facing away from the player, registered
void burrowerBind(AnimatedActor& actor, BurrowerFields& f) {
    if (!bindActorModel(actor, burrowerModels[actor.spawnByte])) { // exe 0x5e0bc0
        freeActor(actor);
        return;
    }
    actor.clearClassBlock();
    actor.enterState(uint8_t(BurrowerState::Active));
    actor.subState = uint8_t(ActiveSub::Rise);
    actor.anim = kAnimRise;
    const int g = ground(actor);
    f.groundY = int16_t(g);
    actor.pos.y.whole = int16_t(g + kRiseDepth);
    actor.rotation.y = int16_t(angleFromPlayer(actor));
    f.setBitsByte(kRisingBits);
    actor.prevAnim = kNoAnim;
    const uint16_t hp =
        hasAnimatedStats(actor.type) ? uint16_t(uint32_t(animatedStats(actor.type, actor.spawnByte).hitPoints) << 2) : 0;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(TableId::St22BurrowerBox);
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.velocity.z = 0;
    actor.velocity.y = 0;
    actor.velocity.x = 0;
    f.hitBox.previousX = actor.pos.x.whole;
    f.hitBox.previousZ = actor.pos.z.whole;
    f.heading = int16_t(angleFromPlayer(actor));
    registerEnemy(actor.slot, 1); // exe 0x5e5c10
}
// exe 0x457480's body
void burrower(AnimatedActor& actor) {
    BurrowerFields& f = classState<BurrowerFields>(actor);
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.prevAnim = actor.anim;
    actor.savePreviousContact();
    switch (BurrowerState(actor.state)) { // [0x88ee40]
    case BurrowerState::Bind: burrowerBind(actor, f); break;
    case BurrowerState::Active: // exe 0x457700: [0x88ee4c + +9 * 4]
        switch (ActiveSub(actor.subState)) {
        case ActiveSub::Rise: burrowerRise(actor, f); break;
        case ActiveSub::Chase: burrowerChase(actor, f); break;
        case ActiveSub::Circle: burrowerCircle(actor, f); break;
        case ActiveSub::Wait: burrowerWait(actor, f); break;
        case ActiveSub::Throw: burrowerThrow(actor, f); break;
        case ActiveSub::Flinch: burrowerFlinch(actor, f); break;
        case ActiveSub::Sink: burrowerSink(actor, f); break;
        case ActiveSub::Die: burrowerDie(actor, f); break;
        default: notPortedAt(kActorsPort, "0x457700 sub past [0x88ee4c]", actor.subState);
        }
        break;
    case BurrowerState::Leaving: // exe 0x457720: [0x88ee64 + +9 * 4]
        switch (LeavingSub(actor.subState)) {
        case LeavingSub::Sink: burrowerSink(actor, f); break;
        case LeavingSub::Die: burrowerDie(actor, f); break;
        default: notPortedAt(kActorsPort, "0x457720 sub past [0x88ee64]", actor.subState);
        }
        break;
    default: notPortedAt(kActorsPort, "0x457480 state past [0x88ee40]", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (!(f.bits & kBurrowerNoGround)) {
        if (int32_t(actorTestMove(*E, actor)) >= 0) // exe 0x40f570
            f.pushResult = 0;
        else
            f.pushResult = actorPushOut(*E, actor); // exe 0x410960
        const int g = ground(actor);
        bool snap = true;
        if (int(actor.pos.y.whole) < g - 0x20) {
            actor.velocity.y += 3u;
            actor.pos.y.whole += uint16_t(actor.velocity.y);
            if (int(actor.pos.y.whole) <= g - 0x20) snap = false;
        }
        if (snap) {
            actor.pos.y.whole = int16_t(g);
            actor.velocity.y = 0;
        }
    }
    burrowerHit(actor, f);
    animateAsked(actor, 0);
    burrowerPieces(actor, f);
    // exe 0x5f20f0(actor, 0x40, 0x100, 0): shadow (render output)
    const uint8_t an = actor.anim, fr = uint8_t(actor.animKey.frame);
    if (an == kAnimIdle) {
        if (fr != 0xf && fr != 0x1f) return;
    } else {
        if (an != kAnimTurn) return;
        if (fr != 3 && fr != 9) return;
    }
    soundPlayAtPos(kSoundStep, actor.pos);
}

} // namespace

void burrowerUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    burrower(animatedActorAt(actor));
}

} // namespace od::port
