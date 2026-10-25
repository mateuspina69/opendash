// The attract intro's cast in ST03 (see st03_attract_cast.hpp). The pool-0x20 classes are the talking cast's layout
// (game/cutscene_actor CastFields: the face tracks, the cast flags, the start key, the held key), most bound by the
// cast's common state 0 (castBind: the model, the box, the class block cleared, state 1, the first animation). Their
// tails: on the ground (kCastOnGround), the face tracks or the parts, the animation restarted at its key
// (castAnimateFromKey) or, for the second group, decoded held (castAnimate). Each variant keys on the script's step sub
// and tick (script 4); the ticks are the timeline, the animation numbers the classes' model-bank animations. The
// shadows (exe 0x5f2220 at the position + the cast's shadow offset, its sizes) are render output.
#include "content/intro/st03_attract_cast.hpp"
#include "content/intro/st03_attract_cast_state.hpp"

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
#include "game/field_alias.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/matrix.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st03_attract_cast_state;

namespace {

// the script's timeline (script 4's cast reads the block's step sub and tick)
uint8_t stepSub() { return scriptState().stepSub; }
// ... and its tick
int32_t stepTick() { return scriptState().stepTick; }

// the animation n asked, from key 0
void anim(AnimatedActor& a, CastFields& c, uint8_t n) { requestAnim(a, c.animKey, n); }
// the face tracks' selections (exe 0x5e33d0 / 0x5e33f0)
void eyes(CastFields& c, uint8_t seq) { c.faceTracks.first.select(seq, 0); }
// (the mouth track's)
void mouth(CastFields& c, uint8_t seq) { c.faceTracks.second.select(seq, 0); }
// the pose held (kCastHoldPoseB) / released
void holdPose(CastFields& c) { c.castFlags |= kCastHoldPoseB; }
// (released)
void releasePose(CastFields& c) { c.castFlags &= uint8_t(~kCastHoldPoseB); }
// part `show` shown, part `hide` hidden (two models swapped on one bone)
void swapParts(AnimatedActor& a, size_t show, size_t hide) {
    a.parts[show].flags &= uint8_t(~kPartHidden);
    a.parts[hide].flags |= kPartHidden;
}
// forward by the forward speed (exe 0x5e3690 when it is not 0)
void moveForward(AnimatedActor& a) {
    if (a.velocity.z != 0) integrateVelocity(a, 0, 0, a.velocity.z);
}
// the class tail: on the ground, then `mid` (the face tracks / the parts), then the animation from its key
template <class Mid> void castTail(const PlayerEnv& env, AnimatedActor& a, Mid mid) {
    CastFields& c = classState<CastFields>(a);
    if (!(a.flags & kActorLive)) return;
    if (c.castFlags & kCastOnGround) a.pos.y.whole = int16_t(actorGround(env, a)); // exe 0x40f190
    mid();
    castAnimateFromKey(a, c); // exe 0x5e1c30 / 0x5e1e80
}
// the faces' tail
void faceTail(const PlayerEnv& env, AnimatedActor& a) {
    CastFields& c = classState<CastFields>(a);
    castTail(env, a, [&] { textureTracksTick(a, c.faceTracks); }); // exe 0x5e34f0
}
// the second group's tail: the face tracks (when it has them), the animation decoded held while kCastHoldPose /
// kCastShadow (as the exe tests)
constexpr uint8_t kHeldByShadow = kCastHoldPose | kCastShadow;
// (the second group's tail)
void heldTail(AnimatedActor& a, bool tracks) {
    CastFields& c = classState<CastFields>(a);
    if (!(a.flags & kActorLive)) return;
    if (tracks) textureTracksTick(a, c.faceTracks);
    castAnimate(a, c.animKey, c.castFlags, kHeldByShadow);
}
// the class's variant (the exe tables by the variant byte: the named routines of engine/routines.csv); a variant past
// the table: none (the class's port gap)
template <size_t N> RoutineId variantOf(const DataTable<RoutineTable<N>>& table, const ActorHeader& a) {
    const size_t v = a.variant;
    return v < N ? table[v].id() : RoutineId::None;
}

const DataTable<RoutineTable<2>> carrierVariants{TableId::St03AttractCastCarrierVariants};
constexpr size_t kPieceAHeld = 4, kPieceAOff = 17, kPieceBHeld = 7, kPieceBOff = 18;
constexpr uint8_t kPieceDropA = 0x20, kPieceDropB = 0x2b; // the animation frames the pieces drop on
constexpr uint8_t kPlacedModelType = 1;                     // the pieces' class (exe 0x58d9c0)
constexpr int16_t kCarrierSpeed = -0x28;
enum class CarrierState : uint8_t { Init = 0, Walk = 1, Talk = 2, Leave = 3 };
enum class CarrierSub : uint8_t { Arrive = 0, Turn = 1, Wait = 2, Lift = 3, Drop = 4, Rise = 5, Walk = 6, Talk = 7 };

// a piece the carrier drops: a placed model (static type 1) of `variant` at the point, yawed, its previous pose its pose
void dropPiece(int16_t x, int16_t y, int16_t z, int16_t yaw, uint8_t variant) {
    const uint32_t at = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (at == 0) return;
    StaticActor& p = staticActorAt(at);
    p.flags |= kActorLiveDrawn;
    p.loopFlags |= kActorStageClass;
    p.pos.x.whole = x;
    p.pos.y.whole = y;
    p.pos.z.whole = z;
    p.rotation.x = 0;
    p.rotation.y = yaw;
    p.rotation.z = 0;
    p.type = kPlacedModelType;
    p.subtype = 0;
    p.variant = variant;
    p.spawnByte = 0;
    p.setParamsWord(0);
    p.savePreviousPose();
}

void carrierWalksIn(AnimatedActor& a) { // exe 0x47ec40
    CarrierFields& f = classState<CarrierFields>(a);
    CastFields& c = f;
    const int32_t k = stepTick();
    switch (CarrierState(a.state)) {
    case CarrierState::Init:
        if (!castBind(a, stateAddress(TableId::St03AttractCastCarrierBox), 1, false)) return;
        a.velocity.z = kCarrierSpeed;
        a.acceleration.z = 0;
        c.animKey = 0;
        c.faceTracks.init(stateAddress(TableId::St03AttractCastCarrierEyes), stateAddress(TableId::St03AttractCastCarrierMouth));
        eyes(c, 0);
        mouth(c, 0);
        f.shadowWidth = 0x30;
        f.shadowLength = 0x160;
        c.castFlags |= kCastShadow;
        f.shadowLevel = 0;
        a.parts[kPieceAHeld].flags |= kPartHidden;
        a.parts[kPieceAOff].flags &= uint8_t(~kPartHidden);
        a.parts[kPieceBHeld].flags |= kPartHidden;
        a.parts[kPieceBOff].flags &= uint8_t(~kPartHidden);
        break;
    case CarrierState::Walk: {
        const uint8_t sub = a.subState;
        switch (CarrierSub(sub)) {
        case CarrierSub::Arrive:
            switch (k) {
            case 0x47: anim(a, c, 9); a.velocity.z = 0; eyes(c, 1); mouth(c, 1); break;
            case 0x5b: eyes(c, 2); break;
            case 0x65: eyes(c, 3); mouth(c, 0); break;
            case 0x79: anim(a, c, 10); break;
            case 0x97: anim(a, c, 0xd); a.acceleration.z = -8; break;
            case 0x9b: a.acceleration.z = 0; break;
            case 0xa1: eyes(c, 4); mouth(c, 2); ++a.subState; break;
            default: break;
            }
            break;
        case CarrierSub::Turn:
            if (a.animEnded()) {
                a.anim = 1;
                a.subState = uint8_t(sub + 1);
                c.animKey = 0;
            }
            break;
        case CarrierSub::Wait:
            if (k == 0xb5) {
                a.anim = 0xf;
                a.subState = uint8_t(sub + 1);
                a.velocity.z = 0;
                c.animKey = 0;
            }
            break;
        case CarrierSub::Lift:
            if (a.animEnded()) {
                a.anim = 2;
                a.subState = uint8_t(sub + 1);
                c.animKey = 0;
            }
            break;
        case CarrierSub::Drop:
            if (uint8_t(a.animKey.frame) == kPieceDropA) {
                a.parts[kPieceAHeld].flags &= uint8_t(~kPartHidden);
                a.parts[kPieceAOff].flags |= kPartHidden;
                dropPiece(-0xd0, -0x30, -0x70, 0, 2);
            }
            if (uint8_t(a.animKey.frame) == kPieceDropB) {
                a.parts[kPieceBHeld].flags &= uint8_t(~kPartHidden);
                a.parts[kPieceBOff].flags |= kPartHidden;
                dropPiece(-0xeb, -0x30, -0x94, kQuarterTurn, 3);
            }
            if (a.animEnded()) {
                a.anim = 0xe;
                ++a.subState;
                c.animKey = 0;
            }
            break;
        case CarrierSub::Rise:
            if (a.animEnded()) {
                a.anim = 0x10;
                a.subState = uint8_t(sub + 1);
                a.velocity.z = kCarrierSpeed;
                c.animKey = 0;
                f.turnClock = 0;
            }
            break;
        case CarrierSub::Walk:
            if (f.turnClock < 0x14) a.rotation.y = int16_t(uint16_t(a.rotation.y) + 0xeu);
            else if (f.turnClock > 0x46) a.rotation.y = int16_t(uint16_t(a.rotation.y) - 0xeu);
            ++f.turnClock;
            if (k == 0x12d) {
                a.pos.x.whole = -0x48;
                a.pos.z.whole = -0x2b;
                ++a.subState;
                a.rotation.y = kHalfTurn;
                a.prevAnim = kNoAnimShown;
                c.animKey = 0x13;
                f.turnClock = 0;
            }
            break;
        case CarrierSub::Talk:
            if (stepSub() != 2) {
                a.enterStateBytes(uint8_t(CarrierState::Talk));
                break;
            }
            switch (k) {
            case 0x1ff: anim(a, c, 4); a.pos.z.whole = 0x28; break;
            case 0x142: anim(a, c, 0x17); break;
            case 0x14b: anim(a, c, 3); a.velocity.z = 0; break;
            case 0x173: anim(a, c, 0xb); mouth(c, 3); a.pos.z.whole = 0x10; break;
            case 0x1e7: case 0x24f: mouth(c, 2); break;
            case 0x213: anim(a, c, 5); break;
            case 0x227: anim(a, c, 6); eyes(c, 5); mouth(c, 4); break;
            default: break;
            }
            break;
        default: break;
        }
        break;
    }
    case CarrierState::Talk:
        if (stepSub() != 4) {
            if (stepSub() == 6) {
                mouth(c, 2);
                a.enterStateBytes(uint8_t(CarrierState::Leave));
            }
            break;
        }
        switch (k) {
        case 1: anim(a, c, 7); eyes(c, 4); mouth(c, 2); break;
        case 0x29: eyes(c, 6); break;
        case 0x33: mouth(c, 5); break;
        case 0xb5: mouth(c, 2); break;
        default: break;
        }
        break;
    case CarrierState::Leave:
        if (k == 0x3d) {
            anim(a, c, 8);
            a.pos.z.whole = int16_t(uint16_t(a.pos.z.whole) + 0x10u);
            a.pos.x.whole = -0x140;
            a.rotation.y = 0xe80;
            eyes(c, 7);
            mouth(c, 0);
        } else if (k == 0xbf) {
            eyes(c, 8);
        }
        break;
    default: break;
    }
    if (a.acceleration.z != 0) a.velocity.z = int16_t(uint16_t(a.velocity.z) + uint16_t(a.acceleration.z));
    if (a.velocity.z != 0) integrateVelocity(a, 0, 0, a.velocity.z);
}

// +8 of variant 1: bound, its lines of step 0, of step 1, done
enum class CarrierTalkState : uint8_t { Bind = 0, Talks = 1, Answers = 2, Done = 3 };

void carrierTalks(AnimatedActor& a) { // exe 0x47f460
    CastFields& c = classState<CastFields>(a);
    const int32_t k = stepTick();
    switch (CarrierTalkState(a.state)) {
    case CarrierTalkState::Bind:
        if (!castBind(a, stateAddress(TableId::St03AttractCastCarrierBox), 0x13, false)) return;
        c.animKey = 0;
        a.velocity.z = 0;
        c.faceTracks.init(stateAddress(TableId::St03AttractCastCarrierEyes), stateAddress(TableId::St03AttractCastCarrierMouth));
        eyes(c, 0);
        mouth(c, 0);
        a.parts[kPieceAHeld].flags &= uint8_t(~kPartHidden);
        a.parts[kPieceAOff].flags |= kPartHidden;
        a.parts[kPieceBHeld].flags &= uint8_t(~kPartHidden);
        a.parts[kPieceBOff].flags |= kPartHidden;
        break;
    case CarrierTalkState::Talks:
        if (stepSub() != 0) {
            if (stepSub() == 1) a.enterState(uint8_t(CarrierTalkState::Answers));
            break;
        }
        switch (k) {
        case 0x1a5: anim(a, c, 0x12); eyes(c, 4); mouth(c, 0x12); break;
        case 0x79: anim(a, c, 0x14); eyes(c, 9); mouth(c, 1); break;
        case 0xb5: anim(a, c, 0x15); eyes(c, 10); mouth(c, 0x10); break;
        case 0x15f: anim(a, c, 0x11); eyes(c, 0xb); mouth(c, 0x11); break;
        case 0x24f: eyes(c, 4); mouth(c, 0x12); break;
        case 0x209: case 0x29f: mouth(c, 0x13); break;
        default: break;
        }
        break;
    case CarrierTalkState::Answers:
        if (stepSub() == 1) {
            switch (k) {
            case 1: anim(a, c, 0x18); eyes(c, 0); mouth(c, 0x14); break;
            case 0x1e: eyes(c, 0xc); break;
            case 0x3d: anim(a, c, 0x19); a.velocity.z = 0; eyes(c, 0); mouth(c, 0x14); break;
            case 0xab: anim(a, c, 0x16); eyes(c, 5); mouth(c, 0x15); break;
            case 0xb5: eyes(c, 0xd); mouth(c, 0x16); break;
            default: break;
            }
        } else if (stepSub() == 2) {
            a.enterState(uint8_t(CarrierTalkState::Done));
        }
        break;
    default: break;
    }
    moveForward(a);
}

// exe 0x47eb30: the variant, then the faces' tail
void st03PieceCarrier(const PlayerEnv& env, AnimatedActor& a) {
    castSavePose(a);
    switch (variantOf(carrierVariants, a)) {
    case RoutineId::St03CastCarrierWalksIn: carrierWalksIn(a); break;
    case RoutineId::St03CastCarrierTalks: carrierTalks(a); break;
    default: notPortedAt(kStoryPort, "0x47eb30 variant", a.variant);
    }
    faceTail(env, a);
}

// ---- the walker (exe 0x481100, type 0x1f, variants 0x891948: 0x481210 / 0x481510)
// ----------------------------------
// face tracks 0x8919cc / 0x891abc; variant 0 waits the steps out and is placed at step 4, variant 1 talks and walks
const DataTable<RoutineTable<2>> walkerVariants{TableId::St03AttractCastWalkerVariants};
constexpr size_t kWalkerHiddenPart = 6, kWalkerShownPart = 19;
enum class WalkerState : uint8_t { Init = 0, Step1 = 1, Step3 = 2, Step5 = 3, Step6 = 4 };

void walkerWaits(AnimatedActor& a) { // exe 0x481210
    CastFields& c = classState<CastFields>(a);
    const int32_t k = stepTick();
    switch (WalkerState(a.state)) {
    case WalkerState::Init:
        if (!castBind(a, stateAddress(TableId::St03AttractCastWalkerBox), 0, false)) return;
        c.faceTracks.init(stateAddress(TableId::St03AttractCastWalkerEyes), stateAddress(TableId::St03AttractCastWalkerMouth));
        eyes(c, 0);
        mouth(c, 0);
        return;
    case WalkerState::Step1:
        if (stepSub() == 2) {
            a.enterStateBytes(uint8_t(WalkerState::Step3));
        }
        return;
    case WalkerState::Step3:
        if (stepSub() == 3) {
            if (k == 1) a.anim = 1;
        } else if (stepSub() == 4) {
            a.enterStateBytes(uint8_t(WalkerState::Step5));
        }
        return;
    case WalkerState::Step5:
        if (stepSub() != 5) {
            if (stepSub() == 6) {
                a.enterStateBytes(uint8_t(WalkerState::Step6));
            }
            return;
        }
        switch (k) {
        case 1: eyes(c, 1); mouth(c, 1); return;
        case 0x1f: a.anim = 2; return;
        case 0x51: a.anim = 3; return;
        case 0x5b: eyes(c, 2); mouth(c, 1); return;
        case 0x97: a.anim = 4; mouth(c, 2); return;
        case 0xbf: mouth(c, 1); return;
        case 0xd3: eyes(c, 3); return;
        default: return;
        }
    case WalkerState::Step6:
        if (k == 0x3d) {
            const uint8_t hidden = a.parts[kWalkerHiddenPart].flags, shown = a.parts[kWalkerShownPart].flags;
            a.anim = 5;
            a.pos.x.whole = -0x98;
            a.pos.y.whole = 0;
            a.pos.z.whole = -0x70;
            a.rotation.y = kQuarterTurn;
            a.parts[kWalkerHiddenPart].flags = uint8_t(hidden | kPartHidden);
            a.parts[kWalkerShownPart].flags = uint8_t(shown & ~kPartHidden);
            eyes(c, 4);
            mouth(c, 1);
        } else if (k == 0x6f) {
            eyes(c, 5);
        }
        return;
    default: return;
    }
}

// +8 of variant 1: bound, its lines of step 0, its walk of step 1
enum class WalkerWalkState : uint8_t { Bind = 0, Talks = 1, Walks = 2 };

void walkerWalks(AnimatedActor& a) { // exe 0x481510
    CastFields& c = classState<CastFields>(a);
    const WalkerWalkState st = WalkerWalkState(a.state);
    if (st == WalkerWalkState::Bind) {
        if (!castBind(a, stateAddress(TableId::St03AttractCastWalkerBox), 6, false)) return;
        c.animKey = 0;
        a.velocity.z = 0;
        c.faceTracks.init(stateAddress(TableId::St03AttractCastWalkerEyes), stateAddress(TableId::St03AttractCastWalkerMouth));
        eyes(c, 0);
        mouth(c, 2);
    } else if (st == WalkerWalkState::Talks) {
        if (stepSub() != 0) {
            if (stepSub() == 1) a.enterState(uint8_t(WalkerWalkState::Walks));
        } else {
            switch (stepTick()) {
            case 0x33f: anim(a, c, 0xb); eyes(c, 0xb); mouth(c, 8); break;
            case 0x353: mouth(c, 0xd); break;
            case 0x2e5: eyes(c, 10); mouth(c, 7); break;
            case 0x14b: case 0xdd: mouth(c, 6); break;
            case 0x187: anim(a, c, 9); eyes(c, 8); mouth(c, 6); break;
            case 0x227: anim(a, c, 10); eyes(c, 9); mouth(c, 7); break;
            case 0xf1: case 0xb3: mouth(c, 5); break;
            case 0x51: anim(a, c, 7); mouth(c, 3); break;
            case 0x79: mouth(c, 2); break;
            case 0xa7: eyes(c, 6); break;
            case 0xab: anim(a, c, 8); break;
            default: break;
            }
        }
    } else if (st == WalkerWalkState::Walks && stepSub() == 1) {
        switch (stepTick()) {
        case 1: anim(a, c, 0xc); eyes(c, 10); mouth(c, 0xc); break;
        case 0x1f: mouth(c, 0xb); break;
        case 0x3d: anim(a, c, 0xd); eyes(c, 0xd); mouth(c, 9); break;
        case 0x47: anim(a, c, 0xe); a.velocity.z = -0x80; eyes(c, 0xd); mouth(c, 10); break;
        case 0xc9: a.velocity.z = 0; break;
        default: break;
        }
    }
    if (a.velocity.z != 0) moveAlongYaw(a.pos, 0, 0, 0, a.velocity.z); // exe 0x5e36f0
}

// exe 0x481100: the variant, then the faces' tail
void st03Walker(const PlayerEnv& env, AnimatedActor& a) {
    castSavePose(a);
    switch (variantOf(walkerVariants, a)) {
    case RoutineId::St03CastWalkerWaits: walkerWaits(a); break;
    case RoutineId::St03CastWalkerWalks: walkerWalks(a); break;
    default: notPortedAt(kStoryPort, "0x481100 variant", a.variant);
    }
    faceTail(env, a);
}

const DataTable<RoutineTable<2>> flutterVariants{TableId::St03AttractCastFlutterVariants};
constexpr int32_t kLiftTick = 0x51;
constexpr int16_t kLiftAcceleration = -0x40, kLiftTopSpeed = -0x4ff;

// +8 of the bobbing Flutter: bound, bobbing
enum class FlutterBobState : uint8_t { Bind = 0, Bobs = 1 };
// +8 of the lifting Flutter: bound, waiting for tick 0x51, rising, at its top speed
enum class FlutterLiftState : uint8_t { Bind = 0, Waits = 1, Rises = 2, Risen = 3 };

// exe 0x49d8c0: the variant, then animation 0 or stepped
void st03Flutter(AnimatedActor& a) {
    castSavePose(a);
    switch (variantOf(flutterVariants, a)) {
    case RoutineId::St03CastFlutterBobs: {
        FlutterFields& f = classState<FlutterFields>(a);
        if (FlutterBobState(a.state) == FlutterBobState::Bind) {
            if (!castBind(a, stateAddress(TableId::St03AttractCastFlutterBox), 0, false)) return;
            a.airborne = kAirborneFalling;
            f.bobClock = 0;
            f.bobBase = a.pos.y.whole;
        } else if (FlutterBobState(a.state) == FlutterBobState::Bobs) {
            const uint32_t i = (uint32_t(uint8_t(f.bobClock)) >> 2) & 0x3f;
            a.pos.y.whole = int16_t(((int32_t(sin64(i)) << 5) >> 0xc) + f.bobBase);
            ++f.bobClock;
        }
        break;
    }
    case RoutineId::St03CastFlutterLifts: {
        const FlutterLiftState st = FlutterLiftState(a.state);
        if (st == FlutterLiftState::Bind) {
            if (!castBind(a, stateAddress(TableId::St03AttractCastFlutterBox), 0, false)) return;
            a.airborne = kAirborneFalling;
            a.velocity.y = 0;
            a.acceleration.y = 0;
        } else if (st == FlutterLiftState::Waits) {
            if (stepTick() == kLiftTick) {
                a.acceleration.y = kLiftAcceleration;
                a.enterState(uint8_t(FlutterLiftState::Rises));
            }
        } else if (st == FlutterLiftState::Rises && a.velocity.y < kLiftTopSpeed) {
            a.acceleration.y = 0;
            a.enterState(uint8_t(FlutterLiftState::Risen));
        }
        if (a.acceleration.y != 0) a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
        if (a.velocity.y != 0) integrateVelocity(a, 0, a.velocity.y, 0);
        break;
    }
    default: notPortedAt(kStoryPort, "0x49d8c0 variant", a.variant);
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim != a.prevAnim) animSetPose(a, 0, 0); // (the exe passes animation 0, key 0)
    else animStepPose(a);
}

// ---- the placed models (exe 0x58d9c0, static type 1, variants 0x8cd930)
// ---------------------------------------------
// placed (variant 0x58d9e0), turning 0x20 a tick (0x58da30), or moved to a fixed point at step 6 tick 0 (0x58da90 /
// 0x58db10; its spawn byte 8 set: parked in state 2, never moved)
struct PlacedModelFields {
    explicit PlacedModelFields(const StaticActor& actor) : PlacedModelFields(const_cast<StaticActor&>(actor)) {}
    explicit PlacedModelFields(StaticActor& actor) : parked(actor.params[0]) {}
    uint8_t& parked;
};
const DataTable<RoutineTable<4>> placedModelVariants{TableId::St03AttractCastPlacedModelVariants};
constexpr int16_t kPlacedTurn = 0x20;
constexpr uint8_t kPlacedMoveStep = 6;

// the placed models' state 0: the model of the variant (none: freed, false), the box, the pose and the motion cleared
bool placedModelBind(StaticActor& a) {
    if (!bindActorModel(a, a.variant)) {
        freeActor(a);
        return false;
    }
    a.collisionBox.pointAt(TableId::St03AttractCastPlacedModelBox);
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.velocity.z = 0;
    a.velocity.y = 0;
    a.velocity.x = 0;
    return true;
}

// +8: bound, placed (moved at step 6), parked (never moved)
enum class PlacedModelState : uint8_t { Bind = 0, Placed = 1, Parked = 2 };

// exe 0x58d9c0: the variant (exe table 0x8cd930)
void st03PlacedModel(StaticActor& a) {
    const RoutineId v = variantOf(placedModelVariants, a);
    switch (v) {
    case RoutineId::St03CastPlacedStill:
        if (PlacedModelState(a.state) == PlacedModelState::Bind && placedModelBind(a)) a.enterState(uint8_t(PlacedModelState::Placed));
        return;
    case RoutineId::St03CastPlacedTurning:
        if (PlacedModelState(a.state) == PlacedModelState::Bind) {
            if (!placedModelBind(a)) return;
            a.enterState(uint8_t(PlacedModelState::Placed));
        }
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + uint16_t(kPlacedTurn));
        return;
    case RoutineId::St03CastPlacedMovedA:
    case RoutineId::St03CastPlacedMovedB: {
        const ScriptState& s = scriptState();
        if (PlacedModelState(a.state) == PlacedModelState::Bind) {
            const uint8_t parked = PlacedModelFields(a).parked;
            if (!placedModelBind(a)) return;
            a.enterState(uint8_t(parked != 0 ? PlacedModelState::Parked : PlacedModelState::Placed));
        } else if (PlacedModelState(a.state) == PlacedModelState::Placed && s.stepSub == kPlacedMoveStep && s.stepTick == 0) {
            if (v == RoutineId::St03CastPlacedMovedB) {
                a.pos.x.whole = -0xf8;
                a.pos.z.whole = -0xac;
                a.rotation.y = 0x518;
            } else {
                a.pos.x.whole = -0xec;
                a.pos.z.whole = -0x70;
            }
        }
        return;
    }
    default: notPortedAt(kStoryPort, "0x58d9c0 variant", a.variant);
    }
}

