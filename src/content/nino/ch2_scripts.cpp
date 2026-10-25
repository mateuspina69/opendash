// Story chapter 2 after the Manda key: the cutscene scripts (see content/nino/ch2_scripts.hpp), one routine per exe
// routine; their rooms start them (content/nino/ch2_rooms). Each script's states table: the set-up, the steps runner
// (exe 0x52f420 without / exe 0x53a5e0 with the player's face tracks), the wrap-up.
//   0x31 (exe 0x527810, states 0x8a5f58; camera 0x8a5d28, steps 0x8a5f30): ST46 sub 0 at story 5, once (flag 0x24) --
//        the Flutter leaves the Sulphur-Bottom. Steps 0x527a60 (animation 0, the warp to ST46 sub 1 yaw 0xc00 at 0x95),
//        0x527ae0 (the warp to sub 2 at 0x115), 0x527b40 (sounds 0xb1 / 0x8b, the warp to sub 1 at 0xb3), 0x527c20 (the
//        puffs, fade 0x12 at 0xf9). Ends with request 2 -> ST17 sub 0 (0x540, -0x800, 0) yaw 0x70.
//   0x3e (exe 0x52b910, states 0x8a78f8; camera 0x8a77e8, steps 0x8a78d8): ST17 sub 0 at story 6 -- the landing on the
//        island. The player at (-0xaca, -0x801, -0x7f0) yaw 0x800, animation 0x80, voice 0xf; steps 0x52bb50, 0x52bb70
//        (walks while its key is below 0x19; animations 0x82 / 0x83 at 0x1b8 / 0x2a6), 0x52bc10 (hidden at 0; fade at
//        399). Ends with request 2 -> ST19 sub 0 (-0x280, -0x801, -0x680) yaw 0x800.
//   0x32 (exe 0x527d30, states 0x8a6180; camera 0x8a604c, steps 0x8a6158): ST19 subs 1 / 3 at story 6 (flag 0x25) --
//        the city's gate. Steps 0x527ef0 (DAT 0x9f, the bank switched, voice 0x10), 0x527f90, 0x527fa0 (the warp to
//        ST19 sub 2 at 0x4a), 0x528000 (fade at 0x252). Ends with request 2 -> ST1B sub 2 (-0x100, -1, 0) yaw 0xc00.
//   0x34 (exe 0x528310, states 0x8a63b0; camera 0x8a62d8, steps 0x8a6390): ST16, flag 0x27 -- the crane mission done;
//        a flying point (the gate lock's flag 0x690), face keys; ending -> ST19 sub 0.
//   0x35 (exe 0x5287f0, states 0x8a65c0; camera 0x8a64b8, steps 0x8a65a0): ST15, flag 0x28 -- the cannon repaired; the
//        cell (0x3d, 0x3a) to variant 1, a flying point, the jump and the run away; ending -> ST19 sub 0.
//   0x3d (exe 0x52b7c0, states 0x8a7760; camera 0x8a76b0, steps 0x8a7748): ST17 sub 0 on a visit after the landing --
//        the mission's look-around; the wrap-up spawns the mission's cast (records 0x8a75fc x 9).
//   0x36 (exe 0x528e30, states 0x8a6740; camera 0x8a6660, steps 0x8a6728): ST17, flag 0x29 -- the enemy ships beaten;
//        ending -> ST19 sub 0 (-0x280, -0x801, -0x680) yaw 0x800.
//   0x37 (exe 0x529270, states 0x8a69b0; camera 0x8a6854, steps 0x8a6988): ST18 sub 0, flag 0x2a -- the ship fight
//        survived: warps between ST18's subs, the script actors, the player's spark burst between its bones 6 / 7.
//   0x38 (exe 0x529890, states 0x8a6a50; camera 0x8a69e8, steps 0x8a6a40): ST18 sub 0 at story 7, flag 0x2b -- the
//        gunship shot down; ending -> ST19 sub 0 (0x300, -0x800, 0x680), flag 0x7b8.
#include "content/nino/ch2_scripts.hpp"
#include "content/nino/ch2_rooms.hpp"
#include "content/nino/fly_point.hpp"

#include "engine/routine_ids.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/encounter.hpp"
#include "game/fade_state.hpp"
#include "game/field_alias.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/message.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/room_grid.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spawn.hpp"
#include "game/stage_files.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"
#include "game/dat_names.hpp"

