// The crash landing's cutscene cast (ST3A, script 6; see crash_landing.hpp): every class keys its animation, its face
// (the texture tracks), its placements and its motion on the script block's step sub and step tick. The props and the
// Flutter's falling pieces (pool 0x20 / 0x60), Roll, the crew, the flying craft (each with its linked second body /
// static and their exhaust puffs) and the jets with their bursts.
#include "content/intro/crash_landing.hpp"
#include "content/intro/crash_landing_cast_state.hpp"

#include "content/intro/opening_effects.hpp"
#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/cutscene_actor.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/matrix.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace crash_landing_cast_state;

namespace {

// the script's timeline the cast keys on
uint8_t stepSub() { return scriptState().stepSub; }
// ... and its tick
int32_t stepTick() { return scriptState().stepTick; }

void setScale(AnimatedActor& a, int16_t s) { // z, y, x (the exe's order)
    a.scale.z = s;
    a.scale.y = s;
    a.scale.x = s;
}

// ---- the props (exe 0x48a2f0, pool 0x20, variants 0x8923e8)
// ----------------------------------------------------------
// every variant's state 0 binds the model (box 0x892408), animation 0, the motion cleared and a scale; then the step
// tick moves them; the class adds the accelerations, moves and steps the animation
const DataTable<RoutineTable<7>> propVariants{TableId::CrashLandingCastPropVariants};
constexpr int16_t kPropDrift = -0x2f;
constexpr uint8_t kTrailPuffKind = 1, kTrailPuffLife = 4;

// a prop's state 0: bound (box 0x892408, animation 0, the motion cleared), its scale
bool propInit(AnimatedActor& a, int16_t scale) {
    if (!castBind(a, stateAddress(TableId::CrashLandingCastPropBox), 0, true)) return false;
    setScale(a, scale);
    return true;
}

// +8 of the props: bound, waiting for their tick, moving, coasting, diving (each variant uses its first ones)
enum class PropState : uint8_t { Bind = 0, Wait = 1, Move = 2, Coast = 3, Dive = 4 };

// exe 0x48a2f0: the variant, then the motion and the animation
void crashProp(AnimatedActor& a) {
    castSavePose(a);
    const int32_t t = stepTick();
    switch (propVariants->idAt(a.variant)) {
    case RoutineId::CrashCastPropDrifting:
        if (PropState(a.state) == PropState::Bind) {
            if (propInit(a, 0x800)) a.velocity.z = kPropDrift;
        } else if (PropState(a.state) == PropState::Wait && t == 0x182) {
            a.velocity.z = 0;
            a.state = uint8_t(PropState::Move);
        }
        break;
    case RoutineId::CrashCastPropFalling:
        switch (PropState(a.state)) {
        case PropState::Bind: propInit(a, 0x800); break;
        case PropState::Wait:
            if (t == 0x21) {
                a.velocity.z = -0x40;
                a.state = uint8_t(PropState::Move);
                a.acceleration.z = -4;
            }
            break;
        case PropState::Move:
            if (t == 0x47) {
                a.acceleration.z = 0;
                a.state = uint8_t(PropState::Coast);
            }
            break;
        case PropState::Coast:
            if (t == 0x5b) {
                a.pos.x = 0;
                a.state = uint8_t(PropState::Dive);
                a.pos.y = int32_t(0xfd800000u);
                a.pos.z = int32_t(0xf8000000u);
                a.rotation.y = kHalfTurn;
                setScale(a, 0x200);
                a.velocity.z = -0x50;
            }
            break;
        default: break;
        }
        break;
    case RoutineId::CrashCastPropLarge: if (PropState(a.state) == PropState::Bind) propInit(a, 0x800); break;
    case RoutineId::CrashCastPropSmall: if (PropState(a.state) == PropState::Bind) propInit(a, 0x200); break;
    case RoutineId::CrashCastPropHuge:
    case RoutineId::CrashCastPropHugeB: if (PropState(a.state) == PropState::Bind) propInit(a, 0x2800); break;
    case RoutineId::CrashCastPropTrailing: {
        TrailingPropFields& f = classState<TrailingPropFields>(a);
        if (PropState(a.state) == PropState::Bind) {
            if (!propInit(a, 0x200)) return;
            f.trailClock = 0;
        } else if (PropState(a.state) == PropState::Wait) {
            a.enterState(uint8_t(PropState::Move));
            a.velocity.y = 0x40;
            a.velocity.z = -0x40;
        }
        const uint8_t c = uint8_t(f.trailClock);
        if ((c & 3) == 0) {
            if (EffectActor* puff = allocThrownPuff(kActorLive)) { // a trail streak behind it
                const int16_t p[3] = {a.pos.x.whole, int16_t(a.pos.y.whole - 0xc), int16_t(a.pos.z.whole - 0x58)};
                const int k = int(c & 0xf) * 0x20;
                const uint16_t yaw = (c & 1) == 0 ? uint16_t(uint32_t(-(k + a.rotation.y)) & 0xfffu)
                                                  : uint16_t(uint32_t(k - a.rotation.y) & 0xfffu);
                placeThrownPuff(*puff, p, yaw, kTrailPuffKind, kTrailPuffLife);
            }
        }
        f.trailClock = f.trailClock + 1u;
        break;
    }
    default: notPortedAt(kStoryPort, "0x48a2f0 variant", a.variant);
    }
    if (!(a.flags & kActorLive)) return;
    castMove(a);
    if (a.anim != a.prevAnim) animSetPose(a, 0, 0);
    else animStepPose(a);
}

// ---- the Flutter's falling pieces (exe 0x59d100, pool 0x60, variants 0..2)
// --------------------------------------------
// the model by the spawn's byte; at step tick 0x182 they stop, then each flies off at its tick
struct CrashPieceFields {
    explicit CrashPieceFields(const StaticActor& actor) : CrashPieceFields(const_cast<StaticActor&>(actor)) {}
    explicit CrashPieceFields(StaticActor& actor) : model(actor.params[0]), flyTicks(actor.params[1]) {}
    uint8_t& model;
    uint8_t& flyTicks;
};
enum class PieceState : uint8_t { Init = 0, Wait = 1, Piece0 = 2, Piece1 = 3, Piece2 = 4, Fly = 5 };
constexpr uint8_t kLastPiece = 2;
constexpr uint8_t kFlyTicks = 0x10;

// exe 0x59d100: the state, then the motion
void crashPiece(StaticActor& a) {
    CrashPieceFields f(a);
    a.savePreviousPose();
    const int32_t t = stepTick();
    switch (PieceState(a.state)) {
    case PieceState::Init:
        if (!bindActorModel(a, f.model)) {
            freeActor(a); // exe 0x5e1540
            return;
        }
        a.scale.z = 0x100;
        a.scale.y = 0x100;
        a.scale.x = 0x100;
        a.collisionBox.pointAt(TableId::CrashLandingCastPieceBox);
        a.airborne = 0;
        a.rotation.z = 0;
        a.rotation.x = 0;
        clearVelocity(a);
        if (a.variant <= kLastPiece) a.velocity.z = kPropDrift;
        break;
    case PieceState::Wait:
        if (t == 0x182) {
            a.velocity.z = 0;
            if (a.variant <= kLastPiece) a.enterState(uint8_t(a.variant + uint8_t(PieceState::Piece0)));
        }
        break;
    case PieceState::Piece0:
        if (t == 0x1e6) {
            a.velocity.x = 8;
            a.velocity.z = -0x10;
            a.acceleration.x = 4;
            a.acceleration.z = -2;
            a.state = uint8_t(PieceState::Fly);
            f.flyTicks = 0;
        }
        break;
    case PieceState::Piece1:
        if (t == 0x218) {
            a.acceleration.x = -4;
            a.velocity.x = -0x10;
            a.velocity.z = -0x10;
            a.acceleration.z = -2;
            a.state = uint8_t(PieceState::Fly);
            f.flyTicks = 0;
        }
        break;
    case PieceState::Piece2:
        if (t == 0x24a) {
            a.acceleration.x = -4;
            a.velocity.x = -0x10;
            a.velocity.y = -0x10;
            a.velocity.z = -0x10;
            a.acceleration.y = -2;
            a.acceleration.z = -2;
            a.state = uint8_t(PieceState::Fly);
            f.flyTicks = 0;
        }
        break;
    case PieceState::Fly:
        if (f.flyTicks == kFlyTicks) a.state = uint8_t(PieceState::Fly);
        else ++f.flyTicks;
        break;
    default: break;
    }
    a.velocity.y = int16_t(a.velocity.y + a.acceleration.y);
    a.velocity.z = int16_t(a.velocity.z + a.acceleration.z);
    a.velocity.x = int16_t(a.velocity.x + a.acceleration.x);
    integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z);
}