// ---- the still model (exe 0x5ad760, static type 0x34): state 0 only (model variant 0, none: freed; no class block
// clear)

// +8 of the still model and the scenery: bound, still
enum class StillState : uint8_t { Bind = 0, Still = 1 };

// exe 0x5ad760
void st03StillModel(StaticActor& a) {
    if (StillState(a.state) != StillState::Bind) return;
    if (!bindActorModel(a, 0)) {
        freeActor(a); // exe 0x5e1540
        return;
    }
    a.collisionBox.pointAt(TableId::St03AttractCastStillModelBox);
    a.airborne = 0;
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.velocity.z = 0;
    a.velocity.y = 0;
    a.velocity.x = 0;
    a.enterState(uint8_t(StillState::Still));
}

// ---- the scenery (exe 0x59b690, static type 0x16): state 0 only -- the record of its kind and index {u8 model
// variant, ., ., ., s16 x, s16 z} (exe tables 0x8ce730 by kind): its model, placed on the ground plane, yawed
struct SceneryRecord : DataRecord {
    uint8_t variant = 0;
    int16_t x = 0, z = 0;
    static SceneryRecord load(const TableReader& r) { return {{}, r.u8(0), r.s16(4), r.s16(6)}; }
};
constexpr uint32_t kSceneryRecordSize = 8;
// the kinds' records (exe table 0x8ce730: a pointer per kind to its records by index)
struct SceneryKinds : DataRecord {
    static constexpr size_t kKinds = 6, kMaxRecords = 0x40;
    static constexpr uint32_t kBytes = kKinds * 4;
    std::array<TableReader, kKinds> kinds;
    SceneryRecord record(size_t kind, size_t index) const {
        if (kind >= kKinds) tableIndexError(kind, kKinds);
        return SceneryRecord::load(kinds[kind].sub(index * kSceneryRecordSize, kSceneryRecordSize));
    }
    static SceneryKinds load(const TableReader& r) {
        SceneryKinds t;
        for (size_t k = 0; k < kKinds; ++k) t.kinds[k] = r.follow(k * 4, kMaxRecords * kSceneryRecordSize);
        return t;
    }
};
const DataTable<SceneryKinds> sceneryKinds{TableId::St03SceneryKinds};
constexpr int kSceneryYawShift = 4;

