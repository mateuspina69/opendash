// The static classes of ST21 / ST22 and ST25 (see st21_props.hpp), transcribed from dash2.exe one function per exe
// function. An entry past a class's own states (the exe's tables run into the next class's tables) throws notPortedAt,
// as the other ports.
//
// The lure piece (exe 0x596e60, ST21 / ST22; states [0x8ce154]: 0 0x596ee0 (random yaw / pop velocity, boxes 0x8ce138
//   / 0x8ce148, life 0x100), 1 0x596ff0 (bounces; hit by 0x20000 -> spark and freed; liftable; thrown: a hit entry, else
//   lures the actors registered at its owner's parameter slots; blinks below 0x28 ticks of life), 2 0x5971c0 carried,
//   3 freed). Its lift block byte: no lift interaction while set (cleared every tick).
// The rising model (exe 0x5957d0, ST25; states [0x8ce04c]: 0 0x595810 init (its rest height and its start height the
//   floor - 0x13a / - 0x27a), 1 0x5958a0: by the cast timeline's step sub (its step id != 0xff) [0x8ce054]: 5 0x5958d0
//   rise / bob / stop, 6 / 7 0x595ae0 bob, 9 0x595b20 bob / sink, 10 0x595cf0 sink and drift). Dust: 4 pool-0xa0 type
//   3 variant 10 particles a tick while its dust ticks count down.
//
// Left out (render output): the shadows (exe 0x5f20f0) of both.
#include "content/calbania/st21_props.hpp"
#include "content/calbania/st21_props_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/address_forms.hpp"
#include "game/angles.hpp"
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/field_alias.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/vectors.hpp"