// ---- the talking cast's tail (the classes of exe 0x480120 / 0x4823c0 / 0x48b530 and 0x4776e0)
// -------------------------
// the ground (kCastOnGround), the texture tracks (not 0x480120), the animation (the shadow: render output)
void castTail(const PlayerEnv& env, AnimatedActor& a, bool tracks) {
    CastFields& c = classState<CastFields>(a);
    if (!(a.flags & kActorLive)) return;
    if (c.castFlags & kCastOnGround) a.pos.y.whole = int16_t(actorGround(env, a));
    if (tracks) textureTracksTick(a, c.faceTracks);
    castAnimate(a, c.animKey, c.castFlags, kCastHeld);
}
// the cast's state 0 with its face tracks: bound, the key 0, the tracks on their tables
bool castFaceInit(AnimatedActor& a, CastFields& c, uint32_t box, uint8_t anim, bool clearMotion, uint32_t eyes,
                  uint32_t mouth) {
    if (!castBind(a, box, anim, clearMotion)) return false;
    c.animKey = 0;
    c.faceTracks.init(eyes, mouth);
    return true;
}

// ---- Roll (exe 0x4776e0, type 0x16, variants 0x8909d8)
// (the animation numbers its timeline asks for are the class's model-bank animations; the ticks are the timeline)
// ----------------------------------------------------------------
const DataTable<RoutineTable<4>> rollVariants{TableId::CrashLandingCastRollVariants};
// the parts her state 0 shows / hides
constexpr size_t kRollShown[2] = {4, 7}, kRollHidden[2] = {16, 15};

// Roll's state 0: bound with her face tracks, both cells 0, the parts shown / hidden
void rollInit(AnimatedActor& a, CastFields& c, uint8_t anim) {
    if (!castFaceInit(a, c, stateAddress(TableId::CrashLandingCastRollBox), anim, true, stateAddress(TableId::CrashLandingCastRollEyes), stateAddress(TableId::CrashLandingCastRollMouth))) return;
    textureTrackStop(a, c.faceTracks, FaceTrack::Eyes, 0);
    textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 0);
    a.parts[kRollShown[0]].flags &= uint8_t(~kPartHidden);
    a.parts[kRollHidden[0]].flags |= kPartHidden;
    a.parts[kRollShown[1]].flags &= uint8_t(~kPartHidden);
    a.parts[kRollHidden[1]].flags |= kPartHidden;
}

// +8 of Roll: bound, then her acts, each entered when its step sub comes
enum class RollState : uint8_t { Bind = 0, Act1 = 1, Act2 = 2, Act3 = 3 };

// Roll's variant (exe table 0x8909d8: the named routines of engine/routines.csv)
void rollVariant(AnimatedActor& a, CastFields& c) {
    const uint8_t sub = stepSub();
    const int32_t k = stepTick();
    auto anim = [&](uint8_t n) { requestAnim(a, c.animKey, n); };
    switch (rollVariants->idAt(a.variant)) {
    case RoutineId::CrashCastRollTalk:
        switch (RollState(a.state)) {
        case RollState::Bind: rollInit(a, c, 0); return;
        case RollState::Act1:
            if (sub != 1) {
                a.enterState(uint8_t(RollState::Act2));
                return;
            }
            if (k == 0x29) a.rotation.y = 0;
            else if (k == 0xb5) anim(1);
            break;
        case RollState::Act2:
            if (sub != 2) {
                a.enterState(uint8_t(RollState::Act3));
                return;
            }
            switch (k) {
            case 1:
                anim(2);
                a.rotation.y = kHalfTurn;
                break;
            case 0x65: textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 2); break;
            case 0x79: case 0x8b: case 0xd3: case 0x1a5: textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 0); break;
            case 0x83: case 0xb5: c.faceTracks.second.select(0, 0); break;
            case 0x191:
                anim(3);
                c.faceTracks.first.select(1, 0);
                c.faceTracks.second.select(0, 0);
                break;
            default: break;
            }
            break;
        case RollState::Act3:
            if (sub != 3) break;
            switch (k) {
            case 1:
                anim(4);
                c.faceTracks.second.select(0, 0);
                break;
            case 0x15: case 0x5b: case 0xe8: textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 0); break;
            case 0x33:
                c.faceTracks.first.select(2, 0);
                c.faceTracks.second.select(0, 0);
                break;
            case 0xb5: c.faceTracks.second.select(0, 0); break;
            default: break;
            }
            break;
        default: break;
        }
        return;
    case RoutineId::CrashCastRollLater:
        switch (RollState(a.state)) {
        case RollState::Bind: rollInit(a, c, 5); return;
        case RollState::Act1:
            if (sub == 9) {
                a.enterState(uint8_t(RollState::Act2));
                return;
            }
            break;
        case RollState::Act2:
            if (sub == 10) {
                a.enterState(uint8_t(RollState::Act3));
                return;
            }
            break;
        case RollState::Act3:
            if (sub != 10) break;
            switch (k) {
            case 0x5b: c.faceTracks.second.select(0, 0); break;
            case 0x79: case 0xb5: textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 0); break;
            case 0x8d:
                anim(6);
                c.faceTracks.second.select(0, 0);
                break;
            default: break;
            }
            break;
        default: break;
        }
        return;
    case RoutineId::CrashCastRollSub3:
        if (RollState(a.state) == RollState::Bind) {
            rollInit(a, c, 7);
            if (!(a.flags & kActorLive)) return;
        } else if (RollState(a.state) != RollState::Act1) {
            return;
        }
        if (sub == 3) {
            if (k == 1) {
                textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 3);
            } else if (k == 0x3d) {
                anim(8);
                c.faceTracks.second.select(0, 0);
            } else if (k == 0x5b) {
                textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 0);
            }
        }
        return;
    case RoutineId::CrashCastRollLate:
        if (RollState(a.state) == RollState::Bind) {
            rollInit(a, c, 9);
            if (!(a.flags & kActorLive)) return;
        } else if (RollState(a.state) != RollState::Act1) {
            return;
        }
        if (k == 0x15f) anim(10);
        return;
    default: notPortedAt(kStoryPort, "0x4776e0 variant", a.variant);
    }
}

