// The scripts inside Glyde's base (ST30 / ST31):
//   0x21 (exe 0x522100, states 0x8a3ca0: 0x522120 / 0x51bc90 / 0x522240; camera 0x8a3b94, steps 0x8a3c78 = timed 0xb4 /
//   0x1ce / 0x50 / 0x96): ST30 sub 3 (room handler 0x585df0, after script 0x20 brought the player here), the way into
//   the base. The cast registers itself in the block (its first two actors) and follows its cue byte. State 0:
//   unskippable, the actors 0xefe swept, the replay pad, the sequence, the player at (0x320, -1, 0xb60) yaw 0; once
//   script driven animation 0x80 and the face tracks 0x8a3b84 / 0x8a3b8c; sound 0x28 + voice 9; state 1 once it plays.
//   Steps: 0x522330 (cast A cue 1 at 0x3c; next once its z is 0xa0), 0x522360 (cast A cues 2 / 3 at 0x19 / 0xe3, the
//   script actor of 0x8a3b54 at 0xe6, cast B cues 1 / 2 / 3 at 0x106 / 0x156 / 0x174), 0x5223d0 (animations 0x81 step
//   0x10 / 0x82 at 0 / 0xf, face keys 1), 0x522430 (both casts cue 4 at 0). State 2: fade 0x10 (unless skipped), the
//   voice faded; once idle and no fade: the actors 0xefe swept, the player back, request 2 -> ST30 sub 0 (0x320, -1,
//   0xb60) yaw 0 (no fade-in), the block ended.
//   0x22 (exe 0x522450, states 0x8a3df4: 0x522470 / 0x52eb60 / 0x522500; camera [0x8a3e00 + room * 4], steps
//   [0x8a3de8 + room * 4]): ST30 subs 0 / 1 / 2 after their wave (their room handlers), a camera move over the base
//   with the player hidden (one timed step; routine 0x52ef00 does nothing). State 0: unskippable, the replay pad, the
//   sequence, the player hidden (not live, not drawn), the actors 0xfbe swept and class 0x40 op 5; state 1 on the third
//   tick. The runner 0x52eb60 (skip: fade 0x11). State 2: the music faded (0x7ff), the voice faded; once idle and no
//   fade: fade 1 if skipped, the player shown on the ground, the actors 0xfbe swept + class 0x40 op 10, the six cells
//   of the opened way to variant 1, the player back.
//   ST30 sub 1's sequence (the tower falls) has the step 0x522730: the timeline 0x522910 over the event list 0x8a3e0c
//   (u16 time, kind, index; kind 0 a death explosion at [0x8a3ed4 + i * 10], 1 a blast at [0x8a3f08 + i * 12] (+ by CRT
//   rand & 3 == 0 a falling Birdbot, pool 0x20 type 0x2d variant 1, when the entry's yaw is > 0), 2 a smoke emitter at
//   [0x8a3f80 + i * 14] (kept in the list 0xa59f54 / count 0xa59f98 unless its variant word has 0x80), 3 a 5 x 7 grid
//   of smoke; anything else = the end, -1). The counters 0xa59f94 (tick) / 0xa59f96 (event) start at 0 (never reset in
//   the exe: the scene runs once).
//   0x23 (exe 0x522c70, states 0x8a4140: 0x522c90 / 0x52fa20 / 0x522d80; camera 0x8a40b0, steps 0x8a4128): ST31 sub 1
//   (room handler 0x586220 step 0, once), Shu's brother held by Glyde's Birdbots. The cast (records 0x8a4060 / 0x8a4074
//   / 0x8a4088 = pool 0x20 type 0x2d sub 2 variants 0..2, 0x8a409c = type 0x71 variant 0) registers in the block (its
//   first four actors). State 0: fade 2, the actors 0xffe swept, the replay pad, the player at (-0x1e0, -0x320, -0x2e0)
//   yaw 0xa00, the sequence; voice 0xb; state 1 once it plays and no fade runs. Steps: 0x522e30 (cues: the fourth at 5
//   / 0x19, the first and the third at 0x1e, the second at 0x23, the third at 0x37), 0x522ed0 (the fourth's cue 3 at 0;
//   from 0xf the replay pad holds the player's forward | walk buttons until the player is airborne (then cleared)). The
//   runner 0x52fa20 (skip: fade 0x22). State 2: the voice faded; once idle and no fade: fade 1 if skipped, the actors
//   0xefe swept, the player back at (-0x190, -0x101, -0x290) yaw 0xa00, the block ended.
//   0x24 (exe 0x522f30, states 0x8a4340: 0x522f50 / 0x51bc90 / 0x523070; camera 0x8a4248, steps 0x8a4318, face tracks
//   0x8a4220 / 0x8a4230): ST31 sub 1 after the fight (room handler 0x586220 step 3: flags 0x23 / 0x670, story + 1). The
//   cast: records 0x8a4150 (type 0x71 variant 1), 0x8a4164 (type 0x2d sub 2 variant 3), 0x8a4178 (type 0x71 variant 2).
//   State 0: the actors 0xefe swept, the replay pad, the sequence, the player at (-0x13c, 0, 0) yaw 0xc00; once script
//   driven: the face tracks, animation 0x83; voice 0xc; state 1 once it plays. Steps: 0x523180 (animations 0x84..0x89,
//   face keys, the first actor's cues 1..5; phase 1: a turn (yaw ^ 0x800, animation 0x88, -0x1f0) once the player's
//   animation ended, phase 2: walks -0x50), 0x523460 (the actors 0xefe swept at 0, the second actor's cue at 10),
//   0x523490 (the player at (-0x190, 0, 0) yaw 0xc00 + animation 0x8a at 0; at 0x18 the base's alarm (pool e0 type
//   0x2a); at 0x1e the third actor's cue 1 + animation 0x8b), 0x523550 (the third actor: cue 2 + yaw 0x400 at 0, cue 3
//   at 0xf; the player turns, animations 8 / 1, walks -0x130). State 2: the voice faded; request 2 -> ST30 sub 2 (0x1e0,
//   0, 0xa80) yaw 0x400, fade 0x10; once done: the player back, the actors 0x6fe swept, the block ended.
//   0x25 (exe 0x5235f0, states 0x8a4578: 0x523610 / 0x51bc90 / 0x523750; camera 0x8a4434, steps 0x8a4548): ST30 sub 2
//   with flag 0x23 (room handler 0x585890, once): the rescue before the escape. The cast registers in the block (its
//   actors 0 / 2 / 4 / 5). State 0: voice 0xf, the actors 0xefe swept, the replay pad; once it plays fade 2, the
//   sequence, the player at (0x1e0, 0, 0xac0) yaw 0x260; once script driven animation 0x8e, the face tracks 0x8a4420 /
//   0x8a4428, state 1. Steps: 0x523870 (walks -0xf0 a tick until 0), 0x5238a0 (sound 0x29 at 0, the script actor of
//   0x8a4364 at 10, animation 0x8f at 20 (stops walking), the first actor's cue 1 at 25, the warp to sub 4 at 0x4b),
//   0x5239f0 (the player shown / hidden at 0, the fifth / sixth actors' cues, the warp to sub 2 at 0x78), 0x523b30 (at
//   0: animation 0x90, records 0x8a4378 / 0x8a43c8, the third actor placed (0x7f0, 0, 0x230) from the player turned by
//   its yaw), 0x523bf0 (animations 0x91..0x96, face keys, the third actor's cues 1..5). State 2: fade 0x10 (unless
//   skipped), the voice faded; then request 2 -> ST30 sub 2 (0x1e0, 0, 0xac0) yaw 0x260, the actors 0xffe swept, the
//   player back there.
#include "content/calbania/glyde_base_scripts_state.hpp"
#include "content/calbania/ch3_scripts.hpp"

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/display.hpp"
#include "game/explosions.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/player/player_input.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/spark_emitter.hpp"
#include "game/spark_emitter_state.hpp"
#include "game/story.hpp"
#include "game/table_records.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::glyde_base_scripts_state;

