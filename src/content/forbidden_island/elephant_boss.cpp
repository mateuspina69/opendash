// One routine per exe routine (addresses in the comments); the names are hypotheses from the observed behaviour.
// The draw-only parts (draw commands, OT packets, shadows) are left out as in the other story classes.
#include "content/forbidden_island/elephant_boss.hpp"
#include "content/forbidden_island/elephant_boss_state.hpp"

#include <algorithm>
#include <array>

#include "engine/routine_ids.hpp"
#include "game/patched_data.hpp"
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
#include "game/collision.hpp"
#include "game/cutscene_actor.hpp"
#include "game/display.hpp"
#include "game/distances.hpp"
#include "game/draw_commands.hpp"
#include "game/dust_puff.hpp"
#include "game/encounter.hpp"
#include "game/explosions.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/message.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/projection_state.hpp"
#include "game/random.hpp"
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
#include "game/vectors.hpp"
#include "game/visibility.hpp"

using namespace od::port::elephant_boss_state;

namespace od::port {

using namespace elephant_boss_state;

namespace {

int16_t toShort(int64_t v) { return int16_t(uint16_t(uint64_t(v))); }

// ---- shared helpers ---------------------------------------------------------------------------------------------------
// the scratch point (exe 0x6d0120) the routines build their points in
LaneVec4& scratchPoint() { return projectionScratch().point; }
void setScratch(int16_t x, int16_t y, int16_t z) {
    LaneVec4& p = scratchPoint();
    p.x = x;
    p.y = y;
    p.z = z;
}
// a point's three lanes as the exe reads them
std::array<int16_t, 3> lanes(const ShortVec4& v) { return {v.x, v.y, v.z}; }
std::array<int16_t, 3> lanes(const ShortVec3& v) { return {v.x, v.y, v.z}; }
// a 16.16 position's three values
std::array<int32_t, 3> values(const Fixed16Vec3& v) { return {v.x, v.y, v.z}; }
// the transform's in / out points of exe 0x5e1970 (the scratch point and the point after it)
LaneVec3 scratchIn() { return geometryScratch().vec3(0x60); }
LaneVec3& scratchOut() { return projectionScratch().rotatedPoint; }
// exe 0x5e4120 on the scratch point
void submitAtScratch(HitBoxHead& box, uint32_t ownerShape, uint32_t attack) {
    const auto p = lanes(scratchPoint());
    hitListSubmit(p.data(), box, ownerShape, attack);
}
// ... for a shape that asks for a contact output (the whole box)
void submitAtScratch(HitBox& box, uint32_t ownerShape, uint32_t attack) {
    const auto p = lanes(scratchPoint());
    hitListSubmit(p.data(), box, ownerShape, attack);
}
// exe 0x5e4190 (game/hit_list hitListSubmitCapsule) between two s16 points of the game state
template <class A, class B>
void submitCapsule(const A& from, uint16_t ra, const B& to, uint16_t rb, HitBoxHead& box, uint32_t ownerShape,
                   uint32_t attack) {
    const auto a = lanes(from), b = lanes(to);
    hitListSubmitCapsule(a.data(), ra, b.data(), rb, box, ownerShape, attack);
}
// exe 0x5e1780(actor, n): the actor's n-th bone block
BoneBlock& ownBoneBlock(const AnimatedActor& a, int n) {
    BoneBlock* block = boneBlockOf(a, n);
    if (!block) notPorted(kStoryPort, "exe 0x5e1780: no such bone block");
    return *block;
}
// the colour of bone `bone` (the spare lane of its point)
void setBoneColour(const AnimatedActor& a, uint32_t bone, uint16_t colour) {
    ownBoneBlock(a, int(bone >> 2)).colour(bone & 3) = colour;
}
ShortVec4& bonePointOf(const AnimatedActor& a, uint32_t bone) { return bonePoint(a, bone, kStoryPort); }
// the collision grid cell under a position (16.16), unchecked as the exe's inlined index
uint32_t gridCellUnderPos(const Fixed16Vec3& p) {
    return gridCellAtIndex(uint32_t(gridColumn(p.x.whole)), uint32_t(gridColumn(p.z.whole)));
}
// the ABS() macro over exe 0x5e3df0 (two calls: the first's sign picks the second)
int absTurn(AnimatedActor& a, uint32_t angle, int step) {
    const int d = turnYawTo(a.rotation.y, angle, step);
    if (d < 0) return -turnYawTo(a.rotation.y, angle, step);
    return turnYawTo(a.rotation.y, angle, step);
}
// the elephant's rows of the animated stats (by its spawn byte; row 1 the attacks of both forms, row 2 the boulder's)
constexpr uint8_t kElephantType = 0x34;
AnimatedActorStats elephantRow(uint8_t type, uint8_t n) { return animatedStats(type, n); }

// the spark of the chapter (global effect type 0 variant 3)
constexpr uint8_t kChapterSparkVariant = 3;
EffectActor* allocChapterSpark() {
    const uint32_t address = allocActorA0(0); // exe 0x5e13b0
    if (address == 0) return nullptr;
    EffectActor& e = effectActorAt(address);
    e.flags = kActorLive;
    e.type = typeByte(GlobalEffectType::HitSpark);
    e.loopFlags |= uint8_t(kActorSweepGroup1 | kActorLateLoop);
    e.variant = kChapterSparkVariant;
    return &e;
}
// a blast (global static type 7, exe 0x5e1300) at the s16 point, attack 0 (the stores in the order of the tusk's
// blasts, exe 0x4a6640)
void spawnBlast(int16_t x, int16_t y, int16_t z, uint16_t size) {
    const uint32_t address = allocActor60(0);
    if (address == 0) return;
    StaticActor& s = staticActorAt(address);
    BlastParams params(s);
    s.flags = kActorLive;
    s.type = typeByte(GlobalStaticType::Blast);
    s.variant = 0;
    s.loopFlags |= kActorSweepGroup2;
    s.pos.x.whole = x;
    s.pos.y.whole = y;
    s.pos.z.whole = z;
    params.attack = 0;
    params.size = size;
}

constexpr uint32_t kFlagCrystalTaken = 0x681; // the room handler starts script 0x28
// exe 0x590290 (the crystal) / 0x5903c0 (the pedestal): the model of the variant, the pose copies, the motion
// cleared, the pedestal's talk box, the class block, the home height; the pedestal semi-transparent at scale 0x1000
void crystalInit(StaticActor& a, bool pedestal) {
    CrystalFields& fields = classState<CrystalFields>(a);
    if (!bindActorModel(a, a.variant)) {
        freeActor(a); // exe 0x5e1540
        return;
    }
    a.flags |= kActorDrawn;
    a.prevPos.x = a.pos.x;
    a.prevPos.y = a.pos.y;
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.prevPos.z = a.pos.z;
    const uint32_t zSpare = a.rotation.zSpareWord();
    const uint32_t xy = a.rotation.xyWord();
    a.prevRotation.setXY(int16_t(xy), int16_t(xy >> 16));
    a.velocity.clearWords();
    a.acceleration.clearWords();
    if (!pedestal) {
        a.airborne = 0;
        a.onPlatform = 0;
    }
    a.platform = nullptr;
    a.collisionBox = nullptr;
    if (!pedestal) {
        a.actorHit = 0;
        a.interactionBox = nullptr;
        a.interaction = 0;
    } else {
        a.lastMover = nullptr;
        a.interactionBox.pointAt(TableId::ElephantBossCrystalTalkBox);
        a.interaction = kInteractTalk;
    }
    a.prevRotation.setZSpare(int16_t(zSpare), int16_t(zSpare >> 16));
    a.clearClassBlock();
    fields.homeY = a.pos.y.whole;
    if (pedestal) {
        a.semiTransparency = semiTransparencyBits(1, 0); // exe 0x5e1a70(actor, 1, 0)
        a.scale.z = 0x1000;
        a.scale.y = 0x1000;
        a.scale.x = 0x1000;
    }
    a.enterState(1);
}
// exe 0x590330: turns, bobs (the 64-step sine of the frame), a hit entry above it
void crystalFloat(StaticActor& a) {
    CrystalFields& fields = classState<CrystalFields>(a);
    a.rotation.y += 10u;
    const int s = sin64(gameState().frame & 0x3f);
    a.pos.y = int32_t(uint32_t(int32_t(fields.homeY) * 0x10000 + s * 0x200));
    fields.hit.result = 0;
    setScratch(a.pos.x.whole, toShort(a.pos.y.whole - 0x1c0), a.pos.z.whole);
    submitAtScratch(fields.hit, uint32_t(a.slot) << 24 | 0x480067, 0xffa80000u);
}
// exe 0x590490: talked to by the player -> flag 0x681; a solid
void pedestalUse(StaticActor& a) {
    if (a.subState == 0 && player().talksTo(a)) setStoryFlag(kFlagCrystalTaken);
    actorCollisionSubmitSolid(a, SolidType::PushOutAligned, kNoBone, 0);
}
// exe 0x590260: the previous rotation; the routine [[0x8cdce8 + variant * 4] + state * 4] (the source names it)
void crystal(StaticActor& a) {
    a.prevRotation = a.rotation;
    const DataArray<uint32_t> byVariant(stateAddress(TableId::ElephantBossCrystalByVariant));
    const DataArray<uint32_t> states(byVariant[a.variant]);
    const RoutineRef routine = stateRoutine(states[a.state]);
    switch (routine.id()) {
    case RoutineId::ElephantCrystalInit: crystalInit(a, false); return;
    case RoutineId::ElephantCrystalFloat: crystalFloat(a); return;
    case RoutineId::ElephantCrystalInitPlaced: crystalInit(a, true); return;
    case RoutineId::ElephantPedestalUse: pedestalUse(a); return;
    case RoutineId::StaticFreedState: freeActor(a); return;
    default: notPortedAt(kStoryPort, "ST1D static type 1 state", routine.pointerValue());
    }
}

// ---- script 0x28 (exe 0x524a30, states 0x8a49c8): the crystal -> the floor caves in -> the arena (ST1D sub 2) ------
ScriptState& block() { return scriptState(); }
constexpr uint32_t kSweepScriptActors = 0xefe, kSweepAllActors = 0xffe, kSweepKeep = 0x80;
constexpr uint32_t kVoiceFadeStep = 8;
constexpr uint32_t kMusicFade = 0xf;
constexpr uint16_t kSoundQuake = 0x17c, kSoundCaveIn = 0x174, kSoundLanding = 0x17d, kSoundArenaMusic = 0x1f;
constexpr uint8_t kSt1d = 0x1d, kSt1c = 0x1c;
constexpr int8_t kMessageWindow = 4;
bool oddTick() { return (block().totalTicks & 1) != 0; }
bool playerAnimEnded() { return (player().body.key.flags & kKeyJump) != 0; }

// exe 0x525310(point, size): a spark at the s16 point (its two dwords copied)
void spawnSparkAt(const ShortVec4& point, uint16_t size) {
    EffectActor* e = allocChapterSpark();
    if (!e) return;
    e->setPositionWords(point);
    classState<HitSparkFields>(*e).size = size;
}
// exe 0x525350: a falling rock of the caving floor (static type 0x20 sub 0, its placement / speed / spin from the RNG)
constexpr uint8_t kSt1dRockType = 0x20;
void spawnCavingRock() {
    const uint32_t address = allocActor60(kActorStageClass);
    if (address == 0) return;
    StaticActor& s = staticActorAt(address);
    s.flags = kActorLive;
    s.type = kSt1dRockType;
    s.loopFlags |= kActorSweepGroup2;
    uint32_t r = rngStep();
    s.variant = uint8_t(r & 1);
    s.pos.x = int32_t((((r >> 1) & 0x7ff) - 0x200) << 16);
    s.pos.y = int32_t(uint32_t(-0x33e - int32_t((r >> 0xc) & 0x1ff)) << 16);
    s.pos.z = int32_t(((r >> 0x15) + 0xcd5) << 16);
    r = rngStep();
    s.velocity.y = toShort(-0x800 - int32_t(r & 0x3ff));
    s.velocity.z = toShort(-0x100 - int32_t((r >> 10) & 0x1ff));
    s.acceleration.y = 0xc0;
    const int16_t sc = int16_t(((r >> 0x13) & 0x3ff) + 0x4000);
    s.scale.z = sc;
    s.scale.y = sc;
    s.scale.x = sc;
    r = rngStep();
    s.rotation.y = int16_t(r & 0xfff);
}
// the player's fall: the vertical speed += the gravity, moved by (0, vy, vz) (exe 0x5e3690)
void playerFall() {
    Player& p = player();
    p.velocity.y += p.acceleration.y;
    integrateVelocity(p, 0, p.velocity.y, p.velocity.z);
}
// exe 0x524d00 (step 0:0): the crystal reacts -- a shake, ST1D's DAT 0xb1, the quake's sound; once loaded the skip
// back and message 10; once it is closed 0x1e ticks, a shake and the next step
void stepCrystalReacts() {
    ScriptState& s = block();
    Script28Bytes& bytes = scriptState().part<Script28Bytes>();
    switch (s.subState) {
    case 0:
        if (voiceClipIdle()) {
            cameraShake(1, 0x4000, 0);
            datLoadRequest(0xb1);
            soundPlayPanned(kSoundQuake, 0, 0);
            ++s.subState;
        }
        return;
    case 1:
        if ((fileLoads().loading & kFileLoading) == 0) {
            s.flags &= uint8_t(~kScriptUnskippable);
            windowOpen(kMessageWindow, 0x73c000, 10);
            ++s.subState;
        }
        return;
    case 2:
        if (windowStatus(kMessageWindow) == 0) {
            bytes.waitTicks = 0;
            ++s.subState;
        }
        return;
    case 3: {
        const int32_t n = bytes.waitTicks + 1;
        bytes.waitTicks = n;
        if (n > 0x1e) {
            cameraShake(1, 0x1000, 0);
            scriptSequenceSkip(1);
        }
        return;
    }
    default: return;
    }
}
// exe 0x524de0 (step 0:1): the floor gives way -- at 0x1e the player falls (animation 0x22), the white fade; at 0x28
// the fall point kept; once the fade is done the landing's sound and the arena's music (stepSubPhase: played), the next
// step. Before 0x1e sparks on the odd ticks
void stepFloorGivesWay() {
    ScriptState& s = block();
    Script28Bytes& bytes = scriptState().part<Script28Bytes>();
    Player& p = player();
    switch (s.stepPhase) {
    case 0:
        if (s.stepTick != 0x1e) break;
        p.acceleration.y = 0x30;
        p.velocity.z = 0x600;
        p.velocity.y = -0x200;
        p.rotation.y = 0x800;
        p.airborne = kAirborneFalling;
        playerScriptAnimId(0x22, 0, 1);
        soundPlayPanned(kSoundCaveIn, 0, 0);
        scriptFade(0x1a, 0);
        s.flags |= kScriptUnskippable;
        ++s.stepPhase;
        break;
    case 1:
        playerFall();
        if (s.stepTick == 0x28) {
            bytes.fallPoint.x = p.pos.x.whole;
            bytes.fallPoint.y = p.pos.y.whole;
            bytes.fallPoint.z = p.pos.z.whole;
            ++s.stepPhase;
        }
        break;
    case 2:
        if (!fadeState().idle()) break;
        soundPlayPanned(kSoundLanding, 0, 0);
        soundPlayFlatForced(kSoundArenaMusic);
        s.stepSubPhase = 1;
        cameraShake(1, 0, 0);
        s.stepPhase = 0;
        scriptSequenceSkip(1);
        break;
    default: break;
    }
    if (s.stepTick < 0x1e && oddTick()) {
        const uint32_t r = rngStep();
        setScratch(toShort(0x300 - int(r & 0x1ff)), toShort(int((r >> 9) & 0xff) - 0x4ec),
                   toShort(((r >> 0x11) & 0x3f) + 0x10d5));
        spawnSparkAt(scratchPoint(), uint16_t(((r >> 0x17) & 0x1f) + 0x80));
    }
}
// the landing in the arena: three sparks around the player's head on the ground, the landing's dust puff (global
// effect type 7) under it
void landingSparksAndDust(const PlayerEnv& env) {
    Player& p = player();
    LaneVec4& sp = scratchPoint();
    const ShortVec3& head = p.bonePoints[kHeadBone];
    for (int i = 0; i < 3; ++i) {
        const uint32_t r = rngStep();
        sp.x = int16_t(uint16_t(head.x) - (r & 0x7f) + 0x40u);
        sp.y = int16_t(actorGround(env, p));
        sp.z = int16_t(((rngLastValue() >> 7) & 0x7f) + uint16_t(head.z) + 0x40u);
        spawnSparkAt(sp, uint16_t(((rngLastValue() >> 0xe) & 0x1f) + 0x80));
    }
    const uint32_t address = allocActorA0(0);
    if (address == 0) return;
    EffectActor& e = effectActorAt(address);
    DustPuffFields& puff = classState<DustPuffFields>(e);
    e.flags = kActorLive;
    e.type = typeByte(GlobalEffectType::DustPuff);
    e.loopFlags |= uint8_t(kActorSweepGroup1 | kActorLateLoop);
    e.pos.x = head.x;
    e.pos.z = head.z;
    e.pos.y = sp.y;
    puff.size = 0x100;
    puff.ticks = 0x10;
    puff.airborne = p.onPlatform;
}
// exe 0x524fa0 (step 1:0): the arena -- the warp to sub 2 at the fall point, the falling rocks and sparks on the odd
// ticks; landed, the landing's animation (0x23) and dust; then the get-up poses (0x25, 0x2a, 0) and the next step
void stepArena(const PlayerEnv& env) {
    ScriptState& s = block();
    Script28Bytes& bytes = scriptState().part<Script28Bytes>();
    Player& p = player();
    switch (s.stepPhase) {
    case 0:
        requestTransition(kTransitionWarp, kSt1d, 2, bytes.fallPoint.x, bytes.fallPoint.y, bytes.fallPoint.z, 0x600,
                          kNoFade, kNoFade);
        scriptFade(10, 0);
        ++s.stepPhase;
        [[fallthrough]];
    case 1:
        if (oddTick()) {
            spawnCavingRock();
            uint32_t r = rngStep();
            const int16_t x = toShort(0x400 - int(r & 0x3ff));
            const int16_t y = toShort(-0x57e - int((r >> 10) & 0xff));
            const int16_t z = toShort(((r >> 0x12) & 0x3ff) + 0x1169);
            setScratch(x, y, z);
            r = rngStep();
            spawnSparkAt(scratchPoint(), uint16_t((r & 0x3f) + 0x300));
        }
        if (p.airborne == 0) {
            p.velocity.z = 0;
            p.velocity.y = 0;
            p.acceleration.y = 0;
            ++s.stepPhase;
            playerScriptAnimId(0x23, 0, 1);
            landingSparksAndDust(env);
        }
        break;
    case 2:
        if (fadeState().idle() && playerAnimEnded()) {
            bytes.waitTicks = 0;
            s.stepPhase = 3;
        }
        break;
    case 3: {
        const int32_t n = bytes.waitTicks + 1;
        bytes.waitTicks = n;
        if (n == 0x32) {
            playerScriptAnimId(0x25, 0, 1);
            ++s.stepPhase;
        }
        break;
    }
    case 4:
        if (playerAnimEnded()) {
            playerScriptAnimId(0x2a, 0, 1);
            ++s.stepPhase;
        }
        break;
    case 5:
        if (playerAnimEnded()) {
            playerScriptAnimId(0, 0, 1);
            s.stepPhase = 0;
            scriptSequenceSkip(1);
        }
        break;
    default: break;
    }
    playerFall();
    if (actorPushOut(env, p) & kWallOnYPlus) p.airborne = 0;
}
// exe 0x5252e0 (step 1:1): the elephant wakes (the script's actor 2: its parameter) at 0x3c, the next step at 0x8c
void stepElephantWakes() {
    ScriptState& s = block();
    if (s.stepTick == 0x3c) (*s.actors[2]).params[0] = 1;
    else if (s.stepTick == 0x8c) scriptSequenceSkip(1);
}
void runStep(const PlayerEnv& env, const RoutineRef& routine) {
    switch (routine.id()) {
    case RoutineId::ElephantScriptCrystalReactsStep: stepCrystalReacts(); return;
    case RoutineId::ElephantScriptFloorGivesWayStep: stepFloorGivesWay(); return;
    case RoutineId::ElephantScriptArenaStep: stepArena(env); return;
    case RoutineId::ElephantScriptElephantWakesStep: stepElephantWakes(); return;
    case RoutineId::ElephantScriptStep5: scriptSequenceSkip(1); return; // script 0x29's only step
    default: notPortedAt(kStoryPort, "ST1D script step", routine.pointerValue());
    }
}
// exe 0x524b30 (script 0x28) / 0x52fa20 (script 0x29): the step runner without face tracks (the step called
// unchecked); a skip (on a frame of buffer 1) -> script 0x28's landing sounds unless played, the skip's wipe; the
// list's end or the skip -> the wrap-up
void stepRunner(const PlayerEnv& env, bool skipSounds) {
    ScriptState& s = block();
    const ScriptStep step = scriptStepAt(s.steps.value());
    if (step.id != kScriptStepEnd) {
        runStep(env, stateRoutine(step.routine));
        if (scriptSkipRequested() == 0 || display().drawBuffer == 0) {
            scriptSequenceTick();
            return;
        }
        if (skipSounds && s.stepSubPhase == 0) {
            soundPlayPanned(kSoundLanding, 0, 0);
            soundPlayFlatForced(kSoundArenaMusic);
        }
        scriptFade(kScriptSkipWipe, 0);
    }
    s.enterState(CutsceneState::WrapUp);
    scriptSequenceTick();
}
// the player's script fields cleared: the move direction's angles, the idle faces, the velocity and the acceleration
// (the dwords z / spare first)
void clearPlayerMotion() {
    Player& p = player();
    p.velocity.setZSpare(0, 0);
    p.velocity.setXY(0, 0);
    p.acceleration.setZSpare(0, 0);
    p.acceleration.setXY(0, 0);
}
void clearPlayerScriptFields() {
    Player& p = player();
    p.clearDirectionOffsetWords();
    p.idleFaces[0] = 0;
    p.idleFaces[1] = 0;
    clearPlayerMotion();
}
// exe 0x524a50: the replay pad, the windows closed, the skip locked; once the player is script driven the scripted
// actors swept, its fields cleared, the camera script and the steps, the music faded; once quiet the steps
void script28SetUp() {
    ScriptState& s = block();
    switch (s.subState) {
    case 0:
        scriptReplayPadOn();
        windowCloseAll(WindowClose::Force);
        s.flags |= kScriptUnskippable;
        ++s.subState;
        return;
    case 1:
        if (playerToScriptIfFree() != 0) return;
        actorsSweep(kSweepScriptActors, kSweepKeep);
        clearPlayerScriptFields();
        scriptSequenceStart(stateAddress(TableId::ElephantBossScript28Camera), stateAddress(TableId::ElephantBossScript28Steps));
        scriptSequenceTick();
        soundFadeOut(kMusicFade, 0x444, 0);
        ++s.subState;
        return;
    case 2:
        if (soundDone(kMusicFade)) s.enterState(CutsceneState::Steps);
        return;
    default: return;
    }
}
// exe 0x524ba0: the warp to the arena (a skip: its fade); the scripted actors swept, the boss, the crystal and the
// exit light, the player back at (0x200, -0x212, 0x53a) facing a half turn
void script28WrapUp() {
    ScriptState& s = block();
    switch (s.subState) {
    case 0:
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFadeStep);
        windowCloseAll(WindowClose::Force);
        requestTransition(kTransitionWarp, kSt1d, 2, 0x200, toShort(0xfdee), 0x53a, 0x800, kNoFade, kNoFade);
        break;
    case 1:
        if (!fadeState().idle()) return;
        if (s.flags & kScriptSkipTaken) {
            scriptFade(1, 0);
            ++s.subState;
            return;
        }
        break;
    case 2: {
        actorsSweep(kSweepScriptActors, kSweepKeep);
        scriptActorSpawnRecord(stateAddress(TableId::ElephantBossScript28FirstRecord));
        scriptActorSpawnRecord(stateAddress(TableId::ElephantBossScript28SecondRecord));
        spawnRecords(stateAddress(TableId::ElephantBossScript28Spawns), 1);
        playerBackToPlay(); // exe 0x5723c0
        scriptReplayPadOff();
        Player& p = player();
        p.airborne = 0;
        p.pos.x = 0x2000000;
        p.pos.y = int32_t(0xfdee0000u);
        p.pos.z = 0x53a0000;
        p.rotation.y = 0x800;
        clearPlayerMotion();
        scriptEndBlock();
        return;
    }
    default: return;
    }
    ++s.subState;
}
void script28(const PlayerEnv& env) {
    switch (block().cutsceneState()) {
    case CutsceneState::SetUp: script28SetUp(); return;           // exe 0x524a50
    case CutsceneState::Steps: stepRunner(env, true); return;     // exe 0x524b30
    case CutsceneState::WrapUp: script28WrapUp(); return;         // exe 0x524ba0
    default: notPortedAt(kStoryPort, "script 0x28 state", block().state);
    }
}

// ---- script 0x29 (exe 0x525480, states 0x8a4a08): after the boss -> the request to ST1C sub 0 ----------------------
// exe 0x5254a0: the replay pad, the skip off, the white fade; once the player stands free (exe 0x572320) it is
// scripted (a hanging player let fall); once the fade is done the actors swept, its fields cleared, the steps
void script29SetUp() {
    ScriptState& s = block();
    Player& p = player();
    switch (s.subState) {
    case 0:
        scriptReplayPadOn();
        s.flags |= kScriptSkipLocked;
        scriptFade(0x1a, 0);
        ++s.subState;
        return;
    case 1:
        if (p.playerState() == PlayerState::Playing && p.subState == 0 && p.step != 0) {
            playerToScripted();
            ++s.subState;
            return;
        }
        if (p.airborne == kAirborneHanging) p.airborne = kAirborneFalling;
        return;
    case 2:
        if (!fadeState().idle()) return;
        actorsSweep(kSweepScriptActors, kSweepKeep);
        clearPlayerScriptFields();
        scriptSequenceStart(stateAddress(TableId::ElephantBossScript29Camera), stateAddress(TableId::ElephantBossScript29Steps));
        scriptSequenceTick();
        s.enterState(CutsceneState::Steps);
        return;
    default: return;
    }
}
// exe 0x525570: the rumble off, idle, every actor swept, the windows closed, the voice faded; once quiet the request
// to ST1C sub 0 at (0x6db, -0x252, 0x125e) (fade in 10); once taken the player shown, the block ended
void script29WrapUp() {
    ScriptState& s = block();
    const uint8_t sub = s.subState;
    if (sub == 0) {
        padVibrate(0, 0x1000000);
        playerScriptAnimId(0, 0, 1);
        actorsSweep(kSweepAllActors, kSweepKeep);
        windowCloseAll(WindowClose::Force);
        if (voiceClipPlaying()) voiceClipFadeOut(kVoiceFadeStep);
        ++s.subState;
    } else if (sub != 1) {
        if (sub != 2 || transitionRequest().state != kTransitionNone) return;
        scriptReplayPadOff();
        player().flags |= kActorDrawn;
        scriptEndBlock();
        return;
    }
    if (voiceClipIdle()) {
        requestTransition(kTransitionAccepted, kSt1c, 0, 0x6db, toShort(0xfdae), 0x125e, 0x766, 10, kNoFade);
        ++s.subState;
    }
}
void script29(const PlayerEnv& env) {
    switch (block().cutsceneState()) {
    case CutsceneState::SetUp: script29SetUp(); return;          // exe 0x5254a0
    case CutsceneState::Steps: stepRunner(env, false); return;   // exe 0x52fa20
    case CutsceneState::WrapUp: script29WrapUp(); return;        // exe 0x525570
    default: notPortedAt(kStoryPort, "script 0x29 state", block().state);
    }
}

// ---- the elephant (ST1D pool 0x20 type 0x34) ---------------------------------------------------------------------
// exe 0x4a9df0(actor, point16.16): the ground height under a point -- a collision cell that is no floor gives -16 * its
// corner 0's height, else exe 0x40f300 (the probe without a box, its mode the actor's y step, mask 2); the cell's
// column / row from the point + 0x100 (unchecked)
int groundUnder(const PlayerEnv& env, const AnimatedActor& a, const int32_t p[3]) {
    const int16_t x = toShort(p[0] >> 16), z = toShort(p[2] >> 16);
    const GridCell cell(gridCellAtIndex(uint32_t(gridColumn(int(x) + 0x100)), uint32_t(gridColumn(int(z) + 0x100))));
    if ((cell.word & (kCellCollides | kCellFloor)) == kCellCollides) return -int(cell.corner0) * 16;
    const int16_t q[3] = {x, toShort(p[1] >> 16), z};
    return probeGroundAt(env, q, CollisionBoxRef::record(0), int(a.pos.y.whole) - int(a.prevPos.y.whole), 2);
}
// exe 0x4a77e0 / 0x4a9a60 (yaw) and 0x4a79a0 / 0x4a9c20 (yaw + 0xc00): the tilt of the body from the ground 0xff0
// units ahead / behind along the angle (the 64-bit sign flag of the exe's result is unused by the callers)
int16_t tilt(const PlayerEnv& env, const AnimatedActor& a, int angle) {
    const int sn = fixedSin(angle), cs = fixedCos(angle);
    const int32_t p1[3] = {a.pos.x - sn * 0xff0, a.pos.y, a.pos.z - cs * 0xff0};
    int16_t h1 = toShort(groundUnder(env, a, p1));
    const int32_t p2[3] = {a.pos.x + sn * 0xff0, a.pos.y, a.pos.z + cs * 0xff0};
    int16_t h2 = toShort(groundUnder(env, a, p2));
    const int16_t y = a.pos.y.whole;
    const uint32_t lim = a.airborne != 0 ? 0xff : 0x1fe;
    if (uint32_t(int(h1) - int(y) + 0xff) > lim) h1 = y;
    if (uint32_t(int(h2) - int(y) + 0xff) > lim) h2 = y;
    const int dx = int(toShort(p1[0] >> 16)) - int(toShort(p2[0] >> 16)),
              dz = int(toShort(p1[2] >> 16)) - int(toShort(p2[2] >> 16));
    const int d = isqrt(int32_t(uint32_t(dz * dz) + uint32_t(dx * dx)));
    int r = 0;
    if (h1 > h2) r = atanAngle(float(int(h1) - int(h2)), float(d));
    if (h1 < h2) r = -atanAngle(float(int(h2) - int(h1)), float(d));
    return toShort(r);
}

constexpr uint32_t kBossAirborne = 0x1;
constexpr uint32_t kBossUntouchable = 0x2;   // no damage, no hit entries (its setter is not traced)
constexpr uint32_t kBossHitClear = 0x8;      // cleared by every damage pass (its reader is not traced)
constexpr uint32_t kBossCharging = 0x10;     // cleared as a charge starts / ends
constexpr uint32_t kBossHurt = 0x20;         // hit this tick
constexpr uint32_t kBossStaggerGuard = 0x100; // staggered: no damage, no stagger
constexpr uint32_t kBossPlayerNearCentre = 0x200;
constexpr uint32_t kBossPlayerAhead = 0x400;
constexpr uint32_t kBossPlayerAside = 0x800;
constexpr uint32_t kBossFarFromCentre = 0x1000;
constexpr uint32_t kBossFirstTuskBroken = 0x2000;
constexpr uint32_t kBossSecondTuskBroken = 0x4000;
constexpr uint32_t kBossGuarded = 0x8000;    // a move that takes no stagger
constexpr uint32_t kBossGuardsOff = ~(kBossGuarded | kBossStaggerGuard);
constexpr uint32_t kBossChargeOff = ~(kBossCharging | kBossHurt);

// the arena's centre (exe data, 16.16)
Fixed16Vec3 arenaCentre() {
    return dataFixed16Vec3(stateAddress(TableId::ElephantBossArenaCentre));
}

// exe 0x4a6cd0(actor, mask): the ground / the landing / the walls; 0 on the ground, 1 started falling, 2 otherwise
int elephantGround(const PlayerEnv& env, AnimatedActor& a, uint32_t mask) {
    BossFields& fields = classState<BossFields>(a);
    if (a.airborne == 0) {
        const int16_t p[3] = {a.pos.x.whole, a.pos.y.whole, a.pos.z.whole};
        const int g = probeGroundAt(env, p, a.collisionBox, int(a.pos.y.whole) - int(a.prevPos.y.whole), mask);
        if (uint32_t(g + (0x80 - int(a.pos.y.whole))) < 0x101) {
            a.pos.y = int32_t(uint32_t(g) << 16);
            return 0;
        }
        if (int(a.pos.y.whole) <= g) {
            a.airborne = kAirborneFalling;
            fields.flags |= kBossAirborne;
            return 1;
        }
        if (int32_t(actorPushOutMask(env, a, mask)) < 0) a.pos = a.prevPos;
        return 2;
    }
    const uint32_t r = actorPushOutMask(env, a, mask);
    if (int32_t(r) < 0) {
        a.pos = a.prevPos;
        return 2;
    }
    if (r & kWallOnYPlus) {
        a.airborne = 0;
        fields.flags &= ~kBossAirborne;
    }
    return 2;
}
// the hit points of the class row `n` (by type), shifted (none without a row)
void hitPointsFromRow(AnimatedActor& a, uint8_t n, int shift) {
    const uint16_t hp = hasAnimatedStats(a.type) ? uint16_t(uint32_t(elephantRow(a.type, n).hitPoints) << shift) : 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
}
// the shared middle of exe 0x4a51a0 / 0x4a97f0: the tilt and the motion cleared, the gravity 0x60, the spin cleared
void elephantCommonInit(AnimatedActor& a) {
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.velocity.setZSpare(0, 0);
    a.velocity.setXY(0, 0);
    a.acceleration.y = 0x60;
    a.spin.x = 0;
    a.spin.y = 0;
}
// the tilt follow: the pitch / roll from the ground ahead and aside
void followGround(const PlayerEnv& env, AnimatedActor& a, int16_t pitchOffset) {
    const int16_t pitch = tilt(env, a, a.rotation.y);
    const int16_t roll = tilt(env, a, a.rotation.y + 0xc00);
    a.rotation.z = roll;
    a.rotation.x = int16_t(pitchOffset + pitch);
}

// ---- sub 3 (exe 0x4a9770, states 0x893df8): the elephant of the cutscene (wakes when the step sets its parameter) ----
enum class ElephantState : uint8_t { Init = 0, Main = 1, Free = 2 };
enum class CutsceneSub : uint8_t { Asleep = 0, Wake = 1, Awake = 2 };
constexpr uint16_t kSoundTrumpet = 0x175;
void cutsceneSub(const PlayerEnv& env, AnimatedActor& a) {
    switch (CutsceneSub(a.subState)) {
    case CutsceneSub::Asleep: // exe 0x4a9980
        if (a.params[0] != 0) {
            a.anim = 6;
            a.subState = uint8_t(CutsceneSub::Wake);
        }
        elephantGround(env, a, 2);
        return;
    case CutsceneSub::Wake: // exe 0x4a99b0: animations 0xe (its trumpet), 7, then 0
        switch (a.step) {
        case 0:
            if (a.animEnded()) {
                a.anim = 0xe;
                soundPlayPanned(kSoundTrumpet, 0, 0);
                ++a.step;
            }
            break;
        case 1:
            if (a.animEnded()) {
                a.anim = 7;
                a.step = 2;
            }
            break;
        case 2:
            if (a.animEnded()) {
                a.anim = 0;
                a.subState = uint8_t(CutsceneSub::Awake);
                a.restartStep();
            }
            break;
        default: break;
        }
        elephantGround(env, a, 2);
        return;
    case CutsceneSub::Awake: elephantGround(env, a, 2); return; // exe 0x4a9a50
    default: notPortedAt(kStoryPort, "elephant (cutscene) sub-state", a.subState);
    }
}
// exe 0x4a97f0: model variant 1, the hit points of row 1 (<< 2), the box 0x893e10, the class block cleared
void cutsceneInit(AnimatedActor& a) {
    if (!bindActorModel(a, 1)) {
        freeActor(a);
        return;
    }
    a.flags |= kActorDrawn;
    a.prevAnim = 0;
    a.anim = 0;
    hitPointsFromRow(a, 1, 2);
    elephantCommonInit(a);
    a.collisionBox.pointAt(TableId::ElephantBossCutsceneBox);
    a.actorHit = 0;
    a.platform = nullptr;
    a.clearClassBlock();
    animSetPose(a, a.anim, 0);
    a.enterState(uint8_t(ElephantState::Main));
}
// exe 0x4a98c0: the sub-state, the tilt, the animation
void cutsceneMain(const PlayerEnv& env, AnimatedActor& a) {
    cutsceneSub(env, a);
    followGround(env, a, 0);
    animateAsked(a, 0);
}
// exe 0x4a9770
void cutsceneElephant(const PlayerEnv& env, AnimatedActor& a) {
    a.prevAnim = a.anim;
    a.tint = kTintNeutral;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (ElephantState(a.state)) {
    case ElephantState::Init: cutsceneInit(a); break;
    case ElephantState::Main: cutsceneMain(env, a); break;
    case ElephantState::Free: freeActor(a); break; // exe 0x468fa0
    default: notPortedAt(kStoryPort, "elephant (cutscene) state", a.state);
    }
    // drawn: exe 0x5f20f0(actor, 0x300, 0x100, 0), the shadow list entry (render output)
}

// ---- sub 2 (exe 0x4a4fd0, states 0x893cfc, sub-states 0x893d08): the boss ------------------------------------------
enum class BossSub : uint8_t {
    Idle = 0x0, Approach = 0x1, Stalk = 0x2, RearUp = 0x3, Wander = 0x4, Return = 0x5, Charge = 0x6, Bounce = 0x7,
    Brake = 0x8, Stop = 0x9, Choose = 0xa, ThrowRocks = 0xb, Stomp = 0xc, Boulder = 0xd, Stagger = 0xe,
    TuskBreaks = 0xf, HomingCharge = 0x10, Boulders = 0x11, Down = 0x12
};
constexpr uint16_t kSoundBossHit = 0x8d, kSoundThrow = 0x177, kSoundStompRise = 0x178, kSoundStompLand = 0x179,
                   kSoundFootstep = 0x176;
constexpr uint32_t kFlagBossDown = 0x682;
constexpr int16_t kWalkSpeed = -0x100, kChargeSpeed = -0x5a0;

// a sub-state at its first step (the exe's word store of the step bytes)
void enterSub(AnimatedActor& a, BossSub s) {
    a.subState = uint8_t(s);
    a.restartStep();
}
void walk(AnimatedActor& a) { integrateVelocity(a, 0, a.velocity.y, a.velocity.z); }
int16_t countBy(BossFields& f, int d) {
    const int16_t v = toShort(f.counter + d);
    f.counter = v;
    return v;
}
uint32_t towardPlayer(const BossFields& f) { return uint32_t(int32_t(f.playerAngle)); }

// exe 0x4a6ef0: the damage of this tick (the body's and the tusks' boxes, the attack kinds 0xc0000): the flash (the
// tusks' bones dimmed when they were hit), its sound, the stagger meter, the hit points; 0 none, 1 hit, -1 destroyed
int bossDamage(AnimatedActor& a, BossFields& f) {
    const uint32_t v = f.flags & ~kBossHitClear;
    f.flags = v;
    const uint32_t d1 = f.body.result & kHitDamageMask, d2 = f.tusks().hit.result & kHitDamageMask;
    if (((f.tusks().hit.result | f.body.result) & (kHitAttackByPlayer | kHitAttackThrown)) == 0 || (d1 == 0 && d2 == 0))
        return 0;
    f.flags = v | kBossHurt;
    if (d2 == 0) {
        a.tint = kTintFlash;
    } else {
        a.tint = 0;
        for (uint32_t bone : {1u, 2u, 3u, 0xcu, 0xdu}) setBoneColour(a, bone, 0x1f);
        const uint16_t c = d1 != 0 ? kTintFlash : kTintNeutral;
        for (uint32_t bone : {0u, 4u, 5u, 6u, 7u, 8u, 9u, 0xau, 0xbu}) setBoneColour(a, bone, c);
    }
    soundPlayAtPos(kSoundBossHit, a.pos);
    if (!(f.flags & kBossStaggerGuard)) {
        if (!(f.flags & kBossGuarded)) {
            auto& t = f.tusks();
            t.stagger = int16_t(t.stagger + toShort(int(d1 + d2) * 0x10));
        }
        if (hitPointsAdd(a, -int(d1 + d2 * 2)) != 0) return -1;
    }
    return 1;
}
// exe 0x4a6dc0(actor, harmless): the hit entries -- the body capsule (bone 0 to a point above it) and, while a tusk
// stands, the tusks' capsule (bones 2 -> 3, or 2 alone with the first tusk broken)
void bossHitEntries(AnimatedActor& a, BossFields& f, uint32_t harmless) {
    f.body.previousX = a.prevPos.x.whole;
    f.body.previousZ = a.prevPos.z.whole;
    f.body.result = 0;
    auto& t = f.tusks();
    t.hit.previousX = a.prevPos.x.whole;
    t.hit.previousZ = a.prevPos.z.whole;
    t.hit.result = 0;
    uint32_t kind, attack;
    if (harmless == 0) {
        kind = kHitAttackSolid | kHitAttackThrown;
        attack = elephantRow(a.type, 1).attack;
    } else {
        kind = kHitAttackBump;
        attack = 0xff000000u;
    }
    LaneVec4& sp = scratchPoint();
    sp.z = 0;
    sp.x = 0;
    sp.y = -0x300;
    LaneVec3 in = scratchIn();
    bonePointOffsetInPlace(a, 0, in, scratchOut()); // exe 0x5e1970
    const uint32_t slot = uint32_t(a.slot) << 24;
    submitCapsule(bonePointOf(a, 0), 0x78, scratchOut(), 0x10, f.body, slot | 0x401000, attack | kind);
    if (!(f.flags & kBossSecondTuskBroken) && harmless == 0) {
        const ShortVec4& from = bonePointOf(a, 2);
        if (f.flags & kBossFirstTuskBroken) submitCapsule(from, 0x10, from, 0x10, t.hit, slot | 0x481000, attack | kind);
        else submitCapsule(from, 0x10, bonePointOf(a, 3), 0x10, t.hit, slot | 0x481000, attack | kind);
    }
}
// exe 0x4a7090(actor, n): n sparks around the feet
void footSparks(const AnimatedActor& a, int n) {
    for (int i = 0; i < n; ++i) {
        EffectActor* e = allocChapterSpark();
        if (!e) continue;
        const uint32_t r = rngStep();
        int16_t dx, dz;
        angleVector(int(r & 0xfff), 0x280, dx, dz);
        e->pos.x = dx;
        e->pos.y = 0;
        e->pos.z = dz;
        e->setRecordYaw(0); // the exe's uninitialised high word of EDX
        e->pos.x = int16_t(e->pos.x + a.pos.x.whole);
        e->pos.y = int16_t(int(a.pos.y.whole) - int((r >> 0xc) & 0xff) - 0x20);
        e->pos.z = int16_t(e->pos.z + a.pos.z.whole);
        classState<HitSparkFields>(*e).size = uint16_t(((r >> 0x14) & 0x7f) + 0x100);
    }
}

constexpr uint8_t kRockThrown = 0x01;  // a hit entry (thrown at the player)
constexpr uint8_t kRockFalling = 0x02; // past the top of its arc

// exe 0x4a7160: a rock thrown from the trunk (static 0x20 sub 0, a hop toward the player: a wider spread and 5 rocks
// with the first tusk broken) and a spark at the trunk
void throwRock(AnimatedActor& a, BossFields& f) {
    if (const uint32_t address = allocActor60(kActorStageClass)) {
        StaticActor& s = staticActorAt(address);
        RockFields& rock = classState<RockFields>(s);
        s.flags = kActorLive;
        s.type = kSt1dRockType;
        s.loopFlags |= kActorSweepGroup2;
        uint32_t r = rngStep();
        s.variant = uint8_t(r & 1);
        const uint32_t bone = ((~(f.flags >> 8) & 0x20 | 0x40) & 0xff) >> 5;
        int16_t pt[3];
        bonePointAhead(a, bone, 0x100, pt);
        setScratch(pt[0], pt[1], pt[2]);
        const LaneVec4& sp = scratchPoint();
        const int32_t from[3] = {int32_t(uint32_t(int32_t(sp.x)) << 16), int32_t(uint32_t(int32_t(sp.y)) << 16),
                                 int32_t(uint32_t(int32_t(sp.z)) << 16)};
        s.pos.x = from[0];
        s.pos.y = from[1];
        s.pos.z = from[2];
        const Player& p = player();
        const uint32_t px = uint32_t(p.pos.x), py = uint32_t(p.pos.y) - 0x800000u, pz = uint32_t(p.pos.z);
        int hx, hy, sp2, u;
        r = rngStep();
        if ((a.runFlags & kActorOffScreen) == 0) {
            if (!(f.flags & kBossFirstTuskBroken)) {
                hx = 0x80 - int(r & 0xff), hy = 0x40 - int((r >> 8) & 0x7f), sp2 = 0x80, u = int((r >> 0xf) & 0xff);
            } else {
                hx = 0x200 - int(r & 0x3ff), hy = 0x80 - int((r >> 10) & 0xff), sp2 = 0x200, u = int((r >> 0x12) & 0x3ff);
            }
        } else if (!(f.flags & kBossFirstTuskBroken)) {
            hx = 0x100 - int(r & 0x1ff), hy = 0x80 - int((r >> 9) & 0xff), sp2 = 0x100, u = int((r >> 0x11) & 0x1ff);
        } else {
            hx = 0x200 - int(r & 0x3ff), hy = 0x100 - int((r >> 10) & 0x1ff), sp2 = 0x200, u = int((r >> 0x13) & 0x3ff);
        }
        // the whole part moved by `add` (the fraction kept)
        auto moveWhole = [](uint32_t v, int add) {
            return int32_t((uint32_t(uint16_t((v >> 16) + uint32_t(add))) << 16) | (v & 0xffff));
        };
        const int32_t to[3] = {moveWhole(px, hx), moveWhole(py, hy), moveWhole(pz, sp2 - u)};
        if (!(f.flags & kBossFirstTuskBroken)) {
            s.velocity.y = int16_t(uint16_t(launchSpeed(from, to, 0x780, 0, 0xffffffffu)));
            s.velocity.z = -0x780;
            u = int(rngStep() & 0x1ff);
        } else {
            s.velocity.y = int16_t(uint16_t(launchSpeed(from, to, 0x600, 0, 0xffffffffu)));
            s.velocity.z = -0x600;
            u = int(rngStep() & 0x3ff);
        }
        const int16_t sc = int16_t(uint16_t(u + 0x1000));
        s.scale.z = sc;
        s.scale.y = sc;
        s.scale.x = sc;
        s.acceleration.y = 0;
        s.rotation.y = int16_t(angleOf(to[0], to[2], from[0], from[2]));
        rock.mode = kRockThrown;
        rock.attack = elephantRow(a.type, 1).strongAttack;
    }
    EffectActor* e = allocChapterSpark();
    if (e) {
        const LaneVec4& sp = scratchPoint();
        e->pos.x = sp.x;
        e->pos.y = sp.y;
        e->pos.z = sp.z;
        classState<HitSparkFields>(*e).size = uint16_t((rngStep() & 0x1f) + 0x80);
    }
}
// the point (0, 0x100, 0) << 3 through bone 0 -> the scratch's out point (the trunk's tip)
void trunkTip(const AnimatedActor& a) {
    LaneVec4& sp = scratchPoint();
    sp.z = 0;
    sp.x = 0;
    sp.y = 0x100;
    LaneVec3 in = scratchIn();
    bonePointOffsetInPlace(a, 0, in, scratchOut());
}
constexpr uint8_t kDustRingType = 0x11, kBoulderType = 0x14;
constexpr uint8_t kBoulderSub = 0, kShockRingSub = 1;
// exe 0x4a75a0: the dust ring of a boulder (effect type 0x11 at the trunk's tip)
void spawnDustRing(const AnimatedActor& a) {
    trunkTip(a);
    const uint32_t address = allocActorA0(kActorStageClass);
    if (address == 0) return;
    EffectActor& e = effectActorAt(address);
    DustRingFields& ring = classState<DustRingFields>(e);
    e.flags = kActorLive;
    e.type = kDustRingType;
    e.variant = 0;
    e.loopFlags |= kActorLateLoop;
    const LaneVec3& tip = scratchOut();
    e.pos.x = tip.x;
    e.pos.y = tip.y;
    e.pos.z = tip.z;
    ring.radius = 0x200;
    ring.shrink = 0x20;
}
// exe 0x4a7510: a boulder (pool 0x40 type 0x14 sub 0) out of the trunk, its yaw, and the dust ring
void blowBoulder(const AnimatedActor& a) {
    trunkTip(a);
    if (const uint32_t address = allocActor40(kActorStageClass)) {
        ProjectileActor& o = projectileActorAt(address);
        const LaneVec3& tip = scratchOut();
        o.flags = kActorLive;
        o.type = kBoulderType;
        o.variant = 0;
        o.loopFlags |= kActorSweepGroup2;
        o.pos.x.whole = tip.x;
        o.pos.y.whole = tip.y;
        o.pos.z.whole = tip.z;
        o.rotation.y = a.rotation.y;
    }
    spawnDustRing(a);
}
// exe 0x4a7630: the shock ring of a landing (pool 0x40 type 0x14 sub 1) at its position
void spawnShockRing(const AnimatedActor& a) {
    const uint32_t address = allocActor40(kActorStageClass);
    if (address == 0) return;
    ProjectileActor& o = projectileActorAt(address);
    o.variant = 0;
    o.spawnByte = 0;
    o.flags = kActorLive;
    o.loopFlags |= kActorSweepGroup2;
    o.type = kBoulderType;
    o.subtype = kShockRingSub;
    o.pos = a.pos;
}
// exe 0x4a76e0: the stomp, else the rocks (the stomp at once when close)
void chooseStompOrRocks(AnimatedActor& a, const BossFields& f) {
    const uint32_t r = rngStep();
    if ((r & 1) == 0) {
        if (r & 2) {
            a.subState = uint8_t(BossSub::Stomp);
            return;
        }
    } else if (f.distance < 0x601) {
        a.subState = uint8_t(BossSub::Stomp);
        return;
    }
    a.subState = uint8_t(BossSub::ThrowRocks);
}
// exe 0x4a7670: after rearing up -- far from the centre back to it; the player near the centre: a charge or the stomp /
// rocks; else the choice by distance
void chooseAfterRearing(AnimatedActor& a, const BossFields& f) {
    if (f.flags & kBossFarFromCentre) {
        enterSub(a, BossSub::Return);
        return;
    }
    if (f.flags & kBossPlayerNearCentre) {
        if (rngStep() & 1) {
            enterSub(a, BossSub::Charge);
            return;
        }
        chooseStompOrRocks(a, f);
        a.restartStep();
        return;
    }
    enterSub(a, BossSub::Choose);
}
// exe 0x4a7730: the player ahead at 0x600..0x9ff (else ahead, half the time)
bool closeAhead(const BossFields& f) {
    if (f.flags & kBossPlayerAhead) {
        const uint32_t d = f.distance;
        if (d >= 0x600 && d < 0xa00) return true;
        if (!(rngStep() & 1)) return true;
    }
    return false;
}
// exe 0x4a7790: the hit points below 2/3 (1/3 with a tusk broken) while the second tusk stands
bool hitPointsLow(const AnimatedActor& a, const BossFields& f) {
    const uint32_t v = f.flags;
    if (!(v & kBossSecondTuskBroken)) {
        const int hp = a.maxHitPoints;
        const int lim = (v & kBossFirstTuskBroken) ? hp / 3 : (hp / 3) * 2;
        if (a.hitPoints < lim) return true;
    }
    return false;
}
// the walk's start of the approaches: a count of 0x1e..0x2d ticks, the walk (animation 0xf) and its turn
void startWalk(AnimatedActor& a, BossFields& f) {
    f.counter = int16_t((rngStep() & 0xf) + 0x1e);
    a.velocity.z = kWalkSpeed;
    a.anim = 0xf;
    a.spin.y = 0x18;
    ++a.step;
}

// exe 0x4a55e0: idle for 0x10..0x4f ticks, then the approach (the player ahead) or the wander
void idle(AnimatedActor& a, BossFields& f) {
    if (a.step == 0) {
        f.counter = int16_t((rngStep() & 0x3f) + 0x10);
        a.anim = 0;
        ++a.step;
    } else if (a.step == 1 && countBy(f, -1) == 0) {
        enterSub(a, (f.flags & kBossPlayerAhead) ? BossSub::Approach : BossSub::Wander);
    }
}
// exe 0x4a5660: walks toward the player (animation 0xf); at the count's end (or far from the centre without the player
// near it), close ahead: the rocks (3 in 4) or back to the centre
void approach(const PlayerEnv& env, AnimatedActor& a, BossFields& f) {
    static constexpr uint8_t kRocks[8] = {0, 0, 0, 0, 0, 0, 1, 1};
    if (a.step == 0) {
        startWalk(a, f);
    } else if (a.step != 1) {
        walk(a);
        elephantGround(env, a, 2);
        return;
    }
    turnYawTo(a.rotation.y, towardPlayer(f), a.spin.y);
    turnYawTo(a.rotation.y, towardPlayer(f), a.spin.y);
    if (countBy(f, -1) == 0 || ((f.flags & kBossFarFromCentre) && !(f.flags & kBossPlayerNearCentre))) {
        f.counter = int16_t((rngStep() & 0xf) + 0x1e);
        if (closeAhead(f)) {
            if (kRocks[rngStep() & 7] == 0) {
                a.spin.y = 0;
                a.velocity.z = 0;
                a.subState = uint8_t(BossSub::ThrowRocks);
            } else {
                a.subState = uint8_t(BossSub::Return);
            }
            a.restartStep();
        }
    }
    walk(a);
    elephantGround(env, a, 2);
}
// exe 0x4a57e0: walks toward the player, then a random attack
void stalk(const PlayerEnv& env, AnimatedActor& a, BossFields& f) {
    static constexpr uint8_t kPick[8] = {0, 0, 1, 1, 2, 3, 3, 4};
    static constexpr BossSub kAttacks[5] = {BossSub::Charge, BossSub::Stomp, BossSub::ThrowRocks, BossSub::Boulder,
                                            BossSub::Return};
    if (a.step == 0) {
        startWalk(a, f);
    } else if (a.step != 1) {
        walk(a);
        elephantGround(env, a, 2);
        return;
    }
    turnYawTo(a.rotation.y, towardPlayer(f), a.spin.y);
    turnYawTo(a.rotation.y, towardPlayer(f), a.spin.y);
    if (countBy(f, -1) == 0) {
        a.spin.y = 0;
        a.velocity.z = 0;
        f.counter = int16_t((rngStep() & 0xf) + 0x1e);
        a.subState = uint8_t(kAttacks[kPick[rngStep() & 7]]);
        a.restartStep();
    }
    walk(a);
    elephantGround(env, a, 2);
}
// exe 0x4a5970: rears up (animation 6, then 0xd), then the choice (the player near the centre) or the boulder
void rearUp(const PlayerEnv& env, AnimatedActor& a, BossFields& f) {
    if (a.step == 0) {
        if (a.stepCounter == 0) {
            a.anim = 6;
            a.stepCounter = 1;
        } else if (a.animEnded()) {
            a.anim = 0xd;
            a.enterStep(1);
        }
    } else if (a.step == 1 && a.animEnded()) {
        if (f.flags & kBossPlayerNearCentre) chooseAfterRearing(a, f);
        else enterSub(a, BossSub::Boulder);
    }
    elephantGround(env, a, 2);
}
// exe 0x4a5a10: wanders (random turns every 15 ticks), back to idle / the approach; far from the centre home
void wander(const PlayerEnv& env, AnimatedActor& a, BossFields& f) {
    static constexpr uint8_t kTurns[32] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2,
                                           2, 2, 2, 2, 2, 2, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4};
    if (a.step == 0) {
        f.counter = 0;
        a.velocity.z = kWalkSpeed;
        a.anim = 0xf;
        a.spin.y = (rngStep() & 1) ? int16_t(0x18) : int16_t(-0x18);
        ++a.step;
    } else if (a.step != 1) {
        walk(a);
        elephantGround(env, a, 2);
        return;
    }
    if (countBy(f, 1) == 0xf) {
        f.counter = 0;
        const uint32_t r = rngStep();
        if ((r & 1) == 0) {
            switch (kTurns[(r >> 1) & 0x1f]) {
            case 0: a.spin.y = 0; break;
            case 1: a.spin.y = 0x18; break;
            case 2: a.spin.y = -0x18; break;
            case 3: enterSub(a, BossSub::Idle); break;
            case 4: enterSub(a, BossSub::Approach); break;
            default: break;
            }
        }
    }
    if (!(f.flags & kBossFarFromCentre)) {
        if ((f.flags & kBossPlayerAhead) && f.distance < 0x800) enterSub(a, BossSub::Approach);
    } else {
        a.spin.y = 0x18;
        a.velocity.z = 0;
        f.targetYaw = uint16_t(angleOf(arenaCentre(), a.pos));
        a.anim = 8;
        enterSub(a, BossSub::Return);
    }
    a.rotation.y += uint16_t(a.spin.x);
    walk(a);
    elephantGround(env, a, 2);
}
// exe 0x4a5c10: walks back to the arena's centre; within 0x600 idle (both tusks gone: the boulders)
void returnToCentre(const PlayerEnv& env, AnimatedActor& a, BossFields& f) {
    if (a.step == 0) {
        a.spin.y = 0x18;
        a.velocity.z = kWalkSpeed;
        a.anim = 0xf;
        f.targetYaw = uint16_t(angleOf(arenaCentre(), a.pos));
        ++a.step;
    } else if (a.step != 1) {
        walk(a);
        elephantGround(env, a, 2);
        return;
    }
    turnYawTo(a.rotation.y, f.targetYaw, a.spin.y);
    if (uint32_t(planarDistance(arenaCentre(), a.pos)) < 0x600)
        enterSub(a, (f.flags & kBossSecondTuskBroken) ? BossSub::Boulders : BossSub::Idle);
    walk(a);
    elephantGround(env, a, 2);
}
// the end of exe 0x4a5cc0 / 0x4a6840: a wall stops the charge (the bounce)
void chargeTail(const PlayerEnv& env, AnimatedActor& a) {
    if (elephantGround(env, a, 2) == 2) {
        a.velocity.z = 0;
        enterSub(a, BossSub::Bounce);
    }
}
// the charge's start (exe 0x4a5cc0 / 0x4a6840): turned to the player's angle (animation 9), the trumpet
// (animation 0xe) once facing it; false while still turning
void chargeAim(AnimatedActor& a, BossFields& f) {
    switch (a.step) {
    case 0:
        a.anim = 9;
        a.velocity.z = 0;
        a.spin.y = 0x50;
        f.targetYaw = uint16_t(f.playerAngle);
        ++a.step;
        return;
    case 1:
        if (absTurn(a, f.targetYaw, a.spin.y) <= a.spin.y) {
            a.rotation.y = int16_t(f.targetYaw);
            f.flags &= ~kBossCharging;
            a.anim = 0xe;
            soundPlayAtPos(kSoundTrumpet, a.pos);
            ++a.step;
        }
        return;
    default: return;
    }
}
// the charge's run: until the count's end or the player neither ahead nor aside, then the stop
void chargeRun(AnimatedActor& a, BossFields& f) {
    walk(a);
    if (countBy(f, -1) == 0 || (f.flags & (kBossPlayerAhead | kBossPlayerAside)) == 0) {
        f.flags &= kBossChargeOff;
        a.velocity.z = 0;
        enterSub(a, BossSub::Stop);
    }
}
// exe 0x4a5cc0: turns to the player, trumpets, charges (animation 0x1c, speed -0x5a0) for 0x3c ticks
void charge(const PlayerEnv& env, AnimatedActor& a, BossFields& f) {
    switch (a.step) {
    case 0:
    case 1: chargeAim(a, f); break;
    case 2:
        if (a.animEnded()) {
            f.counter = 0x3c;
            a.anim = 0x1c;
            a.velocity.z = kChargeSpeed;
            ++a.step;
        }
        break;
    case 3: chargeRun(a, f); break;
    default: break;
    }
    chargeTail(env, a);
}
// exe 0x4a5e50: bounced off a wall -- backs off (animation 5) on its first frames, then the return (1 in 4) or the
// stalk
void bounce(const PlayerEnv& env, AnimatedActor& a) {
    const uint8_t c = a.step;
    if (c == 0) {
        a.anim = 5;
        a.velocity.z = 0x200;
    } else if (c != 1) {
        if (c == 2 && a.animEnded()) enterSub(a, (rngStep() & 3) == 0 ? BossSub::Return : BossSub::Stalk);
        elephantGround(env, a, 2);
        return;
    }
    if (a.animKey.frame < 4) walk(a);
    if (a.animEnded()) {
        a.anim = 0;
        a.velocity.z = 0;
        ++a.step;
    }
    elephantGround(env, a, 2);
}
// exe 0x4a5f20: the charge's brake (animations 3 / 4 / 0), then the return
void brake(const PlayerEnv& env, AnimatedActor& a, BossFields& f) {
    const uint8_t c = a.step;
    switch (c) {
    case 0:
        f.counter = 0;
        a.anim = 0x1c;
        a.velocity.z = kChargeSpeed;
        a.step = uint8_t(c + 1u);
        break;
    case 1:
        if (countBy(f, 1) > 3) {
            a.anim = 3;
            a.step = uint8_t(c + 1u);
        }
        break;
    case 2:
        if (a.animEnded()) {
            a.anim = 4;
            a.velocity.z = 0;
            a.step = uint8_t(c + 1u);
        }
        break;
    case 3:
        if (a.animEnded()) {
            a.anim = 0;
            a.step = uint8_t(c + 1u);
        }
        break;
    case 4:
        if (a.animEnded()) {
            f.flags &= kBossChargeOff;
            a.velocity.z = 0;
            enterSub(a, BossSub::Return);
        }
        break;
    default: break;
    }
    walk(a);
    elephantGround(env, a, 2);
}
// exe 0x4a6010: stops (animation 0), then the stalk
void stopCharge(AnimatedActor& a, BossFields& f) {
    if (a.step == 0) {
        a.anim = 0;
        a.velocity.z = 0;
        a.step = 1;
    } else if (a.step == 1 && a.animEnded()) {
        f.flags &= kBossChargeOff;
        enterSub(a, BossSub::Stalk);
    }
}
// exe 0x4a6060: far -> the charge, else the stomp or the rocks
void chooseAttack(AnimatedActor& a, const BossFields& f) {
    if (f.distance > 0xa00) {
        enterSub(a, BossSub::Charge);
        return;
    }
    enterSub(a, (rngStep() & 1) ? BossSub::Stomp : BossSub::ThrowRocks);
}
// exe 0x4a60b0: turns to the player, raises the trunk (0x19 / 0x1a), throws rocks on frame 7 (5 with the first tusk
// broken), repeats while the player is ahead (10 / 2 times), lowers it (0x1b)
void throwRocks(AnimatedActor& a, BossFields& f) {
    switch (a.step) {
    case 0:
        f.counter = (f.flags & kBossFirstTuskBroken) ? int16_t(2) : int16_t(10);
        ++a.step;
        [[fallthrough]];
    case 1:
        a.anim = 8;
        a.velocity.z = 0;
        a.spin.y = 0x40;
        ++a.step;
        return;
    case 2:
        if (absTurn(a, towardPlayer(f), a.spin.y) <= a.spin.y) {
            a.anim = 0x19;
            ++a.step;
        }
        return;
    case 3:
        if (a.animEnded()) {
            a.anim = 0x1a;
            ++a.step;
        }
        return;
    case 4:
        if (a.animKey.frame == 7) {
            if (!(f.flags & kBossFirstTuskBroken)) {
                throwRock(a, f);
            } else {
                for (int i = 0; i < 5; ++i) throwRock(a, f);
            }
            soundPlayAtPos(kSoundThrow, a.pos);
        }
        if (a.animEnded()) {
            if ((f.flags & kBossPlayerAhead) && countBy(f, -1) != 0) {
                a.prevAnim = kNoAnimShown;
                return;
            }
            a.anim = 0x1b;
            ++a.step;
        }
        return;
    case 5:
        if (a.animEnded()) {
            const uint32_t v = f.flags;
            f.flags = v & ~kBossCharging;
            enterSub(a, (v & kBossSecondTuskBroken) ? BossSub::Stalk : BossSub::Approach);
        }
        return;
    default: return;
    }
}
// exe 0x4a6270: the stomp -- turned to the player, rears up (0x12, its sound), jumps (0x13, vy -0x600), falls (0x14),
// lands (0x15: the shock ring, its sound, foot sparks); at the animation's end a shake
void stomp(const PlayerEnv& env, AnimatedActor& a, BossFields& f) {
    switch (a.step) {
    case 0:
        f.targetYaw = uint16_t(f.playerAngle);
        f.flags |= kBossGuarded;
        a.spin.y = 0x38;
        a.anim = 8;
        ++a.step;
        [[fallthrough]];
    case 1:
        if (absTurn(a, towardPlayer(f), a.spin.y) <= a.spin.y) {
            a.anim = 0x12;
            soundPlayAtPos(kSoundStompRise, a.pos);
            ++a.step;
        }
        break;
    case 2:
        if (a.animEnded()) {
            a.anim = 0x13;
            a.velocity.y = -0x600;
            a.airborne = kAirborneFalling;
            ++a.step;
        }
        break;
    case 3:
        if (a.velocity.y >= 0) a.anim = 0x14;
        if (a.airborne == 0) {
            spawnShockRing(a);
            soundPlayAtPos(kSoundStompLand, a.pos);
            a.anim = 0x15;
            ++a.step;
        }
        break;
    case 4:
        footSparks(a, 2);
        if (a.animEnded()) {
            cameraShakeAt(0x8c000, 0x2000, 0x10, 0x1c0, a.pos);
            const uint32_t v = f.flags;
            f.flags = v & kBossGuardsOff;
            a.subState = uint8_t((v & kBossSecondTuskBroken) ? BossSub::Stalk : BossSub::Approach);
            a.restartStep();
        }
        break;
    default: break;
    }
    integrateVelocity(a, 0, a.velocity.y, 0);
    elephantGround(env, a, 2);
}
// exe 0x4a6430: blows a boulder (animation 0x16, then 0x17), back to the approach / the stalk
void boulderAttack(AnimatedActor& a, BossFields& f) {
    switch (a.step) {
    case 0:
        f.targetYaw = uint16_t(f.playerAngle);
        a.anim = 8;
        a.velocity.z = 0;
        a.spin.y = 0x38;
        ++a.step;
        f.flags |= kBossGuarded;
        [[fallthrough]];
    case 1:
        if (absTurn(a, f.targetYaw, a.spin.y) <= a.spin.y) {
            a.anim = 0x16;
            ++a.step;
        }
        return;
    case 2:
        turnYawTo(a.rotation.y, f.targetYaw, a.spin.y);
        if (a.animEnded()) {
            blowBoulder(a);
            f.counter = 0;
            ++a.step;
        }
        return;
    case 3:
        if (countBy(f, 1) > 0xf) {
            f.counter = 0;
            ++a.step;
        }
        return;
    case 4:
        if (countBy(f, 1) > 0x13) {
            a.anim = 0x17;
            ++a.step;
        }
        return;
    case 5:
        if (a.animEnded()) {
            a.anim = 0;
            ++a.step;
        }
        return;
    case 6:
        if (a.animEnded()) {
            const uint32_t v = f.flags;
            f.flags = v & kBossGuardsOff;
            enterSub(a, (v & kBossSecondTuskBroken) ? BossSub::Stalk : BossSub::Approach);
        }
        return;
    default: return;
    }
}
// exe 0x4a65e0: staggered (flashing), then the boulders (both tusks gone) or the stomp
void stagger(AnimatedActor& a, BossFields& f) {
    a.tint = (gameState().frame & 1) ? kTintFlash : kTintNeutral;
    if (a.animEnded()) {
        f.tusks().stagger = 0;
        enterSub(a, (f.flags & kBossSecondTuskBroken) ? BossSub::Boulders : BossSub::Stomp);
    }
}
// exe 0x4a6640: a tusk breaks (animation 0x10, flashing red): blasts at the tusk every 3 ticks, then the boulder
void tuskBreaks(AnimatedActor& a, BossFields& f) {
    a.tint = (gameState().frame & 1) ? uint16_t(0x421f) : kTintNeutral;
    const ShortVec4& tusk = bonePointOf(a, (f.flags & kBossSecondTuskBroken) ? 3 : 2);
    const uint32_t r = rngStep();
    LaneVec4& sp = scratchPoint();
    sp.x = int16_t(int(tusk.x) - int(r & 0xff) + 0x80);
    sp.y = int16_t(int(tusk.y) - int((r >> 8) & 0xff) + 0x80);
    sp.z = int16_t(int(tusk.z) - int((r >> 16) & 0xff) + 0x80);
    const uint8_t c = a.step;
    if (c == 0) {
        a.anim = 0x10;
        a.step = 1;
        f.counter = 0;
        spawnBlast(sp.x, sp.y, sp.z, uint16_t(((rngLastValue() >> 0x18) & 0x3f) + 0xc0));
    } else if (c == 1) {
        if (countBy(f, 1) > 2) {
            f.counter = 0;
            spawnBlast(sp.x, sp.y, sp.z, uint16_t(((rngLastValue() >> 0x18) & 0x3f) + 0xc0));
        }
        if (a.animEnded()) {
            f.counter = 0;
            ++a.step;
        }
    } else if (c == 2 && countBy(f, 1) > 0xe) {
        f.flags &= kBossGuardsOff;
        a.subState = uint8_t(BossSub::Boulder);
        f.tusks().stagger = 0;
        a.restartStep();
    }
}
// exe 0x4a6840: turns, trumpets, charges 0x5a ticks (homing while far), then the stop
void homingCharge(const PlayerEnv& env, AnimatedActor& a, BossFields& f) {
    switch (a.step) {
    case 0:
    case 1: chargeAim(a, f); break;
    case 2:
        f.counter = 0x5a;
        a.anim = 0x1c;
        a.velocity.z = kChargeSpeed;
        a.spin.y = 0x40;
        ++a.step;
        break;
    case 3:
        turnYawTo(a.rotation.y, towardPlayer(f), a.spin.y);
        walk(a);
        if (f.distance < 0xc00) {
            f.counter = 0x3c;
            ++a.step;
        }
        break;
    case 4: chargeRun(a, f); break;
    default: break;
    }
    chargeTail(env, a);
}
// exe 0x4a6a10: both tusks gone -- blows boulders three times in a row, then the approach
void boulders(AnimatedActor& a, BossFields& f) {
    switch (a.step) {
    case 0:
        f.targetYaw = uint16_t(f.playerAngle);
        f.repeats = 0;
        a.anim = 8;
        a.velocity.z = 0;
        a.spin.y = 0x38;
        f.flags |= kBossGuarded;
        ++a.step;
        [[fallthrough]];
    case 1:
        if (absTurn(a, f.targetYaw, a.spin.y) <= a.spin.y) {
            a.anim = 0x16;
            ++a.step;
        }
        return;
    case 2:
        if (a.animEnded()) {
            blowBoulder(a);
            f.counter = 0;
            ++a.step;
        }
        return;
    case 3:
        if (countBy(f, 1) > 0xf) {
            ++f.repeats;
            f.counter = 0;
            ++a.step;
        }
        return;
    case 4:
        if (countBy(f, 1) > 0x13) {
            if (f.repeats != 3) {
                f.targetYaw = uint16_t(f.playerAngle);
                a.step = 1;
                return;
            }
            a.anim = 0x17;
            ++a.step;
        }
        return;
    case 5:
        if (a.animEnded()) {
            f.flags &= kBossGuardsOff;
            a.anim = 0;
            ++a.step;
        }
        return;
    case 6:
        if (a.animEnded()) enterSub(a, BossSub::Approach);
        return;
    default: return;
    }
}
// exe 0x4a6bc0: down (animations 10 / 11), flag 0x682 after 0x3c ticks; a blast around it every 4th frame
void down(AnimatedActor& a, BossFields& f) {
    const uint8_t c = a.step;
    if (c == 0) {
        f.counter = 0;
        a.anim = 10;
        a.step = uint8_t(c + 1u);
    } else if (c == 1) {
        if (a.animEnded()) {
            a.anim = 0xb;
            a.step = 2;
        }
    } else if (c == 2 && countBy(f, 1) == 0x3c) {
        setStoryFlag(kFlagBossDown);
        a.step = uint8_t(c + 1u);
    }
    if ((gameState().frame & 3) == 0) {
        if (const uint32_t address = allocActor60(0)) {
            StaticActor& s = staticActorAt(address);
            BlastParams params(s);
            s.flags = kActorLive;
            s.type = typeByte(GlobalStaticType::Blast);
            s.variant = 0;
            s.loopFlags |= kActorSweepGroup2;
            const uint32_t r = rngStep();
            s.pos.x.whole = int16_t(int(a.pos.x.whole) - int(r & 0x1ff) + 0x100);
            s.pos.y.whole = int16_t(int(a.pos.y.whole) - int((r >> 9) & 0x1ff));
            params.size = 0xc0;
            params.attack = 0;
            s.pos.z.whole = int16_t(int(a.pos.z.whole) - int((r >> 0x12) & 0x1ff) + 0x100);
        }
    }
}
void bossSub(const PlayerEnv& env, AnimatedActor& a, BossFields& f) {
    switch (BossSub(a.subState)) {
    case BossSub::Idle: idle(a, f); return;
    case BossSub::Approach: approach(env, a, f); return;
    case BossSub::Stalk: stalk(env, a, f); return;
    case BossSub::RearUp: rearUp(env, a, f); return;
    case BossSub::Wander: wander(env, a, f); return;
    case BossSub::Return: returnToCentre(env, a, f); return;
    case BossSub::Charge: charge(env, a, f); return;
    case BossSub::Bounce: bounce(env, a); return;
    case BossSub::Brake: brake(env, a, f); return;
    case BossSub::Stop: stopCharge(a, f); return;
    case BossSub::Choose: chooseAttack(a, f); return;
    case BossSub::ThrowRocks: throwRocks(a, f); return;
    case BossSub::Stomp: stomp(env, a, f); return;
    case BossSub::Boulder: boulderAttack(a, f); return;
    case BossSub::Stagger: stagger(a, f); return;
    case BossSub::TuskBreaks: tuskBreaks(a, f); return;
    case BossSub::HomingCharge: homingCharge(env, a, f); return;
    case BossSub::Boulders: boulders(a, f); return;
    case BossSub::Down: down(a, f); return;
    default: notPortedAt(kStoryPort, "elephant boss sub-state", a.subState);
    }
}
// exe 0x4a51a0: model variant 1, the hit points of its row (<< 4), the box 0x893d58, the class block cleared, a bone
// block for its tusk block (in the fourth block, cleared), the eight pillars (static 0x20 sub 1 on the cells of the
// exe data 0x893d64), its encounter group, the boss gauge
constexpr uint8_t kPillarSub = 1;
void bossInit(const PlayerEnv& env, AnimatedActor& a, BossFields& f) {
    if (!bindActorModel(a, 1)) {
        freeActor(a);
        return;
    }
    a.flags |= kActorDrawn;
    a.prevAnim = 0;
    a.anim = 0;
    {
        const uint16_t hp =
            hasAnimatedStats(a.type) ? uint16_t(uint32_t(elephantRow(a.type, a.spawnByte).hitPoints) << 4) : 0;
        a.maxHitPoints = int16_t(hp);
        a.hitPoints = int16_t(hp);
    }
    elephantCommonInit(a);
    a.collisionBox.pointAt(TableId::ElephantBossBox);
    a.actorHit = 0;
    a.platform = nullptr;
    a.clearClassBlock();
    allocBoneBlocks(a, 1);
    f.tuskBlock = ownBoneBlock(a, 3).at + 0x40;
    f.tusks().clearBytes();
    animSetPose(a, a.anim, 0);
    const DataArray<uint8_t> pillarCells(stateAddress(TableId::ElephantBossPillarCells)); // exe data: {column, row} of the eight pillars
    for (size_t k = 0; k < 8; ++k) {
        const uint32_t address = allocActor60(kActorStageClass);
        if (address == 0) continue;
        StaticActor& s = staticActorAt(address);
        s.flags = kActorLive;
        s.type = kSt1dRockType;
        s.subtype = kPillarSub;
        s.variant = 0;
        s.loopFlags |= kActorSweepGroup2;
        s.pos.x = int32_t((uint32_t(pillarCells[2 * k]) << 25) - 0x7f000000u);
        s.pos.y = 0;
        s.pos.z = int32_t((uint32_t(pillarCells[2 * k + 1]) << 25) - 0x7f000000u);
        const int16_t q[3] = {s.pos.x.whole, 0, s.pos.z.whole};
        s.pos.y = int32_t(uint32_t(probeGroundAt(env, q, CollisionBoxRef::record(0), 1, 0x20) + 0x100) << 16);
    }
    f.groupIndex = uint8_t(enemyGroupFind(actorSlotClass(a.slot), a.type, 1));
    hudAddBossGauge(a, 1, 4, 0xffffff);
    a.enterState(uint8_t(ElephantState::Main));
}
// a tusk breaks: the first one's parts hidden (1 and 2, part 13 shown) or the second's (part 1, 13 hidden, part 12
// shown)
void breakTusk(AnimatedActor& a, BossFields& f) {
    const uint32_t v = f.flags;
    if (!(v & kBossFirstTuskBroken)) {
        f.flags = v | kBossFirstTuskBroken;
        a.parts[3].flags |= kPartHidden;
        a.parts[2].flags |= kPartHidden;
        a.parts[13].flags &= uint8_t(~kPartHidden);
    } else {
        f.flags = v | kBossSecondTuskBroken;
        a.parts[1].flags |= kPartHidden;
        a.parts[13].flags |= kPartHidden;
        a.parts[12].flags &= uint8_t(~kPartHidden);
    }
}
// exe 0x4a5320: its group marked; the damage on the ground (destroyed -> down; the hit points low -> a tusk breaks;
// the stagger meter full -> staggered); the meter's decay, the sub-state, the tilt, the hit entries, the animation,
// the footsteps
void bossMain(const PlayerEnv& env, AnimatedActor& a, BossFields& f) {
    enemyGroupMark(f.groupIndex, 1);
    f.tusks().pitchOffset = 0;
    bool resetCounter = false;
    if (!(f.flagsLow & kBossUntouchable)) {
        const int r = bossDamage(a, f);
        if (a.airborne == 0) {
            if (r == -1) {
                a.subState = uint8_t(BossSub::Down);
                a.clearStepBytes();
                a.velocity.z = 0;
                ++f.deaths;
                resetCounter = true;
            } else if (r != 0 && hitPointsLow(a, f)) {
                if (!(f.flags & kBossGuarded)) {
                    breakTusk(a, f);
                    a.subState = uint8_t(BossSub::TuskBreaks);
                    a.clearStepBytes();
                    f.flags |= kBossGuarded;
                }
            } else if (f.tusks().stagger >= 0x800 && !(f.flags & kBossGuarded) && r != 0) {
                a.subState = uint8_t(BossSub::Stagger);
                a.clearStepBytes();
                f.flags |= kBossGuarded | kBossStaggerGuard;
                a.anim = 0x18;
                a.prevAnim = kNoAnimShown;
                a.velocity.z = 0;
                resetCounter = true;
            }
        }
    }
    if (resetCounter) f.counter = 0;
    {
        auto& t = f.tusks();
        const int16_t s = t.stagger;
        if (s != 0) {
            t.stagger = int16_t(s - 4);
            if (t.stagger < 0) t.stagger = 0;
        }
    }
    bossSub(env, a, f);
    followGround(env, a, f.tusks().pitchOffset);
    if (!(f.flagsLow & kBossUntouchable)) bossHitEntries(a, f, f.deaths);
    animateAsked(a, 0);
    const uint8_t anim = a.anim, key = a.keyIndex;
    if (anim == 1 || anim == 2) {
        if (key != 3 && key != 9) return;
    } else {
        if (anim != 0xf) return;
        if (key != 3 && key != 0x19) return;
    }
    soundPlayAtPos(kSoundFootstep, a.pos);
}
// exe 0x4a4fd0: the pose copies, the tint (both tusks gone: a pulsing red), the gravity; the state; the player's
// place against it and the arena (near the centre, ahead / aside, its distance and angle); off screen or not
void boss(const PlayerEnv& env, AnimatedActor& a) {
    BossFields& f = classState<BossFields>(a);
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    if (f.flags & kBossSecondTuskBroken) a.tint = uint16_t(0x4200u | ((gameState().frame & 0xf) + 0x10));
    else a.tint = kTintNeutral;
    if (a.airborne != 0) {
        a.velocity.y += a.acceleration.y;
        if (a.velocity.y > 0x2000) a.velocity.y = 0x2000;
    } else {
        a.velocity.y = 0;
    }
    switch (ElephantState(a.state)) {
    case ElephantState::Init: bossInit(env, a, f); break;
    case ElephantState::Main: bossMain(env, a, f); break;
    case ElephantState::Free: freeActor(a); break; // exe 0x468fa0
    default: notPortedAt(kStoryPort, "elephant boss state", a.state);
    }
    f.flags = uint32_t(planarDistance(a.pos, arenaCentre())) < 0xb00 ? f.flags & ~kBossFarFromCentre
                                                                     : f.flags | kBossFarFromCentre;
    f.flags = uint32_t(planarDistance(arenaCentre(), player().pos)) < 0xb00 ? f.flags | kBossPlayerNearCentre
                                                                            : f.flags & ~kBossPlayerNearCentre;
    f.distance = uint32_t(planarDistance(a.pos, player().pos));
    f.playerAngle = int16_t(uint32_t(angleOf(player().pos, a.pos)));
    int d = angleDiff(a.rotation.y, f.playerAngle);
    if (d < 0) d = -d;
    if (d < 0x200) f.flags = (f.flags & ~kBossPlayerAside) | kBossPlayerAhead;
    else if (d < 0x600) f.flags = (f.flags & ~kBossPlayerAhead) | kBossPlayerAside;
    else f.flags = f.flags & ~(kBossPlayerAhead | kBossPlayerAside);
    if ((a.runFlags & kActorRunMarked) == 0) { // exe 0x5e5a50(actor, 0): off screen / far
        if (pointOnScreenUnrecorded(a.pos) != 0) a.runFlags |= kActorOffScreen; // exe 0x5e59a0(pos, 0)
        else a.runFlags &= uint8_t(~kActorOffScreen);
    }
    // drawn: exe 0x5f20f0(actor, 0x300, 0x100, 0), the shadow (render output)
}