namespace od::port {

using namespace st21_props_state;

namespace {

using address_forms::E;

int16_t t16(int32_t v) { return int16_t(uint16_t(uint32_t(v))); }
void integerPoint(const Fixed16Vec3& p, int16_t* o) { // the integer parts of a 16.16 point
    o[0] = p.x.whole;
    o[1] = p.y.whole;
    o[2] = p.z.whole;
}
// the actor of a slot id (exe 0x5e0fa0) seen through the Actor layout (the exe reads it without checking the pool)
Actor& actorOfSlot(uint8_t slot) { return actorAt(actorBySlot(slot)); }

// the lurer's registered actors (its parameter slots) and their lure target (their first parameter)
struct LureOwnerFields {
    explicit LureOwnerFields(ActorHeader& actor) : lured(actor.params) {}
    StateArray<uint8_t, 4>& lured; // the owner's spawn parameters: the slots it lures
};
struct LuredFields {
    explicit LuredFields(const ActorHeader& actor) : LuredFields(const_cast<ActorHeader&>(actor)) {}
    explicit LuredFields(ActorHeader& actor) : target(actor.params[0]) {}
    FieldAlias<int8_t, uint8_t> target;
};
enum class LurePieceState : uint8_t { Init = 0, Bounce = 1, Carried = 2, Free = 3 };
constexpr int16_t kLurePieceLife = 0x100, kBlinkLife = 0x28;
constexpr int16_t kLurePieceScale = 0x280;
constexpr uint32_t kHitBreaks = 0x20000;   // the hit box's result: broken
constexpr uint32_t kWallLayer = 2;
constexpr int16_t kPieceGravity = 0x30;
constexpr int kBounceSpeed = 0x41;          // a landing at this vertical speed or more bounces
constexpr int kFallHeight = 0x11;

// exe 0x597280: the actors registered at the owner's parameter slots target it (or the nearer of it and their target)
void lure(StaticActor& actor, LurePieceFields& f) {
    const int8_t s = f.owner;
    if (s == 0) return;
    const LureOwnerFields owner(actorHeaderAt(actorBySlot(uint8_t(s)))); // exe 0x5e0fa0
    for (uint32_t k = 0; k < 4; ++k) {
        const uint8_t sl = owner.lured[k];
        if (sl == 0) continue;
        Actor& e = actorOfSlot(sl);
        LuredFields lured(e);
        const int8_t cur = lured.target;
        if (cur == 0) {
            lured.target = int8_t(actor.slot);
        } else if (cur != int8_t(actor.slot)) {
            const int dOther = distance3(e.pos, actorOfSlot(uint8_t(cur)).pos);
            if (distance3(e.pos, actor.pos) < dOther) lured.target = int8_t(actor.slot);
        }
    }
}
// exe 0x596ff0's body: broken by a hit -> a spark and freed; else the bounce, the ground, the lift, the hit entry /
// the lure
void lureBounce(StaticActor& actor, LurePieceFields& f) {
    if (f.hitBox.result & kHitBreaks) {
        spawnHitSparkWithoutSize(kActorSweepGroup2, actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole); // exe 0x5e5400
        actor.enterState(uint8_t(LurePieceState::Free));
        return;
    }
    actor.velocity.y = int16_t(actor.velocity.y + kPieceGravity);
    integrateVelocity(actor, actor.velocity.x, actor.velocity.y, actor.velocity.z);
    int16_t p[3], q[3];
    integerPoint(actor.pos, p);
    integerPoint(actor.prevPos, q);
    if (int32_t(testMoveBlocked(*E, p, q, actor.collisionBox, kWallLayer)) < 0) { // exe 0x40f5a0
        const uint32_t r = actorPushOutMask(*E, actor, kWallLayer);                      // exe 0x410980
        actor.wallHit = r;
        if (int32_t(r) < 0) actor.pos = actor.prevPos;
        if (reflectRotationOffWalls(actor.rotation, r) != 0) { // exe 0x410740
            const int v = actor.velocity.y;
            const int av = v < 0 ? -v : v;
            if (av >= kBounceSpeed) {
                actor.velocity.z = int16_t(actor.velocity.z >> 1);
                actor.velocity.y = int16_t((-v) >> 1);
                actor.airborne = 1;
                f.pieceFlags &= uint8_t(~kPieceThrown);
            } else {
                actor.velocity.z = 0;
                actor.velocity.y = 0;
                actor.airborne = 0;
            }
        }
    }
    const int g = actorGround(*E, actor); // exe 0x40f190
    if (actor.airborne == 0) {
        if (g - actor.pos.y.whole >= kFallHeight) actor.airborne = 1;
        else actor.pos.y = int32_t(uint32_t(g) << 16);
    }
    if (playerLiftTargetCheck(actor, 0)) { // exe 0x573a60
        actor.enterState(uint8_t(LurePieceState::Carried));
        actor.airborne = 0;
        actor.setDrawn(true);
        f.lifeBytes[0] = 0; // the life 0x100 (the exe's two byte stores)
        f.lifeBytes[1] = 1;
    }
    f.hitBox.previousX = actor.prevPos.x.whole;
    f.hitBox.result = 0;
    f.hitBox.previousZ = actor.prevPos.z.whole;
    if (f.pieceFlags & kPieceThrown) {
        const int16_t pt[3] = {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole};
        hitListSubmit(pt, f.hitBox, uint32_t(actor.slot) << 24 | 0x28000cu, 0x40001u); // exe 0x5e4120
    } else {
        lure(actor, f);
    }
}
// the life count at the end of exe 0x596ff0: blinks under 0x28, freed at 0
void lureLife(StaticActor& actor, LurePieceFields& f) {
    if (f.life >= kBlinkLife) actor.flags |= kActorDrawn;
    else actor.flags ^= kActorDrawn;
    const int16_t n = t16(f.life - 1);
    f.life = n;
    if (n == 0) actor.enterState(uint8_t(LurePieceState::Free));
}
// exe 0x596ff0 (its "byte 0 | 1" test is always true)
void lureBounceState(StaticActor& actor, LurePieceFields& f) {
    if (f.hitBox.result & kHitBreaks) {
        lureBounce(actor, f);
        return;
    }
    lureBounce(actor, f);
    lureLife(actor, f);
}
// exe 0x5971c0: carried until thrown (1) or put down
void lureCarried(StaticActor& actor, LurePieceFields& f) {
    carryAtHands(actor, 0, -0xe, 0); // exe 0x573ab0
    const int r = int(carryReleased()); // exe 0x573950
    if (r == 0) return;
    if (r != 1) {
        actor.velocity.x = 0;
        actor.velocity.y = int16_t(0xfe00);
        actor.velocity.z = int16_t(0xff00);
    } else {
        int32_t v[3];
        playerAimVector(0, -0x100, -0x400, v); // exe 0x5e3e50
        actor.velocity.x = 0;
        actor.velocity.y = int16_t(v[1]);
        actor.velocity.z = int16_t(v[2]);
        f.pieceFlags |= kPieceThrown;
    }
    actor.rotation.y = player().rotation.y;
    actor.airborne = 1;
    carryDropFromPlayer(actor); // exe 0x573990
    f.life = kLurePieceLife;
    actor.enterState(uint8_t(LurePieceState::Bounce));
    lureBounceState(actor, f);
}
// exe 0x596e60's body
void lurePiece(StaticActor& actor) {
    LurePieceFields& f = classState<LurePieceFields>(actor);
    actor.savePreviousPose();
    actor.savePreviousContact();
    actor.interaction = f.liftBlock != 0 ? 0 : kInteractLift;
    switch (LurePieceState(actor.state)) {
    case LurePieceState::Init: { // exe 0x596ee0
        if (!bindActorModel(actor, 0)) {
            freeActor(actor);
            break;
        }
        actor.clearClassBlock();
        actor.rotation.x = 0;
        const uint32_t r1 = rngStep();
        actor.rotation.z = 0;
        actor.rotation.y = int16_t(r1 & 0xfffu);
        actor.velocity.x = 0;
        const uint32_t r2 = rngStep();
        actor.velocity.y = int16_t(((r2 & 0xf) + uint32_t(int32_t(f.liftBlock))) * uint32_t(-0x20));
        const uint32_t r3 = rngStep();
        actor.onPlatform = 0;
        actor.collisionBox.pointAt(TableId::St21PropsLurePieceBox);
        actor.velocity.z = int16_t((0xcu - (r3 & 0xf) - uint32_t(int32_t(f.liftBlock))) * 0x20u);
        actor.scale.x = kLurePieceScale;
        actor.scale.y = kLurePieceScale;
        actor.scale.z = kLurePieceScale;
        actor.airborne = 1;
        actor.interactionBox.pointAt(TableId::St21PropsLurePieceLiftBox);
        f.life = kLurePieceLife;
        actor.enterState(uint8_t(LurePieceState::Bounce));
        break;
    }
    case LurePieceState::Bounce: lureBounceState(actor, f); break;
    case LurePieceState::Carried: lureCarried(actor, f); break;
    case LurePieceState::Free: freeActor(actor); break; // exe 0x5afdd0
    default: notPortedAt(kStoryPort, "static 0x596e60 state", actor.state);
    }
    f.liftBlock = 0;
    // not carried and drawn: exe 0x5f20f0(actor, 0x20, 0x100, 0), the shadow (render output)
}

enum class RisingModelState : uint8_t { Init = 0, Script = 1 }; // exe 0x595810 / 0x5958a0
enum class RiseSub : uint8_t { Rise = 0, Slow = 1, Bob = 2, Stopped = 3 };
enum class SinkSub : uint8_t { Start = 0, Bob = 1, Sink = 2, Sunk = 3 };
enum class DriftSub : uint8_t { Start = 0, Slow = 1, Drift = 2, Gone = 3 };
// the routines of the cast timeline's step subs (exe table 0x8ce054; null: none; the named routines of
// engine/routines.csv)
const DataTable<RoutineTable<0x100>> risingModelRoutines{TableId::St21RisingModelRoutines};
constexpr uint8_t kNoStep = 0xff;
constexpr int16_t kDustBurst = 4;
constexpr uint8_t kDustType = 3, kDustVariant = 10;

// y = the rest height - sin(the bob phase) * 0x20
void bob(StaticActor& actor, RisingModelFields& f) {
    const int d = (int32_t(sin64((uint32_t(uint8_t(f.bobPhase)) >> 1) & 0x3f)) << 5) >> 12;
    actor.pos.y.whole = int16_t(uint16_t(uint16_t(f.restY) - uint16_t(d)));
}
void bobStep(RisingModelFields& f) { f.bobPhase = uint16_t((uint32_t(uint8_t(f.bobPhase)) + 1u) & 0x7fu); }
// the dust burst while the dust ticks count down: 4 pool-0xa0 type 3 variant 10 around it
void dust(StaticActor& actor, RisingModelFields& f) {
    if (f.dustTicks <= 0) return;
    for (int i = 0; i < 8; i += 2) {
        const uint32_t address = allocActorA0(0); // exe 0x5e13b0
        if (!address) continue;
        EffectActor& e = effectActorAt(address);
        e.type = kDustType;
        e.subtype = 0;
        e.flags |= kActorLive;
        e.variant = kDustVariant;
        const int32_t r = int32_t((rngStep() & 0x1f) + 0xb4);
        const int32_t ang = (int32_t(f.dustTicks) + i) << 9;
        e.setParamsLowWord(uint16_t(ang)); // the particle's angle
        const uint32_t idx = ((rngStep() & 7) + uint32_t(ang >> 6)) & 0x3f;
        e.pos.x = int16_t(((int32_t(sin64(idx)) * r) >> 12) + actor.pos.x.whole);
        e.pos.y = int16_t((rngStep() & 3) - 0xc0);
        e.pos.z = int16_t(actor.pos.z.whole - int16_t((int32_t(cos64(idx)) * r) >> 12));
    }
    f.dustTicks = int16_t(f.dustTicks - 1);
}
// exe 0x5958d0 (script 5): rises, slows, bobs, stops on the cue
void rise(StaticActor& actor, RisingModelFields& f) {
    const RiseSub st = RiseSub(actor.subState);
    if (st == RiseSub::Rise) {
        integrateVelocity(actor, 0, actor.velocity.y, 0);
        if (actor.pos.y.whole >= f.startY) ++actor.subState;
    } else if (st == RiseSub::Slow) {
        actor.velocity.y = int16_t(actor.velocity.y - 8);
        if (actor.velocity.y <= 0x40) {
            actor.velocity.y = 0x40;
            if (actor.step == 0 && int(actor.pos.y.whole) >= int(f.restY) - 0x60) {
                f.dustTicks = kDustBurst;
                ++actor.step;
            }
        }
        integrateVelocity(actor, 0, actor.velocity.y, 0);
        if (actor.pos.y.whole >= f.restY) {
            ++actor.subState;
            actor.pos.y.whole = f.restY;
            f.bobPhase = 0;
        }
    } else if (st == RiseSub::Bob) {
        bob(actor, f);
        if (f.stop != 0 && f.bobPhase == 0) {
            f.stop = 0;
            actor.subState = uint8_t(RiseSub::Stopped);
            actor.velocity.y = 0;
        } else {
            bobStep(f);
        }
    }
    dust(actor, f);
}
// exe 0x595b20 (script 9): bobs, then sinks on the cue
void sink(StaticActor& actor, RisingModelFields& f) {
    if (scriptState().part<CastTimelineTick>().stepTick == 0) {
        actor.subState = uint8_t(SinkSub::Start);
        actor.step = 0;
    }
    const SinkSub st = SinkSub(actor.subState);
    bool bobbing = false;
    if (st == SinkSub::Start) {
        f.bobPhase = 0;
        ++actor.subState;
        actor.velocity.y = 0;
        bobbing = true;
    } else if (st == SinkSub::Bob) {
        bobbing = true;
    } else if (st == SinkSub::Sink) {
        actor.velocity.y = int16_t(actor.velocity.y + 8);
        if (actor.velocity.y >= 0x100) {
            actor.velocity.y = 0x100;
            actor.subState = uint8_t(SinkSub::Sunk);
        }
    }
    if (bobbing) {
        bob(actor, f);
        if (f.stop != 0 && f.bobPhase == 0) {
            f.stop = 0;
            f.dustTicks = kDustBurst;
            ++actor.subState;
        } else {
            bobStep(f);
        }
    }
    integrateVelocity(actor, 0, -int(actor.velocity.y), 0);
    dust(actor, f);
}
// exe 0x595cf0 (script 10): sinks and drifts away
void drift(StaticActor& actor) {
    if (scriptState().part<CastTimelineTick>().stepTick == 0) actor.subState = uint8_t(DriftSub::Start);
    DriftSub st = DriftSub(actor.subState);
    if (st == DriftSub::Start) {
        actor.velocity.y = 0x100;
        actor.velocity.z = 0;
        actor.subState = uint8_t(DriftSub::Slow);
        st = DriftSub::Slow;
    }
    if (st == DriftSub::Slow) {
        actor.velocity.y = int16_t(actor.velocity.y - 0xc);
        if (actor.velocity.y <= 0) {
            actor.velocity.y = 0;
            ++actor.subState;
        }
    } else if (st == DriftSub::Drift) {
        actor.velocity.z = int16_t(actor.velocity.z + 0x20);
        if (actor.velocity.z >= 0x300) {
            actor.velocity.z = 0x300;
            actor.subState = uint8_t(DriftSub::Gone);
        }
    }
    integrateVelocity(actor, 0, -int(actor.velocity.y), -int(actor.velocity.z));
}
// exe 0x5958a0(actor, its block, the script block): by the cast timeline's step sub
void risingModelScript(StaticActor& actor, RisingModelFields& f) {
    if (scriptState().part<CastTimelineStep>().stepId == kNoStep) return;
    const RoutineRef& routine = risingModelRoutines[scriptState().part<CastTimelineStep>().stepSub];
    if (routine.null()) return;
    switch (routine.id()) {
    case RoutineId::St21RisingModelRise: rise(actor, f); return;
    case RoutineId::St21RisingModelBob:
        bob(actor, f);
        bobStep(f);
        return;
    case RoutineId::St21RisingModelSink: sink(actor, f); return;
    case RoutineId::St21RisingModelDrift: drift(actor); return;
    default:
        notPortedAt(kStoryPort, "static 0x5957d0 script table entry (another class's states)", scriptState().part<CastTimelineStep>().stepSub);
    }
}
// exe 0x5957d0's body
void risingModel(StaticActor& actor) {
    RisingModelFields& f = classState<RisingModelFields>(actor);
    switch (RisingModelState(actor.state)) {
    case RisingModelState::Init: { // exe 0x595810
        if (!bindActorModel(actor, 0)) {
            freeActor(actor);
            return;
        }
        actor.clearClassBlock();
        actor.airborne = 1;
        actor.flags |= kActorScenery;
        actor.enterState(uint8_t(RisingModelState::Script));
        actor.collisionBox.pointAt(TableId::St21PropsRisingModelBox);
        actor.velocity.y = int16_t(0x100 - int(f.riseBias));
        const int g = actorGround(*E, actor); // exe 0x40f190
        f.restY = int16_t(g - 0x13a);
        f.startY = int16_t(g - 0x27a);
        risingModelScript(actor, f);
        break;
    }
    case RisingModelState::Script: risingModelScript(actor, f); break;
    default: notPortedAt(kStoryPort, "static 0x5957d0 state", actor.state);
    }
    // drawn: exe 0x5f20f0(actor, 0xc0, 0x100, 0), the shadow (render output)
}

} // namespace

void lurePieceUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    lurePiece(staticActorAt(actor));
}
void st25RisingModelUpdate(const PlayerEnv& env, uint32_t actor) {
    E = &env;
    risingModel(staticActorAt(actor));
}

} // namespace od::port