namespace od::port {

namespace {

enum class WrapUpSub : uint8_t { Fade = 0, WaitIdle = 1, Leave = 2, End = 3 };
constexpr uint32_t kVoiceFade = 8;
constexpr uint32_t kSweepKeep = 0x80;
constexpr uint32_t kSweepBase = 0xefe, kSweepAll = 0xffe;
constexpr uint8_t kBaseStage = 0x30;

// the cue of a script's cast member (its parameter byte 0: the step it plays next), stored as the exe does through
// the block's actor address
void cue(const ActorRef<ActorHeader>& actor, uint8_t step) { actor->params[0] = step; }
// the player's animation ended (the body's key carries the jump: the sequence played through)
bool playerAnimEnded() { return (player().body.key.flags & kKeyJump) != 0; }
// the voice faded and the state's sub-state on (the wrap-ups' first two sub-states); true once idle with no fade
bool voiceFadedAndIdle(ScriptState& s) {
    switch (WrapUpSub(s.subState)) {
    case WrapUpSub::Fade:
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
        ++s.subState;
        return false;
    case WrapUpSub::WaitIdle:
        if (voiceClipIdle() && fadeState().busy == 0) ++s.subState;
        return false;
    default: return true;
    }
}

// ==== script 0x21 ========================================================================================================
enum class EntranceSetUp : uint8_t { Start = 0, TakeOver = 1, Voice = 2, WaitVoice = 3 };
constexpr RoutineId kStepCastWalksIn = RoutineId::GlydeBaseScriptsStepCastWalksIn,
                    kStepCastTalks = RoutineId::GlydeBaseScriptsStepCastTalks,
                    kStepPlayerAnswers = RoutineId::GlydeBaseScriptsStepPlayerAnswers,
                   kStepCastLeaves = RoutineId::GlydeBaseScriptsStepCastLeaves;
constexpr size_t kCastA = 0, kCastB = 1;
constexpr int16_t kCastStopZ = 0xa0;

void baseEntranceStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = s.stepTick;
    const ActorRef<ActorHeader> a = s.actors[kCastA], b = s.actors[kCastB];
    switch (routine.id()) {
    case kStepCastWalksIn:
        if (t == 0x3c) cue(a, 1);
        if (a && actorLayoutOf(*a).pos.z.whole == kCastStopZ) scriptSequenceSkip(1);
        return;
    case kStepCastTalks:
        switch (t) {
        case 0x19: cue(a, 2); return;
        case 0xe3: cue(a, 3); return;
        case 0xe6: scriptActorSpawnRecord(stateAddress(TableId::GlydeBaseScriptsEntranceRecord)); return; // exe 0x511740
        case 0x106: cue(b, 1); return;
        case 0x156: cue(b, 2); return;
        case 0x174: cue(b, 3); return;
        default: return;
        }
    case kStepPlayerAnswers:
        if (t == 0) {
            playerAnim(0x81, 0x10);
        } else if (t == 0xf) {
            playerAnim(0x82);
            faceKeys(1);
        }
        return;
    case kStepCastLeaves:
        if (t == 0) {
            cue(a, 4);
            cue(b, 4);
        }
        return;
    default: notPortedAt(kStoryPort, "script 0x21 step", routine.pointerValue());
    }
}

} // namespace

void baseEntranceScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x522120
        switch (EntranceSetUp(s.subState)) {
        case EntranceSetUp::Start:
            s.subState = uint8_t(EntranceSetUp::TakeOver);
            s.flags |= kScriptUnskippable;
            actorsSweep(kSweepBase, kSweepKeep);
            scriptReplayPadOn();
            scriptSequenceStart(stateAddress(TableId::GlydeBaseScriptsEntranceCamera), scriptStepList<TableId::GlydeBaseScriptsEntranceSteps>);
            scriptSequenceTick();
            clearPlayerAnglesAndFaces();
            placePlayer(0x3200000, 0xffff0000u, 0xb600000, 0);
            [[fallthrough]];
        case EntranceSetUp::TakeOver:
            if (playerToScriptIfFree() == 0) {
                ++s.subState;
                playerAnim(0x80);
                startFaceTracks(stateAddress(TableId::GlydeBaseScriptsEntranceEyes), stateAddress(TableId::GlydeBaseScriptsEntranceMouth));
            }
            return;
        case EntranceSetUp::Voice:
            soundPlayFlatForced(0x28);
            voiceClipPlay(9);
            ++s.subState;
            return;
        case EntranceSetUp::WaitVoice:
            if (voiceClipPlaying()) {
                s.enterState(CutsceneState::Steps);
                s.flags &= uint8_t(~kScriptUnskippable);
            }
            return;
        default: return;
        }
    case CutsceneState::Steps: scriptStepsStateAnyRoutine(baseEntranceStep); return; // exe 0x51bc90
    case CutsceneState::WrapUp: // exe 0x522240
        if (WrapUpSub(s.subState) == WrapUpSub::Fade) {
            if (!(s.flags & kScriptSkipTaken)) scriptFade(0x10, 0);
            if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
            ++s.subState;
            return;
        }
        if (!voiceFadedAndIdle(s)) return;
        if (WrapUpSub(s.subState) != WrapUpSub::Leave) return;
        actorsSweep(kSweepBase, kSweepKeep);
        playerBackToPlay();   // exe 0x5723c0
        scriptReplayPadOff(); // exe 0x511890
        requestTransition(kTransitionAccepted, kBaseStage, 0, 800, -1, 0xb60, 0, 0, kNoFade);
        scriptEndBlock();
        return;
    default: notPortedAt(kStoryPort, "script 0x21 state", s.state);
    }
}

