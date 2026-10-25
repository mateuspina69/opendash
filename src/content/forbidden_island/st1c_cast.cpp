// The cast of script 0x13 (see st1c_cast.hpp). Each class: its head (its block's exe pointer, the previous animation
// and pose), its states (exe tables: bind / run / free), the run's routine of its cue table (the named routine), then
// its animation tail. Step ticks are the script's timeline; the animation indices are model-bank indices keyed by it.
#include "content/forbidden_island/st1c_cast.hpp"
#include "content/forbidden_island/st1c_cast_state.hpp"

#include <initializer_list>

#include "content/forbidden_island/st1c_script13.hpp"

#include "game/loaded_data.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/cutscene_actor.hpp"
#include "game/game_state.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/matrix.hpp"
#include "game/port_gap.hpp"
#include "game/projection_state.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"
#include "game/state_array.hpp"
#include "game/story.hpp"

using namespace od::port::st1c_cast_state;

namespace od::port {

using namespace st1c_cast_state;

namespace {

// ---- the cues ----------------------------------------------------------------------------------------------------------
// A class's cue routines: per variant a table of routines by the script's step sub (exe data; the source names each
// routine)
struct CueRoutines : DataRecord {
    static constexpr uint32_t kBytes = 0x400;
    std::array<uint32_t, 0x100> bySub{}; // +0x0
    template <class R> static CueRoutines load(const R& r) {
        CueRoutines v;
        for (size_t i = 0; i < v.bySub.size(); ++i) v.bySub[i] = r.template read<uint32_t>(0x0 + i * sizeof(uint32_t));
        return v;
    }
};
struct CueRoutineTables : DataRecord {
    static constexpr uint32_t kBytes = 0x400;
    std::array<uint32_t, 0x100> byVariant{}; // +0x0
    template <class R> static CueRoutineTables load(const R& r) {
        CueRoutineTables v;
        for (size_t i = 0; i < v.byVariant.size(); ++i) v.byVariant[i] = r.template read<uint32_t>(0x0 + i * sizeof(uint32_t));
        return v;
    }
};
// [[tables + variant * 4] + step sub * 4]
RoutineRef cueRoutine(uint32_t tables, const ActorHeader& actor) {
    const CueRoutines cues = dataRecord<CueRoutines>(dataRecord<CueRoutineTables>(tables, CueRoutineTables::kBytes).byVariant[actor.variant], CueRoutines::kBytes);
    return stateRoutine(cues.bySub[scriptState().part<CastTimelineStep>().stepSub]);
}
constexpr RoutineId kNoCue = RoutineId::EmptyRoutine; // the empty routine (a plain return)
int stepTick() { return scriptState().part<CastTimelineTick>().stepTick; }



// Which tracks a class's key list plays (its copy of the key routine exe 0x480f60: both; 0x4b61f0 / 0x4f17e0: the first,
// the second's keys only counted; 0x480510: none, both only counted)
enum class KeyTracks { None, First, Both };
// the next key of each list due at the exact step tick selected on the tracks (as the mode says) and its cursor moved
void cueTrackKeys(KeyframeTracks& tracks, StateArray<uint8_t, 2>& cursors, uint32_t firstKeys, uint32_t secondKeys,
                  KeyTracks mode) {
    const CueKey keys[2] = {dataRecord<CueKey>(firstKeys + uint32_t(cursors[0]) * kCueKeySize, CueKey::kBytes),
                            dataRecord<CueKey>(secondKeys + uint32_t(cursors[1]) * kCueKeySize, CueKey::kBytes)};
    for (size_t i = 0; i < 2; ++i) {
        const CueKey& k = keys[i];
        if (k.tick == kCueKeysEnd || int32_t(k.tick) != stepTick()) continue;
        if (i == 0 && mode != KeyTracks::None) tracks.first.select(k.sequence, k.key);
        else if (i == 1 && mode == KeyTracks::Both) tracks.second.select(k.sequence, k.key);
        ++cursors[i];
    }
}
void cueKeys(CueBlock& block, uint32_t firstKeys, uint32_t secondKeys, KeyTracks mode = KeyTracks::Both) {
    cueTrackKeys(block.tracks, block.keyCursors, firstKeys, secondKeys, mode);
}

// An animation at a step tick
struct AnimCue {
    int tick;
    uint8_t anim;
};
// the plain cue list: at its tick an animation asked (at tick 0 the key cursors cleared first)
void animCues(AnimatedActor& a, CueBlock& block, std::initializer_list<AnimCue> cues) {
    const int t = stepTick();
    for (const AnimCue& c : cues) {
        if (c.tick != t) continue;
        if (t == 0) block.clearKeyCursors();
        a.anim = c.anim;
        return;
    }
}

// ---- the class steps ------------------------------------------------------------------------------------------------
// the head: the block's exe pointer, the previous animation, the tint, the previous pose and contact
void castHead(AnimatedActor& a, uint32_t& blockPointer) {
    blockPointer = slotFieldWord(a, kAnimatedClassBlockOffset);
    a.prevAnim = a.anim;
    a.tint = kTintNeutral;
    a.savePreviousPose();
    a.savePreviousContact();
}
// the bind (state 0 before the tracks): model variant 0 (none: freed), drawn (with `drawFlags`), animation 0, the block
// and the motion cleared, scale 1
constexpr int16_t kScaleOne = 0x200;
bool castBindModel(AnimatedActor& a, uint8_t drawFlags = kActorDrawn) {
    if (!bindActorModel(a, 0)) {
        freeActor(a);
        return false;
    }
    a.flags |= drawFlags;
    a.anim = 0;
    a.prevAnim = kNoAnimShown;
    a.clearClassBlock();
    clearVelocity(a);
    a.spin.assign(0, 0, 0);
    a.scale.z = kScaleOne;
    a.scale.y = kScaleOne;
    a.scale.x = kScaleOne;
    return true;
}
// the texture tracks on their tables (exe 0x5e33a0), the first (and the second) on sequence 0
void castTracks(CueBlock& block, uint32_t firstTable, uint32_t secondTable, bool second = true) {
    block.tracks.init(firstTable, secondTable);
    block.tracks.first.select(0, 0);
    if (second) block.tracks.second.select(0, 0);
}
// the animation tails: the asked animation from key 0 / from the block's start key (cleared) / with the held pose
void animFromStart(AnimatedActor& a) { animateAsked(a, 0); }
void animFromStartKey(AnimatedActor& a, uint8_t& startKey) {
    if (a.anim == a.prevAnim) {
        animStepPose(a);
        return;
    }
    animSetPose(a, a.anim, startKey);
    startKey = 0;
}
void animHoldingPose(AnimatedActor& a, CueBlock& block) {
    if (a.anim != a.prevAnim) {
        animSetPose(a, a.anim, block.startKey);
        block.startKey = 0;
    } else if (block.holdPose != 0) {
        animDecodeCurrent(a); // exe 0x5e1f30
        animPose(a);          // exe 0x5e2d90
    } else {
        animStepPose(a);
    }
}
// parts hidden / one shown (their kPartHidden bits)
void hideParts(AnimatedActor& a, std::initializer_list<size_t> hidden, size_t shown) {
    for (size_t p : hidden) a.parts[p].flags |= kPartHidden;
    a.parts[shown].flags &= uint8_t(~kPartHidden);
}
void hidePart(AnimatedActor& a, size_t p) { a.parts[p].flags |= kPartHidden; }
void showPart(AnimatedActor& a, size_t p) { a.parts[p].flags &= uint8_t(~kPartHidden); }
// on the ground (exe 0x40f190 << 16)
void groundSnap(const PlayerEnv& env, AnimatedActor& a) { a.pos.y = int32_t(uint32_t(actorGround(env, a)) << 16); }
// the free state (exe 0x468fa0), the only state past the run
enum class CastState : uint8_t { Bind = 0, Run = 1, Free = 2 };
void freeOrUnknown(AnimatedActor& a, const char* cls) {
    if (a.state == uint8_t(CastState::Free)) {
        freeActor(a);
        return;
    }
    notPortedAt(kStoryPort, cls, a.state);
}
bool animEnded(const AnimatedActor& a) { return a.animEnded(); }

// the hop (exe 0x49b230's, 0x4e24a0 / 0x4ef150's): up at -0x400 under 0xc0 a tick, off the ground
constexpr int16_t kHopSpeed = -0x400, kHopGravity = 0xc0;
void hopUp(AnimatedActor& a) {
    a.velocity.y = kHopSpeed;
    a.acceleration.y = kHopGravity;
    a.airborne = kAirborneFalling;
}
// stopped and hidden
void stopHidden(AnimatedActor& a) {
    a.velocity.z = 0;
    a.velocity.y = 0;
    a.acceleration.y = 0;
    a.airborne = 0;
    a.flags &= uint8_t(~kActorDrawn);
}
// the flight (exe 0x49b310 / 0x4bc180): from tick 0 at speed z -0x800 with pitch -0x100; at 0x1e a pitch ramp, at 0x32
// a yaw ramp, at 0x46 / 0x55 the ramps stopped
void flight(AnimatedActor& a, CueBlock* block, uint8_t anim) {
    const int t = stepTick();
    if (t == 0) {
        if (block) block->clearKeyCursors();
        a.anim = anim;
        a.rotation.z = -0x100;
        a.velocity.z = -0x800;
        a.acceleration.x = 0;
        a.acceleration.z = 0;
    } else if (t == 0x1e) {
        a.acceleration.x = -0xc;
    } else if (t == 0x32) {
        a.acceleration.z = -0x20;
    } else if (t == 0x46) {
        a.acceleration.x = 0;
    } else if (t == 0x55) {
        a.acceleration.z = 0;
    }
    a.rotation.x += uint16_t(a.acceleration.x);
    a.rotation.z += uint16_t(a.acceleration.z);
    moveAlongRotation(a.pos, a.rotation, a.velocity.x, a.velocity.y, a.velocity.z); // exe 0x5e37b0
}
// the short hop (exe 0x4e24a0 / 0x4ef150): at tick 0 animation `anim` at z -0x100, at 5 up, at 0x1e stopped, hidden
void shortHop(AnimatedActor& a, CueBlock& block, uint8_t anim) {
    const int t = stepTick();
    if (t == 0) {
        block.clearKeyCursors();
        a.anim = anim;
        a.velocity.z = -0x100;
        a.velocity.y = 0;
        a.acceleration.y = 0;
        a.step = 0;
    } else if (t == 5) {
        hopUp(a);
        ++a.step;
    } else if (t == 0x1e) {
        stopHidden(a);
    }
    a.velocity.y += uint16_t(a.acceleration.y);
    integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z);
}

// ---- exe 0x478120 (type 0x16; states [0x890a40]: bind 0x478190, run 0x478260; cues [0x890a7c]) --------------------
void faceFigureRun(AnimatedActor& a) {
    CueBlock& block = classState<CueBlock>(a);
    const RoutineRef routine = cueRoutine(stateAddress(TableId::St1cCastFaceFigureCues), a);
    const int t = stepTick();
    switch (routine.id()) {
    case RoutineId::St1cFaceFigureCue1:
        if (t == 0) {
            block.clearKeyCursors();
            hidePart(a, 4);
            showPart(a, 15);
            a.anim = 0x12;
        } else if (t == 0x131) {
            a.anim = 0x13;
            hidePart(a, 4);
            showPart(a, 15);
            hidePart(a, 7);
            showPart(a, 16);
        } else if (t == 0x2f8) {
            a.anim = 0x14;
            hidePart(a, 15);
            showPart(a, 4);
            hidePart(a, 16);
            showPart(a, 7);
        }
        cueKeys(block, stateAddress(TableId::St1cCastFaceFigureCue1FirstKeys), stateAddress(TableId::St1cCastFaceFigureCue1SecondKeys));
        break;
    case RoutineId::St1cFaceFigureCue2:
        animCues(a, block, {{0, 8}, {0x136, 9}});
        cueKeys(block, stateAddress(TableId::St1cCastFaceFigureCue2FirstKeys), stateAddress(TableId::St1cCastFaceFigureCue2SecondKeys));
        break;
    case RoutineId::St1cFaceFigureCue3:
        animCues(a, block, {{0, 10}, {0x4b, 0xb}});
        cueKeys(block, stateAddress(TableId::St1cCastFaceFigureCue3FirstKeys), stateAddress(TableId::St1cCastFaceFigureCue3SecondKeys));
        break;
    case RoutineId::St1cFaceFigureCue4:
        if (t == 0x172) a.rotation.y = int16_t(a.rotation.y - 0x3a5);
        animCues(a, block, {{0, 0xc}, {0xaa, 0xd}, {0x172, 0xe}});
        cueKeys(block, stateAddress(TableId::St1cCastFaceFigureCue4FirstKeys), stateAddress(TableId::St1cCastFaceFigureCue4SecondKeys));
        break;
    case RoutineId::St1cFaceFigureCue5: // walks (z -0x33) until tick 0x78 and key 0x14, then stops (animation 0x10)
        if (t == 0) {
            block.clearKeyCursors();
            a.step = 0;
            a.velocity.z = -0x33;
            a.anim = 0xf;
            block.startKey = 0x14;
        }
        if (a.step == 0 && 0x78 < stepTick() && a.keyIndex == 0x14) {
            a.velocity.z = 0;
            a.anim = 0x10;
            a.step = 1;
        }
        integrateVelocity(a, 0, 0, a.velocity.z);
        cueKeys(block, stateAddress(TableId::St1cCastFaceFigureCue5FirstKeys), stateAddress(TableId::St1cCastFaceFigureCue5SecondKeys));
        break;
    case RoutineId::St1cFaceFigureCue6:
        animCues(a, block, {{0, 0}, {0x11e, 1}, {0x1d6, 2}});
        cueKeys(block, stateAddress(TableId::St1cCastFaceFigureCue6FirstKeys), stateAddress(TableId::St1cCastFaceFigureCue6SecondKeys));
        break;
    case RoutineId::St1cFaceFigureCue7:
        animCues(a, block, {{0, 3}, {0x14, 4}});
        cueKeys(block, stateAddress(TableId::St1cCastFaceFigureCue7FirstKeys), stateAddress(TableId::St1cCastFaceFigureCue7SecondKeys));
        break;
    case RoutineId::St1cFaceFigureCue8:
        animCues(a, block, {{0, 5}, {0x6e, 6}, {0xc0, 7}});
        cueKeys(block, stateAddress(TableId::St1cCastFaceFigureCue8FirstKeys), stateAddress(TableId::St1cCastFaceFigureCue8SecondKeys));
        break;
    default: notPortedAt(kStoryPort, "0x478120 chapter function", routine.pointerValue());
    }
    animFromStartKey(a, block.startKey);
    textureTracksTick(a, block.tracks);
}
void faceFigure(AnimatedActor& a) {
    castHead(a, castBlocks().faceFigure);
    switch (CastState(a.state)) {
    case CastState::Bind: {
        if (!castBindModel(a)) return;
        CueBlock& block = classState<CueBlock>(a);
        castTracks(block, stateAddress(TableId::St1cCastFaceFigureFirstTracks), stateAddress(TableId::St1cCastFaceFigureSecondTracks));
        a.enterState(uint8_t(CastState::Run));
        faceFigureRun(a);
        return;
    }
    case CastState::Run: faceFigureRun(a); return;
    default: freeOrUnknown(a, "0x478120 state");
    }
}

// ---- exe 0x47d160 (type 0x1a; states [0x89113c]: bind 0x47d1d0, run 0x47d2a0; cues [0x891178]). Instead of the
// texture tracks, exe 0x47d730: the second track's keys flip the meshes of parts 1 / 11 (key value 0: part 11 hidden)
void meshFlip(AnimatedActor& a, CueBlock& block) {
    keyframeTrackTick(block.tracks.second, [&](const Keyframe& key) {
        if (key.value == 0) {
            hidePart(a, 11);
            showPart(a, 1);
        } else {
            hidePart(a, 1);
            showPart(a, 11);
        }
    });
}
void meshFlipFigureRun(AnimatedActor& a) {
    CueBlock& block = classState<CueBlock>(a);
    const RoutineRef routine = cueRoutine(stateAddress(TableId::St1cCastMeshFlipFigureCues), a);
    const int t = stepTick();
    switch (routine.id()) {
    case RoutineId::St1cMeshFlipFigureCue1:
        animCues(a, block, {{0, 0xc}, {0x1d6, 0xd}, {700, 0xe}});
        cueKeys(block, stateAddress(TableId::St1cCastMeshFlipFigureCue1FirstKeys), stateAddress(TableId::St1cCastMeshFlipFigureCue1SecondKeys));
        break;
    case RoutineId::St1cMeshFlipFigureCue2:
        animCues(a, block, {{0, 6}});
        cueKeys(block, stateAddress(TableId::St1cCastMeshFlipFigureCue2FirstKeys), stateAddress(TableId::St1cCastMeshFlipFigureCue2SecondKeys));
        break;
    case RoutineId::St1cMeshFlipFigureCue3:
        animCues(a, block, {{0, 7}, {10, 8}});
        cueKeys(block, stateAddress(TableId::St1cCastMeshFlipFigureCue3FirstKeys), stateAddress(TableId::St1cCastMeshFlipFigureCue3SecondKeys));
        break;
    case RoutineId::St1cMeshFlipFigureCue4:
        if (t == 0) {
            block.clearKeyCursors();
            a.anim = 9;
            a.step = 0;
        } else if (t == 0x167) {
            a.anim = 10;
        } else if (t == 0x1aa) {
            a.velocity.z = 0x28;
        }
        integrateVelocity(a, 0, 0, a.velocity.z);
        cueKeys(block, stateAddress(TableId::St1cCastMeshFlipFigureCue4FirstKeys), stateAddress(TableId::St1cCastMeshFlipFigureCue4SecondKeys));
        break;
    case RoutineId::St1cMeshFlipFigureCue5:
        switch (t) {
        case 0:
            block.clearKeyCursors();
            a.velocity.z = -0x28;
            a.anim = 0xb;
            break;
        case 0x5a:
            a.velocity.z = 0;
            a.anim = 0xf;
            break;
        case 0xe2: a.anim = 0x10; break;
        case 0xf3:
            a.rotation.y = int16_t(a.rotation.y + kQuarterTurn);
            a.velocity.z = -0x28;
            a.anim = 0xb;
            integrateVelocity(a, 0, 0, -0xb7);
            break;
        default: break;
        }
        integrateVelocity(a, 0, 0, a.velocity.z);
        cueKeys(block, stateAddress(TableId::St1cCastMeshFlipFigureCue5FirstKeys), stateAddress(TableId::St1cCastMeshFlipFigureCue5SecondKeys));
        break;
    case RoutineId::St1cMeshFlipFigureCue6:
        animCues(a, block, {{0, 0}, {200, 1}, {0x159, 2}});
        cueKeys(block, stateAddress(TableId::St1cCastMeshFlipFigureCue6FirstKeys), stateAddress(TableId::St1cCastMeshFlipFigureCue6SecondKeys));
        break;
    case RoutineId::St1cMeshFlipFigureCue7:
        animCues(a, block, {{0, 5}, {0x14, 3}, {300, 4}});
        cueKeys(block, stateAddress(TableId::St1cCastMeshFlipFigureCue7FirstKeys), stateAddress(TableId::St1cCastMeshFlipFigureCue7SecondKeys));
        break;
    default: notPortedAt(kStoryPort, "0x47d160 chapter function", routine.pointerValue());
    }
    animFromStart(a);
    meshFlip(a, block);
}
void meshFlipFigure(AnimatedActor& a) {
    castHead(a, castBlocks().meshFlipFigure);
    switch (CastState(a.state)) {
    case CastState::Bind: {
        if (!castBindModel(a)) return;
        CueBlock& block = classState<CueBlock>(a);
        castTracks(block, stateAddress(TableId::St1cCastMeshFlipFigureFirstTracks), stateAddress(TableId::St1cCastMeshFlipFigureSecondTracks));
        meshFlipFigureRun(a);
        a.enterState(uint8_t(CastState::Run));
        return;
    }
    case CastState::Run: meshFlipFigureRun(a); return;
    default: freeOrUnknown(a, "0x47d160 state");
    }
}

// ---- exe 0x4802e0 (type 0x1d; states [0x89163c]: bind 0x480350, run 0x4803f0; cues [0x891690]) ------------------
void cuedFigureRun(AnimatedActor& a) {
    CueBlock& block = classState<CueBlock>(a);
    const RoutineRef routine = cueRoutine(stateAddress(TableId::St1cCastCuedFigureCues), a);
    const int t = stepTick();
    switch (routine.id()) {
    case RoutineId::St1cCuedFigureCue1:
        if (t == 0) a.anim = 2;
        break;
    case RoutineId::St1cCuedFigureCue2:
        if (t == 0) a.anim = 2;
        else if (t == 0x78) a.anim = 3;
        break;
    case RoutineId::St1cCuedFigureCue3:
        if (t == 0) a.anim = 0;
        else if (t == 0x5a) a.anim = 1;
        break;
    default: notPortedAt(kStoryPort, "0x4802e0 chapter function", routine.pointerValue());
    }
    cueKeys(block, stateAddress(TableId::St1cCastCuedFigureCue3FirstKeys), stateAddress(TableId::St1cCastCuedFigureCue3SecondKeys), KeyTracks::None); // exe 0x480510
    animFromStart(a);
}
void cuedFigure(AnimatedActor& a) {
    castHead(a, castBlocks().cuedFigure);
    switch (CastState(a.state)) {
    case CastState::Bind:
        if (!castBindModel(a)) return;
        a.enterState(uint8_t(CastState::Run));
        cuedFigureRun(a);
        return;
    case CastState::Run: cuedFigureRun(a); return;
    default: freeOrUnknown(a, "0x4802e0 state");
    }
}

// ---- exe 0x480c10 (type 0x1e; states [0x8917cc]: bind 0x480c80, run 0x480d50; cues [0x8917f0]) ------------------
void walkingFigureRun(AnimatedActor& a) {
    CueBlock& block = classState<CueBlock>(a);
    const RoutineRef routine = cueRoutine(stateAddress(TableId::St1cCastWalkingFigureCues), a);
    const int t = stepTick();
    switch (routine.id()) {
    case RoutineId::St1cWalkingFigureCue1:
        if (t == 0) {
            block.clearKeyCursors();
            a.anim = 5;
            hideParts(a, {4, 10}, 11);
        }
        cueKeys(block, stateAddress(TableId::St1cCastWalkingFigureCue1FirstKeys), stateAddress(TableId::St1cCastWalkingFigureCue1SecondKeys));
        break;
    case RoutineId::St1cWalkingFigureCue2:
        if (t == 0) {
            block.clearKeyCursors();
            a.anim = 0;
        } else if (t == 0xaa) {
            a.anim = 1;
        }
        cueKeys(block, stateAddress(TableId::St1cCastWalkingFigureCue2FirstKeys), stateAddress(TableId::St1cCastWalkingFigureCue2SecondKeys));
        break;
    case RoutineId::St1cWalkingFigureCue3:
        if (t == 0) {
            block.clearKeyCursors();
            a.anim = 2;
        } else if (t == 0x188) {
            a.anim = 3;
        }
        cueKeys(block, stateAddress(TableId::St1cCastWalkingFigureCue3FirstKeys), stateAddress(TableId::St1cCastWalkingFigureCue3SecondKeys));
        break;
    case RoutineId::St1cWalkingFigureCue4: // walks (z -0x31)
        if (t == 0) {
            block.clearKeyCursors();
            a.anim = 4;
            a.velocity.z = -0x31;
            hideParts(a, {4, 11}, 10);
        }
        integrateVelocity(a, a.velocity.x, 0, a.velocity.z);
        cueKeys(block, stateAddress(TableId::St1cCastWalkingFigureCue4FirstKeys), stateAddress(TableId::St1cCastWalkingFigureCue4SecondKeys));
        break;
    default: notPortedAt(kStoryPort, "0x480c10 chapter function", routine.pointerValue());
    }
    animFromStart(a);
    textureTracksTick(a, block.tracks);
}
void walkingFigure(AnimatedActor& a) {
    castHead(a, castBlocks().walkingFigure);
    switch (CastState(a.state)) {
    case CastState::Bind: {
        if (!castBindModel(a)) return;
        CueBlock& block = classState<CueBlock>(a);
        castTracks(block, stateAddress(TableId::St1cCastWalkingFigureFirstTracks), stateAddress(TableId::St1cCastWalkingFigureSecondTracks));
        a.enterState(uint8_t(CastState::Run));
        walkingFigureRun(a);
        return;
    }
    case CastState::Run: walkingFigureRun(a); return;
    default: freeOrUnknown(a, "0x480c10 state");
    }
}

// ---- exe 0x482880 (type 0x21; states [0x891cc0]: bind 0x4828f0, run 0x4829c0; cues [0x891cec]) ------------------
void posingFigureRun(AnimatedActor& a) {
    CueBlock& block = classState<CueBlock>(a);
    const RoutineRef routine = cueRoutine(stateAddress(TableId::St1cCastPosingFigureCues), a);
    const int t = stepTick();
    switch (routine.id()) {
    case RoutineId::St1cPosingFigureCue1:
        if (t == 0) {
            block.clearKeyCursors();
            a.anim = 0x11;
            a.rotation.x = 0x98;
        }
        cueKeys(block, stateAddress(TableId::St1cCastPosingFigureCue1FirstKeys), stateAddress(TableId::St1cCastPosingFigureCue1SecondKeys));
        break;
    case RoutineId::St1cPosingFigureCue2:
        animCues(a, block, {{0, 0x12}});
        cueKeys(block, stateAddress(TableId::St1cCastPosingFigureCue2FirstKeys), stateAddress(TableId::St1cCastPosingFigureCue2SecondKeys));
        break;
    case RoutineId::St1cPosingFigureCue3:
        animCues(a, block, {{0, 1}, {0x122, 2}, {0x1bc, 3}});
        cueKeys(block, stateAddress(TableId::St1cCastPosingFigureCue3FirstKeys), stateAddress(TableId::St1cCastPosingFigureCue3SecondKeys));
        break;
    case RoutineId::St1cPosingFigureCue4: // jump table 0x482c1c / 0x482c30
        if (t == 0) {
            block.clearKeyCursors();
            a.anim = 4;
            block.holdPose = 1;
        } else if (t == 0x46) {
            block.holdPose = 0;
        } else if (t == 0x82) {
            a.anim = 5;
        } else if (t == 0xaa) {
            a.anim = 6;
        }
        cueKeys(block, stateAddress(TableId::St1cCastPosingFigureCue4FirstKeys), stateAddress(TableId::St1cCastPosingFigureCue4SecondKeys));
        break;
    case RoutineId::St1cPosingFigureCue5:
        animCues(a, block, {{0, 7}, {0xfa, 8}});
        cueKeys(block, stateAddress(TableId::St1cCastPosingFigureCue5FirstKeys), stateAddress(TableId::St1cCastPosingFigureCue5SecondKeys));
        break;
    case RoutineId::St1cPosingFigureCue6:
        animCues(a, block, {{0, 9}, {0x96, 10}, {0x127, 0xb}, {0x176, 0xc}});
        cueKeys(block, stateAddress(TableId::St1cCastPosingFigureCue6FirstKeys), stateAddress(TableId::St1cCastPosingFigureCue6SecondKeys));
        break;
    case RoutineId::St1cPosingFigureCue7:
        animCues(a, block, {{0, 0xd}, {0x15e, 0xe}});
        cueKeys(block, stateAddress(TableId::St1cCastPosingFigureCue7FirstKeys), stateAddress(TableId::St1cCastPosingFigureCue7SecondKeys));
        break;
    case RoutineId::St1cPosingFigureCue8:
        animCues(a, block, {{0, 0xf}, {0xb4, 0x10}});
        cueKeys(block, stateAddress(TableId::St1cCastPosingFigureCue8FirstKeys), stateAddress(TableId::St1cCastPosingFigureCue8SecondKeys));
        break;
    default: notPortedAt(kStoryPort, "0x482880 chapter function", routine.pointerValue());
    }
    animHoldingPose(a, block);
    textureTracksTick(a, block.tracks);
}
void posingFigure(AnimatedActor& a) {
    castHead(a, castBlocks().posingFigure);
    switch (CastState(a.state)) {
    case CastState::Bind: {
        if (!castBindModel(a)) return;
        CueBlock& block = classState<CueBlock>(a);
        castTracks(block, stateAddress(TableId::St1cCastPosingFigureFirstTracks), stateAddress(TableId::St1cCastPosingFigureSecondTracks));
        posingFigureRun(a);
        a.enterState(uint8_t(CastState::Run));
        return;
    }
    case CastState::Run: posingFigureRun(a); return;
    default: freeOrUnknown(a, "0x482880 state");
    }
}

constexpr uint8_t kRisingPropDrawFlags = 0x62; // drawn, bit 5 of +0 (its reader is not traced), kActorScenery
constexpr uint8_t kSparkVariantProp = 4;       // the hit spark's variant of the prop's sparks
constexpr uint16_t kPropSparkSizeBase = 0x80;
// exe 0x48ac10(actor, side): a spark at (side, random y / z) turned by the actor's rotation
void propSpark(AnimatedActor& a, int16_t side) {
    ProjectionScratch& scratch = projectionScratch();
    scratch.point.x = side;
    const uint32_t r = rngStep();
    scratch.point.y = int16_t((r & 0x3f) - 0x1f);
    scratch.point.z = int16_t(((r >> 6) & 0x7f) + 0x40);
    scratch.transform.setRotationYXZ(a.rotation);
    const int16_t v[3] = {scratch.point.x, scratch.point.y, scratch.point.z};
    int16_t turned[3];
    transformShortVector(scratch.transform.matrix.get(), v, turned); // exe 0x40bb30
    scratch.rotatedPoint.x = turned[0];
    scratch.rotatedPoint.y = turned[1];
    scratch.rotatedPoint.z = turned[2];
    const uint32_t slot = allocActorA0(0); // exe 0x5e13b0: the global hit spark
    if (slot == 0) return;
    EffectActor& e = effectActorAt(slot);
    const uint32_t shift = stage().coordinateShift & 0x1f;
    e.flags = kActorLive;
    e.type = typeByte(GlobalEffectType::HitSpark);
    e.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    e.variant = kSparkVariantProp;
    e.pos.x = int16_t(uint32_t(int(a.pos.x.whole) + scratch.rotatedPoint.x) << shift);
    e.pos.y = int16_t(uint32_t(int(a.pos.y.whole) + scratch.rotatedPoint.y) << shift);
    e.pos.z = int16_t(uint32_t(int(a.pos.z.whole) + scratch.rotatedPoint.z) << shift);
    auto&& spark = classState<HitSparkFields>(e);
    spark.size = uint16_t(((rngLastValue() >> 0xd) & 0xf) + kPropSparkSizeBase);
}
// two sparks either side
void propSparks(AnimatedActor& a) {
    propSpark(a, -0x3f);
    propSpark(a, 0x3f);
}
void risingPropRun(AnimatedActor& a) {
    RisingPropBlock& block = classState<RisingPropBlock>(a);
    const RoutineRef routine = cueRoutine(stateAddress(TableId::St1cCastRisingPropCues), a);
    switch (routine.id()) {
    case RoutineId::St1cRisingPropCue1: // rises: speed z -0xc0, sparks every other frame
        if (a.step == 0) {
            a.velocity.z = -0xc0;
            a.step = 1;
        }
        if (a.step == 1) integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
        if (gameState().frame & 1) propSparks(a);
        break;
    case RoutineId::St1cRisingPropCue2: // pitched 0x100, moves -0x10 along its own z, sparks every fourth frame
        if (a.step == 0) {
            a.rotation.x = 0x100;
            a.velocity.z = -0x10;
            a.step = 1;
        }
        if (a.step == 1) moveAlongRotation(a.pos, a.rotation, 0, 0, a.velocity.z);
        if ((gameState().frame & 3) == 0) propSparks(a);
        break;
    case RoutineId::St1cRisingPropCue3: { // thrown up (0x380, -4 a tick), then it bobs on the sine table
        const uint8_t c = a.step;
        if (c == 0) {
            a.rotation.x = 0x18;
            a.velocity.y = 0x380;
            a.acceleration.y = -4;
            a.scale.z = 0x2800;
            a.scale.y = 0x2800;
            a.scale.x = 0x2800;
            a.step = 1;
        } else if (c != 1) {
            if (c != 2) break;
            ++block.bobPhase;
            a.pos.y = int32_t(uint32_t(int32_t(fixedSin(int((block.bobPhase & 0x1ff) * 8))) +
                                       int32_t(block.baseY) * 0x200) *
                              0x80u);
            break;
        }
        a.velocity.y = int16_t(a.velocity.y + a.acceleration.y);
        if (a.velocity.y < 0) {
            a.velocity.y = 0;
            block.bobPhase = 0;
            ++a.step;
        }
        moveAlongRotation(a.pos, a.rotation, 0, a.velocity.y, 0);
        block.baseY = a.pos.y.whole;
        break;
    }
    case RoutineId::St1cRisingPropCue4: // bobs from here (phase 0, the base its height)
        if (a.step != 0) break;
        block.bobPhase = 0;
        block.baseY = a.pos.y.whole;
        ++a.step;
        break;
    case kNoCue: break;
    default: notPortedAt(kStoryPort, "0x48a910 chapter function", routine.pointerValue());
    }
    animFromStart(a);
}
void risingProp(AnimatedActor& a) {
    castBlocks().risingProp = slotFieldWord(a, kAnimatedClassBlockOffset);
    a.prevAnim = a.anim;
    a.tint = kTintNeutral;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (CastState(a.state)) {
    case CastState::Bind:
        if (!castBindModel(a, kRisingPropDrawFlags)) return;
        risingPropRun(a);
        a.enterState(uint8_t(CastState::Run));
        return;
    case CastState::Run: risingPropRun(a); return;
    case CastState::Free: freeActor(a); return;
    default: notPortedAt(kStoryPort, "0x48a910 state", a.state);
    }
}

// ---- exe 0x48ba20 (type 0x28; states [0x8925d4]: bind 0x48ba90, run 0x48bb60; cues [0x892600]) ------------------
// exe 0x48bd20: animations 1 / 0 alternating with random waits (the step counter); at 0x1ec animation 2, stopped
void fidget(AnimatedActor& a, CueBlock& block) {
    const int t = stepTick();
    if (t == 0) {
        block.clearKeyCursors();
        a.step = 0;
        hideParts(a, {7, 12, 13}, 11);
        a.stepCounter = uint8_t(((rngStep() & 3) + 1) * 5);
        a.anim = 1;
    } else if (t == 0x1ec) {
        a.step = 2;
        a.anim = 2;
    }
    if (a.step == 0) {
        if (animEnded(a)) {
            --a.stepCounter;
            if (a.stepCounter == 0) {
                a.anim = 0;
                const uint32_t r = rngStep();
                a.step = 1;
                a.stepCounter = uint8_t((r & 0x1f) + 0x1e);
            }
        }
    } else if (a.step == 1) {
        --a.stepCounter;
        if (a.stepCounter == 0) {
            a.anim = 1;
            const uint32_t r = rngStep();
            a.step = 0;
            a.stepCounter = uint8_t(((r & 3) + 1) * 5);
        }
    }
    cueKeys(block, stateAddress(TableId::St1cCastFidgetFirstKeys), stateAddress(TableId::St1cCastFidgetSecondKeys));
}
void fidgetingFigureRun(AnimatedActor& a) {
    CueBlock& block = classState<CueBlock>(a);
    const RoutineRef routine = cueRoutine(stateAddress(TableId::St1cCastFidgetingFigureCues), a);
    const int t = stepTick();
    switch (routine.id()) {
    case RoutineId::St1cFidgetingFigureCue1:
        if (t == 0) {
            block.clearKeyCursors();
            a.anim = 0xb;
            a.rotation.x = 0x45;
            hideParts(a, {12, 13, 11}, 7);
        }
        cueKeys(block, stateAddress(TableId::St1cCastFidgetingFigureCue1FirstKeys), stateAddress(TableId::St1cCastFidgetingFigureCue1SecondKeys));
        break;
    case RoutineId::St1cFidgetingFigureCue2:
        if (t == 0) {
            block.clearKeyCursors();
            a.flags &= uint8_t(~kActorDrawn);
        } else if (t == 0x12) {
            a.anim = 0xc;
            a.flags |= kActorDrawn;
        } else if (t == 0x104) {
            a.anim = 0xd;
        }
        cueKeys(block, stateAddress(TableId::St1cCastFidgetingFigureCue2FirstKeys), stateAddress(TableId::St1cCastFidgetingFigureCue2SecondKeys));
        break;
    case RoutineId::St1cFidgetingFigureFidgetCue: fidget(a, block); break;
    case RoutineId::St1cFidgetingFigureCue4:
        if (t == 0) {
            block.clearKeyCursors();
            a.anim = 4;
            hideParts(a, {7, 12, 13}, 11);
            block.holdPose = 1;
        } else if (t == 0x46) {
            block.holdPose = 0;
        }
        cueKeys(block, stateAddress(TableId::St1cCastFidgetingFigureCue4FirstKeys), stateAddress(TableId::St1cCastFidgetingFigureCue4SecondKeys));
        break;
    case RoutineId::St1cFidgetingFigureCue5:
        if (t == 0) {
            block.clearKeyCursors();
            a.anim = 5;
            hideParts(a, {7, 11, 13}, 12);
        } else if (t == 0x6e) {
            a.anim = 6;
        } else if (t == 0x127) {
            a.anim = 7;
        } else if (t == 0x176) {
            a.anim = 8;
        }
        cueKeys(block, stateAddress(TableId::St1cCastFidgetingFigureCue5FirstKeys), stateAddress(TableId::St1cCastFidgetingFigureCue5SecondKeys));
        break;
    case RoutineId::St1cFidgetingFigureCue6:
        if (t == 0) {
            block.clearKeyCursors();
            a.anim = 9;
            hideParts(a, {12, 11, 13}, 7);
        } else if (t == 0xb4) {
            a.anim = 10;
            hideParts(a, {7, 11, 13}, 12);
        }
        cueKeys(block, stateAddress(TableId::St1cCastFidgetingFigureCue6FirstKeys), stateAddress(TableId::St1cCastFidgetingFigureCue6SecondKeys));
        break;
    default: notPortedAt(kStoryPort, "0x48ba20 chapter function", routine.pointerValue());
    }
    animHoldingPose(a, block);
    textureTracksTick(a, block.tracks);
}
void fidgetingFigure(AnimatedActor& a) {
    castHead(a, castBlocks().fidgetingFigure);
    switch (CastState(a.state)) {
    case CastState::Bind: {
        if (!castBindModel(a)) return;
        CueBlock& block = classState<CueBlock>(a);
        castTracks(block, stateAddress(TableId::St1cCastFidgetingFigureFirstTracks), stateAddress(TableId::St1cCastFidgetingFigureSecondTracks));
        a.enterState(uint8_t(CastState::Run));
        fidgetingFigureRun(a);
        return;
    }
    case CastState::Run: fidgetingFigureRun(a); return;
    default: freeOrUnknown(a, "0x48ba20 state");
    }
}

// ---- exe 0x49af90 (type 0x2f; states: bind, run 0x49b0d0; cues [0x8931a4]) -------------------------------------------
void hoppingFlyerRun(const PlayerEnv& env, AnimatedActor& a) {
    CueBlock& block = classState<CueBlock>(a);
    const RoutineRef routine = cueRoutine(stateAddress(TableId::St1cCastHoppingFlyerCues), a);
    const int t = stepTick();
    switch (routine.id()) {
    case RoutineId::St1cHoppingFlyerCue1:
        if (t == 0) {
            block.clearKeyCursors();
            a.anim = 0;
        }
        groundSnap(env, a);
        cueKeys(block, stateAddress(TableId::St1cCastHoppingFlyerCue1FirstKeys), stateAddress(TableId::St1cCastHoppingFlyerCue1SecondKeys));
        break;
    case RoutineId::St1cHoppingFlyerCue2:
        switch (t) {
        case 0: block.clearKeyCursors(); break;
        case 0x1e: a.anim = 1; break;
        case 0x208: a.anim = 2; break;
        case 0x463: a.anim = 3; break;
        case 0x654: a.anim = 4; break;
        case 0x6bd: a.anim = 5; break;
        case 0x722: a.anim = 6; break;
        default: break;
        }
        groundSnap(env, a);
        cueKeys(block, stateAddress(TableId::St1cCastHoppingFlyerCue2FirstKeys), stateAddress(TableId::St1cCastHoppingFlyerCue2SecondKeys));
        break;
    case RoutineId::St1cHoppingFlyerCue3: {
        // the hop: animation 7 (z -0x100), at its end animation 8, up; at 0x3c stopped, hidden
        if (t == 0) {
            block.clearKeyCursors();
            a.velocity.z = 0;
            a.velocity.y = 0;
            a.acceleration.y = 0;
            a.step = 0;
        } else if (t == 0x3c) {
            stopHidden(a);
        }
        const uint8_t s = a.step;
        if (s == 0) {
            a.anim = 7;
            a.velocity.z = -0x100;
            a.step = uint8_t(s + 1u);
        } else if (s == 1 && animEnded(a)) {
            a.anim = 8;
            hopUp(a);
            a.step = uint8_t(s + 1u);
        }
        a.velocity.y += uint16_t(a.acceleration.y);
        integrateVelocity(a, a.velocity.x, a.velocity.y, a.velocity.z);
        if (a.step != 2) groundSnap(env, a);
        cueKeys(block, stateAddress(TableId::St1cCastHoppingFlyerCue1FirstKeys), stateAddress(TableId::St1cCastHoppingFlyerCue1SecondKeys));
        break;
    }
    case RoutineId::St1cHoppingFlyerCue4:
        flight(a, &block, 9);
        cueKeys(block, stateAddress(TableId::St1cCastHoppingFlyerCue1FirstKeys), stateAddress(TableId::St1cCastHoppingFlyerCue1SecondKeys));
        break;
    default: notPortedAt(kStoryPort, "0x49af90 chapter function", routine.pointerValue());
    }
    animFromStart(a);
    textureTracksTick(a, block.tracks);
}
void hoppingFlyer(const PlayerEnv& env, AnimatedActor& a) {
    castHead(a, castBlocks().hoppingFlyer);
    switch (CastState(a.state)) {
    case CastState::Bind: {
        if (!castBindModel(a)) return;
        CueBlock& block = classState<CueBlock>(a);
        castTracks(block, stateAddress(TableId::St1cCastHoppingFlyerFirstTracks), stateAddress(TableId::St1cCastHoppingFlyerSecondTracks));
        hoppingFlyerRun(env, a);
        a.enterState(uint8_t(CastState::Run));
        return;
    }
    case CastState::Run: hoppingFlyerRun(env, a); return;
    default: freeOrUnknown(a, "0x49af90 state");
    }
}

// ---- exe 0x4b5f00 (type 0x3c; states [0x894690]: bind 0x4b5f70, run 0x4b6030; cues [0x8946b4]) ------------------
void vanishingWalkerRun(AnimatedActor& a) {
    CueBlock& block = classState<CueBlock>(a);
    const RoutineRef routine = cueRoutine(stateAddress(TableId::St1cCastVanishingWalkerCues), a);
    const int t = stepTick();
    switch (routine.id()) {
    case RoutineId::St1cVanishingWalkerCue1:
        animCues(a, block, {{0, 2}});
        cueKeys(block, stateAddress(TableId::St1cCastVanishingWalkerCue1FirstKeys), stateAddress(TableId::St1cCastVanishingWalkerCue1SecondKeys), KeyTracks::First);
        break;
    case RoutineId::St1cVanishingWalkerCue2:
        if (t == 0) {
            block.clearKeyCursors();
            a.anim = 0;
        } else if (t == 0x9e) {
            a.anim = 1;
        } else if (t == 0xac) {
            a.flags &= uint8_t(~kActorDrawn);
        }
        cueKeys(block, stateAddress(TableId::St1cCastVanishingWalkerCue2FirstKeys), stateAddress(TableId::St1cCastVanishingWalkerCue2SecondKeys), KeyTracks::First);
        break;
    case RoutineId::St1cVanishingWalkerCue3:
        animCues(a, block, {{0, 2}});
        cueKeys(block, stateAddress(TableId::St1cCastVanishingWalkerCue3FirstKeys), stateAddress(TableId::St1cCastVanishingWalkerCue3SecondKeys), KeyTracks::First);
        break;
    case RoutineId::St1cVanishingWalkerCue4: // walks (z -0x31)
        if (t == 0) {
            block.clearKeyCursors();
            a.anim = 3;
            a.velocity.z = -0x31;
        }
        integrateVelocity(a, a.velocity.x, 0, a.velocity.z);
        cueKeys(block, stateAddress(TableId::St1cCastVanishingWalkerCue4FirstKeys), stateAddress(TableId::St1cCastVanishingWalkerCue4SecondKeys), KeyTracks::First);
        break;
    default: notPortedAt(kStoryPort, "0x4b5f00 chapter function", routine.pointerValue());
    }
    animFromStart(a);
    textureTracksTick(a, block.tracks);
}
void vanishingWalker(AnimatedActor& a) {
    castHead(a, castBlocks().vanishingWalker);
    switch (CastState(a.state)) {
    case CastState::Bind: {
        if (!castBindModel(a)) return;
        CueBlock& block = classState<CueBlock>(a);
        castTracks(block, stateAddress(TableId::St1cCastVanishingWalkerFirstTracks), stateAddress(TableId::St1cCastVanishingWalkerSecondTracks), false);
        a.enterState(uint8_t(CastState::Run));
        vanishingWalkerRun(a);
        return;
    }
    case CastState::Run: vanishingWalkerRun(a); return;
    default: freeOrUnknown(a, "0x4b5f00 state");
    }
}

// ---- exe 0x4bc020 (type 0x3f; states [0x8948c4]: bind 0x4bc090, run 0x4bc130; cues [0x8948e0]) ------------------
void flyerRun(AnimatedActor& a) {
    const RoutineRef routine = cueRoutine(stateAddress(TableId::St1cCastFlyerCues), a);
    if (routine.is(RoutineId::St1cFlyerFlightCue)) flight(a, nullptr, 1);
    else notPortedAt(kStoryPort, "0x4bc020 chapter function", routine.pointerValue());
    animFromStart(a);
}
void flyer(AnimatedActor& a) {
    castHead(a, castBlocks().flyer);
    switch (CastState(a.state)) {
    case CastState::Bind:
        if (!castBindModel(a)) return;
        flyerRun(a);
        a.enterState(uint8_t(CastState::Run));
        return;
    case CastState::Run: flyerRun(a); return;
    default: freeOrUnknown(a, "0x4bc020 state");
    }
}

// ---- exe 0x4e2230 (type 0x59; states [0x895ab0]: bind 0x4e22a0, run 0x4e2370; cues [0x895ac8]) ------------------
void groundedHopperRun(const PlayerEnv& env, AnimatedActor& a) {
    CueBlock& block = classState<CueBlock>(a);
    const RoutineRef routine = cueRoutine(stateAddress(TableId::St1cCastGroundedHopperCues), a);
    const int t = stepTick();
    switch (routine.id()) {
    case RoutineId::St1cGroundedHopperCue1:
        animCues(a, block, {{0, 0}});
        groundSnap(env, a);
        cueKeys(block, stateAddress(TableId::St1cCastGroundedHopperCue1FirstKeys), stateAddress(TableId::St1cCastGroundedHopperCue1SecondKeys));
        break;
    case RoutineId::St1cGroundedHopperCue2:
        switch (t) {
        case 0: block.clearKeyCursors(); break;
        case 0x424: a.anim = 1; break;
        case 0x5be: a.anim = 2; break;
        case 0x722: a.anim = 3; break;
        default: break;
        }
        groundSnap(env, a);
        cueKeys(block, stateAddress(TableId::St1cCastGroundedHopperCue2FirstKeys), stateAddress(TableId::St1cCastGroundedHopperCue2SecondKeys));
        break;
    case RoutineId::St1cGroundedHopperCue3:
        shortHop(a, block, 4);
        cueKeys(block, stateAddress(TableId::St1cCastGroundedHopperCue1FirstKeys), stateAddress(TableId::St1cCastGroundedHopperCue1SecondKeys));
        break;
    default: notPortedAt(kStoryPort, "0x4e2230 chapter function", routine.pointerValue());
    }
    animFromStart(a);
    textureTracksTick(a, block.tracks);
}
void groundedHopper(const PlayerEnv& env, AnimatedActor& a) {
    castHead(a, castBlocks().groundedHopper);
    switch (CastState(a.state)) {
    case CastState::Bind: {
        if (!castBindModel(a)) return;
        CueBlock& block = classState<CueBlock>(a);
        castTracks(block, stateAddress(TableId::St1cCastGroundedHopperFirstTracks), stateAddress(TableId::St1cCastGroundedHopperSecondTracks));
        groundedHopperRun(env, a);
        a.enterState(uint8_t(CastState::Run));
        return;
    }
    case CastState::Run: groundedHopperRun(env, a); return;
    default: freeOrUnknown(a, "0x4e2230 state");
    }
}

// ---- exe 0x4e25f0 (type 0x5a; states [0x895c14]: bind 0x4e2660, run 0x4e2730; cues [0x895c4c]) ------------------
void fallingFigureRun(const PlayerEnv& env, AnimatedActor& a) {
    CueBlock& block = classState<CueBlock>(a);
    const RoutineRef routine = cueRoutine(stateAddress(TableId::St1cCastFallingFigureCues), a);
    const int t = stepTick();
    switch (routine.id()) {
    case RoutineId::St1cFallingFigureCue1:
        animCues(a, block, {{0, 8}});
        cueKeys(block, stateAddress(TableId::St1cCastFallingFigureCue1FirstKeys), stateAddress(TableId::St1cCastFallingFigureCue1SecondKeys));
        break;
    case RoutineId::St1cFallingFigureCue2: cueKeys(block, stateAddress(TableId::St1cCastFallingFigureCue1FirstKeys), stateAddress(TableId::St1cCastFallingFigureCue1SecondKeys)); break;
    case RoutineId::St1cFallingFigureCue3: // falls (y +0xc0 << 12 a tick) to the ground: animation 9, at its end 10
        if (t == 0) {
            block.clearKeyCursors();
            a.step = 0;
            a.velocity.y = 0xc0;
        }
        if (a.step == 0) {
            a.pos.y = int32_t(uint32_t(a.pos.y) + uint32_t(int32_t(a.velocity.y) * 0x1000));
            const int g = actorGround(env, a); // exe 0x40f190
            if (g <= a.pos.y.whole) {
                a.pos.y = int32_t(uint32_t(g) << 16);
                a.anim = 9;
                ++a.step;
            }
        } else if (a.step == 1 && animEnded(a)) {
            a.anim = 10;
            a.step = 2;
        }
        cueKeys(block, stateAddress(TableId::St1cCastFallingFigureCue1FirstKeys), stateAddress(TableId::St1cCastFallingFigureCue1SecondKeys));
        break;
    case RoutineId::St1cFallingFigureCue4: // no keys
        if (t == 0) {
            block.clearKeyCursors();
            a.anim = 10;
            block.startKey = 0x20;
        }
        break;
    case RoutineId::St1cFallingFigureCue5:
        if (t == 0) {
            block.clearKeyCursors();
            groundSnap(env, a);
            a.anim = 10;
            block.startKey = 0x20;
        } else if (t == 10) {
            a.anim = 0xb;
        } else if (t == 0xdc) {
            a.anim = 0xc;
        }
        cueKeys(block, stateAddress(TableId::St1cCastFallingFigureCue5FirstKeys), stateAddress(TableId::St1cCastFallingFigureCue5SecondKeys));
        break;
    case RoutineId::St1cFallingFigureCue6:
    case RoutineId::St1cFallingFigureCue7:
    case RoutineId::St1cFallingFigureCue8:
        animCues(a, block, {{0, 0}});
        cueKeys(block, stateAddress(TableId::St1cCastFallingFigureCue8FirstKeys), stateAddress(TableId::St1cCastFallingFigureCue8SecondKeys));
        break;
    case RoutineId::St1cFallingFigureCue9:
        animCues(a, block, {{0, 1}, {0x29, 2}, {0xbe, 3}});
        cueKeys(block, stateAddress(TableId::St1cCastFallingFigureCue9FirstKeys), stateAddress(TableId::St1cCastFallingFigureCue9SecondKeys));
        break;
    case RoutineId::St1cFallingFigureCue10:
        if (t == 0) {
            block.clearKeyCursors();
            a.anim = 3;
            block.startKey = 0x18;
        } else {
            animCues(a, block, {{0xf, 4}, {0x34, 5}, {0x114, 6}, {0x13a, 7}});
        }
        cueKeys(block, stateAddress(TableId::St1cCastFallingFigureCue10FirstKeys), stateAddress(TableId::St1cCastFallingFigureCue10SecondKeys));
        break;
    default: notPortedAt(kStoryPort, "0x4e25f0 chapter function", routine.pointerValue());
    }
    animFromStartKey(a, block.startKey);
    textureTracksTick(a, block.tracks);
}
void fallingFigure(const PlayerEnv& env, AnimatedActor& a) {
    castHead(a, castBlocks().fallingFigure);
    switch (CastState(a.state)) {
    case CastState::Bind: {
        if (!castBindModel(a)) return;
        CueBlock& block = classState<CueBlock>(a);
        castTracks(block, stateAddress(TableId::St1cCastFallingFigureFirstTracks), stateAddress(TableId::St1cCastFallingFigureSecondTracks));
        a.enterState(uint8_t(CastState::Run));
        fallingFigureRun(env, a);
        return;
    }
    case CastState::Run: fallingFigureRun(env, a); return;
    default: freeOrUnknown(a, "0x4e25f0 state");
    }
}

constexpr uint8_t kGlowType = 0x19, kGlowSubtype = 3; // pool 0xa0 stage type 0x19 sub 3
constexpr uint32_t kGlowBone = 10;
// exe 0x4e4170(actor, scratch point): the glow at the actor's bone 10 (its parameters: the bone point's address) into
// the block (the scratch point the callers pass is cleared first, unused)
void spawnGlow(AnimatedActor& a, GlowBlock& block) {
    projectionScratch().point.clearWords();
    const uint32_t slot = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (slot == 0) return;
    EffectActor& e = effectActorAt(slot);
    e.flags = kActorLive;
    e.type = kGlowType;
    e.subtype = kGlowSubtype;
    e.variant = 0;
    e.loopFlags |= kActorLateLoop;
    e.setParamsWord(bonePointAddress(a, kGlowBone)); // exe 0x5e1810
    e.pos.x = 0;
    e.pos.y = 0;
    e.pos.z = 0;
    block.glow = &e;
}
void glowingFigureRun(const PlayerEnv& env, AnimatedActor& a) {
    GlowBlock& block = classState<GlowBlock>(a);
    const RoutineRef routine = cueRoutine(stateAddress(TableId::St1cCastGlowingFigureCues), a);
    const int t = stepTick();
    auto keys = [&](uint32_t first, uint32_t second) {
        cueTrackKeys(block.tracks, block.keyCursors, first, second, KeyTracks::Both);
    };
    auto start = [&](uint8_t anim) {
        block.clearKeyCursors();
        a.anim = anim;
    };
    switch (routine.id()) {
    case RoutineId::St1cGlowingFigureCue1:
    case RoutineId::St1cGlowingFigureCue2:
        if (t == 0) {
            start(0);
            spawnGlow(a, block);
        }
        keys(stateAddress(TableId::St1cCastGlowingFigureCue2FirstKeys), stateAddress(TableId::St1cCastGlowingFigureCue2SecondKeys));
        break;
    case RoutineId::St1cGlowingFigureCue3: // rises (velocity y << 12 a tick)
        a.pos.y = int32_t(uint32_t(a.pos.y) + uint32_t(int32_t(a.velocity.y) * 0x1000));
        if (t == 0) {
            start(0);
            a.velocity.y = 8;
            spawnGlow(a, block);
        }
        keys(stateAddress(TableId::St1cCastGlowingFigureCue2FirstKeys), stateAddress(TableId::St1cCastGlowingFigureCue2SecondKeys));
        break;
    case RoutineId::St1cGlowingFigureCue4:
        if (t == 0) {
            start(0);
            spawnGlow(a, block);
        } else if (t == 0x1e) {
            a.anim = 1;
        }
        keys(stateAddress(TableId::St1cCastGlowingFigureCue4FirstKeys), stateAddress(TableId::St1cCastGlowingFigureCue4SecondKeys));
        break;
    case RoutineId::St1cGlowingFigureCue5:
        if (t == 0) {
            start(1);
            block.startKey = 0x13;
            spawnGlow(a, block);
        } else if (t == 0x28) {
            a.anim = 2;
        }
        keys(stateAddress(TableId::St1cCastGlowingFigureCue5FirstKeys), stateAddress(TableId::St1cCastGlowingFigureCue5SecondKeys));
        break;
    case RoutineId::St1cGlowingFigureCue6:
        if (t == 0) {
            start(2);
            block.startKey = 10;
            groundSnap(env, a);
            spawnGlow(a, block);
        }
        keys(stateAddress(TableId::St1cCastGlowingFigureCue2FirstKeys), stateAddress(TableId::St1cCastGlowingFigureCue2SecondKeys));
        break; // + the shadow exe 0x5f20f0(a, 0x20, 0x100, 0) (render output)
    case RoutineId::St1cGlowingFigureCue7:
        if (t == 0) {
            start(3);
            spawnGlow(a, block);
            if (block.glow == nullptr) notPorted(kStoryPort, "0x4e3ec0: no effect actor (the exe writes [6])");
            block.glow->variant = 1;
        }
        keys(stateAddress(TableId::St1cCastGlowingFigureCue7FirstKeys), stateAddress(TableId::St1cCastGlowingFigureCue7SecondKeys));
        break; // + the shadow (render output)
    case RoutineId::St1cGlowingFigureCue8:
        if (t == 0) {
            start(3);
            block.startKey = 0x73;
        } else if (t == 0x28) {
            a.anim = 4;
        } else if (t == 0x4d) {
            a.anim = 5;
        }
        keys(stateAddress(TableId::St1cCastGlowingFigureCue8FirstKeys), stateAddress(TableId::St1cCastGlowingFigureCue8SecondKeys));
        break;
    case RoutineId::St1cGlowingFigureCue9:
        if (t == 0) {
            block.clearKeyCursors();
            groundSnap(env, a);
            a.anim = 6;
        } else if (t == 0x14) {
            a.anim = 7;
        } else if (t == 0xf8) {
            a.anim = 8;
        }
        keys(stateAddress(TableId::St1cCastGlowingFigureCue9FirstKeys), stateAddress(TableId::St1cCastGlowingFigureCue9SecondKeys));
        break;
    case RoutineId::St1cGlowingFigureCue10:
        if (t == 0) {
            block.clearKeyCursors();
            groundSnap(env, a);
            a.anim = 6;
        } else if (t == 0xbd) {
            a.anim = 7;
        }
        keys(stateAddress(TableId::St1cCastGlowingFigureCue10FirstKeys), stateAddress(TableId::St1cCastGlowingFigureCue10SecondKeys));
        break;
    default: notPortedAt(kStoryPort, "0x4e3a80 chapter function", routine.pointerValue());
    }
    animFromStartKey(a, block.startKey);
    textureTracksTick(a, block.tracks);
}
void glowingFigure(const PlayerEnv& env, AnimatedActor& a) {
    castHead(a, castBlocks().glowingFigure);
    switch (CastState(a.state)) {
    case CastState::Bind: {
        if (!castBindModel(a)) return;
        GlowBlock& block = classState<GlowBlock>(a);
        block.tracks.init(stateAddress(TableId::St1cCastGlowingFigureBindActor), stateAddress(TableId::St1cCastGlowingFigureBindF));
        block.tracks.first.select(0, 0);
        block.tracks.second.select(0, 0);
        glowingFigureRun(env, a);
        a.enterState(uint8_t(CastState::Run));
        return;
    }
    case CastState::Run: glowingFigureRun(env, a); return;
    default: freeOrUnknown(a, "0x4e3a80 state");
    }
}

// ---- exe 0x4eef30 (type 0x68; states [0x89671c]: bind 0x4eefa0, run 0x4ef070; cues [0x896734]) ------------------
void hopperRun(AnimatedActor& a) {
    CueBlock& block = classState<CueBlock>(a);
    const RoutineRef routine = cueRoutine(stateAddress(TableId::St1cCastHopperCues), a);
    const int t = stepTick();
    switch (routine.id()) {
    case RoutineId::St1cHopperCue1: animCues(a, block, {{0, 0}}); break;
    case RoutineId::St1cHopperCue2:
        if (t == 0) block.clearKeyCursors();
        else if (t == 0x722) a.anim = 1;
        break;
    case RoutineId::St1cHopperShortHopCue: shortHop(a, block, 2); break;
    default: notPortedAt(kStoryPort, "0x4eef30 chapter function", routine.pointerValue());
    }
    cueKeys(block, stateAddress(TableId::St1cCastHopperShortHopCueFirstKeys), stateAddress(TableId::St1cCastHopperShortHopCueSecondKeys)); // exe 0x4ef200 (the tracks are not played by this class)
    animFromStart(a);
}
void hopper(AnimatedActor& a) {
    castHead(a, castBlocks().hopper);
    switch (CastState(a.state)) {
    case CastState::Bind: {
        if (!castBindModel(a)) return;
        CueBlock& block = classState<CueBlock>(a);
        castTracks(block, stateAddress(TableId::St1cCastHopperFirstTracks), stateAddress(TableId::St1cCastHopperSecondTracks));
        hopperRun(a);
        a.enterState(uint8_t(CastState::Run));
        return;
    }
    case CastState::Run: hopperRun(a); return;
    default: freeOrUnknown(a, "0x4eef30 state");
    }
}

// ---- exe 0x4f1550 (type 0x6b; states [0x896af4]: bind 0x4f15c0, run 0x4f1680; cues [0x896b18]) ------------------
void idleFigureRun(AnimatedActor& a) {
    CueBlock& block = classState<CueBlock>(a);
    const RoutineRef routine = cueRoutine(stateAddress(TableId::St1cCastIdleFigureCues), a);
    switch (routine.id()) {
    case RoutineId::St1cIdleFigureCue1:
    case RoutineId::St1cIdleFigureCue2:
    case RoutineId::St1cIdleFigureCue3:
    case RoutineId::St1cIdleFigureCue4:
    case RoutineId::St1cIdleFigureCue5: // animation 0 at tick 0 (the cursors are not cleared)
        if (stepTick() == 0) a.anim = 0;
        cueKeys(block, stateAddress(TableId::St1cCastIdleFigureCue5FirstKeys), stateAddress(TableId::St1cCastIdleFigureCue5SecondKeys), KeyTracks::First); // exe 0x4f17e0
        break;
    default: notPortedAt(kStoryPort, "0x4f1550 chapter function", routine.pointerValue());
    }
    animFromStart(a);
    textureTracksTick(a, block.tracks);
}
void idleFigure(AnimatedActor& a) {
    castHead(a, castBlocks().idleFigure);
    switch (CastState(a.state)) {
    case CastState::Bind: {
        if (!castBindModel(a)) return;
        CueBlock& block = classState<CueBlock>(a);
        castTracks(block, stateAddress(TableId::St1cCastIdleFigureFirstTracks), stateAddress(TableId::St1cCastIdleFigureSecondTracks), false);
        a.enterState(uint8_t(CastState::Run));
        idleFigureRun(a);
        return;
    }
    case CastState::Run: idleFigureRun(a); return;
    default: freeOrUnknown(a, "0x4f1550 state");
    }
}

// ---- the statics -----------------------------------------------------------------------------------------------------
// the statics' head: the class block published, the previous pose and contact
void propHead(StaticActor& a, uint32_t& link) {
    link = slotFieldWord(a, kStaticClassBlockOffset);
    a.savePreviousPose();
    a.savePreviousContact();
}
// the common init after the model (exe 0x5912d0 / 0x5b2630 / 0x5b5dd0): drawn, the class block and the motion cleared
void propInit(StaticActor& a) {
    a.flags |= kActorDrawn;
    a.clearClassBlock();
    a.velocity.z = 0;
    a.velocity.y = 0;
    a.velocity.x = 0;
    a.acceleration.z = 0;
    a.acceleration.y = 0;
    a.acceleration.x = 0;
    a.spin.assign(0, 0, 0);
}
enum class PropState : uint8_t { Init = 0, Run = 1, Free = 2 };

constexpr uint8_t kShipEffectType = 0x19; // pool 0xa0 stage type 0x19
constexpr uint8_t kSmokeSubtype = 1, kFlareSubtype = 2;
constexpr int16_t kSmokeHeight = -0x1c0;
constexpr int32_t kBobHeight = 0x200;
// the ship floats: y = sin(frame) * 0x200 above its base (the two stores of the exe)
void shipBob(StaticActor& a, ShipFields& ship) {
    const int32_t v = int32_t(sin64(gameState().frame & 0x3fu)) * kBobHeight;
    a.pos.y = v;
    a.pos.y = int32_t(int32_t(ship.baseY) * 0x10000 + v);
}
// exe 0x5914f0: a puff of smoke 0x1c0 above the ship
void shipSmoke(StaticActor& a) {
    const uint32_t slot = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (!slot) return;
    EffectActor& e = effectActorAt(slot);
    e.flags = kActorLive;
    e.type = kShipEffectType;
    e.subtype = kSmokeSubtype;
    e.loopFlags |= kActorLateLoop;
    const uint32_t r = rngStep();
    e.params[0] = uint8_t(r);
    e.params[1] = uint8_t(r >> 8);
    e.params[2] = uint8_t(((r >> 16) & 0xf) + 0x10);
    e.pos.x = a.pos.x.whole;
    e.pos.y = int16_t(a.pos.y.whole + kSmokeHeight);
    e.pos.z = a.pos.z.whole;
}
// exe 0x591570: the flare following the ship's position (its parameters: the position's address), kept in the block
void shipFlare(StaticActor& a, ShipFields& ship) {
    const uint32_t slot = allocActorA0(kActorStageClass);
    if (!slot) return;
    EffectActor& e = effectActorAt(slot);
    e.flags = kActorLive;
    e.type = kShipEffectType;
    e.loopFlags |= kActorLateLoop;
    e.subtype = kFlareSubtype;
    e.setParamsWord(slotFieldWord(a, kSlotPositionOffset));
    e.pos.x = 0;
    e.pos.y = kSmokeHeight;
    e.pos.z = 0;
    ship.flare = &e;
}
// exe 0x591350: the ship's routine for the script's step (the step tick read unsigned)
void shipRun(StaticActor& a) {
    ShipFields& ship = classState<ShipFields>(a);
    const RoutineRef routine = cueRoutine(stateAddress(TableId::St3cPropScriptStepTables), a);
    const uint32_t tick = uint32_t(scriptState().part<CastTimelineTick>().stepTick);
    const bool evenFrame = (gameState().frameWord() & 1) == 0;
    switch (routine.id()) {
    case RoutineId::St1cShipCue1: // turns slowly
        a.rotation.y = int16_t(a.rotation.y + 10);
        shipBob(a, ship);
        return;
    case RoutineId::St1cShipCue2: // turns, slowing from 0x78
        if (tick == 0) a.spin.y = 0x90;
        if (tick >= 0x78) {
            a.spin.y = int16_t(a.spin.y - 3);
            if (a.spin.y < 0) a.spin.y = 0;
        }
        a.rotation.y = int16_t(a.rotation.y + (a.spin.y >> 4));
        shipBob(a, ship);
        return;
    case RoutineId::St1cShipCue3: // the flare at 0x78; from 0x3c it turns the other way, faster, smoking
        if (tick == 0) a.spin.y = 0;
        else if (tick == 0x78) shipFlare(a, ship);
        if (tick >= 0x3c) {
            a.spin.y = int16_t(a.spin.y - 0x40);
            if (a.spin.y <= -0xf01) a.spin.y = -0xf00;
            if (evenFrame) shipSmoke(a);
        }
        a.rotation.y = int16_t(a.rotation.y + (a.spin.y >> 4));
        shipBob(a, ship);
        return;
    case RoutineId::St1cShipCue4: // turning fast, low, smoking every tick
        if (tick == 0) a.spin.y = -0xf00;
        a.rotation.y = int16_t(a.rotation.y + (a.spin.y >> 4));
        a.pos.y = int32_t(uint32_t(int32_t(sin64(gameState().frameWord() & 0x3f))) << 9);
        shipSmoke(a);
        return;
    default: notPortedAt(kStoryPort, "static 0x591270 script entry", scriptState().part<CastTimelineStep>().stepSub);
    }
}
void ship(StaticActor& a) {
    propHead(a, propLinks().ship);
    switch (PropState(a.state)) { // [0x8cde00]
    case PropState::Init: {
        if (!bindActorModel(a, 0)) {
            freeActor(a);
            return;
        }
        const int16_t y = a.pos.y.whole;
        propInit(a);
        ShipFields& fields = classState<ShipFields>(a);
        fields.baseY = y;
        shipRun(a);
        a.enterState(uint8_t(PropState::Run));
        return;
    }
    case PropState::Run: shipRun(a); return;
    case PropState::Free: freeActor(a); return;
    default: notPortedAt(kStoryPort, "static 0x591270 state", a.state);
    }
}
// the still models' routines (exe 0x5b26b0 / 0x5b5e50): only the empty one
void stillModelRun(StaticActor& a, uint32_t cues, const char* what) {
    if (!cueRoutine(cues, a).is(kNoCue)) notPortedAt(kStoryPort, what, scriptState().part<CastTimelineStep>().stepSub);
}
void stillModel(StaticActor& a) {
    propHead(a, propLinks().stillModel);
    switch (PropState(a.state)) { // [0x8d0778]
    case PropState::Init:
        if (!bindActorModel(a, 0)) {
            freeActor(a);
            return;
        }
        propInit(a);
        stillModelRun(a, stateAddress(TableId::St1cCastStillModelCues), "static 0x5b25d0 script entry");
        a.enterState(uint8_t(PropState::Run));
        return;
    case PropState::Run: stillModelRun(a, stateAddress(TableId::St1cCastStillModelCues), "static 0x5b25d0 script entry"); return;
    case PropState::Free: freeActor(a); return;
    default: notPortedAt(kStoryPort, "static 0x5b25d0 state", a.state);
    }
}
void secondStillModel(StaticActor& a) {
    propHead(a, propLinks().secondStillModel);
    switch (PropState(a.state)) { // [0x8d09ac]
    case PropState::Init:
        if (!bindActorModel(a, 0)) {
            freeActor(a);
            return;
        }
        propInit(a);
        a.enterState(uint8_t(PropState::Run));
        stillModelRun(a, stateAddress(TableId::St1cCastSecondStillModelCues), "static 0x5b5d70 script entry");
        return;
    case PropState::Run: stillModelRun(a, stateAddress(TableId::St1cCastSecondStillModelCues), "static 0x5b5d70 script entry"); return;
    case PropState::Free: freeActor(a); return;
    default: notPortedAt(kStoryPort, "static 0x5b5d70 state", a.state);
    }
}

} // namespace