// exe 0x4776e0
void crashRoll(const PlayerEnv& env, AnimatedActor& a) {
    CastFields& c = classState<CastFields>(a);
    castSavePose(a);
    rollVariant(a, c);
    castTail(env, a, true);
}

// ---- Barrel, Roll's grandfather (exe 0x47cd40, type 0x1a, variants 0x891110)
// (the animation numbers its timeline asks for are the class's model-bank animations; the ticks are the timeline)
// ---------------------------------------------------------
// his two heads on the same bone (parts 1 / 11; part 10 stays hidden) swapped by his step byte (exe 0x47dfe0): Talking
// alternates them every 4 ticks, Restore puts the first back once
enum class BarrelHeadStep : uint8_t { None = 0, Talking = 1, Restore = 2 };
const DataTable<RoutineTable<3>> barrelVariants{TableId::CrashLandingCastBarrelVariants};
constexpr size_t kBarrelHead = 1, kBarrelHiddenHead = 10, kBarrelTalkHead = 11;
constexpr uint8_t kHeadSwapPhase = 4;
constexpr uint8_t kHeadSwapClockStart = 0; // the swap's clock (the step counter) restarted

// +8 of Barrel: bound, waiting for step sub 2, his lines
enum class BarrelState : uint8_t { Bind = 0, Wait = 1, Talk = 2 };

// Barrel's variant (exe table 0x891110)
void barrelVariant(AnimatedActor& a, CastFields& c) {
    const uint8_t sub = stepSub();
    const int32_t k = stepTick();
    auto mode = [&](BarrelHeadStep m) { a.step = uint8_t(m); };
    switch (barrelVariants->idAt(a.variant)) {
    case RoutineId::CastBarrelTalks: {
        const BarrelState st = BarrelState(a.state);
        if (st == BarrelState::Bind) {
            if (castBind(a, stateAddress(TableId::CrashLandingCastBarrelBox), 0, true)) c.animKey = 0;
        } else if (st == BarrelState::Wait) {
            if (sub != 1) a.enterState(uint8_t(BarrelState::Talk));
        } else if (st == BarrelState::Talk && sub == 2) {
            switch (k) {
            case 0xb: requestAnim(a, c.animKey, 1); break;
            case 0x15: case 0x3d: case 0x137: mode(BarrelHeadStep::Talking); break;
            case 0x2d: case 0x4c: case 0xfb: case 0x119: case 0x173: mode(BarrelHeadStep::Restore); break;
            case 0xf1: case 0x105:
                requestAnim(a, c.animKey, 2);
                mode(BarrelHeadStep::Talking);
                break;
            default: break;
            }
        }
        return;
    }
    case RoutineId::CastBarrelStill:
    case RoutineId::CastBarrelStillB:
        if (BarrelState(a.state) == BarrelState::Bind && castBind(a, stateAddress(TableId::CrashLandingCastBarrelBox), 3, true)) c.animKey = 0;
        return;
    default: notPortedAt(kStoryPort, "0x47cd40 variant", a.variant);
    }
}

// exe 0x47cd40: the variant, the head swap (exe 0x47dfe0), the animation
void crashBarrel(AnimatedActor& a) {
    CastFields& c = classState<CastFields>(a);
    castSavePose(a);
    barrelVariant(a, c);
    if (!(a.flags & kActorLive)) return;
    auto&& head = a.parts[kBarrelHead].flags;
    auto&& hiddenHead = a.parts[kBarrelHiddenHead].flags;
    auto&& talkHead = a.parts[kBarrelTalkHead].flags;
    if (BarrelHeadStep(a.step) == BarrelHeadStep::Talking) { // exe 0x47dfe0
        const uint8_t t = a.stepCounter;
        if (t & kHeadSwapPhase) {
            head |= kPartHidden;
            hiddenHead |= kPartHidden;
            talkHead &= uint8_t(~kPartHidden);
        } else {
            head &= uint8_t(~kPartHidden);
            hiddenHead |= kPartHidden;
            talkHead |= kPartHidden;
        }
        a.stepCounter = uint8_t(t + 1u);
    } else if (BarrelHeadStep(a.step) == BarrelHeadStep::Restore) {
        head &= uint8_t(~kPartHidden);
        hiddenHead |= kPartHidden;
        talkHead |= kPartHidden;
        a.step = uint8_t(BarrelHeadStep::None);
        a.stepCounter = kHeadSwapClockStart;
    }
    castAnimate(a, c.animKey, c.castFlags, kCastHeld);
}

// ---- the second body (exe 0x49a690, type 0x2f: spawned by a craft with its variant, variants 0x8930a8)
// (the animation numbers its timeline asks for are the class's model-bank animations; the ticks are the timeline)
// -----------------
// every tick it takes its parent's (slot params[0]) scale and first bone (its matrix scaled by 0x1000 << (3 - shift),
// its point >> shift); the parent gone: freed
const DataTable<RoutineTable<5>> secondBodyVariants{TableId::CrashLandingCastSecondBodyVariants};
constexpr int kNoCell = -1;
constexpr size_t kParentBone = 0;

// the second body's state 0: bound with its face tracks, the cells given (kNoCell: none)
void secondBodyInit(AnimatedActor& a, CastFields& c, uint8_t anim, int eyesCell, int mouthCell) {
    if (!castFaceInit(a, c, stateAddress(TableId::CrashLandingCastSecondBodyBox), anim, false, stateAddress(TableId::CrashLandingCastSecondBodyEyes), stateAddress(TableId::CrashLandingCastSecondBodyMouth))) return;
    if (eyesCell >= 0) textureTrackStop(a, c.faceTracks, FaceTrack::Eyes, uint32_t(eyesCell));
    if (mouthCell >= 0) textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, uint32_t(mouthCell));
}

// +8 of the second body: bound, waiting for its step sub, its lines, its late lines (never reached: see the variant)
enum class SecondBodyState : uint8_t { Bind = 0, Wait = 1, Talk = 2, Late = 3 };