namespace od::port {

namespace {

constexpr uint32_t kVoiceFade = 8;
constexpr uint8_t kStepsFade = 0x12, kShownFade = 2, kSkipArrivalFade = 1;
constexpr uint8_t kSt17 = 0x17, kSt18 = 0x18, kSt19 = 0x19, kSt1b = 0x1b, kSt46 = 0x46;

// ---- the shared parts ------------------------------------------------------------------------------------------
int32_t tick() { return scriptState().stepTick; }
bool skipTaken() { return (scriptState().flags & kScriptSkipTaken) != 0; }
// the request's fade out: none when the skip was taken
uint8_t wrapUpFade() { return skipTaken() ? kNoFade : kStepsFade; }
// the player's animation keys
bool playerAnimEnded() { return (player().body.key.flags & kKeyJump) != 0; }
uint8_t playerKey() { return player().body.keyIndex; }
void playerAnim(uint8_t anim) { playerScriptAnimId(anim, 0, 1); } // exe 0x5723d0(anim, 0, 1)
// an in-stage warp to a sub-room (no fades), the player at (0, y, 0) facing yaw
void warpTo(uint8_t stage, uint8_t room, int16_t y = 0, int16_t yaw = 0) {
    requestTransition(kTransitionWarp, stage, room, 0, y, 0, yaw, kNoFade, kNoFade);
}
// the player's move directions' angles and its idle faces cleared, the player placed (16.16) facing yaw
void clearPlayerAnglesAndFaces() {
    Player& p = player();
    p.clearDirectionOffsetWords();
    p.idleFaces[0] = 0;
    p.idleFaces[1] = 0;
}
void placePlayer(uint32_t x, uint32_t y, uint32_t z, int16_t yaw) {
    Player& p = player();
    p.pos.x = int32_t(x);
    p.pos.y = int32_t(y);
    p.pos.z = int32_t(z);
    p.rotation.y = yaw;
}
// the face tracks on their tables, both on sequence 0
void startFaceTracks(uint32_t eyes, uint32_t mouth) {
    KeyframeTracks& t = scriptState().faceTracks;
    t.init(eyes, mouth);  // exe 0x5e33a0
    t.first.select(0, 0); // exe 0x5e33d0
    t.second.select(0, 0); // exe 0x5e33f0
}
KeyframeTrack& eyes() { return scriptState().faceTracks.first; }
KeyframeTrack& mouth() { return scriptState().faceTracks.second; }
// the set-up's start of most scripts: the actors swept, the sequence, the replay pad, unskippable
void setUpStart(uint32_t camera, uint32_t steps) {
    actorsSweep(kSweepActors, kSweepFree);
    scriptSequenceStart(camera, steps);
    scriptReplayPadOn(); // exe 0x5117f0
    scriptState().flags |= kScriptUnskippable;
}
// the end of a wrap-up: the player back to its play (not airborne), the block ended
void playerBack() {
    playerBackToPlay();   // exe 0x5723c0
    scriptReplayPadOff(); // exe 0x511890
    player().airborne = 0;
}
bool requestDone() { return transitionRequest().state == 0 && fadeState().busy == 0; }

// The set-up's steps (the script block's sub-state) and the wrap-up's
enum class SetUp : uint8_t { Start = 0, TakeOver = 1, Voice = 2, WaitVoice = 3 };
enum class WrapUp : uint8_t { FadeVoice = 0, WaitVoice = 1, Request = 2, End = 3 };
// the wrap-up's voice faded, then idle (true once done)
void fadeVoiceStep(ScriptState& s) {
    if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFade); // exe 0x601220
    s.subState += 1u;
}

// ---- 0x31 / 0x3e: the set-up (exe 0x527830 / 0x52b930) and the wrap-up (exe 0x527950 / 0x52ba50) they share --------
struct Opening {
    uint32_t camera, steps;
    bool hidePlayer;
    uint32_t x, y, z;
    int16_t yaw;
    uint8_t anim, voice;
};
// the actors swept, the sequence, unskippable, the player placed, the script's bytes cleared; once script driven its
// animation, then the voice; once it plays the cast released, skippable, fade 2, the steps
void flightOpening(const Opening& o) {
    ScriptState& s = scriptState();
    switch (SetUp(s.subState)) {
    case SetUp::Start:
        setUpStart(o.camera, o.steps);
        if (o.hidePlayer) player().setDrawn(false);
        clearPlayerAnglesAndFaces();
        placePlayer(o.x, o.y, o.z, o.yaw);
        s.clearScriptByteRun(); // exe 0x40dc40(+0xc, +0x18)
        scriptSequenceTick();
        s.subState = uint8_t(SetUp::TakeOver);
        [[fallthrough]];
    case SetUp::TakeOver:
        if (playerToScriptIfFree() == 0) {
            playerAnim(o.anim);
            s.subState += 1u;
        }
        return;
    case SetUp::Voice:
        voiceClipPlay(o.voice);
        s.subState += 1u;
        return;
    case SetUp::WaitVoice:
        if (voiceClipPlaying()) {
            s.part<ScriptCastStart>().started = 1;
            s.flags &= uint8_t(~kScriptUnskippable);
            s.enterState(CutsceneState::Steps);
            scriptFade(kShownFade, 0);
        }
        return;
    default: return;
    }
}
// the voice faded, then request 2 (fade out 0x12 unless skipped); once taken the player back and shown, the block ended
void flightEnding(uint8_t stage, int16_t x, int16_t y, int16_t z, int16_t yaw, uint8_t fadeIn) {
    ScriptState& s = scriptState();
    switch (WrapUp(s.subState)) {
    case WrapUp::FadeVoice:
        fadeVoiceStep(s);
        [[fallthrough]];
    case WrapUp::WaitVoice:
        if (!voiceClipPlaying()) s.subState += 1u;
        return;
    case WrapUp::Request:
        requestTransition(kTransitionAccepted, stage, 0, x, y, z, yaw, fadeIn, wrapUpFade());
        s.subState += 1u;
        return;
    case WrapUp::End:
        if (transitionRequest().state == 0) {
            playerBack();
            player().setDrawn(true);
            scriptEndBlock();
        }
        return;
    default: return;
    }
}

// ---- script 0x31: the take-off from the Sulphur-Bottom ------------------------------------------------------------
constexpr RoutineId kTakeOffStart = RoutineId::Ch2ScriptsTakeOffStart, kTakeOffFly = RoutineId::Ch2ScriptsTakeOffFly,
                    kTakeOffEngines = RoutineId::Ch2ScriptsTakeOffEngines,
                   kTakeOffPuffs = RoutineId::Ch2ScriptsTakeOffPuffs;
// the puffs' places and drifts (exe data: s16 points, s16 x4 words)
constexpr uint8_t kPuffType = 0x11;
// The puff's spawn parameters (the stage's pool-0x40 type 0x11): its life and its age
struct PuffParams {
    explicit PuffParams(const ActorHeader& actor) : PuffParams(const_cast<ActorHeader&>(actor)) {}
    explicit PuffParams(ActorHeader& actor) : life(actor.params, 0), age(actor.params, 2) {}
    BytesField<uint16_t, 4> life;
    BytesField<uint16_t, 4> age;
};
// exe 0x527cb0(place, drift): a puff (pool 0x40 stage type 0x11) at the place (<< the stage shift), the drift in its
// rotation words
void puff(uint32_t place, uint32_t drift) {
    const uint32_t a = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!a) return;
    ProjectileActor& p = projectileActorAt(a);
    const ShortVec3 at = dataShortVec3(place);
    const ShortVec4 d = dataShortVec4(drift);
    p.loopFlags |= kActorSweepGroup2;
    p.flags = kActorLive;
    p.type = kPuffType;
    p.variant = 0;
    const uint32_t shift = stage().coordinateShift & 0x1fu;
    p.pos.x.whole = int16_t(uint16_t(uint32_t(int32_t(at.x)) << shift));
    p.pos.y.whole = int16_t(uint16_t(uint32_t(int32_t(at.y)) << shift));
    p.pos.z.whole = int16_t(uint16_t(uint32_t(int32_t(at.z)) << shift));
    p.rotation.setWords(d.xyWord(), d.zSpareWord());
    PuffParams params(actorHeaderAt(a));
    params.life = 0x2d;
    params.age = 0;
}
void takeOffStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = tick();
    switch (routine.id()) {
    case kTakeOffStart:
        if (s.stepPhase == 0) {
            playerAnim(0);
            s.stepPhase += 1u;
        }
        if (t == 0x95) warpTo(kSt46, 1, 0, 0xc00);
        return;
    case kTakeOffFly:
        if (t == 0x115) warpTo(kSt46, 2);
        return;
    case kTakeOffEngines:
        switch (t) {
        case 0x72: case 0x74: case 0x76: case 0x78: case 0x7a: case 0x7b: case 0x7c: case 0x7e: case 0x80:
            soundPlayFlat(0xb1);
            return;
        case 0x9e: soundPlayFlat(0x8b); return;
        case 0xb3: warpTo(kSt46, 1, 0, 0xc00); return;
        default: return;
        }
    case kTakeOffPuffs: {
        if (t <= 0x59) { // every 4th frame (then 8th) a puff of the first engine, the second's in between
            const uint8_t frame = uint8_t(gameState().frame);
            uint8_t c;
            if (t < 0x3c) {
                if ((frame & 3) == 0) puff(stateAddress(TableId::Ch2ScriptsPuffPlaceA), stateAddress(TableId::Ch2ScriptsPuffDriftA));
                c = uint8_t((frame + 1) & 3);
            } else {
                if ((frame & 7) == 0) puff(stateAddress(TableId::Ch2ScriptsPuffPlaceA), stateAddress(TableId::Ch2ScriptsPuffDriftA));
                c = frame & 0xf;
            }
            if (c == 0) puff(stateAddress(TableId::Ch2ScriptsPuffPlaceB), stateAddress(TableId::Ch2ScriptsPuffDriftB));
        }
        if (t == 0xf9) scriptFade(kStepsFade, 0);
        return;
    }
    default: notPortedAt(kStoryPort, "script 0x31 step", routine.pointerValue());
    }
}
void takeOffScript() { // exe 0x527810
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: flightOpening({stateAddress(TableId::Ch2ScriptsFlightToCityCamera), stateAddress(TableId::Ch2ScriptsFlightToCitySteps), true, 0, 0xffff0000u, 0, 0, 0, 0xd}); return;
    case CutsceneState::Steps: scriptStepsStateNoFaces(&takeOffStep); return; // exe 0x52f420
    case CutsceneState::WrapUp: flightEnding(kSt17, 0x540, int16_t(0xf800), 0, 0x70, kNoFade); return;
    default: notPortedAt(kStoryPort, "script 0x31 state", s.state);
    }
}