// ---- ST1D statics of type 0x20 (sub 0 rocks exe 0x5a51c0, sub 1 pillars 0x5a5530, sub 2 trails 0x5a5910) ----------
enum class RockState : uint8_t { Init = 0, Fly = 1, Free = 2 };
// the rocks' shared box (exe data 0x8ceda8 each falling rock sizes: a patched record)
PatchedRecord<BoxExtents> rockBox{TableId::ElephantBossRockBox};
// exe 0x5a51e0: the model of the variant (the scale survives the bind), a random spin, airborne, the box 0x8ceda8
void rockInit(StaticActor& a) {
    RockFields& rock = classState<RockFields>(a);
    const ShortVec3 scale = a.scale; // the spawner's (the bind resets it)
    if (!bindActorModel(a, a.variant)) {
        freeActor(a);
        return;
    }
    a.scale.z = scale.z;
    a.scale.x = scale.x;
    a.scale.y = scale.y;
    a.flags |= kActorDrawn;
    uint32_t r = rngStep();
    a.rotation.x = int16_t(r & 0xfff);
    a.rotation.z = int16_t((r >> 0xc) & 0xfff);
    r = rngStep();
    a.spin.x = int16_t((r & 0x3ff) - 0x1ff);
    a.airborne = kAirborneFalling;
    a.spin.y = int16_t(((r >> 10) & 0x3ff) - 0x1ff);
    a.prevPos.x = a.pos.x;
    a.onPlatform = 0;
    a.platform = nullptr;
    a.prevPos.y = a.pos.y;
    a.collisionBox.pointTo(*rockBox);
    a.prevPos.z = a.pos.z;
    a.prevRotation = a.rotation;
    a.actorHit = 0;
    a.interactionBox = nullptr;
    a.interaction = 0;
    a.clearClassBlock();
    rock.lifetime = 0;
    a.enterState(uint8_t(RockState::Fly));
}
// exe 0x5a54c0: a spark of the rock's size
void rockDust(const StaticActor& a) {
    EffectActor* e = allocChapterSpark();
    if (!e) return;
    e->pos.x = a.pos.x.whole;
    e->pos.y = a.pos.y.whole;
    e->pos.z = a.pos.z.whole;
    classState<HitSparkFields>(*e).size = uint16_t((a.scale.x >> 5) + int(rngStep() & 0x1f));
}
// exe 0x5a5440: the hit entry (its radius the scale >> 8, the rock's attack)
void rockHit(const StaticActor& a, RockFields& rock) {
    rock.hit.previousX = a.prevPos.x.whole;
    rock.hit.previousZ = a.prevPos.z.whole;
    rock.hit.result = 0;
    setScratch(a.pos.x.whole, a.pos.y.whole, a.pos.z.whole);
    submitAtScratch(rock.hit, uint32_t(int32_t(a.scale.x >> 8)) | uint32_t(a.slot) << 24 | 0x680000,
                    rock.attack | 0xff980000u);
}
void rockEnd(StaticActor& a) {
    a.enterState(uint8_t(RockState::Free));
    a.flags &= uint8_t(~kActorDrawn);
    rockDust(a);
}
// exe 0x5a5310: spins, falls, bounces off the walls; breaks on the floor, when hit (a touch or the player's attack) or
// after 600 ticks; a thrown or falling rock breaks the walls it meets
void rockFly(const PlayerEnv& env, StaticActor& a) {
    RockFields& rock = classState<RockFields>(a);
    const int r6 = a.scale.x >> 6;
    const uint32_t box = (uint32_t(-r6) & 0xffff) | (uint32_t(r6) << 16);
    BoxExtents& sharedBox = *rockBox; // the rocks' shared box (exe data: a patched record) sized by this rock
    sharedBox.minX = int16_t(uint16_t(box));
    sharedBox.maxX = int16_t(uint16_t(box >> 16));
    sharedBox.minY = sharedBox.minX;
    sharedBox.maxY = sharedBox.maxX;
    sharedBox.minZ = sharedBox.minX;
    sharedBox.maxZ = sharedBox.maxX;
    if (rock.hit.result & (kHitAttackBump | kHitAttackByPlayer)) {
        rockEnd(a);
        return;
    }
    a.rotation.x += uint16_t(a.spin.x);
    a.velocity.y += a.acceleration.y;
    a.rotation.z += uint16_t(a.spin.y);
    if (a.velocity.y > 0) rock.mode |= kRockFalling;
    integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
    const uint32_t t = actorTestMove(env, a);
    if (int32_t(t) < 0) {
        const uint32_t p = actorPushOut(env, a);
        if (p & kWallOnYPlus) {
            rockEnd(a);
            return;
        }
        if (rock.mode != 0) {
            a.enterState(uint8_t(RockState::Free));
            a.flags &= uint8_t(~kActorDrawn);
            rockDust(a);
            queueBreakableWallHit(t, rock.attack);
            return;
        }
        int16_t pitch = 0, yaw = a.rotation.y; // the exe's local rotation {0, yaw}
        reflectRotationOffWalls(pitch, yaw, p);
        a.rotation.y = yaw;
    }
    if (rock.mode & kRockThrown) rockHit(a, rock);
    ++rock.lifetime;
    if (rock.lifetime < 600) return;
    rockEnd(a);
}
// exe 0x5a51c0: [0x8ced98 + state * 4]
void rock(const PlayerEnv& env, StaticActor& a) {
    switch (RockState(a.state)) {
    case RockState::Init: rockInit(a); return;
    case RockState::Fly: rockFly(env, a); return;
    case RockState::Free: freeActor(a); return;
    default: notPortedAt(kStoryPort, "ST1D rock state", a.state);
    }
}
enum class PillarState : uint8_t { Init = 0, Stand = 1, Free = 2 };
// exe 0x5a56c0: 4 falling pieces (rocks of variants 2 / 3) and 8 sparks
void pillarDebris(const StaticActor& a, const PillarFields& pillar) {
    const int16_t yaw = pillar.debrisYaw;
    for (uint32_t k = 0; k < 4; ++k) {
        const uint32_t address = allocActor60(kActorStageClass);
        if (address == 0) continue;
        StaticActor& s = staticActorAt(address);
        RockFields& piece = classState<RockFields>(s);
        s.flags = kActorLive;
        s.type = kSt1dRockType;
        s.loopFlags |= kActorSweepGroup2;
        uint32_t r = rngStep();
        s.variant = uint8_t((r & 1) + 2);
        s.pos.x = int32_t(uint32_t(int(a.pos.x.whole) - int((r >> 1) & 0xff) + 0x80) << 16);
        s.pos.z = int32_t(uint32_t(int(a.pos.z.whole) - int((r >> 9) & 0xff) + 0x80) << 16);
        s.velocity.y = toShort(-0x600 - int((r >> 0x12) & 0x3ff));
        r = rngStep();
        s.velocity.z = toShort(-0x260 - int(r & 0x1ff));
        const int16_t sc = toShort(((r >> 9) & 0x7ff) + 0x1400);
        s.scale.z = sc;
        s.scale.y = sc;
        s.scale.x = sc;
        const uint32_t u = r >> 0x14;
        s.acceleration.y = 0xc0;
        s.pos.y = int32_t(uint32_t(int((u & 0xf)) - int(sc >> 6) + int(a.pos.y.whole) - (0x40 << k)) << 16);
        r = rngStep();
        s.rotation.y = int16_t(((r & 0x3ff) + uint32_t(yaw) - 0x200) & 0xfff);
        piece.mode = 0;
    }
    for (int i = 0; i < 8; ++i) {
        EffectActor* e = allocChapterSpark();
        if (!e) continue;
        const uint32_t r = rngStep();
        e->pos.x = int16_t(int(a.pos.x.whole) - int(r & 0xff) + 0x80);
        e->pos.y = int16_t(int(a.pos.y.whole) - int((r >> 8) & 0xff));
        e->pos.z = int16_t(int(a.pos.z.whole) - int((r >> 16) & 0xff) + 0x80);
        classState<HitSparkFields>(*e).size = uint16_t(((r >> 0x18) & 0x1f) + 0x100);
    }
}
// exe 0x5a5530: [0x8cedb4 + state * 4] -- an arena pillar standing on its floor cell (segment variant 0) until the
// charging boss (its group marked) hits it (a solid's touch): the cell to variant 1, the debris, freed
void pillar(StaticActor& a) {
    PillarFields& fields = classState<PillarFields>(a);
    switch (PillarState(a.state)) {
    case PillarState::Init: // exe 0x5a5550
        a.flags &= uint8_t(~kActorDrawn);
        a.clearClassBlock();
        fields.group = int16_t(enemyGroupFind(kAnimatedClass, kElephantType, 1));
        a.enterState(uint8_t(PillarState::Stand));
        return;
    case PillarState::Stand: { // exe 0x5a5590
        const GridCell cell(gridCellUnderPos(a.pos));
        bool hitOnly = false;
        if (cellSegmentVariant(cell.head) == 0) {
            const uint32_t w = enemyGroupMarks(fields.group, EnemyGroupMarks::Either);
            if (w == 0 || !(fields.hit.result & kHitAttackSolid)) {
                hitOnly = true;
            } else {
                switchCellSegmentVariant(cell.head, 1);
                pillarDebris(a, fields);
            }
        }
        if (!hitOnly) a.enterState(uint8_t(PillarState::Free));
        // exe 0x5a5660: the hit entry (a solid body 0x100 above the ground)
        fields.hit.result = 0;
        setScratch(a.pos.x.whole, toShort(a.pos.y.whole - 0x100), a.pos.z.whole);
        submitAtScratch(fields.hit, uint32_t(a.slot) << 24 | 0xc88050, 0xff900000u);
        return;
    }
    case PillarState::Free: freeActor(a); return;
    default: notPortedAt(kStoryPort, "ST1D pillar state", a.state);
    }
}
// a short of the exe the trail clears every tick (its reader, in the draw code, is not traced)
int16_t& trailMarker() { return elephantGlobals().trailMarker; }
enum class TrailState : uint8_t { Init = 0, Follow = 1, Free = 2 };
// exe 0x5a5910: the 16 last positions of its owner; its sprites are render output
void trail(StaticActor& a) {
    TrailFields& fields = classState<TrailFields>(a);
    switch (TrailState(a.state)) {
    case TrailState::Init: { // exe 0x5a5930
        Actor& o = *fields.owner;
        a.flags &= uint8_t(~kActorDrawn);
        fields.oldest = 0;
        fields.next = 1;
        fields.xs[0] = o.pos.x.whole;
        fields.ys[0] = o.pos.y.whole;
        fields.zs[0] = o.pos.z.whole;
        a.enterState(uint8_t(TrailState::Follow));
        return;
    }
    case TrailState::Follow: { // exe 0x5a5970
        Actor& o = *fields.owner;
        if (!(o.flags & kActorLive)) {
            a.enterState(uint8_t(TrailState::Free));
            return;
        }
        trailMarker() = 0;
        if (fields.oldest == fields.next) {
            const int16_t n = toShort(fields.oldest + 1);
            fields.oldest = n == 0x10 ? int16_t(0) : n;
        }
        const size_t i = size_t(uint32_t(int32_t(fields.next)));
        fields.xs[i] = o.pos.x.whole;
        fields.ys[i] = o.pos.y.whole;
        fields.zs[i] = o.pos.z.whole;
        const int16_t n = toShort(fields.next + 1);
        fields.next = n == 0x10 ? int16_t(0) : n;
        return;
    }
    case TrailState::Free: freeActor(a); return;
    default: notPortedAt(kStoryPort, "ST1D trail state", a.state);
    }
}
constexpr uint8_t kRockSub = 0, kTrailSub = 2;
void static20(const PlayerEnv& env, StaticActor& a) {
    switch (a.subtype) {
    case kRockSub: rock(env, a); return;
    case kPillarSub: pillar(a); return;
    case kTrailSub: trail(a); return;
    default: notPortedAt(kStoryPort, "ST1D static 0x20 sub", a.subtype);
    }
}