// the second body's variant (exe table 0x8930a8)
void secondBodyVariant(AnimatedActor& a, CastFields& c) {
    const uint8_t sub = stepSub();
    const int32_t k = stepTick();
    auto anim = [&](uint8_t n) { requestAnim(a, c.animKey, n); };
    switch (secondBodyVariants->idAt(a.variant)) {
    case RoutineId::CrashCastSecondBodyTalk:
        switch (SecondBodyState(a.state)) {
        case SecondBodyState::Bind: secondBodyInit(a, c, 0, 0, 0); return;
        case SecondBodyState::Wait:
            if (sub == 6) {
                a.enterState(uint8_t(SecondBodyState::Talk));
                return;
            }
            break;
        case SecondBodyState::Talk:
            if (sub == 6) {
                switch (k) {
                case 1: anim(1); break;
                case 0x10: c.faceTracks.second.select(0, 0); break;
                case 0x4c: textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 0); break;
                case 0x51: c.faceTracks.first.select(0, 0); break;
                case 0x6f: c.faceTracks.first.select(1, 0); break;
                case 0x79: anim(2); break;
                case 0x83: c.faceTracks.second.select(1, 0); break;
                case 0xab: textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 3); break;
                default: break;
                }
            } else if (sub == 7) {
                a.enterState(uint8_t(SecondBodyState::Talk)); // (the exe writes 2 again: state 3 is never reached)
                return;
            }
            break;
        case SecondBodyState::Late:
            if (sub == 7 && k == 1) {
                anim(3);
                textureTrackStop(a, c.faceTracks, FaceTrack::Eyes, 3);
                textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 0);
            }
            break;
        default: break;
        }
        return;
    case RoutineId::CrashCastSecondBodyIdle:
        if (SecondBodyState(a.state) == SecondBodyState::Bind) {
            secondBodyInit(a, c, 5, 3, 0);
        } else if (SecondBodyState(a.state) == SecondBodyState::Wait && sub == 2) {
            a.enterState(uint8_t(SecondBodyState::Talk));
            return;
        }
        return;
    case RoutineId::CrashCastSecondBodyStill:
        if (SecondBodyState(a.state) == SecondBodyState::Bind) secondBodyInit(a, c, 5, 3, 0);
        return;
    case RoutineId::CrashCastSecondBodyBlink:
        if (SecondBodyState(a.state) == SecondBodyState::Bind) {
            secondBodyInit(a, c, 6, 0, 0);
            if (!(a.flags & kActorLive)) return;
        } else if (SecondBodyState(a.state) != SecondBodyState::Wait) {
            return;
        }
        if (k == 0x15) {
            textureTrackStop(a, c.faceTracks, FaceTrack::Eyes, 5);
            textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 6);
        }
        return;
    case RoutineId::CrashCastSecondBodySpeech:
        if (SecondBodyState(a.state) == SecondBodyState::Bind) {
            secondBodyInit(a, c, 7, kNoCell, kNoCell);
            if (!(a.flags & kActorLive)) return;
        } else if (SecondBodyState(a.state) != SecondBodyState::Wait) {
            return;
        }
        switch (k) {
        case 1:
            c.faceTracks.first.select(2, 0);
            c.faceTracks.second.select(2, 0);
            break;
        case 0x38: textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 7); break;
        case 0x83:
            anim(8);
            c.faceTracks.first.select(3, 0);
            textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 10);
            break;
        case 0xa1:
            anim(9);
            textureTrackStop(a, c.faceTracks, FaceTrack::Eyes, 10);
            textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 3);
            break;
        case 0xbf: anim(10); break;
        case 0x10f:
            textureTrackStop(a, c.faceTracks, FaceTrack::Eyes, 10);
            c.faceTracks.second.select(1, 0);
            break;
        case 0x141: textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 3); break;
        case 0x14b:
            anim(11);
            c.faceTracks.first.select(4, 0);
            break;
        case 0x155: c.faceTracks.second.select(0, 0); break;
        case 0x187: textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 0); break;
        default: break;
        }
        return;
    default: notPortedAt(kStoryPort, "0x49a690 variant", a.variant);
    }
}

// exe 0x49a690: the variant, then the parent's scale and bone, the tracks, the animation
void crashSecondBody(AnimatedActor& a) {
    CastFields& c = classState<CastFields>(a);
    castSavePose(a);
    secondBodyVariant(a, c);
    if (!(a.flags & kActorLive)) return;
    const uint32_t parentAt = actorBySlot(uint32_t(int32_t(int8_t(a.params[0]))));
    const AnimatedActor& parent = animatedActorAt(parentAt);
    if (!(parent.flags & kActorLive)) {
        freeActor(a); // exe 0x5e1490
        return;
    }
    a.scale.x = parent.scale.x;
    a.scale.y = parent.scale.y;
    a.scale.z = parent.scale.z;
    const BoneBlock* block = boneBlockOf(parent, 0); // exe 0x5e1780
    if (!block) notPorted(kStoryPort, "the parent has no bone block");
    a.copyRootWordsFrom(*block, kParentBone);
    const uint8_t shift = stage().coordinateShift;
    const int32_t scale = int32_t(0x1000u << ((3u - shift) & 0x1f));
    Matrix3 m = a.rootMatrix;
    for (size_t i = 0; i < 9; ++i) m.e[i] = int16_t((int32_t(m.e[i]) * scale) >> 12); // exe 0x40c0e0
    a.rootMatrix = m;
    const ShortVec4& bone = block->points[kParentBone];
    a.pos.x.whole = int16_t(bone.x >> (shift & 0x1f));
    a.pos.y.whole = int16_t(bone.y >> (shift & 0x1f));
    a.pos.z.whole = int16_t(bone.z >> (shift & 0x1f));
    textureTracksTick(a, c.faceTracks);
    castAnimate(a, c.animKey, c.castFlags, kCastHeld);
}

// ---- the talker (exe 0x4b0e20, type 0x39, variant 0x4b0ed0)
// (the animation numbers its timeline asks for are the class's model-bank animations; the ticks are the timeline)
// -----------------------------------------------------------
const DataTable<RoutineTable<1>> talkerVariants{TableId::CrashLandingCastTalkerVariants};
constexpr uint8_t kTalkerHold = kCastHoldPose | kCastShadow; // what holds its pose (as the exe tests)

// +8 of the talker: bound, its lines
enum class TalkerState : uint8_t { Bind = 0, Talk = 1 };

// exe 0x4b0e20
void crashTalker(AnimatedActor& a) {
    CastFields& c = classState<CastFields>(a);
    castSavePose(a);
    if (talkerVariants->idAt(a.variant) != RoutineId::CrashCastTalker) notPortedAt(kStoryPort, "0x4b0e20 variant", a.variant);
    const int32_t k = stepTick();
    if (TalkerState(a.state) == TalkerState::Bind) {
        if (castFaceInit(a, c, stateAddress(TableId::CrashLandingCastTalkerBox), 0, false, stateAddress(TableId::CrashLandingCastTalkerEyes), stateAddress(TableId::CrashLandingCastTalkerMouth))) {
            textureTrackStop(a, c.faceTracks, FaceTrack::Eyes, 0);
            textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 0);
        }
    } else if (TalkerState(a.state) == TalkerState::Talk) {
        switch (k) {
        case 0xf1: c.faceTracks.second.select(0, 0); break;
        case 0xfb: c.faceTracks.first.select(0, 0); break;
        case 0x105: textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 0); break;
        case 0x141: c.faceTracks.first.select(1, 0); break;
        case 0x14b:
            requestAnim(a, c.animKey, 1);
            c.faceTracks.second.select(1, 0);
            break;
        case 0x187: c.faceTracks.first.select(2, 0); break;
        case 0x191: textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 3); break;
        case 0x1af:
            requestAnim(a, c.animKey, 2);
            c.faceTracks.first.select(3, 0);
            c.faceTracks.second.select(2, 0);
            break;
        case 0x227: textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 6); break;
        default: break;
        }
    }
    if (!(a.flags & kActorLive)) return;
    textureTracksTick(a, c.faceTracks);
    castAnimate(a, c.animKey, c.castFlags, kTalkerHold);
}