// ---- script 0x3e: the landing on the island ------------------------------------------------------------------------
constexpr RoutineId kLandingStand = RoutineId::Ch2ScriptsLandingStand, kLandingWalk = RoutineId::Ch2ScriptsLandingWalk,
                    kLandingLeave = RoutineId::Ch2ScriptsLandingLeave;
enum class LandingWalk : uint8_t { Start = 0, Walk = 1, Look = 2 };
void landingStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    Player& p = player();
    switch (routine.id()) {
    case kLandingStand:
        playerAnim(0x81);
        s.clearStepPhase();
        return;
    case kLandingWalk:
        switch (LandingWalk(s.stepPhase)) {
        case LandingWalk::Start:
            playerAnim(0x81);
            s.stepPhase += 1u;
            [[fallthrough]];
        case LandingWalk::Walk:
            if (int8_t(playerKey()) < 0x19) moveAlongYaw(p.pos, p.rotation.y, -0xe, 0, -0x22); // exe 0x5e36f0
            if (tick() == 0x1b8) {
                playerAnim(0x82);
                s.stepPhase += 1u;
            }
            return;
        case LandingWalk::Look:
            if (tick() == 0x2a6) {
                playerAnim(0x83);
                s.stepPhase += 1u;
            }
            return;
        default: return;
        }
    case kLandingLeave:
        if (tick() == 0) {
            p.setDrawn(false);
        } else if (tick() == 399) {
            s.flags |= kScriptUnskippable;
            scriptFade(kStepsFade, 0);
        }
        return;
    default: notPortedAt(kStoryPort, "script 0x3e step", routine.pointerValue());
    }
}
void landingScript() { // exe 0x52b910
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp:
        flightOpening({stateAddress(TableId::Ch2ScriptsFlightToShipsCamera), stateAddress(TableId::Ch2ScriptsFlightToShipsSteps), false, 0xf5360000u, 0xf7ff0000u, 0xf8100000u, 0x800, 0x80, 0xf});
        return;
    case CutsceneState::Steps: scriptStepsStateNoFaces(&landingStep); return;
    case CutsceneState::WrapUp:
        flightEnding(kSt19, int16_t(0xfd80), int16_t(0xf7ff), int16_t(0xf980), 0x800, kShownFade);
        return;
    default: notPortedAt(kStoryPort, "script 0x3e state", s.state);
    }
}

// ---- script 0x32: the city's gate ----------------------------------------------------------------------------------
constexpr RoutineId kGateBank = RoutineId::Ch2ScriptsGateBank, kGateSkippable = RoutineId::Ch2ScriptsGateSkippable,
                    kGateWarp = RoutineId::Ch2ScriptsGateWarp, kGateFade = RoutineId::Ch2ScriptsGateFade;
enum class GateBank : uint8_t { Load = 0, Switch = 1, Voice = 2, WaitVoice = 3 };
bool fileLoading() { return (fileLoads().loading & kFileLoading) != 0; }
void cityGateStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = tick();
    switch (routine.id()) {
    case kGateBank:
        switch (GateBank(s.stepPhase)) {
        case GateBank::Load:
            if (!voiceClipPlaying()) {
                datLoadRequest(DatFile{"ST1901.DAT"}); // exe 0x40efe0
                s.stepPhase += 1u;
            }
            return;
        case GateBank::Switch:
            if (!fileLoading() && t > 0x3c) {
                switchStageBank(); // exe 0x5f3200
                s.stepPhase += 1u;
            }
            return;
        case GateBank::Voice:
            voiceClipPlay(0x10);
            s.stepPhase += 1u;
            return;
        case GateBank::WaitVoice:
            if (voiceClipPlaying()) scriptSequenceSkip(1);
            return;
        default: return;
        }
    case kGateSkippable: s.flags &= uint8_t(~kScriptUnskippable); return;
    case kGateWarp:
        if (t == 0x4a) warpTo(kSt19, 2);
        return;
    case kGateFade:
        if (t == 0x252) scriptFade(kStepsFade, 0);
        return;
    default: notPortedAt(kStoryPort, "script 0x32 step", routine.pointerValue());
    }
}
void cityGateScript() { // exe 0x527d30
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x527d50
        if (s.subState == 0) {
            setUpStart(stateAddress(TableId::Ch2ScriptsCityGateCamera), stateAddress(TableId::Ch2ScriptsCityGateSteps));
            player().setDrawn(false);
            clearPlayerAnglesAndFaces();
            placePlayer(0, 0xffff0000u, 0, 0);
            s.subState += 1u;
        } else if (s.subState != 1) {
            return;
        }
        if (playerToScriptIfFree() == 0) {
            s.enterState(CutsceneState::Steps);
            playerAnim(0);
        }
        return;
    case CutsceneState::Steps: scriptStepsStateNoFaces(&cityGateStep); return;
    case CutsceneState::WrapUp: // exe 0x527e00
        switch (WrapUp(s.subState)) {
        case WrapUp::FadeVoice:
            fadeVoiceStep(s);
            [[fallthrough]];
        case WrapUp::WaitVoice:
            if (!voiceClipPlaying()) s.subState += 1u;
            return;
        case WrapUp::Request:
            requestTransition(kTransitionAccepted, kSt1b, 2, int16_t(0xff00), int16_t(0xffff), 0, 0xc00, kShownFade,
                              kNoFade);
            s.subState += 1u;
            return;
        case WrapUp::End:
            if (requestDone()) {
                playerBack();
                player().setDrawn(true);
                scriptEndBlock();
            }
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x32 state", s.state);
    }
}

