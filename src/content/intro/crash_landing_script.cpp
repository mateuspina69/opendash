// The crash landing's script (ST3A, script 6, exe handler 0x516520; see crash_landing.hpp) and ST3A's room case:
//   state 0 (exe 0x516540): the camera script and the steps, the replay pad, the player hidden, the 2D backdrop on, the
//           RNG saved and reseeded, the player script driven, its face tracks, voice clip 8 (state 1 once it plays)
//   state 1 (exe 0x5166b0): the step runner (as exe 0x53a5e0) and the backdrop's scroll while it is on
//   state 2 (exe 0x516720): the wrap up: ST3A02.DAT (unless a step loaded it), the request to ST08 room 0, the RNG back
// Steps (exe 0x89f440; the in-stage warps go to ST3A's rooms 0..3):
//   0:0 0x5168a0 the backdrop off, warp to room 1   0:3 / 0:11 0x5169b0 warp to room 0   0:7 0x516940 warp to room 1
//   1:0 0x516a20 the alarm, ST3A01.DAT, voice 10    1:1 0x516b20 the alarm off   1:2 0x516b50 the streaks, room 1
//   1:3 0x516cd0 the shake, room 0                  1:5 0x516d90 the Flutter's streaks, room 2
//   1:6 0x516f50 the escape, room 0                 1:7 0x5171c0 the streaks and the debris
//   1:8 0x517420 the burning statics, the fades, room 1    1:9 0x517790 room 0   1:10 0x517820 the actors swept
//   1:11 0x517840 ST3A02.DAT, voice 12, room 3       2:0 0x5179a0 the other bank, the player walks
// The 2D backdrop (exe 0x517cf0): its scroll by its speeds every tick, an 8 x 8 grid of 128-pixel sprites from its
// origin (drawn by content/intro/crash_landing_draw).
#include "content/intro/crash_landing.hpp"

#include "content/intro/opening_effects.hpp"
#include "engine/data_source.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/room_scripts.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_sweeps.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera.hpp"
#include "game/display.hpp"
#include "game/fade.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/message.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spawn.hpp"
#include "game/stage_files.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/table_records.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