// ---- the flying craft (exe 0x4bb750, type 0x3f, variants 0x8948a0; exe 0x4faa20's jets share its layout)
// (the animation numbers its timeline asks for are the class's model-bank animations; the ticks are the timeline)
// ---------------
// the linked second actor of a craft (exe 0x4bbe30: a type-0x2f second body; exe 0x4faf40: a type-0x47 static)
constexpr uint8_t kSecondBodyType = 0x2f, kLinkedStaticType = 0x47;
constexpr uint8_t kSecondBodyFlags = 0xc3, kLinkedStaticFlags = 0x83;
// the craft's exhaust puffs (exe 0x4bbec0)
constexpr uint16_t kExhaustSound = 0x9a;

// exe 0x4bbe30 / 0x4faf40: the linked second actor with the craft's variant and position, yaw 0; their slots exchanged
// in their first parameters
void castLinked(AnimatedActor& a, bool animated) {
    const uint32_t at = animated ? allocActor20() : allocActor60(kActorStageClass);
    if (at == 0) return;
    Actor& c = actorAt(at);
    c.type = animated ? kSecondBodyType : kLinkedStaticType;
    c.flags |= animated ? kSecondBodyFlags : kLinkedStaticFlags;
    c.subtype = 0;
    c.variant = a.variant;
    c.spawnByte = 0;
    c.setParamsWord(0);
    c.prevPos = a.pos;
    c.pos = a.pos;
    c.rotation.z = 0;
    c.rotation.y = 0;
    c.rotation.x = 0;
    c.prevRotation = c.rotation;
    c.params[0] = a.slot;
    a.params[0] = c.slot;
}

// exe 0x4bbec0: two exhaust jets at Ry Rx Rz(rotation) * (+-0x400, 0, -0x1340) from the craft; every 4th tick of its
// clock a puff at each (OwnerPuff, owned by the craft) with its sound (the jets' sprites: render output)
void castJets(AnimatedActor& a, const CraftFields& f) {
    const uint16_t clock = f.exhaustClock;
    if ((clock & 3) > 1) return;
    for (int i = 0; i < 2; ++i) {
        int32_t v[3];
        rotateVector(a.rotation, i == 0 ? kExhaustSide : -kExhaustSide, 0, kExhaustBack, v);
        const int32_t x = int32_t(uint32_t(v[0]) + uint32_t(a.pos.x)), y = int32_t(uint32_t(v[1]) + uint32_t(a.pos.y)),
                      z = int32_t(uint32_t(v[2]) + uint32_t(a.pos.z));
        if ((uint8_t(f.exhaustClock) & 3) != 0) continue;
        const uint32_t at = allocActorA0(kActorStageClass); // exe 0x5e13a0
        if (at == 0) continue;
        EffectActor& e = effectActorAt(at);
        e.pos.x = int16_t(uint32_t(x) >> 16);
        e.pos.y = int16_t(uint32_t(y) >> 16);
        e.pos.z = int16_t(uint32_t(z) >> 16);
        e.flags = kActorLiveDrawn;
        e.type = kOpeningEffectType;
        e.variant = uint8_t(OpeningEffect::OwnerPuff);
        OpeningEffectFields(e).yaw = 0;
        e.params[0] = a.slot;
        e.params[1] = 0;
        e.params[2] = 0;
        e.params[3] = 0;
        soundPlayAtPosS16(kExhaustSound, e.pos);
    }
}

// +9 of the arriving craft's Act1: its approach (the speed set), braking to its glide, gliding in, placed at the
// landing point; of its Act3: waiting for step sub 7, leaving
enum class ArrivalSub : uint8_t { Start = 0, Braking = 1, Gliding = 2, Placed = 3 };
enum class LeaveSub : uint8_t { Wait = 0, Leaving = 1 };

