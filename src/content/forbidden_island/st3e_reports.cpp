// The ST3E talks, their speakers and the floating model (see st3e_reports.hpp). The scripts' states are their own
// exe tables' (0x8ac930 / 0x8aca60 / 0x8acb90 / 0x8accc0: state 0, the talk scripts' runner exe 0x52f420, state 2);
// their steps are dispatched by the named routine. Step ticks are the talks' timeline.
#include "content/forbidden_island/st3e_reports.hpp"
#include "content/forbidden_island/st3e_reports_state.hpp"

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"
#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/cutscene_actor.hpp"
#include "game/fade_state.hpp"
#include "game/field_alias.hpp"
#include "game/game_state.hpp"
#include "game/message.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"
#include "game/transition.hpp"
#include "game/transition_state.hpp"

using namespace od::port::st3e_reports_state;

namespace od::port {

using namespace st3e_reports_state;

namespace {

// ---- the talks --------------------------------------------------------------------------------------------------------
constexpr uint32_t kTalkBank = 0x73c000; // the talks' message bank (exe data)
constexpr int8_t kTalkWindow = 0;
constexpr uint8_t kSpeakerTalkAnim = 3;  // the speaker's talking animation (its +0xc request)
// the speakers: the first and the second script actor
constexpr size_t kFirstSpeaker = 0, kSecondSpeaker = 1;
// script 0x64's flags: its first talk done, its second; the question's answer; a second flag it clears with it
constexpr uint32_t kFlagFirstTalkDone = kFlagSt3eStory2Event, kFlagSecondTalkDone = 0x5a1;
constexpr uint32_t kFlagAnswer = 0x680, kFlagAnswerOther = 0x681;
constexpr uint32_t kFlagAfterSecondTalk[2] = {0xe0, 0xe1};
// the request back to ST3E sub 1 at (0, 0, -0x80) facing a half turn
constexpr uint8_t kSt3e = 0x3e, kReturnRoom = 1;
constexpr int16_t kReturnZ = -0x80, kReturnYaw = 0x800;
constexpr uint8_t kReturnFadeIn = 2;
constexpr uint32_t kFadeIn = 2, kFadeOut = 0x12;
constexpr int16_t kWalkInSpeed = -0x50; // the walk in, along the player's facing
constexpr int kWalkInTicks = 0x1f;

ScriptState& block() { return scriptState(); }
uint8_t phase() { return block().stepPhase; }
void nextPhase() { ++block().stepPhase; }
bool talkWindowOpen() { return windowStatus(kTalkWindow) != 0; } // exe 0x557810(0)
void openTalk(int16_t message) { windowOpen(kTalkWindow, kTalkBank, message); }
void speakerTalks(size_t speaker) { (*block().actors[speaker]).params[0] = kSpeakerTalkAnim; }

// the plain message step: the message opened, the speaker talks, the next step once the window is closed
void messageStep(int16_t message, size_t speaker) {
    switch (phase()) {
    case 0:
        openTalk(message);
        nextPhase();
        return;
    case 1:
        speakerTalks(speaker);
        nextPhase();
        return;
    case 2:
        if (!talkWindowOpen()) scriptNextStep();
        return;
    default: return;
    }
}
// the last message of a talk: then a fade, the next step once it is done
void lastMessageStep(int16_t message, size_t speaker) {
    switch (phase()) {
    case 0:
        openTalk(message);
        nextPhase();
        return;
    case 1:
        speakerTalks(speaker);
        nextPhase();
        return;
    case 2:
        if (!talkWindowOpen()) {
            scriptFade(kFadeOut, 0);
            nextPhase();
        }
        return;
    case 3:
        if (fadeState().idle()) scriptNextStep();
        return;
    default: return;
    }
}
// the player walks in for 0x20 ticks (exe 0x538730 / 0x538dc0 ...), then the talk's first message (the first speaker)
void walkInStep(int16_t message) {
    switch (phase()) {
    case 0:
        playerScriptAnim(PlayerAnim::Walk, 0, true);
        nextPhase();
        [[fallthrough]];
    case 1:
        if (kWalkInTicks < block().stepTick) {
            playerScriptAnim(PlayerAnim::Talk, 0, true);
            nextPhase();
            return;
        }
        integrateVelocity(player(), 0, 0, kWalkInSpeed);
        return;
    case 2:
        openTalk(message);
        nextPhase();
        return;
    case 3:
        speakerTalks(kFirstSpeaker);
        nextPhase();
        return;
    case 4:
        if (!talkWindowOpen()) scriptNextStep();
        return;
    default: return;
    }
}
// the question (exe 0x538920: message 0x74, then 0x5389b0: 0x75 again until the answer sets kFlagAnswer): first, the
// answer skips the repeat
void questionStep(int16_t message, size_t speaker, bool first) {
    switch (phase()) {
    case 0:
        openTalk(message);
        nextPhase();
        return;
    case 1:
        speakerTalks(speaker);
        nextPhase();
        return;
    case 2:
        if (!talkWindowOpen()) nextPhase();
        return;
    case 3: {
        const bool answered = storyFlag(kFlagAnswer);
        block().clearStepPhase();
        if (first) scriptSequenceSkip(answered ? 2 : 1);
        else if (answered) scriptSequenceSkip(1);
        return;
    }
    default: return;
    }
}

// ---- script 0x64 (exe 0x538520): the first visit -- messages 0x70..0x77 (the question 0x74 / 0x75), then back to sub
// 1; the second -- messages 0x7c / 0x7d, then a fade and the block ends in place ---------------------------------------


void step64(const RoutineRef& routine) {
    switch (routine.id()) {
    case RoutineId::St3eReport64WalkInStep: walkInStep(0x70); return;
    case RoutineId::St3eReport64Step2: messageStep(0x71, kSecondSpeaker); return;
    case RoutineId::St3eReport64Step3: messageStep(0x72, kFirstSpeaker); return;
    case RoutineId::St3eReport64Step4: messageStep(0x73, kSecondSpeaker); return;
    case RoutineId::St3eReport64Step5: questionStep(0x74, kFirstSpeaker, true); return;
    case RoutineId::St3eReport64Step6: questionStep(0x75, kSecondSpeaker, false); return;
    case RoutineId::St3eReport64Step7:
        if (phase() == 0) scriptCamera().script = stateAddress(TableId::St3eReportsFirstTalkLateCamera);
        messageStep(0x76, kFirstSpeaker);
        return;
    case RoutineId::St3eReport64Step8: lastMessageStep(0x77, kSecondSpeaker); return;
    case RoutineId::St3eReport64Step9: messageStep(0x7c, kFirstSpeaker); return;
    case RoutineId::St3eReport64Step10: lastMessageStep(0x7d, kFirstSpeaker); return;
    default: notPortedAt(kStoryPort, "script 0x64 step", routine.pointerValue());
    }
}
// the player taken over for a talk: talking, its motion and its face cleared, state 1
void takePlayerForTalk() {
    if (playerToScriptIfFree() != 0) return;
    playerScriptAnim(PlayerAnim::Talk, 0, true);
    Player& p = player();
    p.clearDirectionOffsetWords();
    p.idleFaces[0] = 0;
    p.idleFaces[1] = 0;
    block().enterState(CutsceneState::Steps);
}
// exe 0x538540: the first talk or the second by its flag, the sequence on the replay pad, the answer cleared
void script64SetUp() {
    ScriptState& s = block();
    if (s.subState == 0) {
        Script64Bytes& bytes = scriptState().part<Script64Bytes>();
        uint32_t camera, steps;
        if (!storyFlag(kFlagFirstTalkDone)) {
            bytes.secondTalk = 0;
            setStoryFlag(kFlagFirstTalkDone);
            steps = stateAddress(TableId::St3eReportsFirstTalkSteps);
            camera = stateAddress(TableId::St3eReportsFirstTalkCamera);
        } else {
            bytes.secondTalk = 1;
            setStoryFlag(kFlagSecondTalkDone);
            steps = stateAddress(TableId::St3eReportsSecondTalkSteps);
            camera = stateAddress(TableId::St3eReportsSecondTalkCamera);
        }
        scriptSequenceStart(camera, steps);
        scriptReplayPadOn();
        clearStoryFlag(kFlagAnswer);
        clearStoryFlag(kFlagAnswerOther);
        ++s.subState;
    } else if (s.subState != 1) {
        scriptSequenceTick();
        return;
    }
    takePlayerForTalk();
    scriptSequenceTick();
}
// the wrap-up's first part: the actors swept, the player idle, the windows closed (once); false while the fade or the
// talk window runs
bool talkClosed() {
    ScriptState& s = block();
    if (s.stepPhase == 0) {
        actorsSweep(kSweepActors, kSweepFree);
        playerScriptAnim(PlayerAnim::Idle, 0, true);
        windowCloseAll(WindowClose::Gentle);
        ++s.stepPhase;
    }
    return fadeState().idle() && !talkWindowOpen();
}
void requestReturn() {
    requestTransition(kTransitionAccepted, kSt3e, kReturnRoom, 0, 0, kReturnZ, kReturnYaw, kReturnFadeIn, kNoFade);
}
// exe 0x538610: the first talk asks for sub 1 (the block ends once it is taken); the second fades in place, its flags
void script64WrapUp() {
    ScriptState& s = block();
    if (s.subState == 0) {
        if (!talkClosed()) return;
        if (scriptState().part<Script64Bytes>().secondTalk == 0) {
            requestReturn();
            ++s.subState;
            return;
        }
        scriptFade(kFadeIn, 0);
        scriptReplayPadOff();
        playerBackToPlay();
        for (uint32_t f : kFlagAfterSecondTalk) setStoryFlag(f);
        scriptEndBlock();
    } else if (s.subState == 1 && transitionRequest().state == kTransitionNone) {
        scriptReplayPadOff();
        playerBackToPlay();
        scriptEndBlock();
    }
}
void script64() {
    switch (block().cutsceneState()) { // [0x8ac930]
    case CutsceneState::SetUp: script64SetUp(); return;
    case CutsceneState::Steps: scriptStepsStateNoFaces(&step64); return;
    case CutsceneState::WrapUp: script64WrapUp(); return;
    default: notPortedAt(kStoryPort, "script 0x64 state", block().state);
    }
}

// ---- scripts 0x65 / 0x66 / 0x67 (exe 0x538c30 / 0x539010 / 0x5393f0): a ruin key reported -- the walk in and four
// messages (m .. m + 3), then back to sub 1, the report's flag cleared and the next story ---------------------------
struct KeyReport {
    RoutineId handler; // the script's handler (the named routine)
    TableId camera, steps; // the camera script and the steps
    RoutineId stepRoutines[4];
    int16_t firstMessage;
    uint32_t flag;
    const char* name;
};
constexpr KeyReport kKeyReports[3] = {
    {RoutineId::St3eKeyReport65, TableId::St3eReportsReport65Camera, TableId::St3eReportsReport65Steps,
     {RoutineId::St3eReport65WalkInStep, RoutineId::St3eReport65SecondSpeakerStep,
      RoutineId::St3eReport65FirstSpeakerStep, RoutineId::St3eReport65LastStep},
     0x80, 0x3d0, "script 0x65"},
    {RoutineId::St3eKeyReport66, TableId::St3eReportsReport66Camera, TableId::St3eReportsReport66Steps,
     {RoutineId::St3eReport66WalkInStep, RoutineId::St3eReport66SecondSpeakerStep,
      RoutineId::St3eReport66FirstSpeakerStep, RoutineId::St3eReport66LastStep},
     0x90, 0x3d1, "script 0x66"},
    {RoutineId::St3eKeyReport67, TableId::St3eReportsReport67Camera, TableId::St3eReportsReport67Steps,
     {RoutineId::St3eReport67WalkInStep, RoutineId::St3eReport67SecondSpeakerStep,
      RoutineId::St3eReport67FirstSpeakerStep, RoutineId::St3eReport67LastStep},
     0xa0, 0x3d2, "script 0x67"},
};
// the step routines of report K (the runner's routine table)
template <size_t K> void reportStep(const RoutineRef& routine) {
    const KeyReport& k = kKeyReports[K];
    if (routine.is(k.stepRoutines[0])) walkInStep(k.firstMessage);
    else if (routine.is(k.stepRoutines[1])) messageStep(int16_t(k.firstMessage + 1), kSecondSpeaker);
    else if (routine.is(k.stepRoutines[2])) messageStep(int16_t(k.firstMessage + 2), kFirstSpeaker);
    else if (routine.is(k.stepRoutines[3])) lastMessageStep(int16_t(k.firstMessage + 3), kSecondSpeaker);
    else notPortedAt(kStoryPort, k.name, routine.pointerValue());
}
template <size_t K> void reportScript() {
    const KeyReport& k = kKeyReports[K];
    ScriptState& s = block();
    switch (s.cutsceneState()) {
    case CutsceneState::SetUp:
        if (s.subState == 0) {
            scriptSequenceStart(stateAddress(k.camera), stateAddress(k.steps));
            scriptReplayPadOn();
            ++s.subState;
        } else if (s.subState != 1) {
            scriptSequenceTick();
            return;
        }
        takePlayerForTalk();
        scriptSequenceTick();
        return;
    case CutsceneState::Steps:
        scriptStepsStateNoFaces(&reportStep<K>);
        return;
    case CutsceneState::WrapUp:
        if (s.subState == 0) {
            if (!talkClosed()) return;
            requestReturn();
            ++s.subState;
        } else if (s.subState == 1 && transitionRequest().state == kTransitionNone) {
            scriptReplayPadOff();
            playerBackToPlay();
            clearStoryFlag(k.flag);
            ++gameState().nextStory; // exe 0x512800
            scriptEndBlock();
        }
        return;
    default: notPortedAt(kStoryPort, k.name, s.state);
    }
}

// ---- the speaker (exe 0x445660, pool 0x20 type 0) ----------------------------------------------------------------------
// The speaker's variants (exe data): its model variant and shadow (0x88e1d8 + v * 4: the shadow is render output) and
// its collision box (0x88e1a8 + v * 0xc)
struct SpeakerModel : DataRecord {
    static constexpr uint32_t kBytes = 0x1;
    uint8_t modelVariant = 0; // +0x0
    template <class R> static SpeakerModel load(const R& r) {
        SpeakerModel v;
        v.modelVariant = r.template read<uint8_t>(0x0);
        return v;
    }
};
constexpr uint32_t kSpeakerBoxSize = 0xc;
constexpr uint8_t kNoRequest = 0xff;
constexpr int8_t kFacePlayer = 0x20;
constexpr int16_t kDefaultTurnStep = 0x40;
enum class SpeakerState : uint8_t { Bind = 0, Stand = 1 };

// the position the speaker faces: script actor `slot`'s or the player's (the exe reads the actor word at slot * 4
// unchecked: a negative slot reads the script block before the actors, an ActorLayoutError here)
const Fixed16Vec3& facedPosition(int8_t slot) {
    if (slot >= kFacePlayer) return player().pos;
    if (slot < 0)
        throw ActorLayoutError("the speaker's faced actor: a negative script actor slot (the exe reads the script "
                               "block before the actors)");
    return actorLayoutOf(*scriptState().actors[size_t(slot)]).pos;
}

void speaker(const PlayerEnv& env, AnimatedActor& a) {
    SpeakerFields& f = classState<SpeakerFields>(a);
    a.savePreviousPose();
    a.prevAnim = a.anim;
    const uint32_t v = a.variant;
    if (a.state == uint8_t(SpeakerState::Bind)) {
        if (!bindActorModel(a, dataRecord<SpeakerModel>(stateAddress(TableId::St3eReportsSpeakerModels) + v * 4, SpeakerModel::kBytes).modelVariant)) {
            freeActor(a);
            return;
        }
        a.collisionBox.pointAt(stateAddress(TableId::St3eReportsSpeakerBoxes) + v * kSpeakerBoxSize);
        a.clearClassBlock();
        const int16_t yaw = a.rotation.y;
        a.enterState(uint8_t(SpeakerState::Stand));
        a.anim = 0;
        a.prevAnim = kNoAnimShown;
        f.targetYaw = yaw;
        f.turnStep = 0;
    }
    if (!(a.flags & kActorLive)) return;
    if (f.animRequest != kNoRequest) {
        a.anim = f.animRequest;
        a.prevAnim = kNoAnimShown;
        f.animRequest = kNoRequest;
    }
    if (f.yawOffset != 0) {
        f.targetYaw = int16_t(int(f.yawOffset) * 4 + a.rotation.y);
        f.yawOffset = 0;
    }
    if (f.turnRequest != 0) {
        f.turnStep = int16_t((uint32_t(int32_t(f.turnRequest)) & 0x3ffu) << 2);
        f.turnRequest = 0;
    }
    if (uint8_t(f.faceRequest) != kNoRequest) {
        f.targetYaw = int16_t(angleOf(facedPosition(f.faceRequest), a.pos));
        if (f.turnStep == 0) f.turnStep = kDefaultTurnStep;
    }
    if (f.turnStep != 0 && turnYawTo(a.rotation.y, uint32_t(int32_t(f.targetYaw)), f.turnStep) == 0) {
        const int16_t yaw = a.rotation.y;
        f.turnStep = 0;
        f.targetYaw = yaw;
    }
    a.pos.y.whole = int16_t(actorGround(env, a));
    if (a.anim == a.prevAnim) {
        if ((f.holdPose & 3) == 0) {
            animStepPose(a);
        } else {
            animDecodeCurrent(a);
            animPose(a);
        }
    } else {
        animSetPose(a, a.anim, 0);
    }
    // exe 0x5f20f0: the shadow (render output)
}

// ---- the floating model (exe 0x5b16b0, static) ----------------------------------------------------------------------
// The model's class bytes: its home height (its spawn parameters' bytes, kept as a 16.16 value) and the bob's phase
// (the sub-state byte)
struct FloatingModelFields {
    explicit FloatingModelFields(const StaticActor& actor) : FloatingModelFields(const_cast<StaticActor&>(actor)) {}
    explicit FloatingModelFields(StaticActor& actor) : homeY(actor.params, 0) {}
    BytesField<int32_t, 4> homeY;
};
const DataTable<CollisionBoxRecord> floatingModelBox{TableId::St3eReportsFloatingModelBox};
constexpr int32_t kBobHeight = 0x200;

void floatingModel(StaticActor& a) {
    FloatingModelFields f(a);
    if (a.state == 0) {
        if (!bindActorModel(a, a.variant)) {
            freeActor(a);
            return;
        }
        a.collisionBox.pointAt(floatingModelBox.stateAddress());
        a.enterState(1);
        a.airborne = kAirborneFalling;
        a.rotation.z = 0;
        a.rotation.x = 0;
        a.velocity.z = 0;
        a.velocity.y = 0;
        a.velocity.x = 0;
        a.acceleration.z = 0;
        a.acceleration.y = 0;
        a.acceleration.x = 0;
        a.subState = a.params[0];
        f.homeY = a.pos.y;
    }
    const uint8_t t = a.subState;
    a.rotation.y = int16_t(uint32_t(t & 0x7f) << 5);
    a.pos.y = int32_t(uint32_t(int32_t(fixedSin(int(t) * 16)) * kBobHeight) + uint32_t(f.homeY));
    a.subState = uint8_t(t + 1u);
}

} // namespace

bool st3eReportScriptPorted(RoutineId handler) {
    if (handler == RoutineId::St3eScript64) return true;
    for (const KeyReport& k : kKeyReports)
        if (handler == k.handler) return true;
    return false;
}
bool st3eReportScriptTick(const PlayerEnv&, RoutineId handler) {
    if (handler == RoutineId::St3eScript64) script64();
    else if (handler == kKeyReports[0].handler) reportScript<0>();
    else if (handler == kKeyReports[1].handler) reportScript<1>();
    else if (handler == kKeyReports[2].handler) reportScript<2>();
    else return false;
    return true;
}

void st3eSpeakerUpdate(const PlayerEnv& env, uint32_t actor) { speaker(env, animatedActorAt(actor)); }
void st3eFloatingModelUpdate(const PlayerEnv&, uint32_t actor) { floatingModel(staticActorAt(actor)); }

} // namespace od::port