namespace od::port {

namespace {

constexpr uint8_t kST3A = 0x3a, kST08 = 8;
constexpr uint8_t kRoomCaseDone = 1;

constexpr uint8_t kNeutralFace = 0x14, kClosedMouth = 0x17;
constexpr uint32_t kCrashRngSeed = 0x873ca9e6u;
constexpr uint32_t kVoiceFade = 8;
constexpr uint32_t kOpeningVoice = 8, kAlarmVoice = 10, kLandingVoice = 0xc;
constexpr uint16_t kAlarmOnSound = 0x2a2, kAlarmOffSound = 0x2a3;
constexpr uint16_t kFirstFile = 0x105, kSecondFile = 0x106; // ST3A01.DAT / ST3A02.DAT
constexpr uint32_t kVibrationStop = 0x1000000;
constexpr int32_t kVoiceDelay = 0x77;
// the sweeps of the steps (the effects / the projectiles and the shots)
constexpr uint32_t kSweepEffects = kSweepEffectsGroup0 | kSweepEffectsGroup1 | kSweepEffectsGroup2;
constexpr uint32_t kSweepMissiles = kSweepShots | kSweepProjectiles;
// the fades of the steps
constexpr uint8_t kToWhite = kFadeOutRequest | kFadeWhiteRequest | 1;
constexpr uint8_t kFromWhite = kFadeInRequest | kFadeWhiteRequest | 2;
constexpr uint8_t kSceneFadeOut = kFadeOutRequest | 2, kSceneFadeIn = kFadeInRequest | 1;

// the script block
ScriptState& block() { return scriptState(); }
// the player's face tracks: the eyes ...
KeyframeTrack& eyes() { return block().faceTracks.first; }
// ... and the mouth
KeyframeTrack& mouth() { return block().faceTracks.second; }
// the in-stage warp of the steps (ST3A's room switched in place)
void warpToRoom(uint8_t room, int16_t x = 0, int16_t z = 0) {
    requestTransition(kTransitionWarp, kST3A, room, x, 0, z, 0, kNoFade, kNoFade);
}
// the backdrop shown, its scroll and speeds cleared (the steps' order: y, x, speeds, shown)
void backdropReset(int8_t speedX, int8_t speedY) {
    auto&& b = crashBytes();
    b.backdropY = 0;
    b.backdropX = 0;
    b.backdropSpeedX = speedX;
    b.backdropSpeedY = speedY;
    b.backdropShown = 1;
}

// exe 0x517cf0: the backdrop's origin for its draw, then its scroll
void backdropScroll() {
    auto&& b = crashBytes();
    ScreenPointScratch& origin = screenPointScratch();
    origin.x = backdropOriginX(uint16_t(b.backdropX));
    origin.y = backdropOriginY(uint16_t(b.backdropY));
    b.backdropX = int16_t(b.backdropX + b.backdropSpeedX);
    b.backdropY = int16_t(b.backdropY + b.backdropSpeedY);
}

// A streak's record of the steps (exe data, 8 bytes: an s16 point and the streak's flags)
struct StreakRecord : DataRecord {
    ShortPoint point;
    uint8_t flags = 0;
    static StreakRecord load(const TableReader& r) {
        StreakRecord rec;
        rec.point = ShortPoint::load(r.sub(0, 6));
        rec.flags = r.u8(6);
        return rec;
    }
};
const DataTable<RecordTable<StreakRecord, 6, 8>> alarmStreaks{TableId::CrashLandingScriptAlarmStreaks};
const DataTable<RecordTable<StreakRecord, 4, 8>> flutterStreaks{TableId::CrashLandingScriptFlutterStreaks};
const DataTable<RecordTable<StreakRecord, 4, 8>> escapeStreaks{TableId::CrashLandingScriptEscapeStreaks};
const DataTable<RecordTable<StreakRecord, 8, 8>> debrisStreaks{TableId::CrashLandingScriptDebrisStreaks};
// a debris piece's record (exe data, 8 bytes: an s16 point)
const DataTable<RecordTable<ShortPoint, 8, 8>> debrisPoints{TableId::CrashLandingScriptDebrisPoints};
constexpr uint8_t kStreakSpeedAlarm = 0, kStreakSpeedDebris = 1, kStreakSpeedFlutter = 2, kStreakSpeedEscape = 3;

// exe 0x517e60 at the record's point
void streakAt(const StreakRecord& r, uint8_t speed) {
    const int16_t p[3] = {r.point.x, r.point.y, r.point.z};
    spawnFlagStreak(p, r.flags, speed);
}
// ... relative to an actor's position
void streakNear(const StreakRecord& r, const Actor& actor, uint8_t speed) {
    const int16_t p[3] = {int16_t(r.point.x + actor.pos.x.whole), int16_t(r.point.y + actor.pos.y.whole),
                          int16_t(r.point.z + actor.pos.z.whole)};
    spawnFlagStreak(p, r.flags, speed);
}

// exe 0x517ed0(point): a falling piece (pool 0x40 type 0x21 variant 1, content/intro/crash_landing) at the s16 point
constexpr uint8_t kFallingPieceType = 0x21, kFallingPieceVariant = 1;
// exe 0x517ed0: the falling piece at the s16 point
void debrisAt(const ShortPoint& point) {
    const uint32_t at = allocActor40(kActorStageClass); // exe 0x5e1280
    if (at == 0) return;
    ProjectileActor& a = projectileActorAt(at);
    a.flags = kActorLiveDrawn;
    a.type = kFallingPieceType;
    a.subtype = 0;
    a.variant = kFallingPieceVariant;
    a.setParamsWord(0);
    a.pos.x = int32_t(uint32_t(int32_t(point.x)) << 16);
    a.pos.y = int32_t(uint32_t(int32_t(point.y)) << 16);
    a.pos.z = int32_t(uint32_t(int32_t(point.z)) << 16);
    a.rotation.y = kHalfTurn;
    a.rotation.z = 0;
    a.rotation.x = 0;
}

// ---- the steps
// -------------------------------------------------------------------------------------------------------

// the backdrop step's phases: until its hiding tick, hidden until the warp
enum class BackdropPhase : uint8_t { Shown = 0, Hidden = 1 };

void backdropOffStep() { // exe 0x5168a0
    ScriptState& s = block();
    if (BackdropPhase(s.stepPhase) == BackdropPhase::Shown) {
        if (s.stepTick == 0x181) {
            crashBytes().backdropShown = 0;
            s.stepPhase = uint8_t(BackdropPhase::Hidden);
        }
    } else if (BackdropPhase(s.stepPhase) == BackdropPhase::Hidden && s.stepTick == 0x389) {
        warpToRoom(1);
        s.clearStepPhase();
        crashBytes().ticks = 0;
        scriptSequenceSkip(1);
    }
}

void warpAtStep(int32_t tick, uint8_t room) { // exe 0x5169b0 (0xfa, room 0) / 0x516940 (0x50, room 1)
    if (block().stepTick != tick) return;
    warpToRoom(room);
    scriptNextStep();
}

// the alarm step's phases: the alarm on, the first file asked, loaded, the voice asked, playing
enum class AlarmPhase : uint8_t { Start = 0, LoadFile = 1, WaitFile = 2, WaitStream = 3, WaitVoice = 4 };

void alarmStep() { // exe 0x516a20
    ScriptState& s = block();
    auto&& b = crashBytes();
    switch (AlarmPhase(s.stepPhase)) {
    case AlarmPhase::Start:
        soundPlayFlat(kAlarmOnSound);
        b.alarmOn = 1;
        voiceClipFadeOut(kVoiceFade);
        b.ticks = 0;
        ++s.stepPhase;
        s.flags |= kScriptUnskippable;
        b.ticks += 1u;
        return;
    case AlarmPhase::LoadFile:
        if (voiceClipIdle()) {
            datLoadRequest(kFirstFile);
            ++s.stepPhase;
        }
        break;
    case AlarmPhase::WaitFile:
        if (!(fileLoads().loading & kFileLoading)) {
            ++s.stepPhase;
            b.ticks += 1u;
            return;
        }
        break;
    case AlarmPhase::WaitStream:
        if (!(fileLoads().loading & kStreamBusy) && display().drawBuffer == 0 && b.ticks > kVoiceDelay) {
            voiceClipPlay(kAlarmVoice);
            ++s.stepPhase;
            b.ticks += 1u;
            return;
        }
        break;
    case AlarmPhase::WaitVoice:
        if (display().drawBuffer == 0 && voiceClipPlaying()) {
            scriptNextStepClearingPhases();
            return;
        }
        break;
    default: break;
    }
    b.ticks += 1u;
}

// the alarm-off step's phases: the alarm off, done
enum class AlarmOffPhase : uint8_t { Start = 0, Done = 1 };

void alarmOffStep() { // exe 0x516b20
    ScriptState& s = block();
    if (AlarmOffPhase(s.stepPhase) != AlarmOffPhase::Start) return;
    soundPlayFlat(kAlarmOffSound);
    crashBytes().alarmOn = 0;
    s.flags &= uint8_t(~kScriptUnskippable);
    ++s.stepPhase;
}

// the streaks step's phases: the backdrop reset, its hiding at tick 0x10, the streaks on their ticks
enum class StreaksPhase : uint8_t { Start = 0, Clear = 1, Streaks = 2 };
void streaksStep() { // exe 0x516b50
    ScriptState& s = block();
    const int32_t t = s.stepTick;
    if (t == 0) s.clearStepPhase();
    const StreaksPhase c = StreaksPhase(s.stepPhase);
    if (c == StreaksPhase::Start) {
        actorsSweep(kSweepEffects, kSweepFree);
        backdropReset(-2, 2);
        ++s.stepPhase;
        return;
    }
    if (c == StreaksPhase::Clear) {
        if (t == 0x10) {
            crashBytes().backdropShown = 0;
            streakAt(alarmStreaks[0], kStreakSpeedAlarm);
            streakAt(alarmStreaks[1], kStreakSpeedAlarm);
            ++s.stepPhase;
        }
        return;
    }
    if (c != StreaksPhase::Streaks) return;
    size_t k = 0;
    switch (t) {
    case 0x18: k = 2; break;
    case 0x24: k = 3; break;
    case 0x2d: k = 4; break;
    case 0x40: k = 5; break;
    default: break;
    }
    if (t == 0x44) {
        warpToRoom(1);
        scriptNextStep();
    }
    if (k != 0) streakAt(alarmStreaks[k], kStreakSpeedAlarm);
}

// the shake step's phases: the shake started, waiting for the warp's tick
enum class ShakePhase : uint8_t { Start = 0, Wait = 1 };

void shakeStep() { // exe 0x516cd0
    ScriptState& s = block();
    if (ShakePhase(s.stepPhase) == ShakePhase::Start) {
        actorsSweep(kSweepEffects, kSweepFree);
        cameraShake(false, 0x400, 0x10);
        padVibrate(0x401, 0x10450f8);
        ++s.stepPhase;
    } else if (ShakePhase(s.stepPhase) == ShakePhase::Wait && s.stepTick == 0x78) {
        warpToRoom(0);
        scriptNextStep();
    }
}

constexpr size_t kFlutterActor = 8, kEscapeLeft = 10, kEscapeRight = 11, kEscapeCraft = 12;

void flutterStreaksStep() { // exe 0x516d90
    ScriptState& s = block();
    const uint32_t flutterAt = slotIdentityWord(s.actors[kFlutterActor]); // the Flutter of this chapter
    size_t k = 0;
    switch (s.stepTick) {
    case 0: {
        auto&& b = crashBytes();
        b.backdropShown = 1;
        b.backdropY = 0;
        b.backdropX = 0;
        b.backdropSpeedY = 0;
        b.backdropSpeedX = 0;
        return;
    }
    case 0x40: {
        crashBytes().backdropShown = 0;
        Actor& a = actorLayoutAt(flutterAt);
        a.pos.x = 0x1800000;
        a.pos.y = int32_t(0xff000000u);
        a.pos.z = 0x18000000;
        a.velocity.z = -0x400;
        streakNear(flutterStreaks[0], a, kStreakSpeedFlutter);
        return;
    }
    case 0x48: k = 1; break;
    case 0x50: k = 2; break;
    case 0x58: k = 3; break;
    case 0x60:
        warpToRoom(2);
        scriptNextStep();
        return;
    default: return;
    }
    streakNear(flutterStreaks[k], actorLayoutAt(flutterAt), kStreakSpeedFlutter);
}


// the escape step's phases: the craft spawned, its streaks while it flees, the jets' exhausts
enum class EscapePhase : uint8_t { Spawn = 0, Flee = 1, Exhausts = 2 };
void escapeStep() { // exe 0x516f50
    ScriptState& s = block();
    const EscapePhase c = EscapePhase(s.stepPhase);
    if (c == EscapePhase::Spawn) {
        if (s.stepTick == 0) {
            actorsSweep(kSweepEffects, kSweepFree);
        } else if (s.stepTick == 0x14) {
            scriptActorSpawnRecord(stateAddress(TableId::CrashLandingScriptEscapeRecord)); // exe 0x511740
            const uint32_t a = slotIdentityWord(s.actors[kFlutterActor]);
            if (a != 0) streakNear(escapeStreaks[0], actorLayoutAt(a), kStreakSpeedEscape);
            ++s.stepPhase;
        }
        return;
    }
    if (c == EscapePhase::Flee) {
        const uint32_t a = slotIdentityWord(s.actors[kEscapeCraft]);
        size_t k;
        switch (s.stepTick) {
        case 0x16: k = 1; break;
        case 0x1a: k = 2; break;
        case 0x21: k = 3; break;
        case 0x23: {
            scriptActorFreeRecord(stateAddress(TableId::CrashLandingScriptEscapeRecord)); // exe 0x511760
            actorsSweep(kSweepEffects, kSweepFree);
            auto&& b = crashBytes();
            b.backdropY = 0;
            b.backdropX = 0;
            b.backdropSpeedY = 0;
            b.backdropSpeedX = 0;
            b.backdropShown = 1;
            ++s.stepPhase;
            return;
        }
        default: return;
        }
        if (a != 0) streakNear(escapeStreaks[k], actorLayoutAt(a), kStreakSpeedEscape);
        return;
    }
    if (c != EscapePhase::Exhausts) return;
    // the two escaping jets' exhausts (their +0xd: 1 / 2) alternate every 4 ticks
    auto exhaust = [&](size_t k, uint8_t v) { (*s.actors[k]).params[1] = v; };
    switch ((uint32_t(s.stepTick) - 3u) & 0xf) {
    case 1: exhaust(kEscapeLeft, 1); break;
    case 5: exhaust(kEscapeRight, 2); break;
    case 9: exhaust(kEscapeLeft, 2); break;
    case 0xd: exhaust(kEscapeRight, 1); break;
    default: break;
    }
    if (s.stepTick == 100) {
        warpToRoom(0);
        scriptNextStep();
    }
}

// the debris step's phases: the first debris thrown, the others on their ticks
enum class DebrisPhase : uint8_t { Start = 0, Run = 1 };

void debrisStep() { // exe 0x5171c0
    ScriptState& s = block();
    if (DebrisPhase(s.stepPhase) == DebrisPhase::Start) {
        actorsSweep(kSweepProjectiles, kSweepFree);
        crashBytes().backdropShown = 0;
        streakAt(debrisStreaks[0], kStreakSpeedDebris);
        debrisAt(debrisPoints[0]);
        ++s.stepPhase;
        return;
    }
    if (DebrisPhase(s.stepPhase) != DebrisPhase::Run) return;
    switch (s.stepTick) {
    case 8: debrisAt(debrisPoints[1]); break;
    case 9: streakAt(debrisStreaks[1], kStreakSpeedDebris); break;
    case 0xf: streakAt(debrisStreaks[2], kStreakSpeedDebris); break;
    case 0x10: debrisAt(debrisPoints[2]); break;
    case 0x14: debrisAt(debrisPoints[3]); break;
    case 0x19: streakAt(debrisStreaks[3], kStreakSpeedDebris); break;
    case 0x1c: debrisAt(debrisPoints[4]); break;
    case 0x24: debrisAt(debrisPoints[5]); break;
    case 0x2a: debrisAt(debrisPoints[6]); break;
    case 0x2f: streakAt(debrisStreaks[4], kStreakSpeedDebris); break;
    case 0x35: streakAt(debrisStreaks[5], kStreakSpeedDebris); break;
    case 0x36: debrisAt(debrisPoints[7]); break;
    case 0x47: streakAt(debrisStreaks[6], kStreakSpeedDebris); break;
    case 0x61: streakAt(debrisStreaks[7], kStreakSpeedDebris); break;
    default: break;
    }
}

// A burning static's record (exe data, 8 bytes): its s16 point and a word (bit 15: its variant, the low 12 bits its
// parameter)
struct BurnRecord : DataRecord {
    ShortPoint point;
    uint16_t word = 0;
    static BurnRecord load(const TableReader& r) {
        BurnRecord rec;
        rec.point = ShortPoint::load(r.sub(0, 6));
        rec.word = r.u16(6);
        return rec;
    }
};
const DataTable<RecordTable<BurnRecord, 4, 8>> burnRecords{TableId::CrashLandingScriptBurnRecords};
// the statics the burn spawns (the stage's types 7 / 8) and their two parameter words
constexpr uint8_t kBurnStaticType = 7, kEmberStaticType = 8;
struct BurnStaticParams {
    explicit BurnStaticParams(const StaticActor& actor) : BurnStaticParams(const_cast<StaticActor&>(actor)) {}
    explicit BurnStaticParams(StaticActor& actor) : first(actor.params, 0), second(actor.params, 2) {}
    BytesField<uint16_t, 4> first;
    BytesField<uint16_t, 4> second;
};
constexpr uint8_t kThrownPuffKind = 0, kThrownPuffLife = 4;

// a static (pool 0x60, loop flags 0) of type `type`, live and in sweep group 2
StaticActor* burnStatic(uint8_t type, uint8_t variant) {
    const uint32_t at = allocActor60(0); // exe 0x5e1300
    if (at == 0) return nullptr;
    StaticActor& a = staticActorAt(at);
    a.flags = kActorLive;
    a.type = type;
    a.variant = variant;
    a.loopFlags |= kActorSweepGroup2;
    return &a;
}

// the burn step's phases: the missiles swept, the burning statics on their ticks, the white fade and the puffs
enum class BurnPhase : uint8_t { Start = 0, Burn = 1, White = 2 };
void burnStep() { // exe 0x517420
    ScriptState& s = block();
    auto&& b = crashBytes();
    const int32_t t = s.stepTick;
    if (t == 0) {
        s.clearStepPhaseBytes();
    }
    const BurnPhase c = BurnPhase(s.stepPhase);
    if (c == BurnPhase::Start) {
        actorsSweep(kSweepMissiles, kSweepFree);
        ++s.stepPhase;
        return;
    }
    if (c == BurnPhase::Burn) {
        uint32_t k = 0;
        bool spawn = false;
        switch (t) {
        case 0x14: s.flags |= kScriptUnskippable; break;
        case 0x19: k = 0; spawn = true; break;
        case 0x1e: k = 0x81; spawn = true; break;
        case 0x23: k = 0x82; spawn = true; break;
        case 0x2d:
            scriptFade(kToWhite, 0);
            b.ticks = 0;
            ++s.stepPhase;
            break;
        default: break;
        }
        if (spawn) {
            k &= 0xffffff7fu;
            const BurnRecord r = burnRecords[k];
            if (StaticActor* a = burnStatic(kBurnStaticType, uint8_t(r.word >> 15))) {
                auto&& params = BurnStaticParams(*a);
                a->pos.x.whole = r.point.x;
                a->pos.y.whole = r.point.y;
                a->pos.z.whole = r.point.z;
                params.second = 0;
                params.first = uint16_t(r.word & 0xfffu);
            }
        }
        return;
    }
    if (c != BurnPhase::White) return;
    if (t == 0x46) {
        scriptFade(kFromWhite, 0);
    } else if (t == 0x5a) {
        s.flags &= uint8_t(~kScriptUnskippable);
    } else if (t == 0xdc) {
        warpToRoom(1);
        scriptNextStepClearingPhases();
        return;
    }
    const uint32_t ticks = uint32_t(b.ticks);
    if ((uint8_t(ticks) & 3) == 0) {
        if (EffectActor* puff = allocThrownPuff(kActorLive)) { // its point from the RNG once allocated
            const uint32_t r1 = rngStep();
            const uint32_t r2 = rngStep();
            const int16_t p[3] = {int16_t(0x140u - (r1 & 0x3f)), 0x40, int16_t(-0xcd8 - int(r2 & 0x3f))};
            const uint32_t k = uint8_t(ticks) & 0xfu;
            const uint16_t yaw =
                (uint8_t(ticks) & 1) == 0 ? uint16_t((k * uint32_t(-0x20)) & 0xfffu) : uint16_t(k << 5);
            placeThrownPuff(*puff, p, yaw, kThrownPuffKind, kThrownPuffLife);
        }
    }
    if ((uint8_t(fixedSin(int((ticks & 0x7ff) * 2))) & 0x1f) == 2) {
        if (StaticActor* a = burnStatic(kEmberStaticType, 0)) {
            auto&& params = BurnStaticParams(*a);
            uint32_t r = rngStep();
            a->pos.x.whole = int16_t(0x40u - (r & 0x7f));
            r = rngStep();
            a->pos.y.whole = int16_t((r & 0x7f) + 0x10);
            r = rngStep();
            a->pos.z.whole = int16_t(-0xdd0 - int(r & 0x7f));
            const int i = int(ticks & 0xfff);
            params.first = uint16_t((fixedCos(i) >> 7) + 0x200);
            params.second = uint16_t((fixedSin(i) >> 10) + 0xc);
        }
    }
    b.ticks += 1u;
}

// the wait step's phases: the effects swept, waiting for the warp's tick
enum class WaitPhase : uint8_t { Start = 0, Wait = 1 };

void waitStep() { // exe 0x517790
    ScriptState& s = block();
    if (WaitPhase(s.stepPhase) == WaitPhase::Start) {
        actorsSweep(kSweepEffects, kSweepFree);
        ++s.stepPhase;
    } else if (WaitPhase(s.stepPhase) == WaitPhase::Wait && s.stepTick == 0x1b8) {
        warpToRoom(0);
        scriptNextStep();
    }
}

void clearStep() { // exe 0x517820
    if (block().stepTick == 0x104) actorsSweep(kSweepEffects | kSweepMissiles, kSweepFree);
}

// the second file step's phases: until the fade out's tick, the file asked, loaded, the voice asked, playing
enum class SecondFilePhase : uint8_t { Wait = 0, LoadFile = 1, WaitFile = 2, WaitStream = 3, WaitVoice = 4 };

void secondFileStep() { // exe 0x517840
    ScriptState& s = block();
    if (s.stepTick == 0) {
        s.clearStepPhaseBytes();
    }
    switch (SecondFilePhase(s.stepPhase)) {
    case SecondFilePhase::Wait:
        if (s.stepTick == 0x70) {
            voiceClipFadeOut(kVoiceFade);
            scriptFade(kSceneFadeOut, 0);
            s.flags |= kScriptUnskippable;
            ++s.stepPhase;
        }
        return;
    case SecondFilePhase::LoadFile:
        if (fadeState().idle() && voiceClipIdle()) {
            datLoadRequest(kSecondFile);
            crashBytes().secondFileLoaded = 1;
            ++s.stepPhase;
        }
        return;
    case SecondFilePhase::WaitFile:
        if (!(fileLoads().loading & kFileLoading)) ++s.stepPhase;
        return;
    case SecondFilePhase::WaitStream:
        if (!(fileLoads().loading & kStreamBusy) && display().drawBuffer == 0) {
            voiceClipPlay(kLandingVoice);
            ++s.stepPhase;
        }
        return;
    case SecondFilePhase::WaitVoice:
        if (display().drawBuffer == 0 && voiceClipPlaying()) {
            warpToRoom(3, -0xf0, -0x9a);
            scriptNextStepClearingPhases();
        }
        return;
    default: return;
    }
}

// the walk step's phases: the bank and the fade in, waiting for it, the walk on its ticks, the fade out waited
enum class WalkPhase : uint8_t { Start = 0, FadeIn = 1, Walk = 2, FadeOut = 3 };

void walkStep() { // exe 0x5179a0
    ScriptState& s = block();
    auto&& b = crashBytes();
    switch (WalkPhase(s.stepPhase)) {
    case WalkPhase::Start: {
        actorsSweep(kSweepActors, kSweepFree);
        switchStageBank(); // exe 0x5f3200: the other buffer (ST3A02.DAT's models, the player's animations)
        player().setDrawn(true);
        playerStageAnim(0);
        b.setWalkSpeedBytes(0, 0);
        scriptFade(kSceneFadeIn, 0);
        ++s.stepPhase;
        return;
    }
    case WalkPhase::FadeIn:
        if (fadeState().idle()) {
            s.flags &= uint8_t(~kScriptUnskippable);
            ++s.stepPhase;
        }
        return;
    case WalkPhase::Walk: {
        switch (s.stepTick) {
        case 0x14:
            playerStageAnim(1);
            b.setWalkSpeedBytes(0xd8, 0xff);
            break;
        case 0x3c:
            playerStageAnim(2);
            eyes().select(0, 0);
            b.setWalkSpeedBytes(0, 0);
            break;
        case 0x5a:
            eyes().select(1, 0);
            playerStageAnim(3);
            break;
        case 0x6e: case 0x8c: case 0xaa: mouth().select(1, 0); break;
        case 0x78: case 0xa0: case 0xd2: case 0x140: faceTrackStop(s.faceTracks, FaceTrack::Mouth, kClosedMouth); break;
        case 0x122:
            eyes().select(2, 0);
            mouth().select(1, 0);
            break;
        case 0x1c2: eyes().select(3, 0); break;
        case 0x226: playerStageAnim(4); break;
        case 0x230: faceTrackStop(s.faceTracks, FaceTrack::Eyes, kNeutralFace); break;
        case 0x244:
            scriptFade(kSceneFadeOut, 0);
            s.flags |= kScriptUnskippable;
            ++s.stepPhase;
            break;
        default: break;
        }
        integrateVelocity(player(), 0, 0, b.walkSpeed);
        return;
    }
    case WalkPhase::FadeOut:
        if (fadeState().idle()) scriptNextStepClearingPhases();
        return;
    default: return;
    }
}

constexpr RoutineId kEmptyStep = RoutineId::EmptyRoutine; // a step without its own routine

// the steps' routines (by the named routine)
void crashStep(const RoutineRef& routine) {
    switch (routine.id()) {
    case kEmptyStep: return;
    case RoutineId::CrashLandingBackdropOffStep: backdropOffStep(); return;
    case RoutineId::CrashLandingStep2: warpAtStep(0xfa, 0); return;
    case RoutineId::CrashLandingStep3: warpAtStep(0x50, 1); return;
    case RoutineId::CrashLandingAlarmStep: alarmStep(); return;
    case RoutineId::CrashLandingAlarmOffStep: alarmOffStep(); return;
    case RoutineId::CrashLandingStreaksStep: streaksStep(); return;
    case RoutineId::CrashLandingShakeStep: shakeStep(); return;
    case RoutineId::CrashLandingFlutterStreaksStep: flutterStreaksStep(); return;
    case RoutineId::CrashLandingEscapeStep: escapeStep(); return;
    case RoutineId::CrashLandingDebrisStep: debrisStep(); return;
    case RoutineId::CrashLandingBurnStep: burnStep(); return;
    case RoutineId::CrashLandingWaitStep: waitStep(); return;
    case RoutineId::CrashLandingClearStep: clearStep(); return;
    case RoutineId::CrashLandingSecondFileStep: secondFileStep(); return;
    case RoutineId::CrashLandingWalkStep: walkStep(); return;
    default: notPortedAt(kStoryPort, "script 6 step function", routine.pointerValue());
    }
}

// ---- the states
// ------------------------------------------------------------------------------------------------------
enum class SetUpSub : uint8_t { Start = 0, TakeOver = 1, WaitFile = 2, WaitStream = 3, WaitVoice = 4 };
enum class WrapUpSub : uint8_t { Stop = 0, WaitVoice = 1, LoadSecond = 2, Request = 3, WaitRequest = 4 };

void crashSetUp() { // exe 0x516540
    ScriptState& s = block();
    auto&& b = crashBytes();
    switch (SetUpSub(s.subState)) {
    case SetUpSub::Start:
        scriptSequenceStart(stateAddress(TableId::CrashLandingScriptCameraScript), stateAddress(TableId::CrashLandingScriptSteps));
        scriptReplayPadOn();
        player().setDrawn(false);
        backdropReset(-1, 1);
        b.secondFileLoaded = 0;
        b.savedRng = rngNext(rngLastValue());
        rngSetState(kCrashRngSeed);
        scriptSequenceTick();
        ++s.subState;
        [[fallthrough]];
    case SetUpSub::TakeOver: {
        if (playerToScriptIfFree() != 0) break;
        Player& p = player();
        p.pos.x = 0;
        p.pos.y = 0;
        p.pos.z = 0;
        p.clearDirectionOffsetZWord();
        p.idleFaces[0] = 0;
        p.idleFaces[1] = 0;
        s.faceTracks.init(stateAddress(TableId::CrashLandingScriptFaceEyes), stateAddress(TableId::CrashLandingScriptFaceMouth));
        faceTrackStop(s.faceTracks, FaceTrack::Eyes, kNeutralFace);
        faceTrackStop(s.faceTracks, FaceTrack::Mouth, kNeutralFace);
        ++s.subState;
        break;
    }
    case SetUpSub::WaitFile:
        if (!(fileLoads().loading & kFileLoading)) ++s.subState;
        break;
    case SetUpSub::WaitStream:
        if (!(fileLoads().loading & kStreamBusy)) {
            voiceClipPlay(kOpeningVoice);
            ++s.subState;
        }
        break;
    case SetUpSub::WaitVoice:
        if (voiceClipPlaying()) s.enterState(CutsceneState::Steps);
        break;
    default: break;
    }
    backdropScroll();
}

void crashSteps() { // exe 0x5166b0
    scriptStepsState(&crashStep);
    if (crashBytes().backdropShown != 0) backdropScroll();
}

// the wrap-up's phases of its LoadSecond sub-state: the second file asked, loaded
enum class LoadSecondPhase : uint8_t { Load = 0, Wait = 1 };

void crashWrapUp() { // exe 0x516720
    ScriptState& s = block();
    auto&& b = crashBytes();
    switch (WrapUpSub(s.subState)) {
    case WrapUpSub::Stop:
        padVibrate(0, kVibrationStop);
        playerScriptAnim(PlayerAnim::Idle, 0, true);
        actorsSweep(kSweepActors, kSweepFree);
        windowCloseAll(WindowClose::Gentle);
        if (b.alarmOn != 0) {
            soundPlayFlat(kAlarmOffSound);
            b.alarmOn = 0;
        }
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
        ++s.subState;
        [[fallthrough]];
    case WrapUpSub::WaitVoice:
        if (voiceClipIdle()) s.subState = uint8_t((b.secondFileLoaded != 0 ? 1u : 0u) + 2u);
        return;
    case WrapUpSub::LoadSecond:
        if (LoadSecondPhase(s.stepPhase) == LoadSecondPhase::Load) {
            datLoadRequest(kSecondFile);
            ++s.stepPhase;
            return;
        }
        if (!(fileLoads().loading & kFileLoading)) {
            s.subState = uint8_t(WrapUpSub::Request);
            s.stepPhase = uint8_t(LoadSecondPhase::Load);
        }
        return;
    case WrapUpSub::Request:
        s.subState = uint8_t(WrapUpSub::WaitRequest);
        requestTransition(kTransitionAccepted, kST08, 0, -0x580, -0x400, 0x10, 0xc00, kNoFade, kNoFade);
        return;
    case WrapUpSub::WaitRequest:
        if (transitionRequest().state != kTransitionNone) return;
        playerBackToPlay(); // exe 0x5723c0
        scriptReplayPadOff();
        player().setDrawn(true);
        rngSetState(rngNext(b.savedRng));
        scriptEndBlock();
        return;
    default: return;
    }
}

// exe 0x516520: the state
void crashScript() {
    ScriptState& s = block();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: crashSetUp(); return;
    case CutsceneState::Steps: crashSteps(); return;
    case CutsceneState::WrapUp: crashWrapUp(); return;
    default: notPortedAt(kStoryPort, "script 6 state", s.state);
    }
}

} // namespace

bool crashLandingScriptPorted(RoutineId handler) { return handler == RoutineId::CrashLandingScript; }

bool crashLandingScriptTick(const PlayerEnv&, RoutineId handler) {
    if (handler != RoutineId::CrashLandingScript) return false;
    crashScript();
    return true;
}

bool crashLandingRoomCaseKnown(uint8_t stage) { return stage == kST3A; }

bool crashLandingRoomCase() { // exe 0x587c00
    const GameState& g = gameState();
    if (g.stage != kST3A) return false;
    RoomScriptState& room = roomScriptState();
    if (g.room == 0 && room.handlerStep == 0) {
        scriptRun(kCrashScript);
        room.handlerStep = kRoomCaseDone;
    }
    return true;
}

} // namespace od::port