// the craft's variant (exe table 0x8948a0)
void craftVariant(AnimatedActor& a, CraftFields& f) {
    const uint8_t sub = stepSub();
    const int32_t k = stepTick();
    auto init = [&](uint8_t anim) {
        if (!castBind(a, stateAddress(TableId::CrashLandingCastCraftBox), anim, true)) return false;
        f.animKey = 0;
        castLinked(a, true);
        return true;
    };
    auto place = [&](int32_t x, int32_t y, int32_t z) {
        a.pos.x = x;
        a.pos.y = y;
        a.pos.z = z;
    };
    switch (craftVariants->idAt(a.variant)) {
    case RoutineId::CrashCraftArriving:
        switch (CraftState(a.state)) {
        case CraftState::Bind:
            if (!init(1)) return;
            a.pos.x.whole = -0x400;
            a.pos.y.whole = -0x200;
            a.rotation.z = 0x80;
            f.exhaustClock = 0;
            return;
        case CraftState::Act1: {
            if (sub != 5) {
                a.enterState(uint8_t(CraftState::Act2));
                return;
            }
            const ArrivalSub s9 = ArrivalSub(a.subState);
            if (s9 == ArrivalSub::Start) {
                a.velocity.x = -0x400;
                a.acceleration.x = 0x20;
                a.velocity.y = -0x200;
                a.acceleration.y = 0x10;
                a.subState = uint8_t(ArrivalSub::Braking);
            }
            if (s9 == ArrivalSub::Start || s9 == ArrivalSub::Braking) {
                if (a.velocity.x == -0x40) {
                    a.velocity.x = -0x40;
                    a.acceleration.x = 0;
                    a.velocity.y = -0x20;
                    a.acceleration.y = 0;
                    a.subState = uint8_t(ArrivalSub::Gliding);
                }
            } else if (s9 != ArrivalSub::Gliding) {
                return;
            }
            if (a.pos.x.whole >= 0) {
                a.pos.x = 0;
                a.pos.y = int32_t(0xfc000000u);
                a.velocity.x = 0;
                a.velocity.y = 0;
                a.acceleration.x = 0;
                a.acceleration.y = 0;
                a.subState = uint8_t(ArrivalSub::Placed);
            }
            return;
        }
        case CraftState::Act2:
            if (sub != 6) a.enterState(uint8_t(CraftState::Act3));
            return;
        case CraftState::Act3:
            if (sub == 7 && LeaveSub(a.subState) == LeaveSub::Wait) {
                a.acceleration.x = 0x10;
                a.acceleration.y = 0xc;
                a.acceleration.z = -8;
                a.subState = uint8_t(LeaveSub::Leaving);
            }
            return;
        default: return;
        }
    case RoutineId::CrashCraftLanding: {
        const CraftState st = CraftState(a.state);
        if (st == CraftState::Bind) {
            if (!bindActorModel(a, 0)) {
                freeActor(a);
                return;
            }
            a.collisionBox.pointAt(TableId::CrashLandingCastCraftBox);
            a.clearClassBlock();
            a.enterState(uint8_t(CraftState::Act1));
            a.anim = 2;
            a.prevAnim = kNoAnimShown;
            a.rotation.x = 0x200;
            a.rotation.z = 0;
            a.velocity.x = 0;
            a.acceleration.z = 0;
            a.acceleration.y = 0;
            a.acceleration.x = 0;
            a.velocity.y = 0x1000;
            a.velocity.z = -0x1000;
            f.animKey = 0;
            castLinked(a, true);
            if (sub == 2) a.enterState(uint8_t(CraftState::Act2)); // (falls through to state 1 in the exe)
            return;
        }
        if (st == CraftState::Act1) {
            if (sub == 2) a.enterState(uint8_t(CraftState::Act2));
            return;
        }
        if (st != CraftState::Act2) return;
        if (sub == 2 && k > 1 && k < 0x11) {
            a.scale.x -= 0x10u;
            a.scale.y -= 0x10u;
            a.scale.z -= 0x10u;
        }
        if (k == 1) {
            place(0, int32_t(0xf8000000u), 0x8000000);
            a.rotation.y = 0;
            a.velocity.y = 0x800;
            a.velocity.z = -0x800;
        } else if (k == 0x11) {
            place(0, int32_t(0xf8000000u), 0);
            a.velocity.y = 0;
            a.velocity.z = 0;
            setScale(a, 0x200);
            f.exhaustClock = 0;
        }
        if (k < 0x16) return;
        castJets(a, f);
        f.exhaustClock += 1u;
        return;
    }
    case RoutineId::CrashCraftDiving:
        if (CraftState(a.state) == CraftState::Bind) {
            if (!init(3)) return;
        } else if (CraftState(a.state) != CraftState::Act1) {
            return;
        }
        switch (k) {
        case 1:
            a.anim = 3;
            place(0, int32_t(0xfc000000u), 0);
            a.rotation.y = kHalfTurn;
            a.velocity.y = -0x600;
            a.velocity.z = 0x600;
            a.acceleration.y = 0;
            a.acceleration.z = 0;
            break;
        case 0x18: a.anim = 4; break;
        case 0x19:
            a.acceleration.y = 0x66;
            a.acceleration.z = -0x66;
            break;
        case 0x28:
            a.anim = 4;
            a.velocity.y = 0;
            a.velocity.z = 0;
            a.acceleration.y = 0;
            a.acceleration.z = 0;
            break;
        case 0x29:
            a.acceleration.y = 0x133;
            a.acceleration.z = -0x133;
            break;
        case 0x32:
            a.anim = 5;
            a.velocity.y = 0xc00;
            a.velocity.z = -0xc00;
            break;
        default: break;
        }
        return;
    case RoutineId::CrashCastCraftParked:
        if (CraftState(a.state) == CraftState::Bind) init(2);
        return;
    case RoutineId::CrashCastCraftLeaving:
        if (CraftState(a.state) == CraftState::Bind) {
            init(1);
            return;
        }
        if (CraftState(a.state) != CraftState::Act1) return;
        if (k == 0xa1) {
            requestAnim(a, f.animKey, 0);
            a.rotation.z = 0x80;
        } else if (k == 0xbf) {
            a.acceleration.x = 0x10;
            a.acceleration.y = 0xc;
            a.acceleration.z = -8;
        } else if (k == 0x105) {
            requestAnim(a, f.animKey, 1);
            a.rotation.z = 0x40;
            place(0, int32_t(0xfc000000u), 0);
            clearVelocity(a);
        }
        return;
    default: notPortedAt(kStoryPort, "0x4bb750 variant", a.variant);
    }
}

// exe 0x4bb750: the variant, the motion, the animation
void crashCraft(AnimatedActor& a) {
    CraftFields& f = classState<CraftFields>(a);
    castSavePose(a);
    craftVariant(a, f);
    castMove(a);
    if (!(a.flags & kActorLive)) return;
    castAnimate(a, f.animKey, f.castFlags, kCastHeld);
}

// ---- the jets (exe 0x4faa20, type 0x74, variants 0x897900)
// (the animation numbers its timeline asks for are the class's model-bank animations; the ticks are the timeline)
// ------------------------------------------------------------
// variants 0 / 1 spawn a type-0x47 static (exe 0x4faf40); variant 0 a type-0x74 variant-2 companion at sub 5 tick 0xd
// (freed at 0x41) that sits 0xa0 below it
const DataTable<RoutineTable<3>> jetVariants{TableId::CrashLandingCastJetVariants};
constexpr uint8_t kJetType = 0x74, kCompanionVariant = 2;
constexpr int16_t kCompanionDrop = 0xa0;
constexpr uint8_t kCompanionSemi = ((1 & 1) << 4 | (0 & 3)) << 1; // exe 0x5e1a00(actor, 1, 0)
constexpr uint16_t kCompanionTint = 0x400;
// the jet's companion slot (its fourth parameter)
size_t constexpr kCompanionParam = 3;
// a fighter's bursts (exe 0x4fafd0): k < 2 a bone puff (BonePuff, thrown = k) on it, k = 2 a burst puff (BurstPuff) and
// a late flash ring (FlashRing) at Ry Rx Rz(rotation) * (0, 0, -0x700) from it
constexpr uint32_t kBurstBack = 0x700;
enum class Burst : uint32_t { Puff = 0, ThrownPuff = 1, Flash = 2 };

// exe 0x4fafd0(actor, k)
void jetBurst(const AnimatedActor& a, Burst k) {
    const uint32_t at = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (at == 0) return;
    EffectActor& e = effectActorAt(at);
    e.flags = kActorLiveDrawn;
    e.type = kOpeningEffectType;
    OpeningEffectFields(e).yaw = 0;
    e.params[0] = a.slot;
    if (k != Burst::Flash) {
        e.variant = uint8_t(OpeningEffect::BonePuff);
        e.params[1] = uint8_t(k);
        e.params[2] = 0;
        e.params[3] = 0;
        e.pos.x = 0;
        e.pos.y = 0;
        e.pos.z = 0;
        return;
    }
    int32_t v[3];
    rotateVector(a.rotation, 0, 0, -int(kBurstBack), v);
    const int16_t dx = int16_t(uint32_t(v[0]) >> 16), dy = int16_t(uint32_t(v[1]) >> 16);
    const int16_t dz = int16_t(uint32_t(v[2]) >> 16);
    e.variant = uint8_t(OpeningEffect::BurstPuff);
    e.params[1] = 0;
    e.params[2] = 0;
    e.params[3] = 0;
    e.pos.x = int16_t(a.pos.x.whole + dx);
    e.pos.y = int16_t(a.pos.y.whole + dy);
    e.pos.z = int16_t(a.pos.z.whole + dz);
    const uint32_t ringAt = allocActorA0(kActorStageClass);
    if (ringAt == 0) return;
    EffectActor& ring = effectActorAt(ringAt);
    ring.flags = kActorLiveDrawn;
    ring.type = kOpeningEffectType;
    ring.loopFlags |= kActorLateLoop;
    ring.params[0] = a.slot;
    ring.variant = uint8_t(OpeningEffect::FlashRing);
    ring.pos.x = int16_t(a.pos.x.whole + dx);
    ring.pos.y = int16_t(a.pos.y.whole + dy);
    ring.pos.z = int16_t(a.pos.z.whole + dz);
}