// ---- the missions' ends: scripts 0x34 / 0x35 (the runner with the face tracks); both end with request 2 -> ST19 sub 0
// (the city, the next gate lock open)
// a flying point (pool 0xe0 type 0x1c) from one point to another in `ticks`, its flag at the end
uint32_t flyPoint(int16_t x0, int16_t y0, int16_t z0, int16_t x1, int16_t y1, int16_t z1, int16_t ticks, int16_t flag) {
    const uint32_t a = allocActorE0(); // exe 0x5e1440
    if (!a) return 0;
    ControllerActor& point = controllerActorAt(a);
    auto&& fly = classState<FlyPointFields>(controllerActorAt(a));
    point.flags = kActorLive;
    point.type = kFlyPointType;
    fly.from.x = x0;
    fly.from.y = y0;
    fly.from.z = z0;
    fly.to.x = x1;
    fly.to.y = y1;
    fly.to.z = z1;
    fly.ticks = ticks;
    fly.flag = flag;
    return a;
}
constexpr int16_t kFlagGateLock = 0x690;
// exe 0x528490 / 0x528930: the voice faded, request 2 -> ST19 sub 0 (animation 0 first unless skipped, for 0x35);
// once taken the actors swept, the player back, the block ended
void missionEnding(int16_t x, int16_t y, int16_t z, int16_t yaw, bool standFirst) {
    ScriptState& s = scriptState();
    switch (WrapUp(s.subState)) {
    case WrapUp::FadeVoice:
        fadeVoiceStep(s);
        [[fallthrough]];
    case WrapUp::WaitVoice:
        if (!voiceClipPlaying()) s.subState += 1u;
        return;
    case WrapUp::Request: {
        const bool skipped = skipTaken();
        if (standFirst && !skipped) playerAnim(0);
        requestTransition(kTransitionAccepted, kSt19, 0, x, y, z, yaw, kShownFade, skipped ? kNoFade : kStepsFade);
        s.subState += 1u;
        return;
    }
    case WrapUp::End:
        if (requestDone()) {
            actorsSweep(kSweepActors, kSweepFree);
            playerBack();
            scriptEndBlock();
        }
        return;
    default: return;
    }
}

// script 0x34 (ST16: the crane mission done): steps 0x5285a0 (the cast unless enemies 1 / 2 are beaten, animation
// 0x83), 0x528600 (animation 0x83, the idle face 0x14), 0x528620 (a flying point at 6, face keys, fade at 0x167); the
// doors of the cell (0x43, 0x40) shown; voice 0; ending -> ST19 sub 0 (-0x680, -0x800, -0x580) yaw 0xc00
constexpr RoutineId kCraneCast = RoutineId::Ch2ScriptsCraneCast, kCraneFace = RoutineId::Ch2ScriptsCraneFace,
                    kCraneEnd = RoutineId::Ch2ScriptsCraneEnd;
void craneDoneStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    switch (routine.id()) {
    case kCraneCast:
        if (s.stepPhase == 0) {
            if (!enemyDefeated(1)) spawnRecordTracked(stateAddress(TableId::Ch2ScriptsCraneFirstEnemyRecord));
            if (!enemyDefeated(2)) spawnRecordTracked(stateAddress(TableId::Ch2ScriptsCraneSecondEnemyRecord));
            s.stepPhase += 1u;
        }
        playerAnim(0x83);
        return;
    case kCraneFace:
        playerAnim(0x83);
        player().idleFaces[0] = 0x14;
        return;
    case kCraneEnd: {
        const int32_t t = tick();
        switch (t) {
        case 6:
            flyPoint(int16_t(0xfcc8), int16_t(0xf767), 0x1c8, int16_t(0xf970), int16_t(0xf300), int16_t(0xfa67), 0xc,
                     kFlagGateLock);
            break;
        case 0x32: mouth().select(1, 0); break;
        case 100: mouth().select(0, 0); break;
        case 0x78:
            eyes().select(2, 0);
            mouth().select(2, 0);
            break;
        case 0xd2: eyes().select(4, 0); break;
        case 0x136:
            eyes().select(3, 0);
            mouth().select(3, 0);
            break;
        case 0x152: mouth().select(2, 0); break;
        case 0x167: scriptFade(kStepsFade, 0); break;
        default: break;
        }
        if (t < 0x78) playerAnim(0x83);
        return;
    }
    default: notPortedAt(kStoryPort, "script 0x34 step", routine.pointerValue());
    }
}
void craneDoneScript() { // exe 0x528310
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x528330
        switch (SetUp(s.subState)) {
        case SetUp::Start:
            actorsSweep(kSweepActors, kSweepFree);
            windowClose(4, WindowClose::Force);
            scriptSequenceStart(stateAddress(TableId::Ch2ScriptsCraneDoneCamera), stateAddress(TableId::Ch2ScriptsCraneDoneSteps));
            scriptReplayPadOn();
            s.flags |= kScriptUnskippable;
            clearPlayerAnglesAndFaces();
            placePlayer(0x4800000u, 0xf7ff0000u, 0xff400000u, 0xf00);
            segmentSetDoorsHidden(ninoGridCell(0x43, 0x40), true); // exe 0x600f50
            scriptSequenceTick();
            s.subState += 1u;
            [[fallthrough]];
        case SetUp::TakeOver:
            if (playerToScriptIfFree() == 0) {
                playerAnim(0);
                startFaceTracks(stateAddress(TableId::Ch2ScriptsCraneDoneEyes), stateAddress(TableId::Ch2ScriptsCraneDoneMouth));
                s.subState += 1u;
            }
            return;
        case SetUp::Voice:
            voiceClipPlay(0);
            s.subState += 1u;
            return;
        case SetUp::WaitVoice:
            if (voiceClipPlaying()) {
                s.enterState(CutsceneState::Steps);
                s.flags &= uint8_t(~kScriptUnskippable);
            }
            return;
        default: return;
        }
    case CutsceneState::Steps: scriptStepsState(&craneDoneStep); return; // exe 0x53a5e0
    case CutsceneState::WrapUp: missionEnding(int16_t(0xf980), int16_t(0xf800), int16_t(0xfa80), 0xc00, false); return;
    default: notPortedAt(kStoryPort, "script 0x34 state", s.state);
    }
}

