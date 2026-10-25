// Story chapter 4: after Calbania. See content/late_game/ch4_story.hpp. Names are hypotheses from the observed
// behaviour.
#include "content/late_game/ch4_story.hpp"
#include "content/late_game/ch4_story_state.hpp"

#include <optional>

#include "content/late_game/exam_loadout.hpp"
#include "content/nino/ch2_rooms.hpp"

#include "engine/data_source.hpp"
#include "engine/routine_ids.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/actor_sweeps.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/camera.hpp"
#include "game/cutscene_actor.hpp"
#include "game/display.hpp"
#include "game/draw_commands.hpp"
#include "game/encounter.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/explosions.hpp"
#include "game/fade_state.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/message.hpp"
#include "game/player/event_loadout.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/rewards.hpp"
#include "game/room_scripts.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/spark_emitter.hpp"
#include "game/spawn.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::ch4_story_state;

namespace od::port {

using namespace ch4_story_state;

namespace {

void anim(uint8_t a, uint8_t sub) { playerScriptAnimId(a, sub, 1); } // exe 0x5723d0(a, sub, 1)
bool fadeBusy() { return fadeState().busy != 0; }
void unskippable(bool on) {
    if (on) scriptState().flags |= kScriptUnskippable;
    else scriptState().flags &= uint8_t(~kScriptUnskippable);
}
// the player taken over: its move direction's angles and its idle faces cleared
void playerScripted() {
    Player& p = player();
    p.clearDirectionOffsetWords();
    p.idleFaces[0] = 0;
    p.idleFaces[1] = 0;
}

// the shared runner exe 0x52f420 (state 1 of the step scripts) with this script's step routine
template <class F> void runner(F step) {
    ScriptState& s = scriptState();
    const ScriptStep current = scriptStepAt(s.steps.value());
    if (current.id != kScriptStepEnd) {
        if (const RoutineRef routine = stateRoutine(current.routine)) step(routine);
        if (scriptSkipRequested() == 0 || display().drawBuffer == 0) {
            scriptSequenceTick();
            return;
        }
        scriptFade(0x22, 0);
    }
    s.enterState(CutsceneState::WrapUp);
    scriptSequenceTick();
}
void next() { // the step's phases cleared, exe 0x5115c0(1)
    scriptState().clearStepPhase();
    scriptSequenceSkip(1);
}

// ---- script 0x6a (exe 0x53a0c0, states 0x8ad0a0 = 0x53a0e0 / runner 0x52f420 / 0x53a160; camera 0x8acf44, steps
// 0x8ad068): ST40 sub 1, the first arrival (handler 0x5898a0, flag 0x150). The cast of record 0x8acf30 (pool 0x20
// type 0 sub 1, class 0x445840; spawned by the camera script into the script's first actor) talks: messages
// 0x40..0x45 of bank 0x73c000 in window 0, its four parameter bytes set per message; step 0x53a1d0 waits flag 0x68b
// (set by message 0x40's answer). End: fade 0x12, then the cast freed, fade 2, the player back.
constexpr uint32_t kMessageBank = 0x73c000;
constexpr int kKeep = -1; // a parameter byte left as it is
// the cast's message step head: its parameter bytes (kKeep: unchanged), window 0 on message `msg`
void castSay(int c, int d, int e, int f, int16_t msg) {
    ScriptState& s = scriptState();
    ActorHeader& cast = (*s.actors[0]);
    if (c >= 0) cast.params[0] = uint8_t(c);
    if (d >= 0) cast.params[1] = uint8_t(d);
    if (e >= 0) cast.params[2] = uint8_t(e);
    if (f >= 0) cast.params[3] = uint8_t(f);
    windowOpen(0, kMessageBank, msg); // exe 0x557390(0, bank, msg)
    s.stepPhase += 1u;
}
bool windowFree() { return windowStatus(0) == 0; }
void arrivalStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    const int32_t t = s.stepTick;
    switch (routine.id()) {
    case RoutineId::Ch4ArrivalStep1:
        switch (s.stepPhase) {
        case 0:
            if (t >= 0x1e) castSay(3, 2, kKeep, kKeep, 0x40);
            return;
        case 1: {
            if (!storyFlag(0x68b)) return;
            ActorHeader& cast = (*s.actors[0]);
            cast.params[1] = 1;
            cast.params[3] = 0xaa;
            s.stepPhase += 1u;
            return;
        }
        case 2:
            if (windowFree()) next();
            return;
        default: return;
        }
    case RoutineId::Ch4ArrivalStep2:
        if (s.stepPhase == 0) castSay(kKeep, 2, kKeep, 0x80, 0x41);
        else if (s.stepPhase == 1 && windowFree()) next();
        return;
    case RoutineId::Ch4ArrivalStep3:
        switch (s.stepPhase) {
        case 0: castSay(kKeep, 1, kKeep, 0x40, 0x42); return;
        case 1:
            if (t == 0xf) player().leanTarget = 0x280;
            if (t == 0x1e) s.stepPhase += 1u;
            return;
        case 2:
            if (windowFree()) next();
            return;
        default: return;
        }
    case RoutineId::Ch4ArrivalStep4:
        if (s.stepPhase == 0) {
            player().leanTarget = 0;
            castSay(3, 1, 1, kKeep, 0x43);
        } else if (s.stepPhase == 1 && windowFree()) {
            next();
        }
        return;
    case RoutineId::Ch4ArrivalStep5:
        if (s.stepPhase == 0) castSay(kKeep, 2, 0, 0x80, 0x44);
        else if (s.stepPhase == 1 && windowFree()) next();
        return;
    case RoutineId::Ch4ArrivalStep6:
        switch (s.stepPhase) {
        case 0: castSay(3, 1, kKeep, 0x40, 0x45); return;
        case 1:
            if (!windowFree()) return;
            scriptFade(0x12, 0); // exe 0x53f6b0
            s.stepPhase += 1u;
            return;
        case 2:
            if (!fadeBusy()) next();
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x6a step", routine.pointerValue());
    }
}
const DataTable<RoutineTable<3>> arrivalStates{TableId::Ch4StoryArrivalStates};
void arrivalScript() { // exe 0x53a0c0
    ScriptState& s = scriptState();
    switch (arrivalStates->idAt(s.state)) {
    case RoutineId::Ch4ArrivalSetUp:
        switch (s.subState) {
        case 0:
            scriptSequenceStart(stateAddress(TableId::Ch4StoryArrivalCamera), stateAddress(TableId::Ch4StoryArrivalSteps)); // exe 0x511390
            scriptReplayPadOn();                     // exe 0x5117f0
            s.subState += 1u;
            [[fallthrough]];
        case 1:
            if (playerToScriptIfFree() == 0) { // exe 0x572300
                anim(0x50, 0);
                playerScripted();
                s.enterState(CutsceneState::Steps);
            }
            break;
        default: break;
        }
        scriptSequenceTick(); // exe 0x5114d0
        return;
    case RoutineId::Ch4StepRunner: runner(arrivalStep); return;
    case RoutineId::Ch4ArrivalEnd:
        if (s.subState == 0) {
            anim(0, 0);
            scriptActorFreeRecord(stateAddress(TableId::Ch4StoryArrivalRecord));     // exe 0x511760
            windowCloseAll(WindowClose::Gentle); // exe 0x5577d0
            s.subState += 1u;
        }
        if (fadeBusy() || !windowFree()) return;
        scriptFade(2, 0);
        scriptReplayPadOff(); // exe 0x511890
        playerBackToPlay();   // exe 0x5723c0
        scriptEndBlock();     // exe 0x5116a0
        return;
    default: notPortedAt(kStoryPort, "script 0x6a state", s.state);
    }
}

// ---- ST40 room-script handlers (sub-indexed table 0x8cc648, subs 0..13). Every sub spawns the 4 teleporter pads
// (records 0x8cc680 x 4: pool 0xa0 type 0x1e, class 0x43adb0, into the room's actors 0..3) and sets their panels still
// or animated, spawns its extra records, then dips the music channel group 4 (exe 0x5d5830(4, 0x111, 0, 0)) or
// restores it (exe 0x5d58a0(4, 0x222, 0)); step 0 -> 1.
struct PadSub {
    RoutineId fn;
    uint8_t still[4];  // the pads' panels still (content/late_game/st40_fx)
    std::optional<TableId> extra; // exe 0x511090(extra, n): its spawn records (none: no call)
    uint32_t n;
    bool restore;      // exe 0x5d58a0 instead of exe 0x5d5830
};
constexpr PadSub kPadSubs[] = {
    {RoutineId::St40RoomHandlerSub3, {0, 0, 0, 1}, TableId::Ch4StorySt40Sub3ExtraSpawns, 1, true},  // sub 3
    {RoutineId::St40RoomHandlerSub4, {0, 1, 1, 0}, {}, 0, false},        // sub 4
    {RoutineId::St40RoomHandlerSub5, {1, 0, 0, 0}, TableId::Ch4StorySt40Sub5ExtraSpawns, 2, true},  // sub 5
    {RoutineId::St40RoomHandlerSub6, {0, 0, 1, 1}, {}, 0, false},        // sub 6
    {RoutineId::St40RoomHandlerSub7, {1, 0, 0, 0}, TableId::Ch4StorySt40Sub7ExtraSpawns, 3, true},  // sub 7
    {RoutineId::St40RoomHandlerSub8, {1, 0, 1, 1}, {}, 0, false},        // sub 8
    {RoutineId::St40RoomHandlerSub9, {0, 0, 1, 0}, {}, 0, false},        // sub 9
    {RoutineId::St40RoomHandlerSub10, {1, 1, 0, 1}, {}, 0, false},        // sub 10
    {RoutineId::St40RoomHandlerSub11, {1, 1, 1, 0}, {}, 0, false},        // sub 11
    {RoutineId::St40RoomHandlerSub12, {0, 1, 1, 1}, {}, 0, false},        // sub 12
    {RoutineId::St40RoomHandlerSub13, {1, 0, 1, 1}, {}, 0, false},        // sub 13
};
// the teleporter pad's panel byte the handlers set (content/late_game/st40_fx: still, else animated)
struct PadPanelParams {
    explicit PadPanelParams(const ActorHeader& actor) : PadPanelParams(const_cast<ActorHeader&>(actor)) {}
    explicit PadPanelParams(ActorHeader& actor) : still(actor.params[1]) {}
    uint8_t& still;
};
void setPadStill(uint32_t pad, uint8_t still) { PadPanelParams(actorHeaderAt(pad)).still = still; }
void musicDip() { soundFadeDip(4, 0x111, 0, 0); }      // exe 0x5d5830(4, 0x111, 0, 0)
void musicRestore() { soundFadeRestore(4, 0x222, 0); } // exe 0x5d58a0(4, 0x222, 0)

// exe 0x58a830(v, c): a pool-0xe0 controller type 0x1b, variant v, its parameter word c, its third byte 0x12, its yaw -1
void spawnStageController(uint8_t v, uint16_t c) {
    const uint32_t slot = allocActorE0(); // exe 0x5e1440
    if (slot == 0) return;
    ControllerActor& e = controllerActorAt(slot);
    e.flags = kActorLive;
    e.type = 0x1b;
    e.variant = v;
    e.setRecordYaw(-1);
    e.setParamsLowWord(c);
    e.params[2] = 0x12;
}
// the handlers of ST43 / ST44 / ST45 open with the door test: flag 0x700 -> script 0 (exe 0x511270(0)), nothing else
constexpr uint32_t kDoorFlag = 0x700;
bool doorTest() {
    if (!storyFlag(kDoorFlag)) return false;
    scriptRun(0);
    return true;
}

// ---- the smoke column (exe 0x5a9680, stage static type 0x26 sub 0; states 0x8cf048 = 0x5a96a0 / 0x5a97c0) ---------
// 8 puffs; variant 0 follows its parent (its slot and type in its parameter bytes) or its own orbit (its yaw, its z as
// the radius, its y as the height); other variants rise by their rise speed (0x18 a tick up to 0x180) and stop by
// size (variant 1) or by their countdown (variant 2). Drawn as 0x2c draw-command billboards.
enum class SmokeState : uint8_t { Start = 0, Run = 1 };
enum class SmokeSub : uint8_t { Emit = 0, Drain = 1, Shrink = 2 };
constexpr uint8_t kPuffLive = 0x80, kPuffFrameMask = 0x7f;
// a frame of the puffs' sprite (exe data): its texture words and its colour
struct PuffFrame : DataRecord {
    static constexpr uint32_t kBytes = 0x14;
    uint32_t tpage = 0; // +0x0
    uint32_t clut = 0; // +0x4
    int16_t u = 0; // +0x8
    int16_t v = 0; // +0xa
    int16_t cell = 0; // +0xc
    uint32_t colour = 0; // +0x10
    template <class R> static PuffFrame load(const R& r) {
        PuffFrame v;
        v.tpage = r.template read<uint32_t>(0x0);
        v.clut = r.template read<uint32_t>(0x4);
        v.u = r.template read<int16_t>(0x8);
        v.v = r.template read<int16_t>(0xa);
        v.cell = r.template read<int16_t>(0xc);
        v.colour = r.template read<uint32_t>(0x10);
        return v;
    }
};
// exe 0x5a9ce0(actor, out): the emitter point (0 ok, -1 parent gone)
int smokePoint(const StaticActor& actor, const SmokeColumnFields& col, int16_t out[4]) {
    int16_t dx, dz;
    if (col.parentSlot != 0) {
        const Actor& parent = actorAt(actorBySlot(uint32_t(int32_t(col.parentSlot))));
        if (!(parent.flags & kActorLive) || int32_t(parent.type) != int32_t(col.parentType) ||
            !(parent.loopFlags & kActorStageClass))
            return -1;
        angleVector(int16_t(uint16_t(parent.rotation.y) + uint16_t(actor.rotation.y)), actor.pos.z.whole, dx, dz);
        out[0] = int16_t(dx + parent.pos.x.whole);
        out[1] = int16_t(parent.pos.y.whole + actor.pos.y.whole);
        out[2] = int16_t(dz + parent.pos.z.whole);
        return 0;
    }
    angleVector(actor.rotation.y, actor.pos.z.whole, dx, dz);
    out[0] = dx;
    out[1] = actor.pos.y.whole;
    out[2] = dz;
    return 0;
}
void smokeStart(StaticActor& actor, SmokeColumnFields& col) { // exe 0x5a96a0
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.collisionBox = nullptr;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    col.clearPuffWords(); // exe 0x40dc40
    if (actor.variant == 0) {
        actor.velocity.setXY(0, 0);
        col.puffSpin = int16_t(((rngStep() & 1) + 8) << 4);
        if (rngStep() & 1) col.puffSpin = int16_t(-int32_t(col.puffSpin));
        col.growth = uint16_t((rngStep() & 1) + 5);
        col.size = 0x40;
    } else {
        actor.velocity.x = 0;
        col.puffSpin = int16_t((actor.slot & 1) ? 0xff80 : 0x80);
        col.growth = 5;
        col.size = 0x80;
    }
    col.tick = 6;
    col.glow = 1;
    actor.enterState(uint8_t(SmokeState::Run));
}
void smokeFollow(StaticActor& actor, SmokeColumnFields& col, int16_t out[4]) { // exe 0x5a9a00 (variant 0)
    switch (SmokeSub(actor.subState)) {
    case SmokeSub::Shrink:
        actor.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + 4u);
        if (smokePoint(actor, col, out) != 0) {
            col.glow = 0;
            actor.subState = uint8_t(SmokeSub::Drain);
        }
        col.size -= 4u;
        col.tick -= 1u;
        if (col.size < 4) freeActor(actor);
        return;
    case SmokeSub::Drain:
        col.tick -= 1u;
        col.glow = 0;
        if (col.livePuffs <= 0) freeActor(actor);
        return;
    case SmokeSub::Emit:
        if (smokePoint(actor, col, out) != 0) {
            col.glow = 0;
            actor.subState = uint8_t(SmokeSub::Drain);
        }
        col.tick -= 1u;
        if (col.tick == 0) {
            SmokePuff& p = col.puffs[col.nextPuff];
            if (!(p.state & kPuffLive)) {
                p.state = kPuffLive;
                p.rise = 6;
                p.point.x = out[0];
                p.point.y = out[1];
                p.point.z = out[2];
                p.spin = out[3];
                p.size = 0x30;
                p.spin = int16_t((rngStep() & 0x1f) << 5);
                col.livePuffs += 1u;
            }
            col.nextPuff = uint8_t((col.nextPuff + 1u) & 7u);
            col.tick = uint8_t((rngStep() & 1) + 6);
        }
        if (col.stop != 0) actor.subState = uint8_t(SmokeSub::Shrink);
        return;
    default: return;
    }
}
void smokeRise(StaticActor& actor, SmokeColumnFields& col, int16_t out[4]) { // exe 0x5a9ba0 (other variants)
    actor.velocity.y += 0x18u;
    if (actor.velocity.y > 0x180) actor.velocity.y = 0x180;
    integrateVelocity(actor, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e3690
    out[0] = actor.pos.x.whole;
    out[1] = actor.pos.y.whole;
    out[2] = actor.pos.z.whole;
    if (actor.subState != 0) {
        col.tick -= 1u;
        col.glow = 0;
        if (col.livePuffs <= 0) freeActor(actor);
        return;
    }
    col.tick -= 1u;
    if (col.tick == 0) {
        SmokePuff& p = col.puffs[col.nextPuff];
        if (!(p.state & kPuffLive)) {
            p.point.x = out[0];
            p.point.y = out[1];
            p.point.z = out[2];
            p.state = kPuffLive | 1;
            p.rise = 6;
            p.spin = 0;
            p.size = uint16_t(col.size);
            col.livePuffs += 1u;
        }
        col.tick = 6;
        col.nextPuff = uint8_t((col.nextPuff + 1u) & 7u);
    }
    switch (actor.variant) {
    case 1:
        col.size -= 4u;
        if (col.size >= 4) return;
        break;
    case 2:
        col.countdown -= 1u;
        if (col.countdown != 0) return;
        break;
    default: return;
    }
    col.glow = 0;
    actor.subState = uint8_t(SmokeSub::Drain);
}
void smokeRun(StaticActor& actor, SmokeColumnFields& col) { // exe 0x5a97c0
    int16_t out[4] = {0, 0, 0, 0};
    if (actor.variant == 0) smokeFollow(actor, col, out);
    else smokeRise(actor, col, out);
    if (!(actor.flags & kActorLive)) return;
    const uint32_t sh = stage().coordinateShift & 31u, shy = (stage().coordinateShift + 16u) & 31u;
    if (col.glow != 0) { // the base glow
        const uint32_t g = col.glowTexel;
        dc::emit({0, 0x2e808080u, (uint32_t(uint16_t(out[1])) << shy) | (uint32_t(uint16_t(out[0])) << sh),
                  g | 0x7cd24000u, uint32_t(uint16_t(out[2])) << sh, (g + 0x1f) | 0x2f4000u,
                  uint32_t(int32_t(col.size)), g | 0x5f00u, 0, (g + 0x1f) | 0x5f00u},
                 actor.at, false);
        col.glowTexel += 0x20u;
    }
    for (uint32_t k = 0; k < 8; ++k) {
        SmokePuff& p = col.puffs[k];
        const uint8_t f = p.state;
        if (!(f & kPuffLive)) continue;
        uint32_t fr = f & kPuffFrameMask;
        const int8_t rise = p.rise;
        p.point.y = int16_t(uint16_t(p.point.y) - uint16_t(int16_t(rise)));
        p.rise = int8_t(rise + 1);
        p.size += uint16_t(col.growth);
        p.spin += uint16_t(col.puffSpin);
        if (col.tick & 1) {
            ++fr;
            p.state = uint8_t(fr | kPuffLive);
            if (fr == 8) {
                p.state = 0;
                col.livePuffs -= 1u;
                continue;
            }
        }
        const PuffFrame e = dataRecord<PuffFrame>(stateAddress(TableId::Ch4StoryPuffFrames) + fr * 0x14, PuffFrame::kBytes);
        const uint32_t e8 = uint32_t(int32_t(e.u)), ea = uint32_t(int32_t(e.v)), ec = uint32_t(int32_t(e.cell));
        const uint32_t u0 = ec + e8, v1 = ec + ea;
        dc::emit({0, e.colour | 0x2e000000u,
                  (uint32_t(uint16_t(p.point.y)) << shy) | (uint32_t(uint16_t(p.point.x)) << sh),
                  ((e.clut << 8 | ea) << 8) | e8, uint32_t(uint16_t(p.point.z)) << sh, ((e.tpage << 8 | ea) << 8) | u0,
                  uint32_t(p.size) | uint32_t(int32_t(p.spin)) << 16, (v1 << 8) | e8, 0, (v1 << 8) | u0},
                 actor.at, false);
    }
}
void smokeColumn(StaticActor& actor) { // exe 0x5a9680
    SmokeColumnFields& col = classState<SmokeColumnFields>(actor);
    switch (SmokeState(actor.state)) {
    case SmokeState::Start: smokeStart(actor, col); return;
    case SmokeState::Run: smokeRun(actor, col); return;
    default: notPortedAt(kStoryPort, "0x5a9680 state", actor.state);
    }
}

// ---- script 0x62's cast ---------------------------------------------------------------------------------------------

Script62Fields& script62() {
    return scriptState().part<Script62Fields>();
}
// the step classes (pool 0x20): state 0 binds model variant (none: freed), its box, the class block cleared, the
// texture tracks, cells 0 / 0, state 1 and its first tick; state 1 calls its steps by the cast timeline's step (while
// it is not the end) with the timeline's tick. Tail: the texture tracks, the animation (the hold bit 1: decode + pose),
// the shadow exe 0x5f20f0(actor, 0x30, 0x100, 0) (render output).
template <class F> void stepCast(AnimatedActor& actor, uint32_t box, uint32_t t0, uint32_t t1, F step) {
    CastFields& cast = classState<CastFields>(actor);
    castSavePose(actor);
    switch (actor.state) {
    case 0:
        if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0(actor, &variant)
            freeActor(actor);                       // exe 0x5e1490
            break;
        }
        actor.clearClassBlock();
        actor.collisionBox.pointAt(box);
        actor.anim = 0;
        actor.prevAnim = kNoAnimShown;
        actor.airborne = 0;
        cast.faceTracks.init(t0, t1);
        textureTrackStop(actor, cast.faceTracks, FaceTrack::Eyes, 0);
        textureTrackStop(actor, cast.faceTracks, FaceTrack::Mouth, 0);
        actor.enterState(1);
        [[fallthrough]];
    case 1: {
        if (scriptState().part<CastTimelineStep>().stepId != kScriptStepEnd) step(actor, cast.faceTracks, scriptState().part<CastTimelineStep>().stepSub, scriptState().part<CastTimelineTick>().stepTick);
        break;
    }
    default: break;
    }
    if (!(actor.flags & kActorLive)) return;
    textureTracksTick(actor, cast.faceTracks);
    castAnimate(actor, cast.animKey, cast.castFlags, kCastHoldPose);
}
void cueTalkerA(AnimatedActor& actor) { // exe 0x45d240 (ST53 type 9 sub 2; steps 0x88f794: 0x45d3c0 / 0x52ef00)
    stepCast(actor, stateAddress(TableId::Ch4StoryTalkerABox), stateAddress(TableId::Ch4StoryTalkerAFirstTracks), stateAddress(TableId::Ch4StoryTalkerASecondTracks), [](AnimatedActor& a, KeyframeTracks& t, uint8_t step, int32_t k) {
        if (step != 0) return;
        switch (k) {
        case 0: a.anim = 8; return;
        case 0x28: case 0x8c: case 0x12c: t.first.select(0, 0); return;
        case 0x10d: a.anim = 9; return;
        case 0x112:
            script62().cue = 5;
            textureTrackStop(a, t, FaceTrack::Eyes, 3);
            t.second.select(0, 0);
            return;
        case 0x129: textureTrackStop(a, t, FaceTrack::Mouth, 0); return;
        case 0x12f: t.second.select(0, 0); return;
        case 0x157:
            textureTrackStop(a, t, FaceTrack::Mouth, 0);
            script62().cue = 0;
            return;
        default: return;
        }
    });
}
void cueTalkerB(AnimatedActor& actor) { // exe 0x4f66b0 (ST53 type 0x72 sub 2; steps 0x89730c: 0x4f6830 / 0x4f6a00)
    stepCast(actor, stateAddress(TableId::Ch4StoryTalkerBBox), stateAddress(TableId::Ch4StoryTalkerBFirstTracks), stateAddress(TableId::Ch4StoryTalkerBSecondTracks), [](AnimatedActor& a, KeyframeTracks& t, uint8_t step, int32_t k) {
        if (step == 0) {
            switch (k) {
            case 0: a.anim = 0xe; return;
            case 0x1e:
                textureTrackStop(a, t, FaceTrack::Eyes, 6);
                t.second.select(0, 0);
                script62().cue = 3;
                return;
            case 0x2d: textureTrackStop(a, t, FaceTrack::Mouth, 0xa); return;
            case 0x46:
                textureTrackStop(a, t, FaceTrack::Eyes, 6);
                t.second.select(1, 0);
                return;
            case 0x82:
                textureTrackStop(a, t, FaceTrack::Mouth, 6);
                script62().cue = 0;
                return;
            case 0x91: script62().cue = 4; return;
            case 0xf4: script62().cue = 0; return;
            default: return;
            }
        }
        if (step != 1) return;
        switch (k) {
        case 0xa:
            a.anim = 0xf;
            textureTrackStop(a, t, FaceTrack::Eyes, 2);
            t.second.select(1, 0);
            script62().cue = 3;
            return;
        case 0x1e: case 0x5d: case 0x94: textureTrackStop(a, t, FaceTrack::Mouth, 6); return;
        case 0x2e: case 0x6a: case 0x9c: t.second.select(1, 0); return;
        case 0xcc:
            textureTrackStop(a, t, FaceTrack::Mouth, 6);
            script62().cue = 0;
            return;
        default: return;
        }
    });
}
// the braking train (exe 0x5b6a60, a stage static; states 0x8d0a5c = 0x5b6ad0 / 0x5b6b50; step table 0x8d0a64 = {0,
// 0, 0x5b6b80, 0}): a model solid (exe 0x414050(actor, 1, -1, 0) every tick)
const DataTable<RoutineTable<4>> trainSteps{TableId::Ch4TrainSteps}; // exe 0x8d0a64 (null: no step)
// exe 0x5b6b80 (script 0x62's step 2, the warp to ST53 sub 0): at the step's tick 0 speed 0x200 and deceleration -4;
// while moving it moves (0, 0, -speed), the speed slows, both 0 once stopped (the train brakes to a stop with
// Megaman on its roof)
void trainBrake(StaticActor& actor) {
    if (scriptState().part<CastTimelineTick>().stepTick == 0) {
        actor.velocity.z = 0x200;
        actor.acceleration.z = int16_t(0xfffc);
    }
    if (actor.velocity.z == 0) return;
    integrateVelocity(actor, 0, 0, -int(actor.velocity.z)); // exe 0x5e3690
    actor.velocity.z = int16_t(actor.velocity.z + actor.acceleration.z);
    if (actor.velocity.z > 0) return;
    actor.velocity.z = 0;
    actor.acceleration.z = 0;
}
void brakingTrain(StaticActor& actor) {
    actor.savePreviousPose();
    actor.savePreviousContact();
    if (actor.state == 0) {
        if (!bindActorModel(actor, actor.variant)) {
            freeActor(actor);
            return;
        }
        actor.clearClassBlock(); // exe 0x40dc40 (its 4 words)
        actor.collisionBox = nullptr;
        actor.interactionBox = nullptr;
        actor.interaction = 0;
        actor.velocity.clearWords();
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        actor.airborne = 0;
        actor.onPlatform = 0;
        actor.platform = nullptr;
        actor.actorHit = 0;
        actor.flags |= kActorDrawn | kActorScenery;
        actor.enterState(1);
    } else if (actor.state != 1) {
        notPortedAt(kStoryPort, "0x5b6a60 state", actor.state);
    }
    // state 1 (exe 0x5b6b50): the step of the timeline's step while it is not the end
    if (scriptState().part<CastTimelineStep>().stepId != kScriptStepEnd) {
        if (scriptState().part<CastTimelineStep>().stepSub >= trainSteps.size()) notPortedAt(kStoryPort, "0x5b6a60 step sub", scriptState().part<CastTimelineStep>().stepSub);
        const RoutineRef& routine = trainSteps[scriptState().part<CastTimelineStep>().stepSub];
        if (routine.is(RoutineId::Ch4TrainBrake)) trainBrake(actor);
        else if (!routine.null()) notPortedAt(kStoryPort, "0x5b6a60 step", routine.pointerValue());
    }
    if (actor.flags & kActorLive) actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0); // exe 0x414050
}