enum class ExitLightState : uint8_t { Init = 0, Shine = 1, Open = 2 };
int depthOf(int16_t x, int16_t y, int16_t z) {
    const int16_t v[3] = {x, y, z};
    const CameraPoint c = cameraPoint(v);
    const int32_t zz = c.view[2];
    if (zz >= projection().nearDepth) return zz;
    return zz < 1 ? 0 : zz;
}
// the light quads' POLY_FT4 words (exe 0x5aa130): code 0x2e (semi-transparent), the grey of params[0]; CLUT 0x7dd1,
// tpage 0x2e (abr 1: additive), the texture cell (0x80, 0x80)-(0x9f, 0x9f) corner by corner
constexpr uint32_t kExitLightClutUv0 = 0x7dd1809f, kExitLightTpageUv1 = 0x002e9f9f, kExitLightUv2 = 0x8080,
                   kExitLightUv3 = 0x9f80;
// exe 0x5aa130: two curtains of 7 x 8 light quads, each corner jittered by the RNG, linked into the OT at their depth
void exitLightTick(StaticActor& a) {
    ExitLightFields& fields = classState<ExitLightFields>(a);
    const uint32_t grey = a.params[0];
    const uint32_t colour = ((grey | dc::kPolyFT4Semi << 8) << 8 | grey) << 8 | grey;
    for (uint32_t k = 0; k < 2; ++k) {
        fields.columnStep = k == 0 ? int16_t(-0x80) : int16_t(0x80);
        fields.curtainX = k == 0 ? int16_t(0xf0) : int16_t(0x110);
        const int16_t c6 = fields.columnStep, c4 = fields.curtainX, bc = fields.originX, be = fields.originY,
                      c0 = fields.originZ;
        int16_t l18 = 0, l16 = 0, l14 = 0, l10 = 0, le = 0, lc = 0;
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 8; ++col) {
                const int16_t u4 = int16_t(rngStep() & 0x3f);
                int16_t v[4][3]; // the corners in packet order (0x6d00c0, 0x6d00c8, 0x6d00d0, 0x6d00d8)
                if (col == 0) {
                    const int16_t x8 = toShort(c4 + bc);
                    int16_t y7 = toShort(be - row * 0x4c - 0x20);
                    lc = c0;
                    const uint32_t r2 = rngStep();
                    l18 = toShort(c6 + x8);
                    l16 = toShort(y7 + u4);
                    y7 = toShort((r2 & 0x3f) + y7);
                    l10 = toShort(c6 + x8);
                    le = toShort(l16 + 0x20);
                    const int16_t q[4][3] = {{l18, l16, lc}, {x8, y7, lc}, {l10, le, lc}, {x8, toShort(y7 + 0x20), lc}};
                    std::copy(&q[0][0], &q[0][0] + 12, &v[0][0]);
                    l14 = lc;
                } else {
                    const int16_t x7 = toShort((col + 1) * c6 + c4 + bc);
                    const int16_t oy = l16, oz = l14, ox = l18;
                    l16 = toShort(u4 + be - row * 0x4c);
                    const int16_t q[4][3] = {{x7, l16, c0}, {ox, oy, oz}, {x7, toShort(l16 - 0x20), c0}, {l10, le, lc}};
                    std::copy(&q[0][0], &q[0][0] + 12, &v[0][0]);
                    l10 = x7;
                    le = toShort(l16 - 0x20);
                    lc = c0;
                    l18 = x7;
                    l14 = c0;
                }
                const int otz = (depthOf(v[0][0], v[0][1], v[0][2]) + depthOf(v[1][0], v[1][1], v[1][2]) +
                                 depthOf(v[2][0], v[2][1], v[2][2]) + depthOf(v[3][0], v[3][1], v[3][2])) >> 4;
                if (int32_t(stage().depthRange) < otz) return;
                if (otz >= 1) {
                    const uint32_t w[10] = {0, colour, 0, kExitLightClutUv0, 0, kExitLightTpageUv1, 0, kExitLightUv2,
                                            0, kExitLightUv3};
                    dc::otQuadFT4(a.at, w, v);
                }
            }
    }
}
// exe 0x5aa060: [0x8cf28c + state * 4]
void exitLight(StaticActor& a) {
    ExitLightFields& fields = classState<ExitLightFields>(a);
    switch (ExitLightState(a.state)) {
    case ExitLightState::Init: // exe 0x5aa080: the quads' origin, the late loop, its floor cell to variant 1 (closed)
        fields.originX = int16_t(uint16_t(a.pos.x.whole) & 0xfe00);
        fields.originY = toShort(a.pos.y.whole - 0x90);
        fields.originZ = int16_t((uint16_t(a.pos.z.whole) & 0xfe00) + 0x100);
        a.loopFlags |= kActorLateLoop;
        switchCellSegmentVariant(GridCell(gridCellUnderPos(a.pos)).head, 1);
        a.enterStateBytes(uint8_t(ExitLightState::Shine));
        return;
    case ExitLightState::Shine: exitLightTick(a); return;
    case ExitLightState::Open: // exe 0x5aa520: the floor cell back to variant 0, freed
        switchCellSegmentVariant(GridCell(gridCellUnderPos(a.pos)).head, 0);
        freeActor(a);
        return;
    default: notPortedAt(kStoryPort, "ST1D exit light state", a.state);
    }
}