// exe 0x59b690
void st03Scenery(StaticActor& a) {
    if (StillState(a.state) != StillState::Bind) return;
    AttractSceneryFields f(a);
    const SceneryRecord rec = sceneryKinds->record(size_t(int32_t(f.kind)), size_t(int32_t(f.index)));
    a.variant = rec.variant;
    if (!bindActorModel(a, a.variant)) {
        freeActor(a); // exe 0x5e1540
        return;
    }
    a.pos.y.whole = 0;
    a.pos.x.whole = rec.x;
    a.pos.z.whole = rec.z;
    a.rotation.x = 0;
    a.rotation.y = int16_t(int32_t(f.yaw) << kSceneryYawShift);
    a.rotation.z = 0;
    a.collisionBox.pointAt(TableId::St03AttractCastSceneryBox);
    a.enterState(uint8_t(StillState::Still));
}

// ---- Roll (exe 0x476f80, type 0x16, variants 0x890920: 0x477090 / 0x477440 / 0x4775e0)
// -----------------------------
// face tracks 0x89097c / 0x8909c8; two pairs of parts swapped (4 / 16 and 7 / 15)
const DataTable<RoutineTable<3>> rollVariants{TableId::St03AttractCastRollVariants};
constexpr size_t kRollHead = 4, kRollOtherHead = 16, kRollArm = 7, kRollOtherArm = 15;

// the state 0's own part (after the bind): the key, the pose held (`held`), the face tracks, the parts swapped
void rollInit(AnimatedActor& a, CastFields& c, bool held, uint8_t eyesSeq) {
    c.animKey = 0;
    if (held) holdPose(c);
    c.faceTracks.init(stateAddress(TableId::St03AttractCastRollEyes), stateAddress(TableId::St03AttractCastRollMouth)); // exe 0x5e33a0
    eyes(c, eyesSeq);
    mouth(c, 0);
    swapParts(a, kRollHead, kRollOtherHead);
    swapParts(a, kRollArm, kRollOtherArm);
}

// +8: bound, her scene, waiting for the next, her late lines (the variants use the first ones)
enum class RollState : uint8_t { Bind = 0, Scene = 1, Waits = 2, Late = 3 };

void rollTalks(AnimatedActor& a, CastFields& c) { // exe 0x477090
    switch (RollState(a.state)) {
    case RollState::Bind:
        if (!castBind(a, stateAddress(TableId::St03AttractCastRollBox), 0, false)) return;
        rollInit(a, c, false, 0);
        return;
    case RollState::Scene: // no step check: the tick alone
        switch (stepTick()) {
        case 2: anim(a, c, 1); mouth(c, 2); break;
        case 0x47: mouth(c, 0); break;
        case 0x65: anim(a, c, 2); mouth(c, 2); break;
        case 0xdd: mouth(c, 0); break;
        case 0xe7: anim(a, c, 3); mouth(c, 2); break;
        case 0x137: mouth(c, 0); break;
        case 0x1d7: anim(a, c, 4); eyes(c, 1); mouth(c, 1); break;
        case 0x204: anim(a, c, 5); swapParts(a, kRollOtherArm, kRollArm); break;
        case 0x213: case 0x26d: mouth(c, 2); break;
        case 0x214: eyes(c, 7); swapParts(a, kRollOtherHead, kRollHead); break;
        case 0x24f: mouth(c, 0); break;
        case 0x2bd: mouth(c, 0); a.state = uint8_t(RollState::Waits); break;
        default: break;
        }
        return;
    case RollState::Waits:
        if (stepSub() == 1 && stepTick() == 1) {
            anim(a, c, 6);
            holdPose(c);
            swapParts(a, kRollHead, kRollOtherHead);
            a.state = uint8_t(RollState::Late);
        }
        return;
    case RollState::Late:
        if (stepSub() != 2) return;
        switch (stepTick()) {
        case 0xbf:
            a.anim = 6;
            a.prevAnim = kNoAnimShown;
            c.animKey = 0;
            releasePose(c);
            break;
        case 0x245: anim(a, c, 7); break;
        case 0x295: anim(a, c, 8); break;
        default: break;
        }
        return;
    default: return;
    }
}

void rollLater(AnimatedActor& a, CastFields& c) { // exe 0x477440
    switch (RollState(a.state)) {
    case RollState::Bind:
        if (!castBind(a, stateAddress(TableId::St03AttractCastRollBox), 9, false)) return;
        rollInit(a, c, true, 0);
        return;
    case RollState::Scene:
        if (stepSub() == 6) {
            switch (stepTick()) {
            case 0x15:
                a.anim = 9;
                a.prevAnim = kNoAnimShown;
                c.animKey = 0;
                releasePose(c);
                swapParts(a, kRollOtherHead, kRollHead);
                eyes(c, 4);
                break;
            case 0x29: mouth(c, 2); break;
            case 0x47: mouth(c, 0); break;
            default: break;
            }
        } else if (stepSub() == 7) {
            a.enterState(uint8_t(RollState::Scene));
        }
        return;
    case RollState::Waits:
        if (stepSub() == 8 && stepTick() == 1) a.rotation.y = -0x180;
        return;
    default: return;
    }
}

void rollLast(AnimatedActor& a, CastFields& c) { // exe 0x4775e0
    if (RollState(a.state) == RollState::Bind) {
        if (!castBind(a, stateAddress(TableId::St03AttractCastRollBox), 10, false)) return;
        rollInit(a, c, true, 5);
    } else if (RollState(a.state) == RollState::Scene && stepSub() == 0x10 && stepTick() == 0x3d) {
        releasePose(c);
    }
}