// ---- script 0x62 (exe 0x537b20, states 0x8ac590 = 0x537b40 / 0x537c50 / 0x537cb0; camera 0x8ac494, steps 0x8ac548 =
// 0x52ef00 / 0x537dc0 / 0x537e90): ST53 sub 1 at story 0xf (handler 0x58c100, flag 0x5a2). The cast (camera records
// 0x8ac458 / 0x8ac46c: 0x45d240 / 0x4f66b0; 0x8ac480: the train 0x5b6a60), three spark emitters (variant 0x1c), a
// warp to ST53 sub 0 at tick 0xcb of step 1, then the column of blasts and smoke (exe 0x5381e0), voices 0xff01 /
// 0x12, the banner (0x44, 0x30, mission complete); end: the request to ST09 sub 0 (fades 2 / 0x12), the story + 1.
// exe 0x538330(pos, variant, lifetime): a spark emitter (stage static type 3 sub 0) at the s16 point
void sparkEmitterAt(uint32_t point, uint8_t v, int16_t lifetime) {
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (slot == 0) return;
    const ShortVec3 p = dataShortVec3(point);
    StaticActor& e = staticActorAt(slot);
    SparkEmitterParams& params = classState<SparkEmitterParams>(staticActorAt(slot));
    e.variant = v;
    e.loopFlags |= kActorSweepGroup2;
    e.flags = kActorLive;
    e.type = kSparkEmitterStageType;
    e.subtype = 0;
    e.pos.x = int32_t(uint32_t(int32_t(p.x)) << 16);
    e.pos.y = int32_t(uint32_t(int32_t(p.y)) << 16);
    params.driftX = 0;
    params.driftZ = 0;
    e.pos.z = int32_t(uint32_t(int32_t(p.z)) << 16);
    params.lifetime = lifetime;
}
// the blasts of the column: their offset and size (exe table 0x8ac5a0)
struct ColumnBlast : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    ShortVec3 offset; // +0x0
    uint16_t size = 0; // +0x6
    template <class R> static ColumnBlast load(const R& r) {
        ColumnBlast v;
        v.offset = {r.template read<int16_t>(0x0), r.template read<int16_t>(0x2), r.template read<int16_t>(0x4)};
        v.size = r.template read<uint16_t>(0x6);
        return v;
    }
};
// the smoke of the column: its velocity's two words (their high half the yaw) (exe table 0x8ac5b8)
struct ColumnSmoke : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    uint32_t velocityXY = 0; // +0x0
    uint32_t velocityZ = 0; // +0x4
    uint16_t yaw = 0; // +0x6
    template <class R> static ColumnSmoke load(const R& r) {
        ColumnSmoke v;
        v.velocityXY = r.template read<uint32_t>(0x0);
        v.velocityZ = r.template read<uint32_t>(0x4);
        v.yaw = r.template read<uint16_t>(0x6);
        return v;
    }
};
constexpr uint8_t kSmokeColumnType = 0x26;
// exe 0x5381e0(pos): 3 blasts (variant 0x10) at the table's offsets; 4 pairs of smoke columns (variants 2 / 1) with
// the table's yaw and velocity (variant 2: its countdown 0xf0)
void blastColumn(uint32_t point) {
    const ShortVec3 p = dataShortVec3(point);
    for (uint32_t i = 0; i < 3; ++i) {
        const ColumnBlast b = dataRecord<ColumnBlast>(stateAddress(TableId::Ch4StoryColumnBlasts) + i * 8, ColumnBlast::kBytes);
        const uint32_t slot = allocActor60(0); // exe 0x5e1300
        if (slot == 0) continue;
        StaticActor& e = staticActorAt(slot);
        BlastParams params(actorHeaderAt(slot));
        e.flags = kActorLive;
        e.type = typeByte(GlobalStaticType::Blast);
        e.subtype = 0;
        e.variant = 0x10;
        e.loopFlags |= kActorSweepGroup2;
        e.pos.x = int32_t(uint32_t(int32_t(b.offset.x) + int32_t(p.x)) << 16);
        e.pos.y = int32_t(uint32_t(int32_t(b.offset.y) + int32_t(p.y)) << 16);
        e.pos.z = int32_t(uint32_t(int32_t(p.z) + int32_t(b.offset.z)) << 16);
        params.size = b.size;
        params.attack = 0;
    }
    for (uint32_t i = 0; i < 4; ++i) {
        const ColumnSmoke d = dataRecord<ColumnSmoke>(stateAddress(TableId::Ch4StoryColumnSmokes) + i * 8, ColumnSmoke::kBytes);
        for (uint8_t v : {uint8_t(2), uint8_t(1)}) {
            const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
            if (slot == 0) continue;
            StaticActor& e = staticActorAt(slot);
            e.flags = kActorLive;
            e.type = kSmokeColumnType;
            e.subtype = 0;
            e.variant = v;
            e.loopFlags |= kActorSweepGroup2;
            e.pos.x = int32_t(uint32_t(int32_t(p.x)) << 16);
            e.pos.y = int32_t(uint32_t(int32_t(p.y)) << 16);
            e.pos.z = int32_t(uint32_t(int32_t(p.z)) << 16);
            e.rotation.y = int16_t(d.yaw);
            e.velocity.setWords(d.velocityXY, d.velocityZ);
            if (v == 2) classState<SmokeColumnFields>(e).countdown = 0xf0;
        }
    }
}
void ridingStep(const RoutineRef& routine) {
    ScriptState& s = scriptState();
    Script62Fields& f = script62();
    const int32_t t = s.stepTick;
    switch (routine.id()) {
    case RoutineId::EmptyRoutine: return;
    case RoutineId::Ch4RidingStep1: // the warp to ST53 sub 0
        if (t == 0) {
            s.subState = 0;
            f.counter = 0;
        } else if (t == 0xcb) {
            scriptFade(0x1a, 0);
            s.subState = 1;
        }
        switch (s.subState) {
        case 1:
            if (fadeBusy()) return;
            s.subState = 2;
            [[fallthrough]];
        case 2: {
            const int32_t c = f.counter;
            f.counter = c + 1;
            if (c <= 0x26) return;
            s.subState = 3;
            [[fallthrough]];
        }
        case 3:
            requestTransition(0xff, 0x53, 0, 0, int16_t(-0x680), 0x300, 0x400, 0xff, 0xff);
            scriptFade(0xa, 0);
            scriptSequenceSkip(1); // exe 0x5115c0(1)
            return;
        default: return;
        }
    case RoutineId::Ch4RidingStep2: { // the column of blasts, the voices, the banner
        Player& p = player();
        switch (t) {
        case 0:
            cameraShake(true, 0, 0);
            p.setDrawn(true);
            playerBackToPlay(); // exe 0x5723c0
            s.subState = 0;
            f.counter = 0;
            p.pos.x = 0;
            p.pos.y = int32_t(0xf9800000u);
            p.pos.z = 0x3000000;
            p.rotation.y = 0x400;
            break;
        case 5:
            f.cue = 3;
            blastColumn(stateAddress(TableId::Ch4StoryRidingBlastPoint));
            break;
        case 0xc: case 0x1c: blastColumn(stateAddress(TableId::Ch4StoryRidingBlastPoint)); break;
        case 0x12: blastColumn(stateAddress(TableId::Ch4StoryRidingSecondBlastPoint)); break;
        case 0x20: sparkEmitterAt(stateAddress(TableId::Ch4StoryRidingBlastPoint), 0x13, 0); break;
        case 0x45: f.cue = 0; break;
        case 0xc8: s.subState = 1; break;
        default: break;
        }
        if (pad1().pressed & kPadR1) s.subState = 0xa;
        switch (s.subState) {
        case 1:
            if (voiceClipPlaying()) voiceClipFadeOut(4); // exe 0x601220
            s.subState = 2;
            [[fallthrough]];
        case 2:
            if (voiceClipIdle()) s.subState = 3;
            return;
        case 3:
            if (playerToScriptIfFree() != 0) return; // exe 0x572300
            voiceClipPlay(0xff01);
            hudBannerShow(0x44, 0x30, HudBannerStyle::MissionComplete); // exe 0x5f25e0
            anim(0x56, 0);
            unskippable(true);
            s.subState = 4;
            return;
        case 4:
            if (!voiceClipPlaying()) return;
            unskippable(false);
            s.subState = 5;
            [[fallthrough]];
        case 5:
            if (!voiceClipIdle()) return;
            anim(0x57, 0);
            s.subState = 6;
            return;
        case 6: {
            const int32_t c = f.counter;
            f.counter = c + 1;
            if (c <= 0x1e) return;
            hudBanner().hide(); // exe 0x5f2680
            f.counter = 0;
            s.subState = 7;
            return;
        }
        case 7:
            voiceClipPlay(0x12);
            unskippable(true);
            s.subState = 8;
            [[fallthrough]];
        case 8:
            if (!voiceClipPlaying()) return;
            unskippable(false);
            s.subState = 9;
            [[fallthrough]];
        case 9: {
            const int32_t c = f.counter;
            if (c > 0xc7) {
                if (c == 0xdb) f.cue = 2;
                else if (c == 0x119) f.cue = 0;
                else if (c == 0x12d) {
                    f.counter = c + 1;
                    s.subState = 0xa;
                    return;
                }
            } else if (c == 0xc7 || c == 0x41) {
                f.cue = 0;
            } else if (c == 0x14 || c == 0x55) {
                f.cue = 2;
            }
            f.counter = c + 1;
            return;
        }
        case 0xa: scriptSequenceSkip(1); return; // exe 0x5115c0(1)
        default: return;
        }
    }
    default: notPortedAt(kStoryPort, "script 0x62 step", routine.pointerValue());
    }
}
const DataTable<RoutineTable<3>> ridingStates{TableId::Ch4StoryRidingStates};
void ridingScript() { // exe 0x537b20
    ScriptState& s = scriptState();
    Player& p = player();
    switch (ridingStates->idAt(s.state)) {
    case RoutineId::Ch4RidingSetUp:
        switch (s.subState) {
        case 0:
            actorsSweep(0xffe, 0x80);                // exe 0x5e05d0
            scriptSequenceStart(stateAddress(TableId::Ch4StoryRidingCamera), stateAddress(TableId::Ch4StoryRidingSteps)); // exe 0x511390
            cameraShake(false, 0x40, 0);             // exe 0x40ea80
            scriptReplayPadOn();                     // exe 0x5117f0
            p.setDrawn(false);
            unskippable(true);
            sparkEmitterAt(stateAddress(TableId::Ch4StoryRidingFirstSparkPoint), 0x1c, 0);
            sparkEmitterAt(stateAddress(TableId::Ch4StoryRidingSecondSparkPoint), 0x1c, 0);
            sparkEmitterAt(stateAddress(TableId::Ch4StoryRidingThirdSparkPoint), 0x1c, 0);
            scriptSequenceTick(); // exe 0x5114d0
            s.subState = 1;
            [[fallthrough]];
        case 1:
            if (playerToScriptIfFree() != 0) return; // exe 0x572300
            playerScripted();
            p.pos.x = 0;
            p.pos.y = 0;
            p.pos.z = 0;
            p.rotation.y = 0;
            anim(0, 0);
            voiceClipPlay(0xc);
            s.subState = 2;
            return;
        case 2:
            if (!voiceClipPlaying()) return; // exe 0x601240
            s.enterState(CutsceneState::Steps);
            unskippable(false);
            return;
        default: return;
        }
    case RoutineId::ScriptCastStepsState: // the runner with the cast's copy of the timeline (game/script_engine)
        scriptStepsStateForCast(ridingStep);
        return;
    case RoutineId::Ch4RidingEnd:
        switch (s.subState) {
        case 0:
            hudBanner().hide();        // exe 0x5f2680
            cameraShake(true, 0, 0);   // exe 0x40ea80
            padVibrate(0, 0x1000000);  // exe 0x55cd10
            if (voiceClipPlaying()) voiceClipFadeOut(8); // exe 0x601220
            if (!(s.flags & kScriptSkipTaken)) scriptFade(0x12, 0);
            s.subState = 1;
            [[fallthrough]];
        case 1:
            if (!voiceClipIdle() || fadeBusy()) return;
            requestTransition(2, 9, 0, 0, 0, 0, 0, 2, 0x12);
            s.subState = 2;
            return;
        case 2:
            if (transitionRequest().state != 0) return;
            playerBackToPlay();   // exe 0x5723c0
            scriptReplayPadOff(); // exe 0x511890
            p.airborne = 0;
            p.animPaused = 0;
            p.setDrawn(true);
            gameState().nextStory += 1u; // exe 0x512800
            scriptEndBlock();            // exe 0x5116a0
            return;
        default: return;
        }
    default: notPortedAt(kStoryPort, "script 0x62 state", s.state);
    }
}

} // namespace