enum class BoulderState : uint8_t { Init = 0, Run = 1, Free = 2 };
enum class BoulderSub : uint8_t { Grow = 0, Roll = 1, Burst = 2 };
constexpr uint16_t kSoundBoulderBlown = 0x17a, kSoundBoulderBounce = 0x17b;

// exe 0x5c7d80: the hit entry (the boulder row's shot attack)
void boulderHit(const ProjectileActor& a, BoulderFields& f) {
    f.hit.previousX = a.prevPos.x.whole;
    f.hit.previousZ = a.prevPos.z.whole;
    f.hit.result = 0;
    setScratch(a.pos.x.whole, a.pos.y.whole, a.pos.z.whole);
    submitAtScratch(f.hit, uint32_t(a.slot) << 24 | 0x600030, elephantRow(kElephantType, 2).shotAttack | 0x1a0000u);
}
// exe 0x5c7ca0: the flying boulder's glow, a 0xf0 draw command (0x0000ff radius 0x80 -> black radius 0x100, additive
// 0x22) at its centre
void boulderGlow(const ProjectileActor& a) {
    dc::emit({0x4000u, 0xf00000ffu,
              uint32_t(uint16_t(a.pos.x.whole)) | uint32_t(uint16_t(a.pos.y.whole)) << 16, 0x22000000u,
              uint32_t(uint16_t(a.pos.z.whole)) | 0x20000u, 0x1000080u},
             a.at);
}
// exe 0x5c7f00: the bounce gait (a random hop pattern, high bounces where the ground drops), the move along the
// rotation, the walls (they break; homing at the player a wall bursts it)
void boulderRoll(const PlayerEnv& env, ProjectileActor& a, BoulderFields& f) {
    const auto aimAt = [&a, &f](int pitchTurn, int yawTurn) {
        a.rotation.x = int16_t((uint16_t(f.aimPitch) + uint32_t(pitchTurn)) & 0xfff);
        a.rotation.y = int16_t((uint16_t(f.aimYaw) + uint32_t(yawTurn)) & 0xfff);
    };
    if (a.stepCounter > 1) {
        aimAt(0, 0);
    } else {
        switch (f.gait >> 1) {
        case 0: {
            a.velocity.z = -0x600;
            const uint8_t n = uint8_t(f.gait + 1u);
            f.gait = n;
            if (n > 1) {
                const uint8_t b = uint8_t(rngStep() & 1);
                f.gait = uint8_t(b + 1u);
                if (f.gaitFlags & 2) f.gait = uint8_t(b + 3u);
                f.gaitFlags ^= 2u;
                if (f.gait == 1 &&
                    int(a.velocity.z) * 2 - 0x400 < (int(a.pos.y.whole) - actorGround(env, a)) * 0x10) {
                    f.gait = 2;
                    f.gaitFlags = uint8_t((f.gaitFlags & 0xfdu) | 1u);
                }
                f.gait = uint8_t(f.gait << 1);
                soundPlayAtPos(kSoundBoulderBounce, a.pos);
            }
            aimAt(0, 0);
            break;
        }
        case 1: {
            a.velocity.z = -0xc00;
            const uint8_t n = uint8_t(f.gait + 1u);
            f.gait = n > 3 ? uint8_t(0) : n;
            aimAt(0x400, 0);
            break;
        }
        case 2: {
            a.velocity.z = -0xc00;
            const uint8_t n = uint8_t(f.gait + 1u);
            f.gait = n;
            if (n > 5) {
                f.gait = 0;
                f.gaitFlags &= 0xfeu;
            }
            aimAt(-0x400, 0);
            break;
        }
        case 3: {
            a.velocity.z = -0xc00;
            const uint8_t n = uint8_t(f.gait + 1u);
            f.gait = n > 7 ? uint8_t(0) : n;
            aimAt(0, 0x400);
            break;
        }
        case 4: {
            a.velocity.z = -0xc00;
            const uint8_t n = uint8_t(f.gait + 1u);
            f.gait = n > 9 ? uint8_t(0) : n;
            aimAt(0, -0x400);
            break;
        }
        default: break;
        }
    }
    moveAlongRotation(a.pos, a.rotation, 0, 0, a.velocity.z); // exe 0x5e37b0
    if (a.stepCounter != 0) {
        const uint32_t t = actorTestMove(env, a);
        if (t != 0) {
            queueBreakableWallHit(t, 0x1000);
            if (a.stepCounter == 3) {
                actorPushOut(env, a);
                a.subState = uint8_t(BoulderSub::Burst);
                a.restartStep();
            }
        }
    }
}
// exe 0x5c81d0(the scratch point, size): a blast (global static type 7) there
void blastAtScratch(uint16_t size) {
    const uint32_t address = allocActor60(0);
    if (address == 0) return;
    StaticActor& s = staticActorAt(address);
    BlastParams params(s);
    const LaneVec4& sp = scratchPoint();
    s.flags = kActorLive;
    s.loopFlags |= kActorSweepGroup2;
    s.type = typeByte(GlobalStaticType::Blast);
    s.variant = 0;
    s.pos.x.whole = sp.x;
    s.pos.y.whole = sp.y;
    s.pos.z.whole = sp.z;
    params.size = size;
    params.attack = 0;
}
// the player's chest (its position 0x80 up), 16.16
std::array<int32_t, 3> playerChest() {
    const Player& p = player();
    return {p.pos.x, int32_t(p.pos.y - 0x800000), p.pos.z};
}
void boulderSub(const PlayerEnv& env, ProjectileActor& a, BoulderFields& f) {
    switch (BoulderSub(a.subState)) {
    case BoulderSub::Grow: { // exe 0x5c78f0: grows in 16 ticks (the 64-step sine >> 4), then rolls
        a.rotation.z += 0xffu;
        const int16_t sc = toShort(trig64Tables().sine[size_t(int32_t(f.timer))] >> 4);
        a.scale.z = sc;
        a.scale.y = sc;
        a.scale.x = sc;
        ++f.timer;
        if (f.timer > 0xf) {
            f.timer = 0;
            a.scale.z = 0x200;
            a.scale.y = 0x200;
            a.scale.x = 0x200;
            a.subState = uint8_t(BoulderSub::Roll);
        }
        return;
    }
    case BoulderSub::Roll: { // exe 0x5c7960: aims at the player (pitch / yaw), launched within 0x800, homing turns of 0x30
        const uint8_t c = a.step;
        a.rotation.z += 0xffu;
        bool aim = false;
        if (c == 0) {
            f.spareWord = 0;
            f.aimWord = a.rotation.xyWord();
            f.timer = 0;
            a.stepCounter = 1;
            a.step = uint8_t(c + 1u);
            aim = true;
        } else if (c == 1) {
            aim = true;
        } else if (c == 2) {
            const auto p = playerChest();
            const auto me = values(a.pos);
            const int16_t ang = toShort(angleOf(p[0], p[2], me[0], me[2]));
            const int d = angleDiff(f.aimYaw, ang);
            if (uint32_t(int(toShort(d)) + 0x200) > 0x400) {
                const int pitch = pitchOf(p.data(), values(a.pos).data());
                int st = angleDiff(f.aimPitch, toShort(0x800 - pitch)); // exe 0x5c81a0(.., .., 0x30)
                if (0x30 <= st) st = 0x30;
                else if (st <= -0x30) st = -0x30;
                f.aimPitch = int16_t(f.aimPitch + st);
                a.stepCounter = 3;
            }
        }
        if (aim) {
            const auto p = playerChest();
            const int pitch = pitchOf(p.data(), values(a.pos).data());
            const auto me = values(a.pos);
            const int ang = angleOf(p[0], p[2], me[0], me[2]);
            f.aimPitch = int16_t(0x800 - pitch);
            f.aimYaw = int16_t(ang);
            if (planarDistance(a.pos, p.data()) < 0x800) {
                a.velocity.z = -0xb00;
                f.timer = 0;
                a.stepCounter = 2;
                ++a.step;
            }
        }
        boulderRoll(env, a, f);
        ++f.timer;
        if (f.timer > 0x77) {
            a.subState = uint8_t(BoulderSub::Burst);
            a.restartStep();
        }
        boulderGlow(a);
        return;
    }
    case BoulderSub::Burst: { // exe 0x5c7b00: bursts (blasts every 4 ticks for 8 ticks), then freed
        const uint8_t c = a.step;
        if (c == 0) {
            a.flags &= uint8_t(~kActorDrawn);
            f.timer = 0;
            setScratch(a.pos.x.whole, a.pos.y.whole, a.pos.z.whole);
            blastAtScratch(0xa0);
            a.step = uint8_t(c + 1u);
        } else if (c != 1) {
            return;
        }
        ++f.timer;
        if ((f.timer & 3) == 0) {
            const uint32_t r = rngStep();
            LaneVec4& sp = scratchPoint();
            sp.x = int16_t(int(a.pos.x.whole) - int(r & 0xff) + 0x80);
            sp.y = int16_t(int(a.pos.y.whole) - int((r >> 8) & 0xff) + 0x80);
            sp.z = int16_t(int(a.pos.z.whole) - int((r >> 16) & 0xff) + 0x80);
            blastAtScratch(0x40);
        }
        if (f.timer == 8) a.enterState(uint8_t(BoulderState::Free));
        return;
    }
    default: notPortedAt(kStoryPort, "ST1D boulder sub-state", a.subState);
    }
}
// exe 0x5c7780: [[0x8d1358 + sub * 4] + state * 4]
void boulder(const PlayerEnv& env, ProjectileActor& a) {
    BoulderFields& f = classState<BoulderFields>(a);
    a.prevPos = a.pos;
    a.prevRotation = a.rotation;
    a.wasAirborne = a.airborne;
    const BoulderState st = BoulderState(a.state);
    if (st == BoulderState::Free) {
        freeActor(a); // exe 0x5d0bc0 -> 0x5e1500
        return;
    }
    if (a.subtype == kBoulderSub) {
        if (st == BoulderState::Init) { // exe 0x5c77d0: model 0, the speed -0x600, the box 0x8d1370, its trail, grown
            if (!bindActorModel(a, 0)) {
                freeActor(a);
                return;
            }
            a.velocity.setZSpare(0, 0);
            a.flags |= kActorDrawn;
            a.velocity.setXY(0, 0);
            a.velocity.z = -0x600;
            a.rotation.x = 0;
            a.rotation.z = 0;
            a.collisionBox.pointAt(TableId::ElephantBossBoulderBox);
            a.actorHit = 0;
            a.platform = nullptr;
            f.clearBlockBytes();
            if (const uint32_t address = allocActor60(kActorStageClass)) { // its trail (static 0x20 sub 2)
                StaticActor& s = staticActorAt(address);
                TrailFields& trailFields = classState<TrailFields>(s);
                s.flags = kActorLive;
                s.type = kSt1dRockType;
                s.subtype = kTrailSub;
                s.variant = 0;
                s.loopFlags |= uint8_t(kActorSweepGroup2 | kActorLateLoop);
                s.pos.x.whole = a.pos.x.whole; // the exe stores the boulder's x in all three
                s.pos.y.whole = a.pos.x.whole;
                s.pos.z.whole = a.pos.x.whole;
                trailFields.owner = &a;
            }
            a.scale.z = 0;
            a.scale.y = 0;
            a.scale.x = 0;
            soundPlayAtPos(kSoundBoulderBlown, a.pos);
            a.enterState(uint8_t(BoulderState::Run));
            return;
        }
        if (st == BoulderState::Run) { // exe 0x5c78a0: a hit bursts it
            if (f.hit.result != 0 && BoulderSub(a.subState) != BoulderSub::Burst) {
                a.subState = uint8_t(BoulderSub::Burst);
                a.restartStep();
            }
            boulderSub(env, a, f);
            if (BoulderSub(a.subState) != BoulderSub::Burst) boulderHit(a, f);
            return;
        }
    } else if (a.subtype == kShockRingSub) {
        if (st == BoulderState::Init) { // exe 0x5c7c00
            a.rotation.z = 0;
            a.rotation.x = 0;
            a.velocity.setZSpare(0, 0);
            a.velocity.setXY(0, 0);
            a.collisionBox = nullptr;
            a.actorHit = 0;
            a.platform = nullptr;
            f.clearBlockBytes();
            f.ringRadius = 0x180;
            a.enterState(uint8_t(BoulderState::Run));
            return;
        }
        if (st == BoulderState::Run) { // exe 0x5c7c50: the ring grows 0x50 a tick; the player on the ground in its band is hit
            f.ringRadius += 0x50u;
            const Player& p = player();
            if (p.airborne == 0 && int(a.pos.y.whole) - int(p.pos.y.whole) < 0x81 &&
                uint32_t(f.ringRadius) - uint32_t(planarDistance(a.pos, p.pos)) < 0x201) {
                f.hit.previousX = a.prevPos.x.whole; // exe 0x5c7d00
                f.hit.previousZ = a.prevPos.z.whole;
                f.hit.result = 0;
                setScratch(p.pos.x.whole, toShort(p.pos.y.whole - 0x40), p.pos.z.whole);
                submitAtScratch(f.hit, uint32_t(a.slot) << 24 | 0x600080, 0x100028);
            }
            st1dShockRingCommand(a.at);
            if (f.ringRadius > 0xfff) a.enterState(uint8_t(BoulderState::Free));
            return;
        }
    }
    notPortedAt(kStoryPort, "ST1D boulder state",
                uint32_t(a.type) | uint32_t(a.subtype) << 8 | uint32_t(a.variant) << 16 | uint32_t(a.spawnByte) << 24);
}