// script 0x35 (ST15: the cannon repaired): steps 0x528a60 (skippable + animation 0x80 at 0 / 1, face keys, animation
// 0x81 at 0x14a), 0x528c50 (at 0 the cell (0x3d, 0x3a) to variant 1, yaw + 0x800, face keys 0), 0x528cd0 (a flying point
// at 0x11, animation 0x82, then at 0xe6 the face, the key-8 wait, the jump (animation 1 at its end) and the run along
// the yaw, fade at 0x10d); voice 3; ending -> ST19 sub 0 (0x680, -0x800, 0x180) yaw 0x400 (animation 0 unless skipped)
constexpr RoutineId kCannonTalk = RoutineId::Ch2ScriptsCannonTalk, kCannonTurn = RoutineId::Ch2ScriptsCannonTurn,
                    kCannonLeave = RoutineId::Ch2ScriptsCannonLeave;
enum class CannonLeave : uint8_t { Wait = 0, Go = 1 };
enum class CannonGo : uint8_t { Talk = 0, Crouch = 1, Jump = 2, Run = 3 };
void cannonDoneStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    Player& p = player();
    const int32_t t = tick();
    switch (routine.id()) {
    case kCannonTalk:
        switch (t) {
        case 0: case 1:
            s.flags &= uint8_t(~kScriptUnskippable);
            playerAnim(0x80);
            return;
        case 0x6e:
            eyes().select(2, 0);
            mouth().select(2, 0);
            return;
        case 0x91: case 0xfc: case 0x136: mouth().select(3, 0); return;
        case 0xf0:
            eyes().select(3, 0);
            mouth().select(1, 0);
            return;
        case 0x104: mouth().select(1, 0); return;
        case 0x14a:
            playerAnim(0x81);
            eyes().select(1, 0);
            return;
        default: return;
        }
    case kCannonTurn:
        if (t == 0) {
            switchCellSegmentVariant(ninoGridCell(0x3d, 0x3a), 1);
            s.clearStepPhase();
            p.rotation.y = int16_t(uint16_t(p.rotation.y) + 0x800u);
            eyes().select(0, 0);
            mouth().select(0, 0);
        }
        return;
    case kCannonLeave:
        if (CannonLeave(s.stepPhase) == CannonLeave::Wait) {
            if (t == 0x11 && flyPoint(int16_t(0xfc00), int16_t(0xf6e7), int16_t(0xf64f), int16_t(0xfb22),
                                      int16_t(0xf280), int16_t(0xfa7f), 0x11, kFlagGateLock)) {
                s.stepPhase = uint8_t(CannonLeave::Go);
                s.stepSubPhase = uint8_t(CannonGo::Talk);
            }
            playerAnim(0x82);
        } else if (CannonLeave(s.stepPhase) == CannonLeave::Go) {
            switch (CannonGo(s.stepSubPhase)) {
            case CannonGo::Talk:
                playerAnim(0x82);
                if (t == 0xe6) {
                    playerAnim(0x82);
                    mouth().select(1, 0);
                    s.stepSubPhase = uint8_t(CannonGo::Crouch);
                }
                break;
            case CannonGo::Crouch:
                if (playerKey() == 8) s.stepSubPhase = uint8_t(CannonGo::Jump);
                break;
            case CannonGo::Jump:
                if (playerAnimEnded()) {
                    playerAnim(1);
                    s.stepSubPhase = uint8_t(CannonGo::Run);
                }
                [[fallthrough]];
            case CannonGo::Run: moveAlongYaw(p.pos, p.rotation.y, 0, 0, -0x140); break;
            default: break;
            }
        }
        if (t == 0x10d) {
            s.flags |= kScriptUnskippable;
            scriptFade(kStepsFade, 0);
        }
        return;
    default: notPortedAt(kStoryPort, "script 0x35 step", routine.pointerValue());
    }
}
void cannonDoneScript() { // exe 0x5287f0
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x528810
        switch (SetUp(s.subState)) {
        case SetUp::Start:
            actorsSweep(kSweepActors, kSweepFree);
            windowClose(4, WindowClose::Force);
            scriptSequenceStart(stateAddress(TableId::Ch2ScriptsCannonDoneCamera), stateAddress(TableId::Ch2ScriptsCannonDoneSteps));
            scriptReplayPadOn();
            s.flags |= kScriptUnskippable;
            clearPlayerAnglesAndFaces();
            placePlayer(0xfcd00000u, 0xf7ff0000u, 0xd00000u, 0);
            s.subState = uint8_t(SetUp::TakeOver);
            [[fallthrough]];
        case SetUp::TakeOver:
            if (playerToScriptIfFree() == 0) {
                playerAnim(0x80);
                startFaceTracks(stateAddress(TableId::Ch2ScriptsCannonDoneEyes), stateAddress(TableId::Ch2ScriptsCannonDoneMouth));
                scriptSequenceTick();
                s.subState += 1u;
            }
            return;
        case SetUp::Voice:
            voiceClipPlay(3);
            s.subState += 1u;
            return;
        case SetUp::WaitVoice:
            if (voiceClipPlaying()) s.enterState(CutsceneState::Steps);
            return;
        default: return;
        }
    case CutsceneState::Steps: scriptStepsState(&cannonDoneStep); return; // exe 0x53a5e0
    case CutsceneState::WrapUp: missionEnding(0x680, int16_t(0xf800), 0x180, 0x400, true); return;
    default: notPortedAt(kStoryPort, "script 0x35 state", s.state);
    }
}