// exe 0x476f80: the variant, then the faces' tail
void st03Roll(const PlayerEnv& env, AnimatedActor& a) {
    CastFields& c = classState<CastFields>(a);
    castSavePose(a);
    switch (variantOf(rollVariants, a)) {
    case RoutineId::St03CastRollTalks: rollTalks(a, c); break;
    case RoutineId::St03CastRollLater: rollLater(a, c); break;
    case RoutineId::St03CastRollLast: rollLast(a, c); break;
    default: notPortedAt(kStoryPort, "0x476f80 variant", a.variant);
    }
    faceTail(env, a);
}

// ---- Barrel (exe 0x47c790, type 0x1a, variants 0x891104: 0x47c8c0 / 0x47ca30 / 0x47cac0 / 0x47ce60 / 0x47d060 /
// 0x47d0e0) ------------------------------------------------------------------------------------------------------
// his heads (parts 1 / 10 / 11 on one bone) by the request in his step byte (exe 0x47cc40: Talk alternates the first
// and the third every 4 ticks of his step counter; First / Second show one once)
enum class HeadsRequest : uint8_t { None = 0, Talk = 1, First = 2, Second = 3 };

constexpr size_t kBarrelHead = 1, kBarrelSecondHead = 10, kBarrelTalkHead = 11;
constexpr uint8_t kTalkPhase = 4;
constexpr uint8_t kTalkClockStart = 0; // the talk's clock (his step counter) restarted
const DataTable<RoutineTable<6>> barrelVariants{TableId::St03AttractCastBarrelVariants};

void barrelHeads(AnimatedActor& a) { // exe 0x47cc40
    auto&& head = a.parts[kBarrelHead].flags;
    auto&& second = a.parts[kBarrelSecondHead].flags;
    auto&& talk = a.parts[kBarrelTalkHead].flags;
    switch (HeadsRequest(a.step)) {
    case HeadsRequest::Talk: {
        const uint8_t clock = a.stepCounter;
        if (clock & kTalkPhase) {
            head |= kPartHidden;
            second |= kPartHidden;
            talk &= uint8_t(~kPartHidden);
        } else {
            head &= uint8_t(~kPartHidden);
            second |= kPartHidden;
            talk |= kPartHidden;
        }
        a.stepCounter = uint8_t(clock + 1);
        break;
    }
    case HeadsRequest::First:
        head &= uint8_t(~kPartHidden);
        second |= kPartHidden;
        talk |= kPartHidden;
        a.step = uint8_t(HeadsRequest::None);
        a.stepCounter = kTalkClockStart;
        break;
    case HeadsRequest::Second:
        head |= kPartHidden;
        second &= uint8_t(~kPartHidden);
        talk |= kPartHidden;
        a.step = uint8_t(HeadsRequest::None);
        a.stepCounter = kTalkClockStart;
        break;
    default: break;
    }
}
// his heads' request (his step byte)
void heads(AnimatedActor& a, HeadsRequest r) { a.step = uint8_t(r); }

// +8: bound, his lines, done
enum class BarrelState : uint8_t { Bind = 0, Acts = 1, Done = 2 };

void barrelPlaced(AnimatedActor& a, CastFields& c) { // exe 0x47c8c0
    switch (BarrelState(a.state)) {
    case BarrelState::Bind:
        if (!castBind(a, stateAddress(TableId::St03AttractCastBarrelBox), 0, false)) return;
        a.velocity.z = 0;
        c.animKey = 0;
        break;
    case BarrelState::Acts:
        if (stepSub() == 2) {
            if (stepTick() == 0x12d) {
                a.pos.x.whole = 0xf4;
                a.pos.z.whole = 0x694;
            }
        } else if (stepSub() == 3) {
            switch (stepTick()) {
            case 1:
                a.pos.x.whole = 0xb4;
                a.pos.z.whole = 0x6c4;
                anim(a, c, 1);
                break;
            case 0x1f: heads(a, HeadsRequest::Talk); break;
            case 0x47: heads(a, HeadsRequest::First); break;
            case 0x83: anim(a, c, 2); heads(a, HeadsRequest::Second); break;
            case 0xd3:
                anim(a, c, 3);
                a.pos.x.whole = 0x100;
                a.pos.z.whole = 0x5f8;
                a.rotation.y = 0x88;
                break;
            case 0xe8: a.velocity.z = -8; break;
            case 0x110: a.velocity.z = 0; break;
            default: break;
            }
        } else if (stepSub() == 4) {
            a.enterStateKeepingStep(uint8_t(BarrelState::Done)); // the step (his heads' request) kept
        }
        break;
    default: break;
    }
    moveForward(a);
}

void barrelTurns(AnimatedActor& a, CastFields& c) { // exe 0x47ca30
    if (BarrelState(a.state) == BarrelState::Bind) {
        if (!castBind(a, stateAddress(TableId::St03AttractCastBarrelBox), 0, false)) return;
        c.animKey = 0;
        heads(a, HeadsRequest::Second);
    } else if (BarrelState(a.state) == BarrelState::Acts && stepSub() == 8 && stepTick() == 1) {
        a.enterStateKeepingStep(uint8_t(BarrelState::Done));
        a.rotation.y = -0x280;
    }
}

void barrelTalksLate(AnimatedActor& a, CastFields& c) { // exe 0x47cac0
    if (BarrelState(a.state) == BarrelState::Bind) {
        if (!castBind(a, stateAddress(TableId::St03AttractCastBarrelBox), 4, false)) return;
        a.velocity.z = 0;
        c.animKey = 0;
        holdPose(c);
    } else if (BarrelState(a.state) == BarrelState::Acts && stepSub() == 0x10) {
        switch (stepTick()) {
        case 0x15: releasePose(c); heads(a, HeadsRequest::Talk); break;
        case 0x56: case 0x97: heads(a, HeadsRequest::Talk); break;
        case 0x47: case 0x79: case 0xc9: heads(a, HeadsRequest::First); break;
        default: break;
        }
    }
}

void barrelTalks(AnimatedActor& a, CastFields& c) { // exe 0x47ce60
    switch (BarrelState(a.state)) {
    case BarrelState::Bind:
        if (!castBind(a, stateAddress(TableId::CrashLandingCastBarrelBox), 0, false)) return;
        clearVelocity(a);
        c.animKey = 0;
        return;
    case BarrelState::Acts:
        if (stepSub() != 1) a.enterState(uint8_t(BarrelState::Done));
        return;
    case BarrelState::Done:
        if (stepSub() != 2) return;
        switch (stepTick()) {
        case 0xb: anim(a, c, 1); break;
        case 0xf1: case 0x105: anim(a, c, 2); heads(a, HeadsRequest::Talk); break;
        case 0x15: case 0x3d: case 0x137: heads(a, HeadsRequest::Talk); break;
        case 0x2d: case 0x4c: case 0xfb: case 0x119: case 0x173: heads(a, HeadsRequest::First); break;
        default: break;
        }
        return;
    default: return;
    }
}

void barrelStill(AnimatedActor& a, CastFields& c) { // exe 0x47d060 (0x47d0e0 is byte-identical)
    if (BarrelState(a.state) != BarrelState::Bind) return;
    if (!castBind(a, stateAddress(TableId::CrashLandingCastBarrelBox), 3, false)) return;
    clearVelocity(a);
    c.animKey = 0;
}

// exe 0x47c790: the variant, then the tail with his heads
void st03Barrel(const PlayerEnv& env, AnimatedActor& a) {
    CastFields& c = classState<CastFields>(a);
    castSavePose(a);
    switch (variantOf(barrelVariants, a)) {
    case RoutineId::St03CastBarrelPlaced: barrelPlaced(a, c); break;
    case RoutineId::St03CastBarrelTurns: barrelTurns(a, c); break;
    case RoutineId::St03CastBarrelTalksLate: barrelTalksLate(a, c); break;
    case RoutineId::CastBarrelTalks: barrelTalks(a, c); break;
    case RoutineId::CastBarrelStill: barrelStill(a, c); break;
    case RoutineId::CastBarrelStillB: barrelStill(a, c); break;
    default: notPortedAt(kStoryPort, "0x47c790 variant", a.variant);
    }
    castTail(env, a, [&] { barrelHeads(a); });
}

// ---- the watcher (exe 0x47fa80, type 0x1d, variants 0x8915fc: 0x47fbb0 / 0x47fc20 / 0x47fda0 / 0x47fed0 /
// 0x480000 / 0x480090; the table's 7th entry is 0 and the next class's follows) --------------------------------------
// variant 2 throws a spark on its animation frame 0x26, variant 3 one riding it at its state 0 (the opening's effect
// class 0x435960, its variant RiderSpark: content/intro/opening_effects)
struct WatcherFields {
    explicit WatcherFields(const AnimatedActor& actor) : WatcherFields(const_cast<AnimatedActor&>(actor)) {}
    explicit WatcherFields(AnimatedActor& actor) : firstAnim(actor.params[0]) {}
    uint8_t& firstAnim; // variant 0's animation (its spawn byte 8)
};
const DataTable<RoutineTable<6>> watcherVariants{TableId::St03AttractCastWatcherVariants};
constexpr uint8_t kSparkFrame = 0x26;
constexpr uint8_t kSparkColour = 0x80;
constexpr int8_t kSparkEndless = -1; // the rider spark's ticks byte 0xff: never freed by its ticks
constexpr int16_t kSparkDrawParam = 0x18;

// exe 0x47fda0 / 0x47fed0's spark: a rider spark (effect type 0xf) at the watcher's position moved by Ry(yaw) *
// (-0x2e5, -0xa68, z) (exe 0x5e3750), riding the actor of `owner` (0: none)
void spawnWatcherSpark(AnimatedActor& a, int z, uint8_t owner) {
    const uint32_t at = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (at == 0) return;
    EffectActor& e = effectActorAt(at);
    RiderSparkFields& s = classState<RiderSparkFields>(e);
    e.flags = kActorLiveDrawn;
    e.type = kOpeningEffectType;
    e.loopFlags |= kActorLateLoop;
    e.variant = uint8_t(OpeningEffect::RiderSpark);
    e.pos.x = a.pos.x.whole;
    e.pos.y = a.pos.y.whole;
    e.pos.z = a.pos.z.whole;
    moveAlongYaw16(e.pos, a.rotation.y, -0x2e5, -0xa68, z);
    s.drawParam = uint16_t(kSparkDrawParam);
    s.ownerSlot = int8_t(owner);
    s.colour = kSparkColour;
    s.ticksParam = kSparkEndless;
    s.unusedParam = 0;
}

// +8: bound, waiting for its cue, acting (variant 2: throwing its sparks)
enum class WatcherState : uint8_t { Bind = 0, Waits = 1, Acts = 2 };

void watcherHolds(AnimatedActor& a, CastFields& c, uint8_t first, int32_t releaseTick) { // exe 0x480000 / 0x480090
    if (WatcherState(a.state) == WatcherState::Bind) {
        if (!castBind(a, stateAddress(TableId::St03AttractCastWatcherBox), first, false)) return;
        c.animKey = 0;
        holdPose(c);
    } else if (WatcherState(a.state) == WatcherState::Waits && stepSub() == 0xe && stepTick() == releaseTick) {
        releasePose(c);
    }
}