// ---- the boulder's dust ring (exe 0x4372b0, ST1D effect type 0x11, late loop): shrinks, 16 RNG rays when seen -------
enum class DustRingState : uint8_t { Init = 0, Shrink = 1, Free = 2 };
void dustRing(EffectActor& a) {
    DustRingFields& ring = classState<DustRingFields>(a);
    switch (DustRingState(a.state)) {
    case DustRingState::Init: // exe 0x4372d0
        a.state = uint8_t(DustRingState::Shrink);
        a.flags &= uint8_t(~kActorDrawn);
        return;
    case DustRingState::Shrink: { // exe 0x4372e0
        const int32_t r = ring.radius - ring.shrink;
        ring.radius = r;
        if (r < 1) {
            ring.radius = 0;
            a.enterState(uint8_t(DustRingState::Free));
            return;
        }
        const auto p = lanes(a.pos);
        int16_t sxy[2];
        projectOnScreen(p.data(), sxy);
        int16_t depth = 0;
        const int w = projectedWidth(p.data(), 0x200, &depth);
        if (w != 0 && depth > 0x2f && !(uint16_t(sxy[0]) == 0x8000 && uint16_t(sxy[1]) == 0x8000))
            for (int i = 0; i < 16; ++i) rngStep(); // the rays (G4 packets, render output)
        return;
    }
    case DustRingState::Free: freeActor(a); return; // exe 0x4358c0
    default: notPortedAt(kStoryPort, "ST1D dust ring state", a.state);
    }
}

} // namespace