// ---- script 0x3d: the mission's look-around (ST17 sub 0 on a visit after the landing) ------------------------------
// The set-up: the player hidden at (-0x990, -0x801, 0xac0) yaw 0; once script driven the steps + animation 0. Steps
// 0x51d380 (animation 0 once), a ret (0x52ef00). The wrap-up: once no fade (fade 1 if skipped), the actors swept, the
// mission's cast (records 0x8a75fc x 9), the player back, the block ended.
constexpr RoutineId kLookStand = RoutineId::St24GateStep1, kLookWait = RoutineId::EmptyRoutine;
enum class LookWrapUp : uint8_t { Start = 0, WaitFade = 1, End = 2 };
void lookAroundStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    if (routine.is(kLookWait)) return; // ret
    if (!routine.is(kLookStand)) notPortedAt(kStoryPort, "script 0x3d step", routine.pointerValue());
    if (s.stepPhase == 0) {
        playerAnim(0);
        s.stepPhase += 1u;
    }
}
void lookAroundScript() { // exe 0x52b7c0
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x52b7e0
        if (s.subState == 0) {
            actorsSweep(kSweepActors, kSweepFree);
            scriptSequenceStart(stateAddress(TableId::Ch2ScriptsLookAroundCamera), stateAddress(TableId::Ch2ScriptsLookAroundSteps));
            scriptReplayPadOn();
            player().setDrawn(false);
            clearPlayerAnglesAndFaces();
            placePlayer(0xf6700000u, 0xf7ff0000u, 0xac00000u, 0);
            s.subState = 1;
        } else if (s.subState != 1) {
            return;
        }
        if (playerToScriptIfFree() == 0) {
            s.enterState(CutsceneState::Steps);
            playerAnim(0);
        }
        return;
    case CutsceneState::Steps: scriptStepsStateNoFaces(&lookAroundStep); return; // exe 0x52f420
    case CutsceneState::WrapUp: // exe 0x52b890
        switch (LookWrapUp(s.subState)) {
        case LookWrapUp::Start:
            s.subState = uint8_t(LookWrapUp::WaitFade);
            [[fallthrough]];
        case LookWrapUp::WaitFade:
            if (fadeState().busy == 0) {
                if (skipTaken()) scriptFade(kSkipArrivalFade, 0);
                s.subState += 1u;
            }
            return;
        case LookWrapUp::End:
            actorsSweep(kSweepActors, kSweepFree);
            spawnRecords(stateAddress(TableId::Ch2ScriptsLookAroundSpawns), 9);
            playerBack();
            player().setDrawn(true);
            scriptEndBlock();
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x3d state", s.state);
    }
}

// ---- script 0x36: the enemy ships beaten (ST17, flag 0x29) ---------------------------------------------------------
// The player at (-0x980, -0x801, 0x200) yaw 0, the face tracks 0x8a6644 / 0x8a6658, voice 6; steps 0x529070 (the music
// faded at 100), 0x5290a0 (animations 0x81..0x83 + face keys at 0x2d / 0x50 / 0x54 / 0x5a / 0x19a, the turn and the
// jump (yaw + 0x800 at its end), the run along the yaw, fade at 0x1b8)
constexpr RoutineId kShipsQuiet = RoutineId::Ch2ScriptsShipsQuiet, kShipsTalk = RoutineId::Ch2ScriptsShipsTalk;
enum class ShipsTalk : uint8_t {
    Start = 0, Eyes = 1, Mouth = 2, Pause = 3, Turn = 4, Look = 5, Jump = 6, Run = 7,
};
constexpr uint8_t kNoFaceKey = 0xff;
void shipsDoneStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    Player& p = player();
    const int32_t t = tick();
    switch (routine.id()) {
    case kShipsQuiet:
        s.stepPhase = 0;
        if (t == 100) soundFadeOut(kMusicFades, 0x222, 0);
        return;
    case kShipsTalk: {
        const uint8_t phase = s.stepPhase;
        uint8_t eyesKey = kNoFaceKey;
        switch (ShipsTalk(phase)) {
        case ShipsTalk::Start:
            playerAnim(0x81);
            eyesKey = 1;
            break;
        case ShipsTalk::Eyes:
            if (t == 0x2d) {
                s.stepPhase = uint8_t(phase + 1u);
                eyes().select(2, 0);
            }
            break;
        case ShipsTalk::Mouth:
            if (t == 0x50) {
                s.stepPhase = uint8_t(phase + 1u);
                mouth().select(0, 0);
            }
            break;
        case ShipsTalk::Pause:
            if (t == 0x54) s.stepPhase = uint8_t(phase + 1u);
            break;
        case ShipsTalk::Turn:
            if (t == 0x5a) {
                playerAnim(0x82);
                eyes().select(3, 0);
                s.stepPhase += 1u;
            }
            break;
        case ShipsTalk::Look:
            if (t == 0x19a) {
                playerAnim(0x83);
                eyesKey = 4;
            }
            break;
        case ShipsTalk::Jump:
            if (int8_t(playerKey()) > 10) moveAlongYaw(p.pos, p.rotation.y, 0, 0, 0x140);
            if (playerAnimEnded()) {
                p.rotation.y = int16_t(uint16_t(p.rotation.y) + 0x800u);
                playerAnim(1);
                s.stepPhase += 1u;
            }
            break;
        case ShipsTalk::Run: moveAlongYaw(p.pos, p.rotation.y, 0, 0, -0x140); break;
        default: break;
        }
        if (eyesKey != kNoFaceKey) { // the eyes' key, the mouth's key 1, the next phase
            eyes().select(eyesKey, 0);
            mouth().select(1, 0);
            s.stepPhase += 1u;
        }
        if (t == 0x1b8) scriptFade(kStepsFade, 0);
        return;
    }
    default: notPortedAt(kStoryPort, "script 0x36 step", routine.pointerValue());
    }
}
void shipsDoneScript() { // exe 0x528e30
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x528e50
        switch (SetUp(s.subState)) {
        case SetUp::Start:
            actorsSweep(kSweepActors, kSweepFree);
            windowClose(4, WindowClose::Force);
            scriptSequenceStart(stateAddress(TableId::Ch2ScriptsShipsDoneCamera), stateAddress(TableId::Ch2ScriptsShipsDoneSteps));
            scriptReplayPadOn();
            s.flags |= kScriptUnskippable;
            clearPlayerAnglesAndFaces();
            placePlayer(0xf6800000u, 0xf7ff0000u, 0x2000000u, 0);
            s.subState = uint8_t(SetUp::TakeOver);
            [[fallthrough]];
        case SetUp::TakeOver:
            if (playerToScriptIfFree() == 0) {
                playerAnim(0);
                startFaceTracks(stateAddress(TableId::Ch2ScriptsShipsDoneEyes), stateAddress(TableId::Ch2ScriptsShipsDoneMouth));
                scriptSequenceTick();
                s.subState += 1u;
            }
            return;
        case SetUp::Voice:
            voiceClipPlay(6);
            s.subState += 1u;
            return;
        case SetUp::WaitVoice:
            if (voiceClipPlaying()) {
                s.enterState(CutsceneState::Steps);
                s.flags &= uint8_t(~kScriptUnskippable);
            }
            return;
        default: return;
        }
    case CutsceneState::Steps: scriptStepsState(&shipsDoneStep); return; // exe 0x53a5e0
    case CutsceneState::WrapUp: // exe 0x528f70: as the missions' ending without the actor sweep
        switch (WrapUp(s.subState)) {
        case WrapUp::FadeVoice:
            fadeVoiceStep(s);
            [[fallthrough]];
        case WrapUp::WaitVoice:
            if (!voiceClipPlaying()) s.subState += 1u;
            return;
        case WrapUp::Request:
            requestTransition(kTransitionAccepted, kSt19, 0, int16_t(0xfd80), int16_t(0xf7ff), int16_t(0xf980), 0x800,
                              kShownFade, wrapUpFade());
            s.subState += 1u;
            return;
        case WrapUp::End:
            if (requestDone()) {
                playerBack();
                scriptEndBlock();
            }
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x36 state", s.state);
    }
}