// exe 0x47fa80: the variant, then the tail
void st03Watcher(const PlayerEnv& env, AnimatedActor& a) {
    CastFields& c = classState<CastFields>(a);
    castSavePose(a);
    switch (variantOf(watcherVariants, a)) {
    case RoutineId::St03CastWatcherStill: // exe 0x47fbb0: its spawn's animation
        if (WatcherState(a.state) != WatcherState::Bind) break;
        if (!castBind(a, stateAddress(TableId::St03AttractCastWatcherBox), 0, false)) break;
        a.anim = WatcherFields(a).firstAnim;
        c.animKey = 0;
        break;
    case RoutineId::St03CastWatcherComes: { // exe 0x47fc20
        const WatcherState st = WatcherState(a.state);
        if (st == WatcherState::Bind) {
            if (!castBind(a, stateAddress(TableId::St03AttractCastWatcherBox), 1, false)) break;
            c.animKey = 0;
            a.velocity.z = 0;
        }
        if ((st == WatcherState::Bind || st == WatcherState::Waits) && stepSub() == 6) { // state 0 goes on into state 1's test
            switch (stepTick()) {
            case 1:
                anim(a, c, 1);
                holdPose(c);
                a.pos.x.whole = -0x90;
                a.pos.y.whole = -0x100;
                a.pos.z.whole = 0x71c;
                a.rotation.y = 0x1a8;
                break;
            case 0x2b: releasePose(c); break;
            case 0x41: a.velocity.z = -0x4e; break;
            case 0x73: anim(a, c, 2); break;
            default: break;
            }
        }
        moveForward(a);
        break;
    }
    case RoutineId::St03CastWatcherSparks: // exe 0x47fda0
        switch (WatcherState(a.state)) {
        case WatcherState::Bind:
            if (!castBind(a, stateAddress(TableId::St03AttractCastWatcherBox), 3, false)) break;
            c.animKey = 0;
            holdPose(c);
            a.velocity.z = 0;
            a.velocity.y = 0;
            a.velocity.x = 0;
            break;
        case WatcherState::Waits:
            if (stepTick() == 0x29) {
                a.state = uint8_t(WatcherState::Acts);
                releasePose(c);
            }
            break;
        case WatcherState::Acts: // every tick on its animation's frame 0x26 (re-entering state 2)
            if (uint8_t(a.animKey.frame) != kSparkFrame) break;
            a.enterState(uint8_t(WatcherState::Acts));
            spawnWatcherSpark(a, -0x7e0, 0);
            break;
        default: break;
        }
        break;
    case RoutineId::St03CastWatcherCarries: // exe 0x47fed0
        switch (WatcherState(a.state)) {
        case WatcherState::Bind:
            if (!castBind(a, stateAddress(TableId::St03AttractCastWatcherBox), 4, false)) break;
            c.animKey = 0;
            a.velocity.x = 0;
            a.velocity.y = 0;
            a.velocity.z = -0x113;
            spawnWatcherSpark(a, -0x850, a.slot);
            moveForward(a);
            break;
        case WatcherState::Waits:
            if (stepSub() == 8 && stepTick() == 0x51) {
                anim(a, c, 7);
                a.velocity.z = 0;
                a.enterState(uint8_t(WatcherState::Acts));
            }
            moveForward(a);
            break;
        default: moveForward(a); break;
        }
        break;
    case RoutineId::St03CastWatcherHoldsA: watcherHolds(a, c, 5, 0x14); break;
    case RoutineId::St03CastWatcherHoldsB: watcherHolds(a, c, 6, 0x32); break;
    default: notPortedAt(kStoryPort, "0x47fa80 variant", a.variant);
    }
    castTail(env, a, [] {});
}

// ---- the craft's body (exe 0x499650, type 0x2f, variants 0x892fb8: 0x499760 / 0x4999b0 / 0x49a110 / 0x49a2a0)
// --------
// face tracks 0x892ffc / 0x89305c; two parts swapped (7 / 17, then 4 / 18). Variants 2 / 3 ride the first bone block
// of the actor of their spawn byte 8's slot (the craft that spawned them), freed with it; variant 1 lays fuses (the
// opening's effect class, its variant Fuse) and hops by a height table
struct CraftBodyFields {
    explicit CraftBodyFields(const AnimatedActor& actor) : CraftBodyFields(const_cast<AnimatedActor&>(actor)) {}
    explicit CraftBodyFields(AnimatedActor& actor) : parentSlot(actor.params[0]) {}
    FieldAlias<int8_t, uint8_t> parentSlot;
};
const DataTable<RoutineTable<4>> craftBodyVariants{TableId::St03AttractCastCraftBodyVariants};
constexpr size_t kBodyPart = 7, kBodyOtherPart = 17, kBodyLatePart = 4, kBodyLateOtherPart = 18;
// the hops' heights by the step tick (exe table 0x893034, s16; ticks 0x1c .. 0x36)
const DataTable<ScalarTable<uint16_t, 0x37>> hopHeights{TableId::St03AttractCastHopHeights};
constexpr int32_t kFirstHopTick = 0x1c, kLastHopTick = 0x36;
constexpr uint16_t kFuseFlashParam = 0x70;

// a fuse (the opening's effect class, variant Fuse) from (fromX, fromZ) to (toX, toZ) in `steps`, freed after `life`
void spawnFuse(int16_t fromX, int16_t fromZ, int16_t toX, int16_t toZ, int8_t steps, int8_t life) { // exe 0x49a640
    const uint32_t at = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (at == 0) return;
    EffectActor& e = effectActorAt(at);
    FuseFields& f = classState<FuseFields>(e);
    f.fromX = fromX;
    f.fromZ = fromZ;
    f.toX = toX;
    f.toZ = toZ;
    e.flags = kActorLiveDrawn;
    e.type = kOpeningEffectType;
    e.variant = uint8_t(OpeningEffect::Fuse);
    f.steps = steps;
    f.life = life;
    f.flashParam = kFuseFlashParam;
}

// the riders' tail (exe 0x49a110 / 0x49a2a0): the parent gone, freed; else its first bone block's matrix slot as the
// root (the columns scaled by 0x1000 << (3 - the stage shift), exe 0x40c0e0) and the block's first point (>> the
// shift) as the position
void rideParent(AnimatedActor& a) {
    ActorHeader& head = actorHeaderAt(actorBySlot(uint32_t(int32_t(CraftBodyFields(a).parentSlot)))); // exe 0x5e0fa0
    if (!(head.flags & kActorLive)) {
        freeActor(a); // exe 0x5e1490
        return;
    }
    BoneBlock* block = boneBlockOf(asAnimated(head), 0); // exe 0x5e1780
    if (!block) notPorted(kStoryPort, "exe 0x5e1780 returned null (the parent has no bone block)");
    a.copyRootWordsFrom(*block, 0);
    const uint32_t shift = stage().coordinateShift;
    const int32_t s = int32_t(0x1000u << ((3u - shift) & 0x1f));
    const int32_t scale[3] = {s, s, s};
    a.rootMatrix = scaleColumns(a.rootMatrix, scale); // exe 0x40c0e0
    const ShortVec4& point = block->points[0];
    a.pos.x.whole = int16_t(int32_t(point.x) >> (shift & 0x1f));
    a.pos.y.whole = int16_t(int32_t(point.y) >> (shift & 0x1f));
    a.pos.z.whole = int16_t(int32_t(point.z) >> (shift & 0x1f));
}

// +8: bound, then its acts, each entered when its step comes
enum class CraftBodyState : uint8_t { Bind = 0, Act1 = 1, Act2 = 2, Act3 = 3, Act4 = 4 };

void bodyWalks(AnimatedActor& a, CastFields& c) { // exe 0x499760
    switch (CraftBodyState(a.state)) {
    case CraftBodyState::Bind:
        if (!castBind(a, stateAddress(TableId::St03AttractCastBodyBox), 0, false)) return;
        a.velocity.z = 0;
        c.faceTracks.init(stateAddress(TableId::St03AttractCastBodyEyes), stateAddress(TableId::St03AttractCastBodyMouth));
        eyes(c, 0);
        mouth(c, 0);
        break;
    case CraftBodyState::Act1:
        if (stepSub() == 1) {
            if (stepTick() == 6) anim(a, c, 1);
            else if (stepTick() == 0x5b) a.setDrawn(false);
        } else {
            a.enterState(uint8_t(CraftBodyState::Act2));
        }
        break;
    case CraftBodyState::Act2:
        if (stepSub() != 2) {
            a.enterState(uint8_t(CraftBodyState::Act3));
            break;
        }
        switch (stepTick()) {
        case 1:
            a.anim = 2;
            a.setDrawn(true);
            c.animKey = 0;
            a.pos.x.whole = 0;
            a.pos.z.whole = 0x2b0;
            a.velocity.z = -0x2d;
            swapParts(a, kBodyPart, kBodyOtherPart);
            break;
        case 0x21: anim(a, c, 3); eyes(c, 4); break;
        case 0x29: a.velocity.z = 0; break;
        case 0x3d: eyes(c, 0); mouth(c, 2); break;
        case 0xab: case 0x2ef: mouth(c, 0); break; // (exe 0x499845: the 0x281 / 0x28 / 0x46 chain)
        case 0x281: anim(a, c, 4); break;
        case 0x2a9: anim(a, c, 5); mouth(c, 2); break;
        default: break;
        }
        break;
    default: break;
    }
    moveForward(a);
}