void script62TalkerAUpdate(const PlayerEnv&, uint32_t actor) { cueTalkerA(animatedActorAt(actor)); }
void script62TalkerBUpdate(const PlayerEnv&, uint32_t actor) { cueTalkerB(animatedActorAt(actor)); }
void brakingTrainUpdate(const PlayerEnv&, uint32_t actor) { brakingTrain(staticActorAt(actor)); }
void risingSmokeColumnUpdate(const PlayerEnv&, uint32_t actor) { smokeColumn(staticActorAt(actor)); }

bool ch4ScriptPorted(RoutineId handler) { return handler == RoutineId::Ch4ArrivalScript || handler == RoutineId::Ch4RidingScript; }
bool ch4ScriptTick(const PlayerEnv&, RoutineId handler) {
    switch (handler) {
    case RoutineId::Ch4ArrivalScript: arrivalScript(); return true;
    case RoutineId::Ch4RidingScript: ridingScript(); return true;
    default: return false;
    }
}

namespace {

// ---- ST55: the digger's licence exam (the stage's own room function 0x58c1f0, the room-script tick's case 0x55;
// sub-handlers [0x8cd858 + sub * 4], the handler's step; the difficulty <= 1 picks the easy groups) ----------------
constexpr uint8_t kExamTargetType = 0x2b;
constexpr uint32_t kFlagExamStarted = 0x24b, kFlagExamPassed = 0x249, kFlagTimeUp = 0x24a, kFlagExamOver = 0x24c,
                   kFlagTimeUpHandled = 0x680, kFlagDoorsLocked = 0x711, kFlagExitLocked = 0x712;
// the exam's clock (the stage's own block, its first word)
uint32_t& examClock() { return stageBlock().part<StageBlockHead>().word; }
bool easyExam() { return gameState().difficulty <= 1; }
bool noEvent() { return eventState().active == 0; }
// exe 0x58c800: the live statics of the stage table with the target type
int32_t examTargets() {
    int32_t n = 0;
    for (uint32_t w = 0; w < 2; ++w) {
        uint32_t i = w * 32;
        for (uint32_t v = actorSystem().staticsLive[w]; v != 0; v <<= 1, ++i) {
            if (int32_t(v) >= 0) continue;
            const StaticActor& s = staticPool()[i];
            if (s.type == kExamTargetType && (s.loopFlags & kActorStageClass)) ++n;
        }
    }
    return n;
}
// exe 0x58c860: the exam clock counts down; reaching 0 sets the time up
void examTimerTick() {
    uint32_t t = examClock();
    if (t == 0) return;
    examClock() = --t;
    if (t == 0) setStoryFlag(kFlagTimeUp);
}
// exe 0x58c880: the time up handled -> true; time up with no script, no event and the player's life >= 0 -> event
// message 0x12 (param 0xb), handled, true; else false
bool examTimeUp() {
    if (storyFlag(kFlagTimeUpHandled)) return true;
    if (!storyFlag(kFlagTimeUp) || scriptState().flags != 0 || !noEvent() || player().hitPoints < 0) return false;
    eventStartAt(0, nullptr, 0xb, 0, 0x12); // exe 0x543b40
    setStoryFlag(kFlagTimeUpHandled);
    return true;
}
// exe 0x58c450: the room's doors locked, enemy ids 16..1 no longer defeated (exe 0x5e63f0)
void examRoomArm() {
    setStoryFlag(kFlagDoorsLocked);
    setStoryFlag(kFlagExitLocked);
    for (uint32_t id = 16; id >= 1; --id) encounterState().defeated[id >> 5] &= ~(0x80000000u >> (id & 0x1f));
}
// the exam rooms 1..6 (0x58c380 / 0x58c490 / 0x58c550 / 0x58c610 / 0x58c6c0 / 0x58c780): 0: arm, the script slot
// spawns group lo (easy, -> 1) or hi (-> 2); 1 / 2: the room cleared (per-room test) -> 3; 3: no event -> the exit
// opens, 4. The clock ticks after every step.
void examRoom(RoutineId fn) {
    RoomScriptState& room = roomScriptState();
    if (examTimeUp()) return;
    struct R {
        RoutineId fn;
        int lo, hi;
    };
    static constexpr R kR[] = {{RoutineId::ExamRoom1Handler, 0, 7},   {RoutineId::ExamRoom2Handler, 2, 8},
                               {RoutineId::ExamRoom3Handler, 3, 9},   {RoutineId::ExamRoom4Handler, 4, 0xa},
                               {RoutineId::ExamRoom5Handler, 5, 0xb}, {RoutineId::ExamRoom6Handler, 6, 0xc}};
    const R* r = nullptr;
    for (const R& k : kR)
        if (k.fn == fn) r = &k;
    if (fn == RoutineId::ExamRoom6Handler) {
        // room 6: 0 -> spawn, 1 (same tick); 1: cleared -> 2 (same tick); 2: no event -> exit open, 3
        uint8_t s = room.handlerStep;
        if (s == 0) {
            examRoomArm();
            encounterSpawnGroupScriptSlot(easyExam() ? r->lo : r->hi); // exe 0x5e66b0
            room.handlerStep += 1u;
            s = 1;
        }
        if (s == 1) {
            if (encounterScriptSlotAlive() != 0) {
                examTimerTick();
                return;
            }
            room.handlerStep += 1u;
            s = 2;
        }
        if (s == 2 && noEvent()) {
            clearStoryFlag(kFlagExitLocked);
            room.handlerStep += 1u;
        }
        examTimerTick();
        return;
    }
    switch (room.handlerStep) {
    case 0:
        if (fn == RoutineId::ExamRoom1Handler) setStoryFlag(kFlagExamStarted); // room 1: the exam started
        examRoomArm();
        if (easyExam()) {
            encounterSpawnGroupScriptSlot(r->lo);
            room.handlerStep = 1;
        } else {
            encounterSpawnGroupScriptSlot(r->hi);
            room.handlerStep = 2;
        }
        if (fn == RoutineId::ExamRoom4Handler && encounterScriptSlotAlive() == 0) room.handlerStep = 3;
        // room 4 tests step 1 at once
        break;
    case 1:
        if (encounterScriptSlotAlive() != 0) break;
        if (fn == RoutineId::ExamRoom1Handler && !enemyDefeated(6)) break; // exe 0x5e6420(6)
        if (fn == RoutineId::ExamRoom5Handler && examTargets() != 0) break;
        room.handlerStep = 3;
        break;
    case 2:
        if (fn == RoutineId::ExamRoom3Handler) { // room 3: exactly one left
            if (encounterScriptSlotAlive() != 1) break;
        } else {
            if (encounterScriptSlotAlive() != 0) break;
            if ((fn == RoutineId::ExamRoom2Handler || fn == RoutineId::ExamRoom4Handler) && examTargets() != 0) break;
        }
        room.handlerStep = 3;
        break;
    case 3:
        if (!noEvent()) break;
        clearStoryFlag(kFlagExitLocked);
        room.handlerStep += 1u;
        break;
    default: break;
    }
    examTimerTick();
}
// exe 0x58c2a0 (room 0, the entrance): 0: 0x710 / 0x711 cleared, the exit locked; the exam not started yet: passed /
// time up / over cleared, the clock 0x1518 (easy) or 0x2328, event message 0x10 / 0x16, -> 1; started -> 2. 2: no
// script -> event message 0x11, passed, 3
void examEntrance() {
    RoomScriptState& room = roomScriptState();
    if (room.handlerStep == 0) {
        clearStoryFlag(0x710);
        clearStoryFlag(kFlagDoorsLocked);
        setStoryFlag(kFlagExitLocked);
        if (storyFlag(kFlagExamStarted)) {
            room.handlerStep = 2;
            return;
        }
        clearStoryFlag(kFlagExamPassed);
        clearStoryFlag(kFlagTimeUp);
        clearStoryFlag(kFlagExamOver);
        const bool easy = easyExam();
        examClock() = easy ? 0x1518u : 0x2328u;
        eventStartAt(0, nullptr, 0, 0, easy ? 0x10 : 0x16);
        room.handlerStep += 1u;
        return;
    }
    if (room.handlerStep == 2 && scriptState().flags == 0) {
        eventStartAt(0, nullptr, 0, 0, 0x11);
        setStoryFlag(kFlagExamPassed);
        room.handlerStep += 1u;
    }
}
// exe 0x58c1f0: the sub's handler; the door flag -> script 0 (game/transition's door test, run after this); else
// once (the exam over) with no script and no event, passed or timed out: the loadout back (exe 0x542810), event
// message 0x14 (entered from ST1B, flag 0x248) / 0x15 (param 0xb), the exam over
// (exe table 0x8cd858: the handlers by sub-room, named by the source)
const DataTable<RoutineTable<7>> examHandlers{TableId::Ch4ExamHandlers};
constexpr RoutineId kExamEntrance = RoutineId::Ch4StoryExamEntrance;
void examStage() {
    const RoutineId fn = examHandlers[gameState().room].id();
    if (fn == kExamEntrance) examEntrance();
    else examRoom(fn);
    if (storyFlag(kDoorFlag)) return;
    if (storyFlag(kFlagExamOver) || scriptState().flags != 0 || !noEvent()) return;
    if (!storyFlag(kFlagExamPassed) && !storyFlag(kFlagTimeUpHandled)) return;
    loadoutRestore();
    eventStartAt(0, nullptr, 0xb, 0, examEnteredFromSt1b() ? 0x14 : 0x15);
    setStoryFlag(kFlagExamOver);
}

} // namespace