// ---- script 0x37: the ship fight survived (ST18 sub 0, flag 0x2a) --------------------------------------------------
// The set-up: the player hidden, the warp to ST18 sub 3; once script driven animation 0. Steps 0x5294f0 (DAT 0x97 once
// the voice is idle; voice 9 after 0x3c ticks and the loader idle; once it plays the warp to ST18 sub 4 and the next
// step), 0x5295e0 (at 0 the script actor 0x8a6764 freed, the bank switched, the five script actors 0x8a6778..0x8a67c8,
// skippable; at 0x1fd the warp to ST18 sub 0 (0, -0xa81, 0) yaw 0xe00), 0x5296b0 (the player shown facing 0xe00,
// animation 0x81; on its key 0 a spark burst (pool 0xa0 stage type 5) between its bones 6 and 7 + sound 0x9a; at
// 0x15d the warp to ST18 sub 3, unskippable), 0x529850 (the voice idle and past 0x32 ticks: DAT 0x96, the next step).
// The wrap-up: the voice faded and no fade; DAT 0x96 if skipped; once loaded the warp to ST18 sub 0 (0, -0xa61, 0)
// (fade 1 if skipped); once there the actors swept, the bank switched, records 0x8a6750 x 1 + 0x8a682c x 2, the player
// back, the block ended.
constexpr RoutineId kFightVoice = RoutineId::Ch2ScriptsFightVoice, kFightCast = RoutineId::Ch2ScriptsFightCast,
                    kFightBurst = RoutineId::Ch2ScriptsFightBurst, kFightLoad = RoutineId::Ch2ScriptsFightLoad;
enum class FightVoice : uint8_t { Load = 0, Wait = 1, Voice = 2, WaitVoice = 3 };
enum class FightBurst : uint8_t { Show = 0, Burst = 1 };
enum class FightSetUp : uint8_t { Start = 0, Warp = 2, TakeOver = 3 };
enum class FightWrapUp : uint8_t { FadeVoice = 0, WaitVoice = 1, Load = 2, Warp = 3, End = 4 };
constexpr uint8_t kBurstType = 5;
constexpr size_t kBurstBoneFrom = 6, kBurstBoneTo = 7;
// The burst's spawn parameter (the stage's pool-0xa0 type 5): its size
struct BurstParams {
    explicit BurstParams(const ActorHeader& actor) : BurstParams(const_cast<ActorHeader&>(actor)) {}
    explicit BurstParams(ActorHeader& actor) : size(actor.params, 0) {}
    BytesField<uint16_t, 4> size;
};
// a lane of the burst's place: 5/2 of the way from bone 6 to bone 7 (in world units: >> the stage shift)
int16_t burstLane(int32_t from, int32_t to, uint32_t shift, uint32_t shift1) {
    return int16_t((from >> shift) + int16_t(((to - from) * 5) >> shift1));
}
void shipFightStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    Player& p = player();
    const int32_t t = tick();
    switch (routine.id()) {
    case kFightVoice:
        switch (FightVoice(s.stepPhase)) {
        case FightVoice::Load:
            if (!voiceClipPlaying()) {
                datLoadRequest(DatFile{"ST1801.DAT"});
                s.stepPhase += 1u;
            }
            return;
        case FightVoice::Wait:
            if (!fileLoading() && t > 0x3c) s.stepPhase += 1u;
            return;
        case FightVoice::Voice:
            voiceClipPlay(9);
            s.stepPhase += 1u;
            return;
        case FightVoice::WaitVoice:
            if (voiceClipPlaying()) {
                warpTo(kSt18, 4);
                s.clearStepPhase();
                scriptSequenceSkip(1);
            }
            return;
        default: return;
        }
    case kFightCast:
        if (t == 0) {
            scriptActorFreeRecord(stateAddress(TableId::Ch2ScriptsShipFightFreedRecord));
            switchStageBank(); // exe 0x5f3200
            for (uint32_t i = 0; i < 5; ++i) scriptActorSpawnRecord(stateAddress(TableId::Ch2ScriptsShipFightCastRecords) + i * 0x14);
            s.flags &= uint8_t(~kScriptUnskippable);
        } else if (t == 0x1fd) {
            warpTo(kSt18, 0, int16_t(0xf57f), 0xe00);
            s.clearStepPhaseBytes();
        }
        return;
    case kFightBurst: {
        bool burst = false;
        if (FightBurst(s.stepPhase) == FightBurst::Show) {
            p.setDrawn(true);
            p.rotation.y = 0xe00;
            playerAnim(0x81);
            s.stepPhase += 1u;
            burst = true;
        } else if (FightBurst(s.stepPhase) == FightBurst::Burst) {
            burst = true;
        }
        if (burst && playerKey() == 0) {
            if (const uint32_t a = allocActorA0(kActorStageClass)) { // exe 0x5e13a0
                EffectActor& spark = effectActorAt(a);
                spark.flags = kActorLiveDrawn;
                spark.type = kBurstType;
                spark.variant = 0;
                const uint32_t shift = stage().coordinateShift & 0x1fu;
                const uint32_t shift1 = (stage().coordinateShift + 1u) & 0x1fu;
                const ShortVec3& from = p.bonePoints[kBurstBoneFrom], to = p.bonePoints[kBurstBoneTo];
                spark.pos.x = burstLane(from.x, to.x, shift, shift1);
                spark.pos.y = burstLane(from.y, to.y, shift, shift1);
                BurstParams(actorHeaderAt(a)).size = 0x80;
                spark.pos.z = burstLane(from.z, to.z, shift, shift1);
                soundPlayAtPosS16(0x9a, spark.pos);
            }
        }
        if (t == 0x15d) {
            warpTo(kSt18, 3);
            s.clearStepPhaseBytes();
            s.flags |= kScriptUnskippable;
        }
        return;
    }
    case kFightLoad:
        if (s.stepPhase == 0 && !voiceClipPlaying() && t > 0x32) {
            datLoadRequest(DatFile{"ST1800.DAT"});
            scriptSequenceSkip(1);
        }
        return;
    default: notPortedAt(kStoryPort, "script 0x37 step", routine.pointerValue());
    }
}
void shipFightScript() { // exe 0x529270
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x529290
        switch (FightSetUp(s.subState)) {
        case FightSetUp::Start:
            setUpStart(stateAddress(TableId::Ch2ScriptsShipFightCamera), stateAddress(TableId::Ch2ScriptsShipFightSteps));
            player().setDrawn(false);
            clearPlayerAnglesAndFaces();
            s.subState = 1;
            [[fallthrough]];
        case FightSetUp::Warp:
            warpTo(kSt18, 3);
            s.subState = uint8_t(FightSetUp::TakeOver);
            [[fallthrough]];
        case FightSetUp::TakeOver:
            if (playerToScriptIfFree() == 0) {
                s.enterState(CutsceneState::Steps);
                playerAnim(0);
            }
            return;
        default: return;
        }
    case CutsceneState::Steps: scriptStepsStateNoFaces(&shipFightStep); return; // exe 0x52f420
    case CutsceneState::WrapUp: // exe 0x529380
        switch (FightWrapUp(s.subState)) {
        case FightWrapUp::FadeVoice:
            fadeVoiceStep(s);
            [[fallthrough]];
        case FightWrapUp::WaitVoice:
            if (!voiceClipPlaying() && fadeState().busy == 0) s.subState += 1u;
            return;
        case FightWrapUp::Load:
            if (skipTaken()) datLoadRequest(DatFile{"ST1800.DAT"});
            s.subState += 1u;
            return;
        case FightWrapUp::Warp:
            if (!fileLoading()) {
                warpTo(kSt18, 0, int16_t(0xf59f));
                s.subState += 1u;
                if (skipTaken()) scriptFade(kSkipArrivalFade, 0);
            }
            return;
        case FightWrapUp::End:
            if (transitionRequest().state == 0) {
                actorsSweep(kSweepActors, kSweepFree);
                switchStageBank();
                spawnRecords(stateAddress(TableId::Ch2ScriptsShipFightSpawns), 1);
                spawnRecords(stateAddress(TableId::Ch2ScriptsShipFightMoreSpawns), 2);
                playerBack();
                player().setDrawn(true);
                scriptEndBlock();
            }
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x37 state", s.state);
    }
}