void st1cFaceFigureUpdate(const PlayerEnv&, uint32_t actor) { faceFigure(animatedActorAt(actor)); }
void st1cMeshFlipFigureUpdate(const PlayerEnv&, uint32_t actor) { meshFlipFigure(animatedActorAt(actor)); }
void st1cCuedFigureUpdate(const PlayerEnv&, uint32_t actor) { cuedFigure(animatedActorAt(actor)); }
void st1cWalkingFigureUpdate(const PlayerEnv&, uint32_t actor) { walkingFigure(animatedActorAt(actor)); }
void st1cPosingFigureUpdate(const PlayerEnv&, uint32_t actor) { posingFigure(animatedActorAt(actor)); }
void st1cRisingPropUpdate(const PlayerEnv&, uint32_t actor) { risingProp(animatedActorAt(actor)); }
void st1cFidgetingFigureUpdate(const PlayerEnv&, uint32_t actor) { fidgetingFigure(animatedActorAt(actor)); }
void st1cHoppingFlyerUpdate(const PlayerEnv& env, uint32_t actor) { hoppingFlyer(env, animatedActorAt(actor)); }
void st1cVanishingWalkerUpdate(const PlayerEnv&, uint32_t actor) { vanishingWalker(animatedActorAt(actor)); }
void st1cFlyerUpdate(const PlayerEnv&, uint32_t actor) { flyer(animatedActorAt(actor)); }
void st1cGroundedHopperUpdate(const PlayerEnv& env, uint32_t actor) { groundedHopper(env, animatedActorAt(actor)); }
void st1cFallingFigureUpdate(const PlayerEnv& env, uint32_t actor) { fallingFigure(env, animatedActorAt(actor)); }
void st1cGlowingFigureUpdate(const PlayerEnv& env, uint32_t actor) { glowingFigure(env, animatedActorAt(actor)); }
void st1cHopperUpdate(const PlayerEnv&, uint32_t actor) { hopper(animatedActorAt(actor)); }
void st1cIdleFigureUpdate(const PlayerEnv&, uint32_t actor) { idleFigure(animatedActorAt(actor)); }
void st1cShipUpdate(const PlayerEnv&, uint32_t actor) { ship(staticActorAt(actor)); }
void st1cStillModelUpdate(const PlayerEnv&, uint32_t actor) { stillModel(staticActorAt(actor)); }
void st1cSecondStillModelUpdate(const PlayerEnv&, uint32_t actor) { secondStillModel(staticActorAt(actor)); }

} // namespace od::port