void bodyHops(AnimatedActor& a, CastFields& c) { // exe 0x4999b0
    switch (CraftBodyState(a.state)) {
    case CraftBodyState::Bind:
        if (!castBind(a, stateAddress(TableId::St03AttractCastBodyBox), 6, false)) return;
        c.animKey = 0;
        holdPose(c);
        a.velocity.x = 0;
        a.velocity.z = 0;
        swapParts(a, kBodyPart, kBodyOtherPart);
        c.faceTracks.init(stateAddress(TableId::St03AttractCastBodyEyes), stateAddress(TableId::St03AttractCastBodyMouth));
        eyes(c, 1);
        mouth(c, 1);
        break;
    case CraftBodyState::Act1:
        if (stepSub() != 5) break;
        switch (stepTick()) {
        case 1:
            anim(a, c, 6);
            releasePose(c);
            a.pos.z.whole = 0x35c;
            a.rotation.y = 0x3c0;
            break;
        case 0x1f: eyes(c, 4); break;
        case 0x24: a.velocity.z = 0x2d; break;
        case 0x2c: anim(a, c, 7); break;
        case 0x3d:
            anim(a, c, 8);
            a.pos.x.whole = 0x200;
            a.pos.z.whole = 0x320;
            a.rotation.y = kQuarterTurn;
            eyes(c, 0);
            mouth(c, 2);
            a.enterState(uint8_t(CraftBodyState::Act2));
            break;
        default: break;
        }
        break;
    case CraftBodyState::Act2: {
        const uint8_t sub = stepSub();
        if (sub == 6 && stepTick() == 1) {
            a.velocity.z = 0;
        } else if (sub == 7) {
            switch (stepTick()) {
            case 1:
                a.anim = 8;
                a.prevAnim = kNoAnimShown;
                c.animKey = 0;
                a.pos.x.whole = 0xd6a;
                a.pos.z.whole = 0x320;
                a.rotation.y = 0x140;
                a.velocity.z = 0x2d;
                eyes(c, 0);
                mouth(c, 3);
                break;
            case 0x5b: case 0xbf: case 0x1c3: eyes(c, 4); mouth(c, 3); break;
            case 0x60: anim(a, c, 9); break;
            case 0x75: a.velocity.z = 0; break;
            case 0x3d: case 0x97: case 0x14e: case 0x17d: case 0x1be: case 0x1d2: case 0x231: case 0x277:
                mouth(c, 1);
                break;
            case 0x173: case 0x191: case 0x1f5: mouth(c, 3); break;
            case 0x268: anim(a, c, 0xa); eyes(c, 5); mouth(c, 3); break;
            default: break;
            }
        } else if (sub == 8) {
            a.enterState(uint8_t(CraftBodyState::Act3));
        }
        break;
    }
    case CraftBodyState::Act3: {
        if (stepSub() != 9) break;
        switch (stepTick()) {
        case 1:
            anim(a, c, 0xb);
            a.pos.x.whole = 0xde0;
            a.pos.y.whole = 0;
            a.pos.z.whole = 0x658;
            a.rotation.y = 0x100;
            a.velocity.x = 0;
            a.velocity.z = 0;
            break;
        case 7: a.velocity.x = 0xcc; a.velocity.z = 0x77; break;
        case 8: spawnFuse(0xde0, 0x698, 0xe55, 0x698, 10, 0x14); break;
        case 0xf: case 0x19: case 0x23: case 0x2d: case 0x3c: a.velocity.x = 0; a.velocity.z = 0; break;
        case 0x11: case 0x25: a.velocity.x = -0x161; a.velocity.z = 0x77; break;
        case 0x12: spawnFuse(0xe55, 0x698, 0xdc9, 0x703, 10, 0x14); break;
        case 0x1b: case 0x2f: a.velocity.x = 0x161; a.velocity.z = 0x77; break;
        case 0x1c: spawnFuse(0xdc9, 0x703, 0xe83, 0x6e6, 10, 10); break;
        case 0x26: spawnFuse(0xe83, 0x6e6, 0xdf6, 0x758, 10, 10); break;
        case 0x37: a.velocity.x = 0x58; break;
        default: break;
        }
        // exe 0x499cab: the hop's height by the tick (signed compares), else 0
        const int32_t k = stepTick();
        a.pos.y.whole = (k >= kFirstHopTick && k <= kLastHopTick) ? int16_t(hopHeights[size_t(k)]) : int16_t(0);
        break;
    }
    default: break;
    }
    if (a.velocity.z != 0) integrateVelocity(a, a.velocity.x, 0, a.velocity.z); // exe 0x5e3690
}

void bodyRides(AnimatedActor& a, CastFields& c) { // exe 0x49a110
    if (CraftBodyState(a.state) == CraftBodyState::Bind) {
        if (!castBind(a, stateAddress(TableId::St03AttractCastBodyBox), 0xf, false)) return;
        c.animKey = 0;
        swapParts(a, kBodyPart, kBodyOtherPart);
        c.faceTracks.init(stateAddress(TableId::St03AttractCastBodyEyes), stateAddress(TableId::St03AttractCastBodyMouth));
        eyes(c, 0);
        mouth(c, 1);
    }
    rideParent(a);
}

void bodyRidesLater(AnimatedActor& a, CastFields& c) { // exe 0x49a2a0
    switch (CraftBodyState(a.state)) {
    case CraftBodyState::Bind:
        if (!castBind(a, stateAddress(TableId::St03AttractCastBodyBox), 0xc, false)) return;
        c.animKey = 0;
        holdPose(c);
        swapParts(a, kBodyPart, kBodyOtherPart);
        c.faceTracks.init(stateAddress(TableId::St03AttractCastBodyEyes), stateAddress(TableId::St03AttractCastBodyMouth));
        eyes(c, 2);
        mouth(c, 1);
        break;
    case CraftBodyState::Act1:
        if (stepSub() == 0xd) a.enterState(uint8_t(CraftBodyState::Act2)); // (step 0xc tested first: nothing)
        break;
    case CraftBodyState::Act2:
        if (stepSub() == 0xd) {
            switch (stepTick()) {
            case 1:
                a.anim = 0xc;
                a.prevAnim = kNoAnimShown;
                c.animKey = 0;
                releasePose(c);
                break;
            case 0x1f: mouth(c, 3); break;
            case 0x5f: mouth(c, 1); break;
            default: break;
            }
        } else if (stepSub() == 0xe) {
            a.enterState(uint8_t(CraftBodyState::Act3));
        }
        break;
    case CraftBodyState::Act3:
        if (stepSub() == 0xe) {
            if (stepTick() == 1) a.setDrawn(false);
        } else if (stepSub() == 0xf) {
            a.enterState(uint8_t(CraftBodyState::Act4));
        }
        break;
    case CraftBodyState::Act4:
        if (stepSub() != 0xf) break;
        switch (stepTick()) {
        case 1:
            a.setDrawn(true);
            anim(a, c, 0xd);
            eyes(c, 3);
            mouth(c, 1);
            break;
        case 0xb: case 0x3d: mouth(c, 3); break;
        case 0x24: case 0x5b: mouth(c, 1); break;
        case 0x65: anim(a, c, 0xe); break;
        case 0xa0: swapParts(a, kBodyLatePart, kBodyLateOtherPart); break;
        default: break;
        }
        break;
    default: break;
    }
    rideParent(a);
}

// exe 0x499650: the variant, then the faces' tail
void st03CraftBody(const PlayerEnv& env, AnimatedActor& a) {
    CastFields& c = classState<CastFields>(a);
    castSavePose(a);
    switch (variantOf(craftBodyVariants, a)) {
    case RoutineId::St03CastBodyWalks: bodyWalks(a, c); break;
    case RoutineId::St03CastBodyHops: bodyHops(a, c); break;
    case RoutineId::St03CastBodyRides: bodyRides(a, c); break;
    case RoutineId::St03CastBodyRidesLater: bodyRidesLater(a, c); break;
    default: notPortedAt(kStoryPort, "0x499650 variant", a.variant);
    }
    faceTail(env, a);
}

// ---- the lights (exe 0x4da1e0, type 0x4e, no variant table)
// ----------------------------------------------------------
// state 1 waits for the step tick 0x78 (any step); state 2 runs a light along bones 1 .. 9 (sub-state n: bone n
// bright, bone n + 1 half bright, the others neutral; sound 0xe3 at the start), then state 3. Animation 0 on a new
// animation (exe 0x5e1c30(actor, 0, 0)), else stepped.
constexpr int32_t kLightsTick = 0x78;
constexpr uint16_t kLightsSound = 0xe3;
constexpr uint8_t kLightsFirstStep = 0, kLightsLastStep = 10; // its sub-state: the bone the light is on
constexpr uint32_t kFirstLitBone = 1, kLastLitBone = 9;
constexpr uint16_t kBoneBright = 0x4a52, kBoneHalfBright = 0x5ad6;
constexpr int16_t kLightsTilt = 0x50;

// +8: bound, waiting for tick 0x78, the light running, done
enum class LightsState : uint8_t { Bind = 0, Waits = 1, Runs = 2, Done = 3 };

// exe 0x4da1e0
void st03Lights(AnimatedActor& a) {
    castSavePose(a);
    switch (LightsState(a.state)) {
    case LightsState::Bind:
        if (!castBind(a, stateAddress(TableId::St03AttractCastLightsBox), 0, false)) return;
        a.rotation.x = 0;
        a.rotation.z = kLightsTilt;
        break;
    case LightsState::Waits:
        if (stepTick() == kLightsTick) a.enterState(uint8_t(LightsState::Runs));
        break;
    case LightsState::Runs: {
        const uint8_t n = a.subState;
        if (n > kLightsLastStep) break;
        if (n == kLightsFirstStep) soundPlayAtPos(kLightsSound, a.pos); // exe 0x5d5380
        a.tint = 0;
        for (uint32_t bone = kFirstLitBone; bone <= kLastLitBone; ++bone) {
            BoneBlock* block = boneBlockOf(a, int(bone >> 2)); // exe 0x5e1810
            if (!block) notPorted(kStoryPort, "exe 0x5e1810 returned null");
            uint16_t colour = kTintNeutral;
            if (n != kLightsLastStep) {
                if (bone == n) colour = kBoneBright;
                else if (bone == uint32_t(n) + 1) colour = kBoneHalfBright;
            }
            block->colour(bone & 3) = colour;
        }
        if (n == kLightsLastStep) a.enterState(uint8_t(LightsState::Done));
        else a.subState = uint8_t(n + 1);
        break;
    }
    default: break;
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim != a.prevAnim) animSetPose(a, 0, 0); // exe 0x5e1c30(actor, 0, 0)
    else animStepPose(a);                           // exe 0x5e1e80
}

// ---- the blinker and the stander (exe 0x484c30 type 0x23 variants 0x8920c4: 0x484d50; exe 0x4890f0 type 0x25
// variants 0x8923a4: 0x489210): animation 1 at their tick, moved aside at tick 0x15 (no step check)
const DataTable<RoutineTable<1>> blinkerVariants{TableId::St03AttractCastBlinkerVariants};
const DataTable<RoutineTable<1>> standerVariants{TableId::St03AttractCastStanderVariants}; // (0x8923a8.. is another handler's state table)
constexpr int32_t kMovedTick = 0x15;

// +8: bound, waiting for its tick, its animation started, moved aside
enum class StartMoveState : uint8_t { Bind = 0, Waits = 1, Started = 2, Moved = 3 };

// exe 0x484d50 / 0x489210: state 0 bound; animation 1 at `startTick`; moved to x `movedX` at tick 0x15
void startsThenMoves(AnimatedActor& a, CastFields& c, uint32_t box, int32_t startTick, int16_t movedX) {
    switch (StartMoveState(a.state)) {
    case StartMoveState::Bind:
        if (!castBind(a, box, 0, false)) return;
        c.animKey = 0;
        return;
    case StartMoveState::Waits:
        if (stepTick() == startTick) {
            a.anim = 1;
            a.prevAnim = kNoAnimShown;
            a.enterState(uint8_t(StartMoveState::Started));
        }
        return;
    case StartMoveState::Started:
        if (stepTick() == kMovedTick) {
            a.pos.x.whole = movedX;
            a.enterState(uint8_t(StartMoveState::Moved));
        }
        return;
    default: return;
    }
}

// exe 0x484c30: the variant, then the tail
void st03Blinker(const PlayerEnv& env, AnimatedActor& a) {
    CastFields& c = classState<CastFields>(a);
    castSavePose(a);
    if (variantOf(blinkerVariants, a) == RoutineId::St03CastBlinkerMoves) startsThenMoves(a, c, stateAddress(TableId::St03AttractCastBlinkerBox), 2, 0x88);
    else notPortedAt(kStoryPort, "0x484c30 variant", a.variant);
    castTail(env, a, [] {});
}

// exe 0x4890f0: the variant, then the tail
void st03Stander(const PlayerEnv& env, AnimatedActor& a) {
    CastFields& c = classState<CastFields>(a);
    castSavePose(a);
    if (variantOf(standerVariants, a) == RoutineId::St03CastStanderMoves) startsThenMoves(a, c, stateAddress(TableId::St03AttractCastStanderBox), 5, -0xd0);
    else notPortedAt(kStoryPort, "0x4890f0 variant", a.variant);
    castTail(env, a, [] {});
}

// ---- the part blinker (exe 0x499310, type 0x2e, variants 0x892f94: 0x499430)
// -----------------------------------------
// its step counter is the blink request (Start the sequence, Run it, Apply a value once); the sequence is the byte list
// 0x892f98 (0xff ends: back to its first), each value showing part 1 (1) or part 11 (else), part 12 hidden
enum class BlinkRequest : uint8_t { None = 0, Start = 1, Run = 2, Apply = 3 };