// ---- the shock ring --------------------------------------------------------------------------------------------------
// exe 0x5c7e00: the shock ring, a 0xf8 draw command (game/draw_commands): the position into the scratch point first;
// colour 0x606060 inside -> black outside (semi, additive tpage 0x2f), tilt bytes 0x0e / 0x10, the texture spin of the
// frame (& 0x3f), 4 texture tiles (b3 2), 32 steps (step shift 1), double sided (w4 >> 24 = 1), both radii the ring's,
// CLUT 0x7c91 uv (0xe0, 0x20) .. (0xff, 0x3f)
void st1dShockRingCommand(uint32_t actor) {
    const ProjectileActor& a = projectileActorAt(actor);
    const BoulderFields& f = classState<BoulderFields>(a);
    setScratch(a.pos.x.whole, a.pos.y.whole, a.pos.z.whole);
    const uint32_t r = f.ringRadius;
    const LaneVec4& sp = scratchPoint();
    dc::emit({(uint32_t(gameState().frame) & 0x3f) << 16 | 0x200100eu, 0xf8606060u, sp.xyWord(), 0x22000000u,
              uint32_t(uint16_t(sp.z)) | 0x1010000u, r << 16 | r, 0x7c9120e0u, 0x2f3fffu},
             a.at);
}