namespace {

// ==== script 0x22 ========================================================================================================
enum class OverviewSetUp : uint8_t { Start = 0, Wait = 1, Run = 2 };
// by the room (exe data 0x8a3e00 / 0x8a3de8: the camera scripts' and step lists' addresses, as the script state stores
// them: the address is the record's identity)
const DataTable<ScalarTable<uint32_t, 3>> overviewCameras{TableId::GlydeBaseOverviewCameras};
const DataTable<ScalarTable<uint32_t, 3>> overviewSteps{TableId::GlydeBaseOverviewSteps};
constexpr RoutineId kStepTowerFalls = RoutineId::GlydeBaseScriptsStepTowerFalls, kStepNothing = RoutineId::EmptyRoutine;
constexpr uint32_t kSweepOverview = 0xfbe, kSweepClass40 = 0x40, kSweepHold = 5, kSweepRelease = 10;
// the cells of the way the falling tower opens ({x, z})
constexpr uint8_t kOpenedWayCells[6][2] = {{0x3b, 0x41}, {0x3b, 0x43}, {0x3b, 0x45},
                                           {0x3d, 0x43}, {0x3d, 0x45}, {0x3f, 0x45}};
constexpr int32_t kTowerSilence = 300;

// the tower's fall: the event list and its counters, the smoke emitters it keeps
struct TowerEvent : DataRecord {
    uint16_t time = 0;
    uint16_t kind = 0;
    uint16_t index = 0;
    static TowerEvent load(const TableReader& r) {
        TowerEvent rec;
        rec.time = r.u16(0x0);
        rec.kind = r.u16(0x2);
        rec.index = r.u16(0x4);
        return rec;
    }
};
enum class TowerEventKind : uint16_t { Explosion = 0, Blast = 1, Smoke = 2, SmokeGrid = 3 };
struct TowerExplosion : DataRecord {
    ShortPoint point;
    uint16_t spread = 0;
    uint16_t flashes = 0;
    static TowerExplosion load(const TableReader& r) {
        TowerExplosion rec;
        rec.point = ShortPoint::load(r.sub(0x0, 6));
        rec.spread = r.u16(0x6);
        rec.flashes = r.u16(0x8);
        return rec;
    }
};
struct TowerBlast : DataRecord {
    ShortPoint point;
    uint16_t size = 0;
    int16_t birdbotYaw = 0; // > 0: a Birdbot may fall off, facing it
    uint16_t attack = 0;
    static TowerBlast load(const TableReader& r) {
        TowerBlast rec;
        rec.point = ShortPoint::load(r.sub(0x0, 6));
        rec.size = r.u16(0x6);
        rec.birdbotYaw = r.s16(0x8);
        rec.attack = r.u16(0xa);
        return rec;
    }
};
struct TowerSmoke : DataRecord {
    uint16_t variant = 0; // the emitter's variant (& 0x7f); kSmokeNotKept: not in the list
    ShortPoint point;
    int8_t driftX = 0;
    int8_t driftZ = 0;
    int16_t lifetime = 0;
    static TowerSmoke load(const TableReader& r) {
        TowerSmoke rec;
        rec.variant = r.u16(0x0);
        rec.point = ShortPoint::load(r.sub(0x2, 6));
        rec.driftX = r.s8(0x8);
        rec.driftZ = r.s8(0xa);
        rec.lifetime = r.s16(0xc);
        return rec;
    }
};
const DataTable<RecordTable<TowerEvent, 0x21, 6>> towerEvents{TableId::GlydeBaseScriptsTowerEvents};
const DataTable<RecordTable<TowerExplosion, 5, 10>> towerExplosions{TableId::GlydeBaseScriptsTowerExplosions};
const DataTable<RecordTable<TowerBlast, 10, 12>> towerBlasts{TableId::GlydeBaseScriptsTowerBlasts};
const DataTable<RecordTable<TowerSmoke, 0x10, 14>> towerSmokes{TableId::GlydeBaseScriptsTowerSmokes};
constexpr uint16_t kSmokeNotKept = 0x80, kSmokeVariantMask = 0x7f;
constexpr uint8_t kEmitterDrain = 2; // the emitter's sub-state that stops its emits (game/spark_emitter)
constexpr uint8_t kFallingBirdbotType = 0x2d, kFallingBirdbotVariant = 1;
constexpr int16_t kFallingBirdbotSpeed = int16_t(0xfc40);
// the smoke grid: 5 rows down from -0x40 by 0xb0, 7 emitters along each from (-0x640, 0x7b0) by (0x90, 0x60), each
// jittered by rand & 0x1f - 0x40, variants 0xc..0xe
constexpr int kGridRows = 5, kGridColumns = 7;
constexpr uint8_t kGridVariantBase = 0xc;
constexpr int16_t kGridLifetime = 8;

uint16_t jitter(int16_t v) { return uint16_t(((crtRand() & 0x1f) - 0x40 + uint32_t(uint16_t(v))) & 0xffff); }

// exe 0x522910: the event of this tick, if due; -1 at the list's end
int towerTimeline() {
    TowerFallState& f = towerFall();
    const int t = f.tick;
    f.tick = int16_t(t + 1);
    const TowerEvent ev = towerEvents[f.nextEvent];
    if (int(ev.time) > t) return 0;
    ++f.nextEvent;
    const uint32_t i = ev.index;
    switch (TowerEventKind(ev.kind)) {
    case TowerEventKind::Explosion: {
        const TowerExplosion e = towerExplosions[i];
        if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300
            StaticActor& x = staticActorAt(address);
            x.variant = 0;
            x.flags = kActorLive;
            x.type = uint8_t(GlobalStaticType::Explosion);
            x.loopFlags |= kActorSweepGroup2;
            x.pos.x.whole = e.point.x;
            x.pos.y.whole = e.point.y;
            x.pos.z.whole = e.point.z;
            ExplosionParams params(actorHeaderAt(address));
            params.spread = e.spread;
            params.flashes = e.flashes;
        }
        return 0;
    }
    case TowerEventKind::Blast: {
        const TowerBlast e = towerBlasts[i];
        const uint32_t address = allocActor60(0); // exe 0x5e1300
        if (address == 0) return 0;
        StaticActor& x = staticActorAt(address);
        x.flags = kActorLive;
        x.type = uint8_t(GlobalStaticType::Blast);
        x.variant = 0;
        x.loopFlags |= kActorSweepGroup1;
        const int16_t px = e.point.x, py = e.point.y, pz = e.point.z;
        x.pos.x.whole = px;
        x.pos.y.whole = py;
        x.pos.z.whole = pz;
        BlastParams params(actorHeaderAt(address));
        params.size = e.size;
        const int16_t yaw = e.birdbotYaw;
        params.attack = e.attack;
        if (yaw > 0 && (crtRand() & 3) == 0) {
            if (const uint32_t c = allocActor20()) { // exe 0x5e1160: a Birdbot falls off
                AnimatedActor& bird = animatedActorAt(c);
                bird.pos.x.whole = px;
                bird.loopFlags |= kActorSweepGroup2;
                bird.pos.y.whole = py;
                bird.flags = kActorLive;
                bird.type = kFallingBirdbotType;
                bird.subtype = 0;
                bird.variant = kFallingBirdbotVariant;
                bird.pos.z.whole = pz;
                bird.rotation.x = 0;
                bird.rotation.y = yaw;
                bird.rotation.z = 0;
                bird.params[0] = 0;
                bird.velocity.z = kFallingBirdbotSpeed;
                bird.velocity.x = 0;
                bird.velocity.y = 0;
            }
        }
        return 0;
    }
    case TowerEventKind::Smoke: {
        const TowerSmoke e = towerSmokes[i];
        const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (address == 0) return 0;
        StaticActor& x = staticActorAt(address);
        x.flags = kActorLive;
        x.type = kSparkEmitterStageType;
        const uint16_t v = e.variant;
        x.loopFlags |= kActorSweepGroup2;
        x.variant = uint8_t(v & kSmokeVariantMask);
        x.pos.x.whole = e.point.x;
        x.pos.y.whole = e.point.y;
        x.pos.z.whole = e.point.z;
        spark_emitter_state::SparkEmitterFields& params = classState<spark_emitter_state::SparkEmitterFields>(staticActorAt(address));
        params.driftX = e.driftX;
        params.driftZ = e.driftZ;
        params.lifetime = e.lifetime;
        if (!(v & kSmokeNotKept)) {
            const uint32_t n = f.smokeCount;
            f.smokes[n] = address;
            f.smokeCount = n + 1u;
        }
        return 0;
    }
    case TowerEventKind::SmokeGrid: {
        int16_t y = -0x40;
        for (int r = 0; r < kGridRows; ++r) {
            int16_t x = -0x640, z = 0x7b0;
            for (int c = 0; c < kGridColumns; ++c) {
                if (const uint32_t address = allocActor60(kActorStageClass)) { // exe 0x5e12f0
                    StaticActor& e = staticActorAt(address);
                    e.flags = kActorLive;
                    e.type = kSparkEmitterStageType;
                    e.variant = uint8_t(int(crtRand()) % 3 + kGridVariantBase);
                    e.loopFlags |= kActorSweepGroup2;
                    e.pos.x.whole = int16_t(jitter(x));
                    e.pos.y.whole = int16_t(jitter(y));
                    const uint32_t rz = crtRand();
                    spark_emitter_state::SparkEmitterFields& params = classState<spark_emitter_state::SparkEmitterFields>(staticActorAt(address));
                    params.driftX = 0;
                    params.driftZ = 0;
                    params.lifetime = kGridLifetime;
                    e.pos.z.whole = int16_t(((rz & 0x1f) - 0x40 + uint32_t(uint16_t(z))) & 0xffff);
                }
                x = int16_t(x + 0x90);
                z = int16_t(z + 0x60);
            }
            y = int16_t(y - 0xb0);
        }
        return 0;
    }
    default: return -1;
    }
}
void openTheWay() {
    for (const auto& c : kOpenedWayCells) switchGridCellVariant(c[0], c[1], 1);
}
// exe 0x522730: the timeline until its end (then the kept smoke drained and the way opened); the silence at 300
void towerFallsStep(ScriptState& s) {
    if (s.stepPhase == 0 && towerTimeline() < 0) {
        ++s.stepPhase;
        TowerFallState& f = towerFall();
        for (uint32_t i = 0, n = f.smokeCount; i < n; ++i) actorHeaderAt(f.smokes[i]).subState = kEmitterDrain;
        openTheWay();
    }
    if (s.stepTick == kTowerSilence) {
        soundStopSfxVoices(); // exe 0x5d59b0
        actorsSweep(kSweepOverview, kSweepKeep);
    }
}
// exe 0x52eb60: the step (the tower's, or nothing), the skip (fade 0x11), the sequence's tick
void overviewRun(ScriptState& s) {
    if (s.steps->id != kScriptStepEnd) {
        const RoutineRef routine = stateRoutine(s.steps->routine);
        if (routine.is(kStepTowerFalls)) towerFallsStep(s);
        else if (!routine.is(kStepNothing)) notPortedAt(kStoryPort, "script 0x22 step", routine.pointerValue());
        if (scriptSkipRequested() == 0 || display().drawBuffer == 0) {
            scriptSequenceTick();
            return;
        }
        scriptFade(0x11, 0);
    }
    s.enterState(CutsceneState::WrapUp);
    scriptSequenceTick();
}

} // namespace

void baseCameraScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x522470
        switch (OverviewSetUp(s.subState)) {
        case OverviewSetUp::Start: {
            s.subState = uint8_t(OverviewSetUp::Wait);
            s.flags |= kScriptUnskippable;
            scriptReplayPadOn();
            const uint32_t room = gameState().room;
            scriptSequenceStart(overviewCameras[room], overviewSteps[room]);
            scriptSequenceTick();
            player().flags &= uint8_t(~kActorLiveDrawn);
            actorsSweep(kSweepOverview, kSweepKeep);
            actorsSweep(kSweepClass40, kSweepHold);
            ++s.subState; // the shared increment: Run
            return;
        }
        case OverviewSetUp::Wait: ++s.subState; return;
        case OverviewSetUp::Run:
            s.enterState(CutsceneState::Steps);
            s.flags &= uint8_t(~kScriptUnskippable);
            return;
        default: return;
        }
    case CutsceneState::Steps: overviewRun(s); return;
    case CutsceneState::WrapUp: // exe 0x522500
        if (WrapUpSub(s.subState) == WrapUpSub::Fade) {
            soundFadeOut(kMusicFades, 0x7ff, 0);
            if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
            ++s.subState;
            return;
        }
        if (!voiceFadedAndIdle(s)) return;
        if (WrapUpSub(s.subState) != WrapUpSub::Leave) return;
        {
            if (s.flags & kScriptSkipTaken) scriptFade(1, 0);
            Player& p = player();
            p.flags |= kActorLiveDrawn;
            p.pos.y.whole = int16_t(actorGround(scriptEnv(), p)); // exe 0x40f190
            actorsSweep(kSweepOverview, kSweepKeep);
            actorsSweep(kSweepClass40, kSweepRelease);
            openTheWay();
            playerBackToPlay();   // exe 0x5723c0
            scriptReplayPadOff(); // exe 0x511890
            scriptEndBlock();
        }
        return;
    default: notPortedAt(kStoryPort, "script 0x22 state", s.state);
    }
}

namespace {

// ==== script 0x23 ========================================================================================================
enum class HeldSetUp : uint8_t { Start = 0, Place = 1, Voice = 2, WaitVoice = 3 };
constexpr RoutineId kStepGuardsCue = RoutineId::GlydeBaseScriptsStepGuardsCue,
                    kStepWalkOut = RoutineId::GlydeBaseScriptsStepWalkOut;
constexpr size_t kGuardA = 0, kGuardB = 1, kGuardC = 2, kBrother = 3;
enum class WalkOutPhase : uint8_t { Off = 0, Walking = 1 };

void brotherHeldStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = s.stepTick;
    switch (routine.id()) {
    case kStepGuardsCue: // switch (t - 5) over the byte table 0x522e98
        switch (t) {
        case 5: cue(s.actors[kBrother], 1); return;
        case 0x19: cue(s.actors[kBrother], 2); return;
        case 0x1e:
            cue(s.actors[kGuardA], 1);
            cue(s.actors[kGuardC], 1);
            return;
        case 0x23: cue(s.actors[kGuardB], 1); return;
        case 0x37: cue(s.actors[kGuardC], 2); return;
        default: return;
        }
    case kStepWalkOut: {
        if (t == 0) cue(s.actors[kBrother], 3);
        else if (t == 0xf) s.stepPhase = uint8_t(WalkOutPhase::Walking);
        if (WalkOutPhase(s.stepPhase) == WalkOutPhase::Off) return;
        Player& p = player();
        ReplayPad& pad = replayPad();
        pad.held = uint16_t(p.buttonMap[size_t(PlayerButton::Forward)] | p.buttonMap[size_t(PlayerButton::Walk)]);
        if (p.airborne != 0) {
            s.stepPhase = uint8_t(WalkOutPhase::Off);
            pad.held = 0;
            pad.pressed = 0;
        }
        return;
    }
    default: notPortedAt(kStoryPort, "script 0x23 step", routine.pointerValue());
    }
}

} // namespace

void brotherHeldScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x522c90
        switch (HeldSetUp(s.subState)) {
        case HeldSetUp::Start:
            s.subState = uint8_t(HeldSetUp::Place);
            scriptFade(2, 0);
            actorsSweep(kSweepAll, kSweepKeep);
            s.flags |= kScriptUnskippable;
            scriptReplayPadOn(); // exe 0x5117f0
            [[fallthrough]];
        case HeldSetUp::Place:
            clearPlayerAnglesAndFaces();
            placePlayer(0xfe200000u, 0xfce00000u, 0xfd200000u, 0xa00);
            scriptSequenceStart(stateAddress(TableId::GlydeBaseScriptsHeldCamera), scriptStepList<TableId::GlydeBaseScriptsHeldSteps>);
            scriptSequenceTick();
            ++s.subState;
            return;
        case HeldSetUp::Voice:
            voiceClipPlay(0xb);
            ++s.subState;
            return;
        case HeldSetUp::WaitVoice:
            if (voiceClipPlaying() && fadeState().busy == 0) {
                s.enterState(CutsceneState::Steps);
                s.flags &= uint8_t(~kScriptUnskippable);
            }
            return;
        default: return;
        }
    case CutsceneState::Steps: scriptStepsStateNoFacesAnyRoutine(brotherHeldStep); return; // exe 0x52fa20
    case CutsceneState::WrapUp:                                                             // exe 0x522d80
        if (!voiceFadedAndIdle(s)) return;
        if (WrapUpSub(s.subState) != WrapUpSub::Leave) return;
        if (s.flags & kScriptSkipTaken) scriptFade(1, 0);
        actorsSweep(kSweepBase, kSweepKeep);
        playerBackToPlay();   // exe 0x5723c0
        scriptReplayPadOff(); // exe 0x511890
        placePlayer(0xfe700000u, 0xfeff0000u, 0xfd700000u, 0xa00);
        scriptEndBlock();
        return;
    default: notPortedAt(kStoryPort, "script 0x23 state", s.state);
    }
}

namespace {

// ==== script 0x24 ========================================================================================================
enum class FreedSetUp : uint8_t { Start = 0, Place = 1, TakeOver = 2, Voice = 3, WaitVoice = 4 };
constexpr RoutineId kStepThanks = RoutineId::GlydeBaseScriptsStepThanks,
                    kStepAlarmNear = RoutineId::GlydeBaseScriptsStepAlarmNear,
                    kStepAlarm = RoutineId::GlydeBaseScriptsStepAlarm,
                    kStepRunOff = RoutineId::GlydeBaseScriptsStepRunOff;
constexpr size_t kFreedBrother = 0, kFreedBirdbot = 1, kFreedGuide = 2;
constexpr uint8_t kAlarmType = 0x2a;
constexpr int16_t kTurnAround = 0x800;
enum class ThanksPhase : uint8_t { None = 0, Turn = 1, Walk = 2, Still = 3 };
enum class RunOffPhase : uint8_t { None = 0, Start = 1, Turn = 2, Run = 3 };

void brotherFreedStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = s.stepTick;
    Player& p = player();
    switch (routine.id()) {
    case kStepThanks: {
        const ActorRef<ActorHeader> brother = s.actors[kFreedBrother];
        switch (t) { // switch (t - 0xf) over the byte table 0x523360
        case 15: cue(brother, 1); break;
        case 90: playerAnim(0x84); break;
        case 115:
            playerAnim(0x85);
            mouthKey(2);
            break;
        case 165: mouthKey(0); break;
        case 185:
            playerAnim(0x86);
            eyesKey(2);
            mouthKey(3);
            break;
        case 210: mouthKey(0); break;
        case 215: cue(brother, 2); break;
        case 225:
            s.stepPhase = uint8_t(ThanksPhase::Turn);
            p.body.key.flags = 0;
            playerAnim(0x87);
            break;
        case 233: cue(brother, 3); break;
        case 251: cue(brother, 4); break;
        case 259:
            s.stepPhase = uint8_t(ThanksPhase::Still);
            p.body.key.flags = 0;
            playerAnim(0x89);
            eyesKey(3);
            mouthKey(4);
            break;
        case 263: cue(brother, 5); break;
        default: break;
        }
        switch (ThanksPhase(s.stepPhase)) {
        case ThanksPhase::Turn:
            if (playerAnimEnded()) {
                s.stepPhase = uint8_t(ThanksPhase::Walk);
                p.rotation.y ^= kTurnAround;
                playerAnim(0x88);
                integrateVelocity(p, 0, 0, -0x1f0); // exe 0x5e3690
            }
            return;
        case ThanksPhase::Walk: integrateVelocity(p, 0, 0, -0x50); return;
        default: return;
        }
    }
    case kStepAlarmNear:
        if (t == 0) actorsSweep(kSweepBase, kSweepKeep);
        else if (t == 0xa) cue(s.actors[kFreedBirdbot], 1);
        return;
    case kStepAlarm:
        if (t == 0) {
            s.stepPhase = 0;
            actorsSweep(kSweepBase, kSweepKeep);
            clearPlayerAnglesAndFaces();
            placePlayer(0xfe700000u, 0, 0, 0xc00);
            playerAnim(0x8a);
        } else if (t == 0x18) {
            if (const uint32_t address = allocActorE0()) { // exe 0x5e1440: the base's alarm
                ControllerActor& alarm = controllerActorAt(address);
                alarm.flags = kActorLive;
                alarm.type = kAlarmType;
            }
        } else if (t == 0x1e) {
            cue(s.actors[kFreedGuide], 1);
            playerAnim(0x8b);
            mouthKey(5);
        }
        return;
    case kStepRunOff:
        if (t == 0) {
            s.stepPhase = uint8_t(RunOffPhase::None);
            const ActorRef<ActorHeader> guide = s.actors[kFreedGuide];
            cue(guide, 2);
            actorLayoutOf(*guide).rotation.y = 0x400;
            p.rotation.y ^= kTurnAround;
            p.body.key.flags = 0;
        } else if (t == 0xf) {
            s.stepPhase = uint8_t(RunOffPhase::Start);
            cue(s.actors[kFreedGuide], 3);
        }
        switch (RunOffPhase(s.stepPhase)) {
        case RunOffPhase::Start:
            s.stepPhase = uint8_t(RunOffPhase::Turn);
            playerAnim(8);
            return;
        case RunOffPhase::Turn:
            if (playerAnimEnded()) {
                s.stepPhase = uint8_t(RunOffPhase::Run);
                playerAnim(1);
            }
            integrateVelocity(p, 0, 0, -0x130);
            return;
        case RunOffPhase::Run: integrateVelocity(p, 0, 0, -0x130); return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x24 step", routine.pointerValue());
    }
}

} // namespace

void brotherFreedScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x522f50
        switch (FreedSetUp(s.subState)) {
        case FreedSetUp::Start:
            s.subState = uint8_t(FreedSetUp::Place);
            s.flags |= kScriptUnskippable;
            [[fallthrough]];
        case FreedSetUp::Place:
            ++s.subState;
            actorsSweep(kSweepBase, kSweepKeep);
            scriptReplayPadOn(); // exe 0x5117f0
            scriptSequenceStart(stateAddress(TableId::GlydeBaseScriptsFreedCamera), scriptStepList<TableId::GlydeBaseScriptsFreedSteps>);
            scriptSequenceTick();
            clearPlayerAnglesAndFaces();
            placePlayer(0xfec40000u, 0, 0, 0xc00);
            [[fallthrough]];
        case FreedSetUp::TakeOver:
            if (playerToScriptIfFree() != 0) return; // exe 0x572300
            ++s.subState;
            startFaceTracks(stateAddress(TableId::GlydeBaseScriptsFreedEyes), stateAddress(TableId::GlydeBaseScriptsFreedMouth));
            playerAnim(0x83);
            return;
        case FreedSetUp::Voice:
            voiceClipPlay(0xc);
            ++s.subState;
            return;
        case FreedSetUp::WaitVoice:
            if (voiceClipPlaying()) {
                s.enterState(CutsceneState::Steps);
                s.flags &= uint8_t(~kScriptUnskippable);
            }
            return;
        default: return;
        }
    case CutsceneState::Steps: scriptStepsStateAnyRoutine(brotherFreedStep); return; // exe 0x51bc90
    case CutsceneState::WrapUp:                                                       // exe 0x523070
        if (!voiceFadedAndIdle(s)) return;
        switch (WrapUpSub(s.subState)) {
        case WrapUpSub::Leave:
            ++s.subState;
            requestTransition(kTransitionAccepted, kBaseStage, 2, 0x1e0, 0, 0xa80, 0x400, kNoFade, kNoFade);
            scriptFade(0x10, 0);
            return;
        case WrapUpSub::End:
            if (fadeState().busy != 0 || transitionRequest().state != 0) return;
            playerBackToPlay();   // exe 0x5723c0
            scriptReplayPadOff(); // exe 0x511890
            actorsSweep(0x6fe, kSweepKeep);
            scriptEndBlock();
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x24 state", s.state);
    }
}