const DataTable<ScalarTable<uint8_t, 0x100>> blinkSequence{TableId::St03AttractCastBlinkSequence};
constexpr uint8_t kBlinkSequenceEnd = 0xff;
const DataTable<RoutineTable<1>> partBlinkerVariants{TableId::St03AttractCastPartBlinkerVariants};
constexpr size_t kBlinkFirst = 1, kBlinkSecond = 11, kBlinkHidden = 12, kBlinkOff = 5, kBlinkOn = 13;
constexpr uint8_t kBlinkShowsFirst = 1;

void blinkParts(AnimatedActor& a, uint32_t value) { // exe 0x4995f0
    if (value == kBlinkShowsFirst) swapParts(a, kBlinkFirst, kBlinkSecond);
    else {
        a.parts[kBlinkFirst].flags |= kPartHidden;
        a.parts[kBlinkSecond].flags &= uint8_t(~kPartHidden);
    }
    a.parts[kBlinkHidden].flags |= kPartHidden;
}

// +8: bound, blinking on step 4's cues
enum class PartBlinkerState : uint8_t { Bind = 0, Blinks = 1 };

void partBlinks(AnimatedActor& a, PartBlinkerFields& f) { // exe 0x499430
    CastFields& c = f;
    if (PartBlinkerState(a.state) == PartBlinkerState::Bind) {
        if (!castBind(a, stateAddress(TableId::St03AttractCastPartBlinkerBox), 1, false)) return;
        c.animKey = 0;
        a.stepCounter = uint8_t(BlinkRequest::Apply);
        f.applyValue = 0xc;
    } else if (PartBlinkerState(a.state) == PartBlinkerState::Blinks && stepSub() == 4) {
        switch (stepTick()) { // exe jump table 0x49959c
        case 0x29: a.stepCounter = uint8_t(BlinkRequest::Start); break;
        case 0x33: anim(a, c, 2); break;
        case 0x41:
            a.parts[kBlinkOff].flags |= kPartHidden;
            a.parts[kBlinkOn].flags &= uint8_t(~kPartHidden);
            break;
        case 0x65:
            a.stepCounter = uint8_t(BlinkRequest::Apply);
            f.applyValue = 1;
            break;
        default: break;
        }
    }
    switch (BlinkRequest(a.stepCounter)) {
    case BlinkRequest::Start:
        a.stepCounter = uint8_t(BlinkRequest::Run);
        f.index = 0;
        f.previousIndex = 0;
        blinkParts(a, blinkSequence[0]);
        return;
    case BlinkRequest::Run: {
        f.previousIndex = f.index;
        ++f.index;
        if (blinkSequence[f.index] == kBlinkSequenceEnd) f.index = 0;
        const uint8_t now = blinkSequence[f.index];
        if (now != blinkSequence[f.previousIndex]) blinkParts(a, now);
        return;
    }
    case BlinkRequest::Apply:
        blinkParts(a, f.applyValue);
        a.stepCounter = uint8_t(BlinkRequest::None);
        return;
    default: return;
    }
}

// exe 0x499310: the variant, then the tail
void st03PartBlinker(const PlayerEnv& env, AnimatedActor& a) {
    PartBlinkerFields& f = classState<PartBlinkerFields>(a);
    castSavePose(a);
    if (variantOf(partBlinkerVariants, a) == RoutineId::St03CastPartBlinks) partBlinks(a, f);
    else notPortedAt(kStoryPort, "0x499310 variant", a.variant);
    castTail(env, a, [] {});
}

// ---- the faces of step 0xb (exe 0x480a70 type 0x1e / 0x482210 type 0x21 / 0x48b390 type 0x28; one variant each,
// the tables' next dword 0) and the screen (exe 0x4b5df0 type 0x3c): their face tracks keyed on step 0xb's ticks; the
// held tail (no ground, no shadow)
struct FaceClass {
    const DataTable<RoutineTable<1>>& variants;
    RoutineId variant;
    TableId box, eyes, mouth; // the box, the face tracks' tables
    const char* name;
};
const DataTable<RoutineTable<1>> talkerVariants{TableId::St03AttractCastTalkerVariants};
const DataTable<RoutineTable<1>> faceVariants{TableId::St03AttractCastFaceVariants};
const DataTable<RoutineTable<1>> speakerVariants{TableId::St03AttractCastSpeakerVariants};
const FaceClass kTalker{talkerVariants, RoutineId::St03CastTalkerLines, TableId::St03AttractCastTalkerBox, TableId::St03AttractCastTalkerEyes, TableId::St03AttractCastTalkerMouth, "0x480a70 variant"};
const FaceClass kFace{faceVariants, RoutineId::St03CastFaceLines, TableId::St03AttractCastFaceBox, TableId::St03AttractCastFaceEyes, TableId::St03AttractCastFaceMouth, "0x482210 variant"};
const FaceClass kSpeaker{speakerVariants, RoutineId::St03CastSpeakerLines, TableId::St03AttractCastSpeakerBox, TableId::St03AttractCastSpeakerEyes, TableId::St03AttractCastSpeakerMouth,
                         "0x48b390 variant"};
constexpr uint8_t kFacesStep = 0xb;

// +8: bound, its lines of step 0xb
enum class FaceState : uint8_t { Bind = 0, Lines = 1 };

// state 0: bound (animation 1), the tracks; state 1 at step 0xb: `lines`
template <class Lines> void faceOfStepB(AnimatedActor& a, const FaceClass& k, bool clearsMotion, Lines lines) {
    CastFields& c = classState<CastFields>(a);
    castSavePose(a);
    if (variantOf(k.variants, a) != k.variant) notPortedAt(kStoryPort, k.name, a.variant);
    switch (FaceState(a.state)) {
    case FaceState::Bind:
        if (!castBind(a, stateAddress(k.box), 1, false)) break; // exe 0x5e0ba0 / 0x5e1490, 0x40dc40
        c.animKey = 0;
        c.faceTracks.init(stateAddress(k.eyes), stateAddress(k.mouth)); // exe 0x5e33a0
        eyes(c, 0);
        mouth(c, 0);
        if (clearsMotion) { // (cleared, never used by this variant)
            a.velocity.z = 0;
            a.velocity.x = 0;
        }
        break;
    case FaceState::Lines:
        if (stepSub() == kFacesStep) lines(c);
        break;
    default: break;
    }
    heldTail(a, true);
}

void st03Talker(AnimatedActor& a) { // exe 0x480b20
    faceOfStepB(a, kTalker, false, [](CastFields& c) {
        switch (stepTick()) {
        case 0xb9: mouth(c, 1); return;
        case 0xc9: eyes(c, 1); return;
        case 0xdd: mouth(c, 0); return;
        default: return;
        }
    });
}

void st03Face(AnimatedActor& a) { // exe 0x4822c0
    faceOfStepB(a, kFace, true, [&a](CastFields& c) {
        switch (stepTick()) {
        case 0x5b: mouth(c, 1); return;
        case 0xb5: mouth(c, 0); return;
        case 0xbd:
            anim(a, c, 2);
            eyes(c, 1);
            mouth(c, 3);
            return;
        default: return;
        }
    });
}

void st03Speaker(AnimatedActor& a) { // exe 0x48b440
    faceOfStepB(a, kSpeaker, false, [](CastFields& c) {
        switch (stepTick()) {
        case 0x10: mouth(c, 1); return;
        case 0x51: mouth(c, 0); return;
        case 0x56: eyes(c, 1); return;
        default: return;
        }
    });
}

// the screen (exe 0x4b5df0, variants 0x89468c: 0x4b5e90; 0x894690 is the next class's table): its state 0 only, no
// texture tracks (the block is only cleared)
const DataTable<RoutineTable<1>> screenVariants{TableId::St03AttractCastScreenVariants};

// +8: bound, still
enum class ScreenState : uint8_t { Bind = 0, Still = 1 };

// exe 0x4b5df0
void st03Screen(AnimatedActor& a) {
    CastFields& c = classState<CastFields>(a);
    castSavePose(a);
    if (variantOf(screenVariants, a) != RoutineId::St03CastScreenStill) notPortedAt(kStoryPort, "0x4b5df0 variant", a.variant);
    if (ScreenState(a.state) == ScreenState::Bind && castBind(a, stateAddress(TableId::St03AttractCastScreenBox), 1, false)) {
        c.animKey = 0;
        a.velocity.z = 0;
        a.velocity.x = 0;
    }
    heldTail(a, false);
}

constexpr uint8_t kCraftHold = 0x01;
enum class CraftHum : uint8_t { Off = 0, Start = 1, On = 2 };
// the flying craft's step byte in its Act1: its pass sound still to play (then 1: played)
constexpr uint8_t kPassSoundDue = 0;
// +9 of the returning craft's Act5: the launch speeds set, then the climb (the roll ramped)
enum class ReturnSub : uint8_t { Launch = 0, Climb = 1 };

const DataTable<RoutineTable<3>> craftVariants{TableId::St03AttractCastCraftVariants};
constexpr uint8_t kCraftBodyType = 0x2f;
constexpr uint8_t kBodyRidingVariant = 2, kBodyRidingLaterVariant = 3;
constexpr uint8_t kRidingBodyFlags = 0xc3; // live, drawn, scenery, posed on its root
constexpr uint16_t kCraftEngineSound = 0xdf, kCraftPassSound = 0xe0, kCraftHumSound = 0xd9;

// the body riding the craft (exe 0x5e1160: pool 0x20) at the craft's position; the two slots exchanged
void spawnRidingBody(AnimatedActor& a, uint8_t variant) {
    const uint32_t at = allocActor20(); // exe 0x5e1160
    if (at == 0) return;
    AnimatedActor& p = animatedActorAt(at);
    p.flags |= kRidingBodyFlags;
    p.type = kCraftBodyType;
    p.subtype = 0;
    p.variant = variant;
    p.spawnByte = 0;
    p.setParamsWord(0);
    p.prevPos = a.pos;
    p.pos = a.pos;
    p.rotation.z = 0;
    p.rotation.y = 0;
    p.rotation.x = 0;
    p.prevRotation = p.rotation;
    p.runFlags |= kActorWarpKept;
    CraftBodyFields(p).parentSlot = int8_t(a.slot);
    classState<CraftFields>(a).bodySlot = p.slot;
}
// the flying variants' motion: the velocity += the acceleration (x, z, y), moved (exe 0x5e3690, always called)
void craftMove(AnimatedActor& a) {
    a.velocity.x = int16_t(a.velocity.x + a.acceleration.x);
    a.velocity.z = int16_t(a.velocity.z + a.acceleration.z);
    a.velocity.y = int16_t(a.velocity.y + a.acceleration.y);
    integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z);
}
// the roll ramped down to `limit` (by `ramp` up to `rampMax` a tick), else held at `limit`
void rollDown(AnimatedActor& a, CraftFields& f, uint8_t rampMax, uint8_t rampStep, int16_t limit, bool clamps) {
    if (f.rollRamp < rampMax) f.rollRamp = uint8_t(f.rollRamp + rampStep);
    if (a.rotation.z > limit) a.rotation.z = int16_t(uint16_t(a.rotation.z) - uint16_t(f.rollRamp));
    else if (clamps) a.rotation.z = limit;
}

// +8: bound, then its acts (variant 0 only Act1 / Act2: sinking, sunk)
enum class CraftState : uint8_t { Bind = 0, Act1 = 1, Act2 = 2, Act3 = 3, Act4 = 4, Act5 = 5 };