bool elephantScriptPorted(RoutineId handler) { return handler == RoutineId::ElephantScript28 || handler == RoutineId::ElephantScript29; }
bool elephantScriptTick(const PlayerEnv& env, RoutineId handler) {
    switch (handler) {
    case RoutineId::ElephantScript28: script28(env); return true;
    case RoutineId::ElephantScript29: script29(env); return true;
    default: return false;
    }
}
void st1dCrystalUpdate(const PlayerEnv&, uint32_t actor) { crystal(staticActorAt(actor)); }
void elephantCutsceneUpdate(const PlayerEnv& env, uint32_t actor) { cutsceneElephant(env, animatedActorAt(actor)); }
void elephantBossUpdate(const PlayerEnv& env, uint32_t actor) { boss(env, animatedActorAt(actor)); }
void st1dRockUpdate(const PlayerEnv& env, uint32_t actor) { static20(env, staticActorAt(actor)); }
void st1dPillarUpdate(const PlayerEnv& env, uint32_t actor) { static20(env, staticActorAt(actor)); }
void st1dExitLightUpdate(const PlayerEnv&, uint32_t actor) { exitLight(staticActorAt(actor)); }
void st1dBoulderUpdate(const PlayerEnv& env, uint32_t actor) { boulder(env, projectileActorAt(actor)); }
void st1dDustRingUpdate(const PlayerEnv&, uint32_t actor) { dustRing(effectActorAt(actor)); }

} // namespace od::port