namespace {

// ==== script 0x25 ========================================================================================================
enum class RescueSetUp : uint8_t { Start = 0, Place = 1, TakeOver = 2, Run = 3 };
constexpr RoutineId kStepWalkIn = RoutineId::GlydeBaseScriptsStepWalkIn,
                    kStepHeard = RoutineId::GlydeBaseScriptsStepHeard,
                    kStepInside = RoutineId::GlydeBaseScriptsStepInside,
                    kStepFound = RoutineId::GlydeBaseScriptsStepFound,
                   kStepTalk = RoutineId::GlydeBaseScriptsStepTalk;
constexpr size_t kRescueGuard = 0, kRescued = 2, kInsideA = 4, kInsideB = 5;
constexpr uint32_t kRescueX = 0x1e00000, kRescueZ = 0xac00000;
constexpr int16_t kRescueYaw = 0x260;
enum class WalkPhase : uint8_t { Walking = 0, Stopped = 1 };

// the step's walk: -0xf0 a tick until the step stops it
void rescueWalk(ScriptState& s) {
    if (WalkPhase(s.stepPhase) == WalkPhase::Walking) integrateVelocity(player(), 0, 0, -0xf0);
}
// the in-stage warp to the base's sub-room at the player's place (the yaw not written), the next step
void warpInBase(uint8_t room) {
    TransitionRequest& r = transitionRequest();
    const Player& p = player();
    r.destination.x = p.pos.x.whole;
    r.state = kTransitionWarp;
    r.keepPosition = 0;
    r.exitId = 0;
    r.exitByte1 = 0;
    r.destStage = kBaseStage;
    r.destRoom = room;
    r.destination.y = p.pos.y.whole;
    r.destination.z = p.pos.z.whole;
    r.fadeOut = kNoFade;
    r.fadeIn = kNoFade;
    scriptSequenceSkip(1);
}
void rescueStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = s.stepTick;
    Player& p = player();
    switch (routine.id()) {
    case kStepWalkIn:
        if (t == 0) s.stepPhase = uint8_t(WalkPhase::Walking);
        rescueWalk(s);
        return;
    case kStepHeard:
        switch (t) {
        case 0:
            s.stepPhase = uint8_t(WalkPhase::Walking);
            soundPlayFlatForced(0x29);
            break;
        case 10: scriptActorSpawnRecord(stateAddress(TableId::GlydeBaseScriptsHeardRecord)); break;
        case 20:
            s.stepPhase = uint8_t(WalkPhase::Stopped);
            playerAnim(0x8f);
            break;
        case 25: cue(s.actors[kRescueGuard], 1); break;
        case 0x4b: warpInBase(4); break;
        default: break;
        }
        rescueWalk(s);
        return;
    case kStepInside:
        switch (t) {
        case 0: p.flags ^= kActorDrawn; return;
        case 10: cue(s.actors[kInsideA], 1); return;
        case 35: cue(s.actors[kInsideA], 2); return;
        case 60: cue(s.actors[kInsideB], 1); return;
        case 0x78: warpInBase(2); return;
        default: return;
        }
    case kStepFound: {
        if (t != 0) return;
        s.stepPhase = uint8_t(WalkPhase::Walking);
        p.flags ^= kActorDrawn;
        playerAnim(0x90);
        scriptActorSpawnRecord(stateAddress(TableId::GlydeBaseScriptsFoundRecordA));
        scriptActorSpawnRecord(stateAddress(TableId::GlydeBaseScriptsFoundRecordB));
        int32_t o[3];
        rotateByYaw(p.rotation.y, 0x7f0, 0, 0x230, o); // exe 0x5e38d0
        Actor& rescued = actorLayoutOf(*s.actors[kRescued]);
        rescued.pos.x = int32_t(uint32_t(p.pos.x) + uint32_t(o[0]));
        rescued.pos.y = p.pos.y;
        rescued.pos.z = int32_t(uint32_t(p.pos.z) + uint32_t(o[2]));
        rescued.rotation = p.rotation;
        return;
    }
    case kStepTalk: {
        const ActorRef<ActorHeader> rescued = s.actors[kRescued];
        switch (t) {
        case 5:
            playerAnim(0x91);
            faceKeys(1);
            return;
        case 45:
        case 102: mouthKey(2); return;
        case 87:
        case 137: mouthKey(0); return;
        case 105: cue(rescued, 1); return;
        case 0x91: cue(rescued, 2); return;
        case 271: playerAnim(0x92); return;
        case 291: playerAnim(0x93); return;
        case 321:
            playerAnim(0x94);
            cue(rescued, 3);
            return;
        case 351:
            playerAnim(0x95);
            cue(rescued, 4);
            return;
        case 411: playerAnim(0x96); return;
        case 450: cue(rescued, 5); return;
        default: return;
        }
    }
    default: notPortedAt(kStoryPort, "script 0x25 step", routine.pointerValue());
    }
}

} // namespace

void rescueScript() {
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x523610
        switch (RescueSetUp(s.subState)) {
        case RescueSetUp::Start:
            if (fadeState().busy != 0) return;
            ++s.subState;
            s.flags |= kScriptUnskippable;
            voiceClipPlay(0xf);
            actorsSweep(kSweepBase, kSweepKeep);
            scriptReplayPadOn();
            return;
        case RescueSetUp::Place: {
            if (!voiceClipPlaying()) return;
            scriptFade(2, 0);
            ++s.subState;
            scriptSequenceStart(stateAddress(TableId::GlydeBaseScriptsRescueCamera), scriptStepList<TableId::GlydeBaseScriptsRescueSteps>);
            scriptSequenceTick();
            clearPlayerAnglesAndFaces();
            Player& p = player();
            p.pos.y = 0;
            p.pos.x = int32_t(kRescueX);
            p.pos.z = int32_t(kRescueZ);
            p.rotation.y = kRescueYaw;
            return;
        }
        case RescueSetUp::TakeOver:
            if (playerToScriptIfFree() != 0) return;
            ++s.subState;
            playerAnim(0x8e);
            startFaceTracks(stateAddress(TableId::GlydeBaseScriptsRescueEyes), stateAddress(TableId::GlydeBaseScriptsRescueMouth));
            [[fallthrough]];
        case RescueSetUp::Run:
            s.enterState(CutsceneState::Steps);
            s.flags &= uint8_t(~kScriptUnskippable);
            return;
        default: return;
        }
    case CutsceneState::Steps: scriptStepsStateAnyRoutine(rescueStep); return; // exe 0x51bc90
    case CutsceneState::WrapUp: {                                               // exe 0x523750
        if (WrapUpSub(s.subState) == WrapUpSub::Fade) {
            if (!(s.flags & kScriptSkipTaken)) scriptFade(0x10, 0);
            if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade);
            ++s.subState;
            return;
        }
        if (!voiceFadedAndIdle(s)) return;
        if (WrapUpSub(s.subState) != WrapUpSub::Leave) return;
        requestTransition(kTransitionAccepted, kBaseStage, 2, 0x1e0, 0, 0xac0, kRescueYaw, 0, kNoFade);
        actorsSweep(kSweepAll, kSweepKeep);
        playerBackToPlay();   // exe 0x5723c0
        scriptReplayPadOff(); // exe 0x511890
        Player& p = player();
        p.pos.x = int32_t(kRescueX);
        p.flags |= kActorDrawn;
        p.pos.y = 0;
        p.pos.z = int32_t(kRescueZ);
        p.rotation.y = kRescueYaw;
        scriptEndBlock();
        return;
    }
    default: notPortedAt(kStoryPort, "script 0x25 state", s.state);
    }
}

} // namespace od::port