void craftSinks(AnimatedActor& a, CraftFields& f) { // exe 0x4baf70: sinks 0xcc a tick until y <= -0xa00
    switch (CraftState(a.state)) {
    case CraftState::Bind:
        if (!castBind(a, stateAddress(TableId::St03AttractCastCraftBox), 0, false)) return;
        f.startKey = 0;
        a.rotation.x = -0x400;
        return;
    case CraftState::Act1:
        if (a.pos.y.whole > -0xa00) a.pos.y.whole = int16_t(uint16_t(a.pos.y.whole) + 0xff34u);
        else a.enterState(uint8_t(CraftState::Act2));
        return;
    default: return;
    }
}

// +9 of the flying craft's Act1: the launch speeds set, rising until its climb slows, the pass of step 9
enum class FlySub : uint8_t { Launch = 0, Rise = 1, Pass = 2 };
void craftFlies(AnimatedActor& a, CraftFields& f) { // exe 0x4bb000 (states 0x4bb320: 0 .. 3)
    switch (CraftState(a.state)) {
    case CraftState::Bind:
        if (!castBind(a, stateAddress(TableId::St03AttractCastCraftBox), 0, false)) return;
        f.startKey = 0;
        a.runFlags |= kActorWarpKept;
        clearVelocity(a);
        f.rollRamp = 0;
        spawnRidingBody(a, kBodyRidingVariant);
        break;
    case CraftState::Act1: {
        const FlySub sub = FlySub(a.subState);
        if (sub == FlySub::Pass) {
            const uint8_t st = stepSub();
            if (st == 9) {
                if (a.step == kPassSoundDue) {
                    soundPlayAtPos(kCraftPassSound, a.pos); // exe 0x5d5380
                    ++a.step;
                }
                if (stepTick() < 0xb4) rollDown(a, f, 0x20, 4, -0x280, true);
                else clearVelocity(a);
            } else if (st == 0xa) {
                a.enterState(uint8_t(CraftState::Act2));
            }
            break;
        }
        if (sub == FlySub::Launch) { // exe 0x4bb1c7, then sub-state 1's test
            soundPlayAtPos(kCraftEngineSound, a.pos);
            const uint8_t n = uint8_t(a.subState + 1);
            a.rotation.z = 0x40;
            a.velocity.y = -0x400;
            a.velocity.x = 0;
            a.velocity.z = -0x80;
            a.acceleration.y = 0x20;
            a.acceleration.x = -8;
            a.acceleration.z = 8;
            a.subState = n;
        } else if (sub != FlySub::Rise) {
            break;
        }
        if (a.velocity.y >= -0x20) { // exe 0x4bb208
            a.acceleration.z = 0x30;
            a.acceleration.y = -0x30;
            a.acceleration.x = -0x30;
            a.step = kPassSoundDue;
            ++a.subState;
        }
        break;
    }
    case CraftState::Act2:
        if (stepSub() == 0xa && stepTick() == 1) {
            a.enterState(uint8_t(CraftState::Act3));
            a.pos.x.whole = 0x200;
            a.pos.y.whole = 0;
            a.pos.z.whole = 0x400;
            a.rotation.x = 0;
            a.rotation.y = 0x540;
            a.rotation.z = 0x40;
            a.velocity.x = -0x40;
            a.velocity.y = -0x1c0;
            a.velocity.z = 0x80;
            a.acceleration.x = -0x20;
            a.acceleration.y = -0x20;
            a.acceleration.z = 0x20;
            f.rollRamp = 0;
            soundPlayAtPos(kCraftPassSound, a.pos);
        }
        break;
    case CraftState::Act3:
        if (a.pos.y.whole <= -0xa0) rollDown(a, f, 0x10, 1, -0x100, true);
        break;
    default: break;
    }
    craftMove(a);
}

void craftReturns(AnimatedActor& a, CraftFields& f) { // exe 0x4bb330 (states 0x4bb670: 0 .. 5)
    switch (CraftState(a.state)) {
    case CraftState::Bind:
        if (!castBind(a, stateAddress(TableId::St03AttractCastCraftBox), 1, false)) return; // the exe stores +0xa1 before +8 / +0xa0
        f.startKey = 0;
        a.runFlags |= kActorWarpKept;
        clearVelocity(a);
        f.rollRamp = 0;
        f.hum = uint8_t(CraftHum::Start);
        f.humHandle = 0;
        f.humParam = 0;
        spawnRidingBody(a, kBodyRidingLaterVariant);
        break;
    case CraftState::Act1:
        if (stepSub() != 0xc && stepSub() == 0xd) a.enterState(uint8_t(CraftState::Act2));
        break;
    case CraftState::Act2:
        if (stepSub() == 0xd) {
            if (stepTick() == 1) {
                a.anim = 3;
                f.startKey = 0;
            }
        } else if (stepSub() == 0xe) {
            a.enterState(uint8_t(CraftState::Act3));
        }
        break;
    case CraftState::Act3:
        if (stepSub() == 0xe) {
            if (stepTick() == 1) {
                f.hum = uint8_t(CraftHum::Off);
                a.setDrawn(false);
            }
        } else if (stepSub() == 0xf) {
            a.enterState(uint8_t(CraftState::Act4));
        }
        break;
    case CraftState::Act4:
        if (stepSub() != 0xf) break;
        switch (stepTick()) { // ticks 1 .. 0xa1 (the exe's dec / cmp 0xa0 / ja)
        case 1:
            f.hum = uint8_t(CraftHum::Start);
            a.anim = 3;
            a.setDrawn(true);
            a.prevAnim = kNoAnimShown;
            f.startKey = 0;
            break;
        case 0x65:
            a.anim = 2;
            f.startKey = 0;
            break;
        case 0x7c:
            a.velocity.y = 0x113;
            a.velocity.z = -0x486;
            break;
        case 0xa0:
            a.anim = 0;
            f.startKey = 0;
            a.pos.y.whole = -0x200;
            a.pos.z.whole = -0x200;
            a.velocity.y = 0;
            a.velocity.z = 0;
            f.rollRamp = 0;
            break;
        case 0xa1: a.enterState(uint8_t(CraftState::Act5)); break;
        default: break;
        }
        break;
    case CraftState::Act5:
        if (ReturnSub(a.subState) == ReturnSub::Launch) {
            a.velocity.y = -0x180;
            a.velocity.x = -0x80;
            a.velocity.z = 0x180;
            a.acceleration.y = -0x80;
            a.acceleration.x = -8;
            a.acceleration.z = 0x20;
            f.rollRamp = 0;
            ++a.subState;
        } else if (ReturnSub(a.subState) == ReturnSub::Climb) {
            if (a.pos.y.whole <= -0x500) { // exe: jg 0xfb00 skips
                if (f.rollRamp < 0x10) f.rollRamp = uint8_t(f.rollRamp + 2);
                if (a.rotation.z > -0x80) a.rotation.z = int16_t(uint16_t(a.rotation.z) - uint16_t(f.rollRamp));
                if (a.rotation.z <= -0x40) a.acceleration.x = 0;
            }
            if (a.velocity.y <= -0x400) { // exe: jg 0xfc00 skips
                a.acceleration.y = 0;
                a.acceleration.z = 0;
            }
        }
        break;
    default: break;
    }
    // the hum (no position, its parameter the dword +0x158)
    if (CraftHum(f.hum) == CraftHum::Start) {
        const uint32_t h = soundStartLoop(kCraftHumSound, 0, uint16_t(f.humParam)); // exe 0x5d5480
        f.humHandle = h;
        if (h != 0) f.hum = uint8_t(CraftHum::On);
    } else if (CraftHum(f.hum) == CraftHum::On) {
        soundUpdateLoop(f.humHandle, 0, uint16_t(f.humParam)); // exe 0x5d55c0
    }
    craftMove(a);
}

// exe 0x4baec0: the variant, then the animation (restarted at the held key while kCraftHold)
void st03Craft(AnimatedActor& a) {
    CraftFields& f = classState<CraftFields>(a);
    castSavePose(a);
    switch (variantOf(craftVariants, a)) {
    case RoutineId::St03CastCraftSinks: craftSinks(a, f); break;
    case RoutineId::St03CastCraftFlies: craftFlies(a, f); break;
    case RoutineId::St03CastCraftReturns: craftReturns(a, f); break;
    default: notPortedAt(kStoryPort, "0x4baec0 variant", a.variant);
    }
    if (!(a.flags & kActorLive)) return;
    const uint8_t n = a.anim;
    if (n == a.prevAnim && !(f.flags & kCraftHold)) animStepPose(a);  // exe 0x5e1e80
    else if (f.flags & kCraftHold) animSetPose(a, n, f.heldKey);      // exe 0x5e1c30 (the held key)
    else animSetPose(a, n, f.startKey);                               // exe 0x5e1c30
}

} // namespace

// the classes' updates (st03_attract_cast.hpp)
void st03PieceCarrierUpdate(const PlayerEnv& env, uint32_t actor) { st03PieceCarrier(env, animatedActorAt(actor)); }
void st03WalkerUpdate(const PlayerEnv& env, uint32_t actor) { st03Walker(env, animatedActorAt(actor)); }
void st03FlutterUpdate(const PlayerEnv&, uint32_t actor) { st03Flutter(animatedActorAt(actor)); }
void st03PlacedModelUpdate(const PlayerEnv&, uint32_t actor) { st03PlacedModel(staticActorAt(actor)); }
void st03StillModelUpdate(const PlayerEnv&, uint32_t actor) { st03StillModel(staticActorAt(actor)); }
void st03SceneryUpdate(const PlayerEnv&, uint32_t actor) { st03Scenery(staticActorAt(actor)); }
void st03RollUpdate(const PlayerEnv& env, uint32_t actor) { st03Roll(env, animatedActorAt(actor)); }
void st03BarrelUpdate(const PlayerEnv& env, uint32_t actor) { st03Barrel(env, animatedActorAt(actor)); }
void st03WatcherUpdate(const PlayerEnv& env, uint32_t actor) { st03Watcher(env, animatedActorAt(actor)); }
void st03CraftBodyUpdate(const PlayerEnv& env, uint32_t actor) { st03CraftBody(env, animatedActorAt(actor)); }
void st03LightsUpdate(const PlayerEnv&, uint32_t actor) { st03Lights(animatedActorAt(actor)); }
void st03BlinkerUpdate(const PlayerEnv& env, uint32_t actor) { st03Blinker(env, animatedActorAt(actor)); }
void st03StanderUpdate(const PlayerEnv& env, uint32_t actor) { st03Stander(env, animatedActorAt(actor)); }
void st03PartBlinkerUpdate(const PlayerEnv& env, uint32_t actor) { st03PartBlinker(env, animatedActorAt(actor)); }
void st03TalkerUpdate(const PlayerEnv&, uint32_t actor) { st03Talker(animatedActorAt(actor)); }
void st03FaceUpdate(const PlayerEnv&, uint32_t actor) { st03Face(animatedActorAt(actor)); }
void st03SpeakerUpdate(const PlayerEnv&, uint32_t actor) { st03Speaker(animatedActorAt(actor)); }
void st03ScreenUpdate(const PlayerEnv&, uint32_t actor) { st03Screen(animatedActorAt(actor)); }
void st03CraftUpdate(const PlayerEnv&, uint32_t actor) { st03Craft(animatedActorAt(actor)); }

} // namespace od::port