// ---- script 0x38: the gunship shot down (ST18 sub 0 at story 7, flag 0x2b) ----------------------------------------
// The set-up: the player at (0, -0x800, 0) yaw 0; once script driven animation 0, voice 10; once it plays the steps
// (skippable). One step 0x529aa0 (fade at 0xb3). The wrap-up: request 2 -> ST19 sub 0 (0x300, -0x800, 0x680) yaw 0,
// then flag 0x7b8, the player back, the block ended.
constexpr RoutineId kGunshipFade = RoutineId::Ch2ScriptsGunshipFade;
constexpr uint32_t kFlagGunshipScene = 0x7b8;
void gunshipDownStep(const RoutineRef& routine) {
    switch (routine.id()) {
    case kGunshipFade:
        if (tick() == 0xb3) scriptFade(kStepsFade, 0);
        return;
    default: notPortedAt(kStoryPort, "script 0x38 step", routine.pointerValue());
    }
}
void gunshipDownScript() { // exe 0x529890
    ScriptState& s = scriptState();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp: // exe 0x5298b0
        switch (SetUp(s.subState)) {
        case SetUp::Start:
            setUpStart(stateAddress(TableId::Ch2ScriptsGunshipDownCamera), stateAddress(TableId::Ch2ScriptsGunshipDownSteps));
            clearPlayerAnglesAndFaces();
            placePlayer(0, 0xf8000000u, 0, 0);
            scriptSequenceTick();
            s.subState = uint8_t(SetUp::TakeOver);
            [[fallthrough]];
        case SetUp::TakeOver:
            if (playerToScriptIfFree() == 0) {
                playerAnim(0);
                s.subState += 1u;
            }
            return;
        case SetUp::Voice:
            voiceClipPlay(10);
            s.subState += 1u;
            return;
        case SetUp::WaitVoice:
            if (voiceClipPlaying()) {
                s.enterState(CutsceneState::Steps);
                s.flags &= uint8_t(~kScriptUnskippable);
            }
            return;
        default: return;
        }
    case CutsceneState::Steps: scriptStepsStateNoFaces(&gunshipDownStep); return; // exe 0x52f420
    case CutsceneState::WrapUp: // exe 0x5299a0
        switch (WrapUp(s.subState)) {
        case WrapUp::FadeVoice:
            fadeVoiceStep(s);
            [[fallthrough]];
        case WrapUp::WaitVoice:
            if (!voiceClipPlaying()) s.subState += 1u;
            return;
        case WrapUp::Request:
            requestTransition(kTransitionAccepted, kSt19, 0, 0x300, int16_t(0xf800), 0x680, 0, kShownFade,
                              wrapUpFade());
            s.subState += 1u;
            return;
        case WrapUp::End:
            if (requestDone()) {
                setStoryFlag(kFlagGunshipScene);
                playerBack();
                scriptEndBlock();
            }
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x38 state", s.state);
    }
}


} // namespace

bool ch2ScriptPorted(RoutineId handler) {
    switch (handler) {
    case RoutineId::Ch2TakeOffScript:
    case RoutineId::Ch2LandingScript:
    case RoutineId::Ch2CityGateScript:
    case RoutineId::Ch2CraneDoneScript:
    case RoutineId::Ch2CannonDoneScript:
    case RoutineId::Ch2LookAroundScript:
    case RoutineId::Ch2ShipsDoneScript:
    case RoutineId::Ch2ShipFightScript:
    case RoutineId::Ch2GunshipDownScript: return true;
    default: return false;
    }
}
bool ch2ScriptTick(const PlayerEnv&, RoutineId handler) {
    switch (handler) {
    case RoutineId::Ch2TakeOffScript: takeOffScript(); return true;
    case RoutineId::Ch2LandingScript: landingScript(); return true;
    case RoutineId::Ch2CityGateScript: cityGateScript(); return true;
    case RoutineId::Ch2CraneDoneScript: craneDoneScript(); return true;
    case RoutineId::Ch2CannonDoneScript: cannonDoneScript(); return true;
    case RoutineId::Ch2LookAroundScript: lookAroundScript(); return true;
    case RoutineId::Ch2ShipsDoneScript: shipsDoneScript(); return true;
    case RoutineId::Ch2ShipFightScript: shipFightScript(); return true;
    case RoutineId::Ch2GunshipDownScript: gunshipDownScript(); return true;
    default: return false;
    }
}

} // namespace od::port
