// The opening's cutscene actors (ST39, script 5; see opening.hpp): the Flutter (its engine flame and the cloud
// streaks), the actors that play the animation the script writes, the crew (animation keys and placements on the
// step timeline) and the talking character (its face on the texture tracks, the acted-out explanation of the choice).
#include "content/intro/opening.hpp"
#include "content/intro/opening_cast_state.hpp"

#include "content/intro/opening_effects.hpp"
#include "content/intro/st39_script68.hpp"
#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/cutscene_actor.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/table_records.hpp"

namespace od::port {

using namespace opening_cast_state;

namespace {

enum class OpeningFlutterState : uint8_t {
    Init = 0,  // the model, its engine flame, the first streak; Fly (step sub 0) or Recede
    Fly = 1,   // flies in (OpeningFlutterSub), stops at z 0 at step sub 0 tick 0x7f
    Recede = 2, // drifts back 0x60 a tick
};
enum class OpeningFlutterSub : uint8_t {
    WaitStart = 0, // until step tick 0x1e: the forward speed
    Cruise = 1,    // until z <= 0x700: the braking
    Brake = 2,     // until the speed is back to the slow one
    Slow = 3,      // at step sub 0 tick 0x7f: placed at z 0, stopped
};
const DataTable<CollisionBoxRecord> openingFlutterBox{TableId::OpeningCastFlutterBox};
// the streaks' points (exe table 0x893960: s16 x / y / z, 8 bytes each)
const DataTable<RecordTable<ShortPoint, 8, 8>> streakPoints{TableId::OpeningCastStreakPoints};
constexpr int16_t kFirstStreakZFly = -0x200, kFirstStreakZRecede = 0x400;
constexpr int32_t kFlyStartTick = 0x1e, kStopTick = 0x7f;
constexpr int16_t kFlySpeed = -0x500, kSlowSpeed = -0x280, kBrake = 0x20, kBrakeZ = 0x700, kRecedeStep = 0x60;
constexpr uint32_t kStreakEvery = 0x1f, kStreakPointShift = 5, kStreakPointMask = 7;

// exe 0x49dab0: the Flutter's state, its motion, its animation and its streaks
void openingFlutter(AnimatedActor& a) {
    OpeningFlutterFields& f = classState<OpeningFlutterFields>(a);
    const ScriptState& s = scriptState();
    castSavePose(a);
    switch (OpeningFlutterState(a.state)) {
    case OpeningFlutterState::Init: {
        if (!bindActorModel(a, 0)) {
            freeActor(a);
            return;
        }
        a.collisionBox.pointAt(openingFlutterBox.stateAddress());
        a.clearClassBlock();
        a.anim = 0;
        a.prevAnim = kNoAnimShown;
        a.airborne = kAirborneFalling;
        spawnEngineFlame(a);
        int16_t z;
        if (s.stepSub == 0) {
            a.enterState(uint8_t(OpeningFlutterState::Fly));
            z = kFirstStreakZFly;
        } else {
            a.enterState(uint8_t(OpeningFlutterState::Recede));
            z = kFirstStreakZRecede;
        }
        const ShortPoint first = streakPoints[0];
        spawnStreak(first.x, first.y, z, 0);
        a.velocity.clearWords();
        a.acceleration.clearWords();
        break;
    }
    case OpeningFlutterState::Fly:
        switch (OpeningFlutterSub(a.subState)) {
        case OpeningFlutterSub::WaitStart:
            if (s.stepTick == kFlyStartTick) {
                a.velocity.z = kFlySpeed;
                a.acceleration.z = 0;
                ++a.subState;
            }
            break;
        case OpeningFlutterSub::Cruise:
            if (a.pos.z.whole <= kBrakeZ) {
                a.acceleration.z = kBrake;
                ++a.subState;
            }
            break;
        case OpeningFlutterSub::Brake:
            if (a.velocity.z >= kSlowSpeed) {
                a.acceleration.z = 0;
                a.velocity.z = kSlowSpeed;
                ++a.subState;
            }
            break;
        case OpeningFlutterSub::Slow:
            if (s.stepSub == 0 && s.stepTick == kStopTick) {
                a.pos.z.whole = 0;
                a.acceleration.z = 0;
                a.velocity.z = 0;
            }
            break;
        default: break;
        }
        a.velocity.z += uint16_t(a.acceleration.z);
        integrateVelocity(a, 0, 0, a.velocity.z);
        break;
    case OpeningFlutterState::Recede: a.pos.z.whole -= uint16_t(kRecedeStep); break;
    default: break;
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim != a.prevAnim) animSetPose(a, 0, 0);
    else animStepPose(a);
    const uint32_t v = f.streakClock;
    if ((v & kStreakEvery) == 0) {
        const ShortPoint p = streakPoints[(v >> kStreakPointShift) & kStreakPointMask];
        spawnStreak(p.x, p.y, p.z, ((v & 0xf0) | 0x800) >> 4);
    }
    f.streakClock = v + 1;
}

// ---- the actor of the script's animation (exe 0x4cd370, type 0x47)
// ---------------------------------------------------
const DataTable<CollisionBoxRecord> scriptAnimatedBox{TableId::OpeningCastScriptAnimatedBox};

// +8: bound, playing the animation the script writes
enum class ScriptAnimatedState : uint8_t { Bind = 0, Play = 1 };
// exe 0x4cd370: the animation the script wrote into its bytes
void scriptAnimated(AnimatedActor& a) {
    const ScriptAnimatedFields f(a);
    castSavePose(a);
    if (ScriptAnimatedState(a.state) == ScriptAnimatedState::Bind) {
        if (!bindActorModel(a, 0)) {
            freeActor(a);
            return;
        }
        a.collisionBox.pointAt(scriptAnimatedBox.stateAddress());
        a.clearClassBlock();
        const uint8_t anim = f.anim;
        a.enterState(uint8_t(ScriptAnimatedState::Play));
        a.anim = anim;
        a.prevAnim = kNoAnimShown;
        a.airborne = kAirborneFalling;
    }
    const uint8_t anim = f.anim;
    a.anim = anim;
    if (!(a.flags & kActorLive)) return;
    if (anim != a.prevAnim) animSetPose(a, anim, f.key);
    else animStepPose(a);
}

const DataTable<CollisionBoxRecord> crewBox{TableId::OpeningCastCrewBox};
// the variants (exe table 0x896480: a routine per variant; the named routines of engine/routines.csv)
const DataTable<RoutineTable<3>> crewVariants{TableId::OpeningCastCrewVariants};
// the variants' states follow the step subs (state n waits for sub n, then plays its keys)
enum class CrewState : uint8_t { Init = 0, Sub1 = 1, Sub2 = 2, Sub3 = 3, Sub4 = 4, Sub5 = 5 };
enum class WalkerState : uint8_t { Init = 0, Wait = 1, Turn = 2, Walk = 3 };
// the walker's animations: it stands (its first), then turns away
enum class WalkerAnim : uint8_t { Stand = 2, Turn = 0xc };
constexpr uint8_t kWalkerFrame = 8;     // the walk's frame the turn counts ticks on
constexpr int16_t kWalkerTurnTicks = 3; // ... before it walks
constexpr int16_t kWalkerTurnStep = 0x20, kWalkerSpeed = -0x7a;

// the crew's placements by step sub / tick
void placeWholes(Actor& a, int16_t x, int16_t y, int16_t z) {
    a.pos.x.whole = x;
    a.pos.y.whole = y;
    a.pos.z.whole = z;
}


void crewWatcher(AnimatedActor& a) { // exe 0x4e9bf0
    CastFields& c = classState<CastFields>(a);
    const ScriptState& s = scriptState();
    const uint8_t sub = s.stepSub;
    const int32_t tick = s.stepTick;
    switch (CrewState(a.state)) {
    case CrewState::Init:
        if (!castBind(a, crewBox.stateAddress(), 0, true)) return;
        c.animKey = 0;
        c.faceTracks.init(stateAddress(TableId::OpeningCastCrewTracks), 0);
        c.faceTracks.first.select(0, 0);
        break;
    case CrewState::Sub1:
        if (sub == 1) {
            if (tick == 0x29) requestAnim(a, c.animKey, 1);
            else if (tick == 0x47) {
                requestAnim(a, c.animKey, 2);
                placeWholes(a, 0, -0x70, -0xf4);
            }
        } else if (sub == 2) {
            a.enterState(uint8_t(CrewState::Sub2));
        }
        break;
    case CrewState::Sub2:
        if (sub == 2) {
            if (tick == 1) {
                requestAnim(a, c.animKey, 3);
                c.faceTracks.first.select(1, 0);
            } else if (tick == 0x15) {
                c.faceTracks.first.select(2, 0);
            } else if (tick == 0x33) {
                c.faceTracks.first.select(1, 0);
            }
        } else if (sub == 3) {
            a.enterState(uint8_t(CrewState::Sub3));
        }
        break;
    case CrewState::Sub3:
        if (sub == 3) {
            switch (tick) {
            case 0x173: requestAnim(a, c.animKey, 8); break;
            case 0x6f:
                requestAnim(a, c.animKey, 4);
                c.faceTracks.first.select(0, 0);
                break;
            case 0x75: requestAnim(a, c.animKey, 5); break;
            case 0xe3: requestAnim(a, c.animKey, 6); break;
            case 0xe9:
            case 0x2d2: requestAnim(a, c.animKey, 2); break;
            case 0x14b: requestAnim(a, c.animKey, 7); break;
            case 0x2cc: requestAnim(a, c.animKey, 6); break;
            case 0x21d: requestAnim(a, c.animKey, 4); break;
            case 0x223: requestAnim(a, c.animKey, 5); break;
            case 0x37b:
                requestAnim(a, c.animKey, 10);
                placeWholes(a, 0, 0, -0xf4);
                break;
            default: break;
            }
        } else if (sub == 4) {
            a.enterState(uint8_t(CrewState::Sub4));
        }
        break;
    case CrewState::Sub4:
        if (sub == 4) {
            if (tick == 1) {
                requestAnim(a, c.animKey, 2);
                placeWholes(a, -0x100, 0, -0x9c);
                a.rotation.y = 0xce8;
            }
        } else if (sub == 5) {
            a.enterState(uint8_t(CrewState::Sub5));
        }
        break;
    default: break;
    }
    castDrift(a);
}

void crewWalker(AnimatedActor& a) { // exe 0x4ea000
    CastFields& c = classState<CastFields>(a);
    CrewFields& w = classState<CrewFields>(a);
    const ScriptState& s = scriptState();
    switch (WalkerState(a.state)) {
    case WalkerState::Init:
        if (!castBind(a, crewBox.stateAddress(), uint8_t(WalkerAnim::Stand), true)) return;
        c.animKey = 0;
        c.faceTracks.init(stateAddress(TableId::OpeningCastCrewTracks), 0);
        c.faceTracks.first.select(0, 0);
        break;
    case WalkerState::Wait:
        if (s.stepSub == 6 && s.stepTick == 0x5b) {
            requestAnim(a, c.animKey, uint8_t(WalkerAnim::Turn));
            a.rotation.y -= uint16_t(kWalkerTurnStep);
            a.enterState(uint8_t(WalkerState::Turn));
            w.frameTicks = 0;
        }
        break;
    case WalkerState::Turn:
        if (uint8_t(a.animKey.frame) == kWalkerFrame) w.frameTicks += 1u;
        else a.rotation.y -= uint16_t(kWalkerTurnStep);
        if (w.frameTicks == kWalkerTurnTicks) {
            a.velocity.z = kWalkerSpeed;
            a.enterState(uint8_t(WalkerState::Walk));
        }
        break;
    default: break;
    }
    if (a.velocity.z != 0) integrateVelocity(a, 0, 0, a.velocity.z);
}

// the class bodies of the crew / the talker after their variant: the ground, the texture tracks, the animation (the
// shadow, kCastShadow: render output)
void crewTail(const PlayerEnv& env, AnimatedActor& a) { // exe 0x4e9ae0's tail
    CastFields& c = classState<CastFields>(a);
    if (!(a.flags & kActorLive)) return;
    if (c.castFlags & kCastOnGround) a.pos.y.whole = int16_t(actorGround(env, a));
    textureTracksTick(a, c.faceTracks);
    castAnimateFromKey(a, c);
}

// exe 0x4e9ae0: the crew's variant, then the class's tail
void openingCrew(const PlayerEnv& env, AnimatedActor& a) {
    classState<CrewFields>(a); // the class's state (the cast's block and its own bytes past it)
    castSavePose(a);
    switch (crewVariants->idAt(a.variant)) {
    case RoutineId::OpeningCastCrewWatcher: crewWatcher(a); break;
    case RoutineId::OpeningCastCrewWalker: crewWalker(a); break;
    case RoutineId::OpeningCastCrewScript68: script68Crew(a); break; // content/intro/st39_script68 (ST39 script 0x68)
    default: notPortedAt(kStoryPort, "ST39 type 0x63 variant", a.variant);
    }
    crewTail(env, a);
}

const DataTable<CollisionBoxRecord> talkerBox{TableId::OpeningCastTalkerBox};
const DataTable<RoutineTable<2>> talkerVariants{TableId::OpeningCastTalkerVariants};
enum class ArrivingState : uint8_t { Init = 0, Keys = 1 };
enum class TalkingState : uint8_t { Init = 0, Talk = 1, Choice = 2, AfterChoice = 3, Leave = 4 };
// the choice's sub-states (exe 0x4b0720): waiting for the player's pick, then acting out the explanation (+ 1)
enum class ExplainSub : uint8_t { WaitPick = 0, Explain0 = 1, Explain1 = 2, Explain2 = 3, Explain3 = 4, Done = 5 };
// the script's choice step's sub-phases the character follows (the window open, the voice clip playing)
constexpr uint8_t kChoiceAsking = 1, kChoiceActing = 5;
constexpr uint32_t kTicksCap = 0x7fffffff;
constexpr uint32_t kIdleBlinkMask = 0x7f;

// exe 0x4b0720 (variant 1, state 2, step sub 7): the explanation the player chose (flags 0x680..0x683) acted out on the
// texture tracks while the voice clip plays; its end clears the flag (the opening's choice comes back)
void talkerExplain(AnimatedActor& a) {
    CastFields& c = classState<CastFields>(a);
    TalkerFields& t = classState<TalkerFields>(a);
    const ScriptState& s = scriptState();
    KeyframeTrack& eyes = c.faceTracks.first;
    KeyframeTrack& mouth = c.faceTracks.second;
    auto count = [&]() {
        if (t.ticks < kTicksCap) t.ticks += 1u;
    };
    auto talk = [&](uint8_t k) {
        mouth.select(k, 0);
        count();
    };
    auto done = [&](uint32_t choice) {
        clearStoryFlag(kOpeningChoiceFlags + choice);
        a.subState = uint8_t(ExplainSub::WaitPick);
        t.ticks = 0;
    };
    const uint32_t n = t.ticks;
    switch (ExplainSub(a.subState)) {
    case ExplainSub::WaitPick: {
        if (s.stepSubPhase == kChoiceAsking && (uint8_t(t.ticks) & kIdleBlinkMask) == 0) eyes.select(0xb, 0);
        t.ticks += 1u;
        if (s.stepSubPhase != kChoiceActing) return;
        for (uint8_t k = 0; k < kOpeningChoices; ++k)
            if (storyFlag(kOpeningChoiceFlags + k)) {
                t.explanation = k;
                break;
            }
        a.clearStepBytes();
        a.subState = uint8_t(t.explanation + 1u);
        t.ticks = 0;
        return;
    }
    case ExplainSub::Explain0:
        switch (n) {
        case 0: case 0x6e: case 0xbe: case 0x104: case 0x15e: talk(1); return;
        case 100: case 0xa2: case 0xfa: case 0x138: case 0x152: case 0x19a: talk(2); return;
        case 0x14a:
            eyes.select(0xb, 0);
            mouth.select(1, 0);
            count();
            return;
        case 0xaa:
            eyes.select(0xb, 0);
            count();
            return;
        case 0x1c2: done(0); return;
        default: count(); return;
        }
    case ExplainSub::Explain1:
        switch (n) {
        case 0: case 0x50: case 0x78: case 0xb4: talk(1); return;
        case 0x3c: case 0x6e: case 0xa0: case 0xd2: case 0x10e: talk(2); return;
        case 0xe6:
            eyes.select(0xb, 0);
            mouth.select(1, 0);
            count();
            return;
        case 0x136: done(1); return;
        default: count(); return;
        }
    case ExplainSub::Explain2:
        switch (n) {
        case 0: case 0x46: case 0x96: case 0xb4: talk(1); return;
        case 0x37: case 0x6e: case 0xa2: case 0xd2: case 0xf0: talk(2); return;
        case 0x8c:
            eyes.select(0xb, 0);
            count();
            return;
        case 0xdc:
            eyes.select(0xb, 0);
            mouth.select(1, 0);
            count();
            return;
        case 0x10e: done(2); return;
        default: count(); return;
        }
    case ExplainSub::Explain3:
        switch (n) {
        case 0: mouth.select(1, 0); break;
        case 0xf: case 0x5a: mouth.select(2, 0); break;
        case 0x14:
            eyes.select(0xb, 0);
            mouth.select(1, 0);
            break;
        case 0x78:
            clearStoryFlag(kOpeningChoiceFlags + kOpeningLastChoice);
            a.subState = uint8_t(ExplainSub::Done);
            return;
        default: break;
        }
        count();
        return;
    default: return;
    }
}

void talkerArriving(AnimatedActor& a) { // exe 0x4af8c0
    CastFields& c = classState<CastFields>(a);
    KeyframeTrack& eyes = c.faceTracks.first;
    KeyframeTrack& mouth = c.faceTracks.second;
    if (ArrivingState(a.state) == ArrivingState::Init) {
        if (!castBind(a, talkerBox.stateAddress(), 1, true)) return;
        c.animKey = 0;
        c.faceTracks.init(stateAddress(TableId::OpeningCastTalkerEyes), stateAddress(TableId::OpeningCastTalkerMouth));
        eyes.select(0, 0);
        mouth.select(0, 0);
    } else if (ArrivingState(a.state) == ArrivingState::Keys) {
        auto place = [&](int16_t x, int16_t z) { placeWholes(a, x, 0, z); };
        switch (scriptState().stepTick) {
        case 0x191:
            requestAnim(a, c.animKey, 4);
            eyes.select(2, 0);
            mouth.select(2, 0);
            break;
        case 0x14:
            eyes.select(0, 0);
            mouth.select(0, 0);
            break;
        case 0x3a:
            requestAnim(a, c.animKey, 2);
            place(-0x48, -0xb8);
            integrateVelocity(a, 0, 0, 0x1600);
            a.velocity.z = -0x80;
            c.castFlags |= kCastShadow;
            eyes.select(1, 0);
            mouth.select(1, 0);
            break;
        case 0x66:
            requestAnim(a, c.animKey, 3);
            place(-0x48, -0xb8);
            a.velocity.z = 0;
            mouth.select(2, 0);
            break;
        case 0xe7:
        case 0x1eb:
        case 0x213: mouth.select(2, 0); break;
        case 0x79:
        case 0x1a5:
        case 0x1ff: mouth.select(1, 0); break;
        case 0x227:
            requestAnim(a, c.animKey, 5);
            place(0, -0xb8);
            eyes.select(0x10, 0);
            mouth.select(2, 0);
            break;
        default: break;
        }
    }
    castDrift(a);
}

void talkerTalking(AnimatedActor& a) { // exe 0x4afc70
    CastFields& c = classState<CastFields>(a);
    TalkerFields& t = classState<TalkerFields>(a);
    const ScriptState& s = scriptState();
    const uint8_t sub = s.stepSub;
    const int32_t tick = s.stepTick;
    auto d = [&](uint8_t k) { c.faceTracks.first.select(k, 0); };
    auto f = [&](uint8_t k) { c.faceTracks.second.select(k, 0); };
    auto anim = [&](uint8_t k) { requestAnim(a, c.animKey, k); };
    switch (TalkingState(a.state)) {
    case TalkingState::Init:
        if (!castBind(a, talkerBox.stateAddress(), 6, true)) return;
        c.animKey = 0;
        t.ticks = 0;
        t.explanation = 0;
        c.castFlags |= kCastShadow;
        c.faceTracks.init(stateAddress(TableId::OpeningCastTalkerEyes), stateAddress(TableId::OpeningCastTalkerMouth));
        d(1);
        f(2);
        break;
    case TalkingState::Talk:
        if (sub != 6) {
            if (sub == 7) a.enterState(uint8_t(TalkingState::Choice));
            break;
        }
        switch (tick) {
        // the eyes / mouth keys of step sub 6 (the opening's long talk)
        case 0xb: anim(7); d(4); f(3); break;
        case 0x5b: anim(8); d(1); f(4); break;
        case 0x14b: d(1); f(1); break;
        case 0x155: case 0x17d: case 0x1c3: case 0x1f5: f(2); break;
        case 0x15f: case 0x1d2: f(1); break;
        case 0x1a5: anim(9); d(5); f(1); break;
        case 0x21d: anim(10); d(6); f(5); break;
        case 0x263: case 0x295: case 0x2db: case 0x367: case 0x3df: case 0x41b: f(4); break;
        case 0x277: case 0x37b: case 0x411: f(5); break;
        case 0x2b3: d(6); f(5); break;
        case 0x335: anim(0xb); d(0xf); f(5); break;
        case 0x425: case 0x4f7: d(7); break;
        case 0x439: case 0x4a7: case 0x4c5: f(5); break;
        case 0x46b: case 0x4bb: case 0x4d9: f(4); break;
        case 0x515: f(5); break;
        case 0x533: case 0x55b: case 0x5b5: case 0x619: f(4); break;
        case 0x551: case 0x579: case 0x605: d(7); f(5); break;
        case 0x641: anim(0xc); d(8); f(6); break;
        case 0x669: case 0x691: case 0x6cd: f(2); break;
        case 0x673: f(6); break;
        case 0x6a5: d(8); f(1); break;
        case 0x6e1: f(1); break;
        case 0x745: case 0x80d: f(2); break;
        case 0x781: anim(0xd); d(9); f(7); break;
        case 0x795: case 0x7e5: f(8); break;
        case 0x7b3: f(7); break;
        case 0x803: anim(0xe); d(10); f(9); break;
        case 0x821: f(9); break;
        case 0x849: case 0x907: case 0x957: f(2); break;
        case 0x8a3: anim(0xf); d(2); f(2); break;
        case 0x8f3: anim(0x10); d(3); f(1); break;
        case 0x911: d(0); f(1); break;
        default: break;
        }
        break;
    case TalkingState::Choice:
        if (sub == 7) talkerExplain(a);
        else if (sub == 8) a.enterState(uint8_t(TalkingState::AfterChoice));
        break;
    case TalkingState::AfterChoice:
        if (sub == 8) {
            if (tick == 0x9c) {
                anim(0x11);
                d(0xc);
                f(8);
            } else if (tick == 0xe7) {
                anim(0x12);
                d(0xd);
                f(10);
            } else if (tick == 0x119) {
                anim(0x13);
                d(0xe);
                f(0xb);
            }
        } else if (sub == 9) {
            a.enterState(uint8_t(TalkingState::Leave));
        }
        break;
    case TalkingState::Leave:
        if (sub != 9) break;
        switch (tick) {
        case 1:
            d(0xe);
            f(0xd);
            break;
        case 0x1f:
            anim(0x14);
            f(0xc);
            break;
        case 0x7d: f(0xc); break;
        case 0x47:
        case 0xaf: f(0xd); break;
        default: break;
        }
        break;
    default: break;
    }
    castDrift(a);
}

// exe 0x4af7e0: the talker's variant, then the class's tail
void openingTalker(const PlayerEnv& env, AnimatedActor& a) {
    classState<TalkerFields>(a); // the class's state (the cast's block and its own bytes past it)
    castSavePose(a);
    switch (talkerVariants->idAt(a.variant)) {
    case RoutineId::OpeningCastTalkerArriving: talkerArriving(a); break;
    case RoutineId::OpeningCastTalkerTalking: talkerTalking(a); break;
    default: notPortedAt(kStoryPort, "ST39 type 0x39 variant", a.variant);
    }
    // the tail (exe 0x4af7e0's): the ground, the texture tracks, the animation (the shadow: render output)
    CastFields& c = classState<CastFields>(a);
    if (!(a.flags & kActorLive)) return;
    if (c.castFlags & kCastOnGround) a.pos.y.whole = int16_t(actorGround(env, a));
    textureTracksTick(a, c.faceTracks);
    castAnimate(a, c.animKey, c.castFlags, kCastHeld);
}

} // namespace

void openingFlutterUpdate(const PlayerEnv&, uint32_t actor) { openingFlutter(animatedActorAt(actor)); }
void scriptAnimatedUpdate(const PlayerEnv&, uint32_t actor) { scriptAnimated(animatedActorAt(actor)); }
void openingCrewUpdate(const PlayerEnv& env, uint32_t actor) { openingCrew(env, animatedActorAt(actor)); }
void openingTalkerUpdate(const PlayerEnv& env, uint32_t actor) { openingTalker(env, animatedActorAt(actor)); }

} // namespace od::port