// +8 of the jets: bound, then their acts, each entered when its step sub comes
enum class JetState : uint8_t { Bind = 0, Act1 = 1, Act2 = 2, Act3 = 3 };

// the jet's variant (exe table 0x897900)
void jetVariant(AnimatedActor& a, CraftFields& f) {
    const uint8_t sub = stepSub();
    const int32_t k = stepTick();
    switch (jetVariants->idAt(a.variant)) {
    case RoutineId::CrashCastJetLeader: {
        const JetState st = JetState(a.state);
        if (st == JetState::Bind) {
            if (!castBind(a, stateAddress(TableId::CrashLandingCastJetBox), 3, true)) return;
            f.animKey = 0;
            castLinked(a, false);
            a.velocity.z = -0x500;
        } else if (st == JetState::Act1) {
            if (sub == 5 && k == 0xd) {
                a.enterState(uint8_t(JetState::Act2));
                if (const uint32_t at = allocActor20()) {
                    AnimatedActor& c = animatedActorAt(at);
                    c.flags |= kActorLiveDrawn;
                    c.type = kJetType;
                    c.subtype = 0;
                    c.variant = kCompanionVariant;
                    c.spawnByte = 0;
                    c.setParamsWord(0);
                    c.prevPos = a.pos;
                    c.pos = a.pos;
                    c.pos.y.whole += uint16_t(kCompanionDrop);
                    c.prevRotation = c.rotation;
                    c.params[kCompanionParam] = a.slot;
                    a.params[kCompanionParam] = c.slot;
                }
            }
        } else if (st == JetState::Act2 && sub == 5 && k == 0x41) {
            a.enterState(uint8_t(JetState::Act3));
            actorFree(actorBySlot(uint32_t(int32_t(int8_t(a.params[kCompanionParam]))))); // exe 0x5e1490
        }
        return;
    }
    case RoutineId::CrashCastJetFighter: {
        const JetState st = JetState(a.state);
        if (st == JetState::Bind) {
            if (!castBind(a, stateAddress(TableId::CrashLandingCastJetBox), 1, true)) return;
            f.animKey = 0;
            castLinked(a, false);
            return;
        }
        if (st == JetState::Act1) {
            if (sub != 7) {
                a.enterState(uint8_t(JetState::Act2));
                return;
            }
            switch (k) {
            case 1: requestAnim(a, f.animKey, 1); break;
            case 0x2d: requestAnim(a, f.animKey, 2); break;
            case 0x4d: jetBurst(a, Burst::Puff); break;
            case 0x61: jetBurst(a, Burst::Flash); break;
            default: break;
            }
            if (k > 0x4c && k < 0x5a)
                for (int i = 0; i < 5; ++i) jetBurst(a, Burst::ThrownPuff);
            return;
        }
        if (st != JetState::Act2) return;
        if (k == 1) {
            a.anim = 3;
            a.pos.x = 0x1800000;
            a.pos.y = int32_t(0xfd800000u);
            a.pos.z = int32_t(0xf8800000u);
            a.rotation.z = 0x100;
            a.velocity.x = -0x180;
            a.velocity.y = -0x80;
            a.velocity.z = -0x400;
            a.acceleration.x = -0x10;
            a.acceleration.y = -8;
            a.acceleration.z = -0x20;
        } else if (k == 0x41) {
            clearVelocity(a);
        }
        return;
    }
    case RoutineId::CrashCastJetCompanion: {
        if (JetState(a.state) == JetState::Bind) {
            if (!castBind(a, stateAddress(TableId::CrashLandingCastJetBox), 3, true)) return;
            f.animKey = 0;
            a.scale.z = 0x200;
            a.scale.x = 0x200;
            a.scale.y = 0;
            a.tint = kCompanionTint;
            a.semiTransparency = kCompanionSemi;
        }
        const Actor& leader = actorLayoutAt(actorBySlot(uint32_t(int32_t(int8_t(a.params[kCompanionParam])))));
        a.pos.x = leader.pos.x;
        a.pos.y = leader.pos.y;
        a.pos.z = leader.pos.z;
        a.rotation.setXY(leader.rotation.x, leader.rotation.y);
        a.pos.y.whole += uint16_t(kCompanionDrop);
        a.rotation.setZSpare(leader.rotation.z, int16_t(leader.rotation.zSpareWord() >> 16));
        return;
    }
    default: notPortedAt(kStoryPort, "0x4faa20 variant", a.variant);
    }
}

// exe 0x4faa20: the variant, the motion, the animation
void crashJet(AnimatedActor& a) {
    CraftFields& f = classState<CraftFields>(a);
    castSavePose(a);
    jetVariant(a, f);
    if (!(a.flags & kActorLive)) return;
    castMove(a);
    castAnimate(a, f.animKey, f.castFlags, kCastHeld);
}

// ---- the watcher (exe 0x480120, type 0x1d, variant 0x480240): animation 1 at sub 1 tick 0x3d
// (the animation numbers its timeline asks for are the class's model-bank animations; the ticks are the timeline)
// ---------------------------
const DataTable<RoutineTable<1>> watcherVariants{TableId::CrashLandingCastWatcherVariants};

// +8 of the watcher: bound, waiting for its tick, done
enum class WatcherState : uint8_t { Bind = 0, Wait = 1, Done = 2 };

// exe 0x480120
void crashWatcher(const PlayerEnv& env, AnimatedActor& a) {
    CastFields& c = classState<CastFields>(a);
    castSavePose(a);
    if (watcherVariants->idAt(a.variant) != RoutineId::CrashCastWatcher) notPortedAt(kStoryPort, "0x480120 variant", a.variant);
    if (WatcherState(a.state) == WatcherState::Bind) {
        if (castBind(a, stateAddress(TableId::CrashLandingCastWatcherBox), 0, true)) c.animKey = 0;
    } else if (WatcherState(a.state) == WatcherState::Wait && stepSub() == 1 && stepTick() == 0x3d) {
        requestAnim(a, c.animKey, 1);
    }
    castTail(env, a, false);
}

// ---- the face (exe 0x4823c0, type 0x21, variants 0x891c38)
// (the animation numbers its timeline asks for are the class's model-bank animations; the ticks are the timeline)
// ------------------------------------------------------------
const DataTable<RoutineTable<2>> faceVariants{TableId::CrashLandingCastFaceVariants};

// +8 of the face: bound, waiting for its tick, done
enum class FaceState : uint8_t { Bind = 0, Wait = 1, Done = 2 };