bool ch4RoomHandler(RoutineId fn, bool run) {
    RoomScriptState& room = roomScriptState();
    for (const PadSub& p : kPadSubs) {
        if (p.fn != fn) continue;
        if (!run || room.handlerStep != 0) return true;
        spawnRecords(stateAddress(TableId::Ch4StorySt40PadSpawns), 4);
        for (uint32_t i = 0; i < 4; ++i) setPadStill(room.actors[i], p.still[i]);
        if (p.n) {
            spawnRecords(stateAddress(*p.extra), p.n);
        }
        if (p.restore) musicRestore();
        else musicDip();
        room.handlerStep += 1u;
        return true;
    }
    switch (fn) {
    case RoutineId::St40Sub0RoomHandler:
        // ST40 sub 0: 0: pads 0x8cc710 x 4 + e0 0x8cc6e4 x 2, the music dipped unless flag 0x2f6 without
        // 0x3e1 -> 1 (same tick); 1: flag 0x2f6 without 0x3e1 -> records 0x8cc760 x 0xb (the crew), music restored -> 2
        if (!run) return true;
        if (room.handlerStep == 0) {
            spawnRecords(stateAddress(TableId::Ch4StorySt40Sub0Spawns), 4);
            spawnRecords(stateAddress(TableId::Ch4StorySt40Sub0MoreSpawns), 2);
            if (storyFlag(0x3e1) || !storyFlag(0x2f6)) musicDip();
            room.handlerStep += 1u;
        }
        if (room.handlerStep == 1 && !storyFlag(0x3e1) && storyFlag(0x2f6)) {
            spawnRecords(stateAddress(TableId::Ch4StorySt40Sub0LateSpawns), 0xb);
            musicRestore();
            room.handlerStep += 1u;
        }
        return true;
    case RoutineId::St53StoryFSub1RoomHandler:
        // ST53 sub 1 at story 0xf: step 0: without flag 0x5a1 that flag + script 0x61, else without flag
        // 0x5a2 that flag + script 0x62; step + 1 either way
        if (!run || room.handlerStep != 0) return true;
        if (!storyFlag(0x5a1)) {
            scriptRun(0x61); // exe 0x511270
            setStoryFlag(0x5a1);
            room.handlerStep += 1u;
            return true;
        }
        if (!storyFlag(0x5a2)) {
            scriptRun(0x62);
            setStoryFlag(0x5a2);
        }
        room.handlerStep += 1u;
        return true;
    case RoutineId::St52RoomRoutine:
        // ST52 (the stage's own room function, the room-script tick's case 0x52): sub 0, step 0 -> script
        // 0x3f, step + 1
        if (!run) return true;
        if (gameState().room == 0 && room.handlerStep == 0) {
            scriptRun(0x3f);
            room.handlerStep += 1u;
        }
        return true;
    case RoutineId::St40Sub1RoomHandler:
        // ST40 sub 1 (script 0x69's arrival): 0: music dipped -> 1 (same tick); 1: without flag 0x150 that
        // flag + script 0x6a; else records 0x8cc840 x 3 with flag 0x2f6 without 0x3e1, else x 2 -> 2
        if (!run) return true;
        if (room.handlerStep == 0) {
            musicDip();
            room.handlerStep += 1u;
        }
        if (room.handlerStep != 1) return true;
        if (!storyFlag(0x150)) {
            setStoryFlag(0x150);
            scriptRun(0x6a); // exe 0x511270
            return true;
        }
        spawnRecords(stateAddress(TableId::Ch4StorySt40Sub1Spawns), storyFlag(0x2f6) && !storyFlag(0x3e1) ? 3 : 2);
        room.handlerStep += 1u;
        return true;
    case RoutineId::St43Sub0RoomHandler:
        // ST43 sub 0: 0 -> 1 with the countdown 0x14; 1: it counts down, at 0 -> 2 and (once, flag 0x5a0)
        // the e0 type 0x1b controller variant 0 (word 0x10)
        if (!run || doorTest()) return true;
        if (room.handlerStep == 0) {
            room.handlerByte1 = 0x14;
            room.handlerStep = 1;
        } else if (room.handlerStep == 1) {
            room.handlerByte1 -= 1u;
            if (room.handlerByte1 != 0) return true;
            room.handlerStep = 2;
            if (storyFlag(0x5a0)) return true;
            setStoryFlag(0x5a0);
            spawnStageController(0, 0x10);
        }
        return true;
    case RoutineId::St43Sub2RoomHandler:
        // ST43 sub 2: 0 -> 1, without flag 0x170 the door lock 0x710 + records 0x8ccbb0 x 5 (4 statics
        // 0x59bee0 + the e0 0x423170 that sets 0x170); 1 (and the same tick): flag 0x170 opens 0x710
        if (!run || doorTest()) return true;
        if (room.handlerStep == 0) {
            room.handlerStep = 1;
            if (!storyFlag(0x170)) {
                setStoryFlag(0x710);
                spawnRecords(stateAddress(TableId::Ch4StorySt43Sub2Spawns), 5);
            }
        } else if (room.handlerStep != 1) {
            return true;
        }
        if (storyFlag(0x170)) clearStoryFlag(0x710);
        return true;
    case RoutineId::St45Sub1RoomHandler:
        // ST45 sub 1: 0 -> 1, without flag 0x174 the door lock 0x710 + the script slot's group 0 (the
        // area boss, class 0x4c78e0); 1 (and the same tick): flag 0x174 opens 0x710
        if (!run || doorTest()) return true;
        if (room.handlerStep == 0) {
            room.handlerStep = 1;
            if (!storyFlag(0x174)) {
                setStoryFlag(0x710);
                encounterSpawnGroupScriptSlot(0); // exe 0x5e66b0(0)
            }
        } else if (room.handlerStep != 1) {
            return true;
        }
        if (storyFlag(0x174)) clearStoryFlag(0x710);
        return true;
    case RoutineId::St40Sub2RoomHandler:
        // ST40 sub 2: pads 0x8cc694 x 3 (the room's actors 1..3: still / animated / animated) + e0 0x8cc6d0,
        // music dipped
        if (!run || room.handlerStep != 0) return true;
        spawnRecords(stateAddress(TableId::Ch4StorySt40Sub2Spawns), 3);
        setPadStill(room.actors[1], 1);
        setPadStill(room.actors[2], 0);
        setPadStill(room.actors[3], 0);
        spawnRecords(stateAddress(TableId::Ch4StorySt40Sub2MoreSpawns), 1);
        musicDip();
        room.handlerStep += 1u;
        return true;
    // ---- story 0xa in Nino (ST17 table 0x8c7414, ST19 0x8c7978, ST1B 0x8c7d08 by story) ----
    case RoutineId::St17Sub0RoomHandler:
        // ST17 sub 0: once 0x710 cleared, records 0x8c7468 x 2 + 0x8c75d8 x 1, the handler's word 1
        if (!run) return true;
        if (room.handlerStep == 0) {
            clearStoryFlag(0x710);
            spawnRecords(stateAddress(TableId::NinoReturnRoomsReturnRoomSpawns), 2);
            spawnRecords(stateAddress(TableId::Ch2RoomsSt17LateSpawns), 1);
            room.enterHandlerStep(1);
        }
        return true;
    case RoutineId::St19Sub0RoomHandler:
        // ST19 sub 0: once 0x710 / 0x713 / 0x715 cleared, 0x711 / 0x712 / 0x714 set, the lock light at
        // 0x8c7b68 (exe 0x57fcd0)
        if (!run) return true;
        if (room.handlerStep == 0) {
            room.handlerStep = 1;
            clearStoryFlag(0x710);
            setStoryFlag(0x711);
            setStoryFlag(0x712);
            clearStoryFlag(0x713);
            setStoryFlag(0x714);
            clearStoryFlag(0x715);
            ninoLockEffect(stateAddress(TableId::Ch2RoomsLockLightC));
        }
        return true;
    case RoutineId::St19Sub1RoomHandler:
        // ST19 sub 1: once, with flag 0x3da: 0x710 cleared, the 4 cells (0x40 / 0x3f, 0x40 / 0x3f) to their
        // segment variant 1 (exe 0x58d5f0(cell, 1)); every tick flag 0x38 -> flag 0x39
        if (!run) return true;
        if (room.handlerStep == 0) {
            room.handlerStep = 1;
            if (storyFlag(0x3da)) {
                clearStoryFlag(0x710);
                switchCellSegmentVariant(ninoGridCell(0x40, 0x40), 1);
                switchCellSegmentVariant(ninoGridCell(0x3f, 0x40), 1);
                switchCellSegmentVariant(ninoGridCell(0x40, 0x3f), 1);
                switchCellSegmentVariant(ninoGridCell(0x3f, 0x3f), 1);
            }
        }
        if (storyFlag(0x38)) setStoryFlag(0x39);
        return true;
    case RoutineId::St1BSub1RoomHandler: // ST1B sub 1: the digger's licence exam (the handler's step)
        // 0: 0x710 cleared; flag 0x246 (back from the exam) -> life to max + 1 (exe 0x5e5320), event message 0x14,
        //    0x246 cleared, 2; else 1
        // 1: flag 0x246 (the exam taken), no event -> the exam's loadout (exe 0x542730), the exam's start cleared,
        //    event message 0x92 (param 0xb)
        // 2: no event -> 0x247 cleared; passed -> the difficulty + 1, its enemy tables (exe 0x50f260); difficulty 2:
        //    flag 0x3d6 (0x3d5 / 0x3d7 cleared) + 3000 to the balance, difficulty 3: flag 0x3d5 + 8000; -> 1
        if (!run) return true;
        switch (room.handlerStep) {
        case 0:
            clearStoryFlag(0x710);
            if (storyFlag(0x246)) {
                hitPointsChange(player(), player().maxHitPoints + 1);
                eventStartAt(0, nullptr, 0, 0, 0x14); // exe 0x543b40
                clearStoryFlag(0x246);
                room.handlerStep = 2;
            } else {
                room.handlerStep += 1u;
            }
            return true;
        case 1:
            if (!storyFlag(0x246) || !noEvent()) return true;
            examLoadoutGive();
            clearStoryFlag(kFlagExamStarted);
            eventStartAt(0, nullptr, 0xb, 0, 0x92);
            return true;
        case 2:
            if (!noEvent()) return true;
            clearStoryFlag(0x247);
            if (storyFlag(kFlagExamPassed)) {
                GameState& g = gameState();
                g.difficulty += 1u;
                selectDifficultyStats(g.difficulty);
                if (g.difficulty == 2) {
                    clearStoryFlag(0x3d7);
                    setStoryFlag(0x3d6);
                    clearStoryFlag(0x3d5);
                    balanceAdd(0xbb8); // exe 0x5e51d0
                } else if (g.difficulty == 3) {
                    clearStoryFlag(0x3d7);
                    clearStoryFlag(0x3d6);
                    setStoryFlag(0x3d5);
                    balanceAdd(0x1f40);
                }
            }
            room.handlerStep = 1;
            return true;
        default: return true;
        }
    case RoutineId::St1BSub2RoomHandler: // ST1B sub 2: once records 0x8c7ea8 x 2 (0x710 cleared)
        if (!run) return true;
        if (room.handlerStep == 0) {
            spawnRecords(stateAddress(TableId::Ch2RoomsSt1BArrivalSpawns), 2);
            clearStoryFlag(0x710);
            room.handlerStep += 1u;
        }
        return true;
    case RoutineId::St55RoomRoutine:
        // ST55 (the stage's own room function, the room-script tick's case 0x55): the licence exam
        if (run && gameState().room <= 6) examStage();
        return gameState().room <= 6;
    default: return false;
    }
}

} // namespace od::port

// ST40 room table (kPadSubs) and ST55 exam rooms (examStage) kHandlersTranscribed: 0x589990 0x5899f0 0x589a40
// 0x589aa0 0x589af0 0x589b50 0x589ba0 0x589bf0 0x589c40 0x589c90 kHandlersTranscribed: 0x589ce0 0x58c2a0 0x58c380
// 0x58c490 0x58c550 0x58c610 0x58c6c0 0x58c780