// the face's variant (exe table 0x891c38)
void faceVariant(AnimatedActor& a, CastFields& c) {
    const uint8_t sub = stepSub();
    const int32_t k = stepTick();
    switch (faceVariants->idAt(a.variant)) {
    case RoutineId::CrashCastFaceFirst:
        if (FaceState(a.state) == FaceState::Bind) {
            if (!castFaceInit(a, c, stateAddress(TableId::CrashLandingCastFaceBox), 0, true, stateAddress(TableId::CrashLandingCastFaceEyes), stateAddress(TableId::CrashLandingCastFaceMouth))) return;
            textureTrackStop(a, c.faceTracks, FaceTrack::Eyes, 0);
            textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 0);
        } else if (FaceState(a.state) == FaceState::Wait && sub == 1) {
            if (k == 0x141 || k == 0x187) c.faceTracks.second.select(0, 0);
            else if (k == 0x175 || k == 0x1cd) textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 0);
        }
        return;
    case RoutineId::CrashCastFaceLater:
        if (FaceState(a.state) == FaceState::Bind) {
            if (!castFaceInit(a, c, stateAddress(TableId::CrashLandingCastFaceBox), 1, true, stateAddress(TableId::CrashLandingCastFaceEyes), stateAddress(TableId::CrashLandingCastFaceMouth))) return;
            textureTrackStop(a, c.faceTracks, FaceTrack::Eyes, 0);
        } else if (FaceState(a.state) == FaceState::Wait && sub == 0xb) {
            switch (k) {
            case 0xb: c.faceTracks.second.select(1, 0); break;
            case 0x1a: requestAnim(a, c.animKey, 2); break;
            case 0x29: case 0x63: case 0xdb: textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 3); break;
            case 0x5e: textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 7); break;
            case 0x83: c.faceTracks.second.select(2, 0); break;
            default: break;
            }
        }
        return;
    default: notPortedAt(kStoryPort, "0x4823c0 variant", a.variant);
    }
}

// exe 0x4823c0
void crashFace(const PlayerEnv& env, AnimatedActor& a) {
    CastFields& c = classState<CastFields>(a);
    castSavePose(a);
    faceVariant(a, c);
    castTail(env, a, true);
}

// ---- the speaker (exe 0x48b530, type 0x28, variants 0x892558)
// (the animation numbers its timeline asks for are the class's model-bank animations; the ticks are the timeline)
// ---------------------------------------------------------
const DataTable<RoutineTable<2>> speakerVariants{TableId::CrashLandingCastSpeakerVariants};

// +8 of the speaker: bound, then its acts, each entered when its step sub comes
enum class SpeakerState : uint8_t { Bind = 0, Act1 = 1, Act2 = 2, Act3 = 3, Act4 = 4 };

// the speaker's variant (exe table 0x892558)
void speakerVariant(AnimatedActor& a, CastFields& c) {
    const uint8_t sub = stepSub();
    const int32_t k = stepTick();
    switch (speakerVariants->idAt(a.variant)) {
    case RoutineId::CrashCastSpeakerFirst:
        if (SpeakerState(a.state) == SpeakerState::Bind) {
            if (!castFaceInit(a, c, stateAddress(TableId::CrashLandingCastSpeakerBox), 0, true, stateAddress(TableId::CrashLandingCastSpeakerEyes), stateAddress(TableId::CrashLandingCastSpeakerMouth))) return;
            textureTrackStop(a, c.faceTracks, FaceTrack::Eyes, 0);
            textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 0);
        } else if (SpeakerState(a.state) == SpeakerState::Act1 && sub == 1 && k == 0x187) {
            requestAnim(a, c.animKey, 1);
            c.faceTracks.first.select(0, 0);
        }
        return;
    case RoutineId::CrashCastSpeakerLater:
        switch (SpeakerState(a.state)) {
        case SpeakerState::Bind:
            if (!castFaceInit(a, c, stateAddress(TableId::CrashLandingCastSpeakerBox), 2, true, stateAddress(TableId::CrashLandingCastSpeakerEyes), stateAddress(TableId::CrashLandingCastSpeakerMouth))) return;
            textureTrackStop(a, c.faceTracks, FaceTrack::Eyes, 0);
            textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 0);
            [[fallthrough]];
        case SpeakerState::Act1:
            if (sub == 9) {
                switch (k) {
                case 0x1d: case 0xbf: c.faceTracks.second.select(0, 0); break;
                case 0x53: case 0xa9: textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 0); break;
                case 0x6a:
                    c.faceTracks.first.select(1, 0);
                    c.faceTracks.second.select(0, 0);
                    break;
                case 0xd3:
                    c.faceTracks.first.select(1, 0);
                    textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 0);
                    break;
                case 0xf1:
                    requestAnim(a, c.animKey, 3);
                    c.faceTracks.first.select(2, 0);
                    textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 3);
                    break;
                default: break;
                }
            } else if (sub == 0xa) {
                a.enterState(uint8_t(SpeakerState::Act3));
            }
            return;
        case SpeakerState::Act3:
            if (sub == 0xa) {
                if (k == 1) {
                    textureTrackStop(a, c.faceTracks, FaceTrack::Eyes, 0);
                    textureTrackStop(a, c.faceTracks, FaceTrack::Mouth, 4);
                }
            } else if (sub == 0xb) {
                a.enterState(uint8_t(SpeakerState::Act4));
            }
            return;
        case SpeakerState::Act4:
            if (sub != 0xb) return;
            if (k == 1) {
                requestAnim(a, c.animKey, 4);
            } else if (k == 0x3d) {
                c.faceTracks.first.select(1, 0);
            } else if (k == 0x6f) {
                requestAnim(a, c.animKey, 5);
                c.faceTracks.first.select(3, 0);
            }
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "0x48b530 variant", a.variant);
    }
}

// exe 0x48b530
void crashSpeaker(const PlayerEnv& env, AnimatedActor& a) {
    CastFields& c = classState<CastFields>(a);
    castSavePose(a);
    speakerVariant(a, c);
    castTail(env, a, true);
}

} // namespace

void crashPropUpdate(const PlayerEnv&, uint32_t actor) { crashProp(animatedActorAt(actor)); }
void crashPieceUpdate(const PlayerEnv&, uint32_t actor) { crashPiece(staticActorAt(actor)); }
void crashRollUpdate(const PlayerEnv& env, uint32_t actor) { crashRoll(env, animatedActorAt(actor)); }
void crashBarrelUpdate(const PlayerEnv&, uint32_t actor) { crashBarrel(animatedActorAt(actor)); }
void crashSecondBodyUpdate(const PlayerEnv&, uint32_t actor) { crashSecondBody(animatedActorAt(actor)); }
void crashTalkerUpdate(const PlayerEnv&, uint32_t actor) { crashTalker(animatedActorAt(actor)); }
void crashCraftUpdate(const PlayerEnv&, uint32_t actor) { crashCraft(animatedActorAt(actor)); }
void crashJetUpdate(const PlayerEnv&, uint32_t actor) { crashJet(animatedActorAt(actor)); }
void crashWatcherUpdate(const PlayerEnv& env, uint32_t actor) { crashWatcher(env, animatedActorAt(actor)); }
void crashFaceUpdate(const PlayerEnv& env, uint32_t actor) { crashFace(env, animatedActorAt(actor)); }
void crashSpeakerUpdate(const PlayerEnv& env, uint32_t actor) { crashSpeaker(env, animatedActorAt(actor)); }

} // namespace od::port
