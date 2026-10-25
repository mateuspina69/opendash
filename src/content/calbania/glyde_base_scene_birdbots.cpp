// The Birdbots of the scripted scenes in Glyde's base (pool 0x20 type 0x2d sub 2 of ST30 / ST31; see
// glyde_base_scene_birdbots.hpp), transcribed from dash2.exe one function per exe function. The script cues them
// through their first parameter (a new value -> the next animation). The shadow's entry (exe 0x5f20f0) is render
// output, its ground probe is kept.
//
// exe 0x497590 (ST30): the model by variant [0x892d9c]; states [0x892d94]: 0 init 0x497640, 1 0x497700 -> the
// variant's routine [0x892da4]: 0 0x52ef00 (nothing), 1 0x497720 / 2 0x497800 (cued animations 0x46 / 0x47 / 0x48; a
// pair of parts [0x892dc0 + variant * 4] blinking on the script's step clock), 3 0x497870 (cued animations
// 0x31..0x35, a walk in), 4..6 0x497970 (while a script runs: shaken, sound 0x191, sinking with smoke statics type 3
// variant 6).
// exe 0x4971c0 (ST31, scripts 0x23 / 0x24): the model [0xa59e44 + variant]; states [0x892d6c]: 0 init 0x497270 (the
// animation [0x892d74 + variant], the parts by variant), 1 0x4973b0 -> the variant's routine [0x892d78]: cued
// animations 0x37..0x39 (0x4973d0), 0x3c / 0x3d and a part swap 0xc ticks later (0x497440), 0x3f..0x41 (0x4974d0),
// 0x43 / 0x44 (0x497540).
#include "content/calbania/glyde_base_scene_birdbots.hpp"
#include "content/calbania/glyde_base_scene_birdbots_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/shadows.hpp"
#include "game/stage_smoke.hpp"

namespace od::port {

using namespace glyde_base_scene_birdbots_state;

namespace {

constexpr const char* kSceneBirdbotPort = "glyde base scene birdbot port"; // the area of its port-gap errors


// the fields of exe 0x4971c0
struct CellBirdbotFields : SceneCue {
    explicit CellBirdbotFields(const AnimatedActor& actor) : CellBirdbotFields(const_cast<AnimatedActor&>(actor)) {}
    explicit CellBirdbotFields(AnimatedActor& actor) : SceneCue(actor), timer(actor.params[2]) {}
    uint8_t& timer;
};

enum class SceneState : uint8_t { Init = 0, Cued = 1 };

// a pair of parts that blink in turn (exe table 0x892dc0, by variant)
struct BlinkPair : DataRecord {
    uint16_t first = 0;
    uint16_t second = 0;
    static BlinkPair load(const TableReader& r) {
        BlinkPair rec;
        rec.first = r.u16(0);
        rec.second = r.u16(2);
        return rec;
    }
};
const DataTable<RecordTable<BlinkPair, 3, 4>> blinkPairs{TableId::GlydeBaseSceneBirdbotsBlinkPairs};
const DataTable<ScalarTable<uint8_t, 7>> sceneModels{TableId::GlydeBaseSceneBirdbotsModels}; // exe 0x497590's model by variant
const DataTable<ScalarTable<uint8_t, 4>> cellModels{TableId::GlydeBaseSceneBirdbotsCellModels}; // exe 0x4971c0's model by variant (the stage's table)
const DataTable<ScalarTable<uint8_t, 4>> cellFirstAnims{TableId::GlydeBaseSceneBirdbotsCellFirstAnims}; // ... and its first animation
constexpr uint8_t kSceneFirstAnim = 0x45;
constexpr uint16_t kSinkSound = 0x191;
constexpr uint8_t kSmokeVariant = 6;
// the parts shown / hidden at the init
constexpr size_t kBodyPart = 2;
constexpr size_t kExtraParts[3] = {6, 7, 8};

int32_t scriptClock() { return scriptState().stepTick; }

// exe 0x497af0: the pair's hide bits swapped, the phase flipped
void blinkStep(AnimatedActor& actor, SceneBirdbotFields& f) {
    const BlinkPair pair = blinkPairs[actor.variant];
    PartEntry& a = actor.parts[pair.first];
    PartEntry& b = actor.parts[pair.second];
    if (f.blink & 1) {
        a.flags &= uint8_t(~kPartHidden);
        b.flags |= kPartHidden;
    } else {
        a.flags |= kPartHidden;
        b.flags &= uint8_t(~kPartHidden);
    }
    f.blink ^= 1u;
}
// exe 0x497ab0: every 3rd tick of [lo, hi] on the script's clock
void blinkDuring(AnimatedActor& actor, SceneBirdbotFields& f, int32_t lo, int32_t hi) {
    const int32_t t = scriptClock();
    if (t < lo || t > hi) return;
    if ((t - lo) % 3 == 0) blinkStep(actor, f);
}
// exe 0x497b80: the blink ended (the first part hidden, the second shown)
void blinkOff(AnimatedActor& actor, SceneBirdbotFields& f) {
    const BlinkPair pair = blinkPairs[actor.variant];
    f.blink = 0;
    PartEntry& a = actor.parts[pair.first];
    PartEntry& b = actor.parts[pair.second];
    a.flags |= kPartHidden;
    b.flags &= uint8_t(~kPartHidden);
}
// the cue taken -> the animation and the next sub-state
void onCue(AnimatedActor& actor, SceneCue& c, uint8_t anim) {
    if (!c.take()) return;
    actor.anim = anim;
    ++actor.subState;
}

// ---- exe 0x497590's variants ----------------------------------------------------------------------------------------
// exe 0x497720: blinking on [0xf, 0x1e] and [0x28, 0x3c]; cued 0x46, 0x47
void sceneVariant1(AnimatedActor& actor, SceneBirdbotFields& f) {
    blinkDuring(actor, f, 0xf, 0x1e);
    blinkDuring(actor, f, 0x28, 0x3c);
    const int32_t t = scriptClock(); // a switch on t - 0xf over the byte table 0x4977cc
    if (t == 0xf || t == 0x28) f.blinkStarted = 1;
    else if (t == 0x1e || t == 0x3c) blinkOff(actor, f);
    const uint8_t s = actor.subState;
    if (s == 0) onCue(actor, f, 0x46);
    else if (s == 1) onCue(actor, f, 0x47);
}
// exe 0x497800: blinking on [0x41, 0x82]; cued 0x48
void sceneVariant2(AnimatedActor& actor, SceneBirdbotFields& f) {
    blinkDuring(actor, f, 0x41, 0x82);
    const int32_t t = scriptClock();
    if (t == 0x41) f.blinkStarted = 1;
    else if (t == 0x82) blinkOff(actor, f);
    if (actor.subState == 0 && f.take()) {
        actor.subState = 1;
        actor.anim = 0x48;
    }
}
// exe 0x497870: walks in (anim 0x31) to z 0xa8, then cued 0x32..0x35 (the last one at its place in the scene)
void sceneVariant3(AnimatedActor& actor, SceneBirdbotFields& f) {
    const uint8_t s = actor.subState;
    switch (s) {
    case 0:
        actor.anim = 0x31;
        ++actor.subState;
        f.animKey = 4;
        break;
    case 1:
        integrateVelocity(actor, 0, 0, -0x100);
        if (actor.pos.z.whole > 0xa8) {
            actor.pos.z.whole = 0xa8;
            actor.anim = 0x32;
            ++actor.subState;
        }
        break;
    case 2:
        if (f.take()) ++actor.subState;
        break;
    case 3: onCue(actor, f, 0x33); break;
    case 4: onCue(actor, f, 0x34); break;
    case 5:
        if (f.take()) {
            ++actor.subState;
            actor.pos.x.whole = int16_t(0xf5d0);
            actor.pos.y.whole = int16_t(0xfce0);
            actor.pos.z.whole = 0x66;
            actor.anim = 0x35;
        }
        break;
    default: break;
    }
}
// exe 0x497970: while the script runs, shaken (unless steady) and sinking, a smoke every second tick; freed after it
void sceneSinking(AnimatedActor& actor, SceneBirdbotFields& f) {
    if (!scriptState().inUse()) {
        freeActor(actor);
        return;
    }
    const uint8_t s = actor.subState;
    if (s == 0) {
        actor.subState = 1;
        actor.anim = 0x36;
        f.baseY = actor.pos.y.whole;
        actor.tint = kTintSpent;
        actor.stepCounter = 0x1e;
    } else if (s == 1 || s == 2) {
        if (s == 1) {
            --actor.stepCounter;
            if (actor.stepCounter == 0) {
                actor.subState = 2;
                soundPlayAtPos(kSinkSound, actor.pos);
            }
        }
        if (f.steady == 0) { // shaken while sinking
            const uint16_t ph = f.shakePhase;
            --f.baseY;
            f.shakePhase = uint16_t(ph + 0x80u);
            const int c = fixedCos(ph); // exe table 0x847ad8
            actor.pos.y.whole = int16_t(((c * 30) >> 12) + int(f.baseY));
            integrateVelocity(actor, 0, 0, -0xa0);
        }
    }
    ++f.smokeTicks;
    if (f.smokeTicks & 1) return;
    if (const uint32_t address = allocActor60(kActorStageClass)) { // exe 0x5e12f0: smoke (static stage type 3 variant 6)
        StaticActor& e = staticActorAt(address);
        e.flags = kActorLive;
        e.loopFlags |= kActorSweepGroup2;
        e.type = kSmokeType;
        e.variant = kSmokeVariant;
        e.pos.x.whole = actor.pos.x.whole;
        e.pos.y.whole = actor.pos.y.whole;
        e.pos.z.whole = actor.pos.z.whole;
        SmokeParams smoke(actorHeaderAt(address));
        smoke.driftX = 0;
        smoke.driftZ = 0;
        smoke.life = 4;
    }
}
// exe 0x497700: the variant's routine [0x892da4]
void sceneCued(AnimatedActor& actor, SceneBirdbotFields& f) {
    switch (actor.variant) {
    case 0: break; // exe 0x52ef00
    case 1: sceneVariant1(actor, f); break;
    case 2: sceneVariant2(actor, f); break;
    case 3: sceneVariant3(actor, f); break;
    case 4:
    case 5:
    case 6: sceneSinking(actor, f); break;
    default: notPortedAt(kSceneBirdbotPort, "class 0x497590 variant", actor.variant);
    }
}
// the parts of a Birdbot with its extras hidden (the body shown)
void hideExtras(AnimatedActor& actor) {
    actor.parts[kBodyPart].flags &= uint8_t(~kPartHidden);
    for (const size_t p : kExtraParts) actor.parts[p].flags |= kPartHidden;
}

// exe 0x497590's body
void sceneBirdbot(const PlayerEnv& env, AnimatedActor& actor) {
    SceneBirdbotFields& f = classState<SceneBirdbotFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (SceneState(actor.state)) { // [0x892d94]
    case SceneState::Init:                   // exe 0x497640
        if (!bindActorModel(actor, sceneModels[actor.variant])) { // exe 0x5e0bc0
            freeActor(actor);
            break;
        }
        actor.setDrawn(true);
        actor.anim = kSceneFirstAnim;
        actor.prevAnim = 0xff;
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        actor.collisionBox.pointAt(TableId::GlydeBaseSceneBirdbotsSceneBox);
        actor.interactionBox = nullptr;
        actor.interaction = 0;
        actor.airborne = 0;
        actor.onPlatform = 0;
        actor.clearClassBlock();
        hideExtras(actor);
        actor.enterState(uint8_t(SceneState::Cued));
        sceneCued(actor, f);
        break;
    case SceneState::Cued: sceneCued(actor, f); break;
    default: notPortedAt(kSceneBirdbotPort, "class 0x497590 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) {
        animStep(actor); // exe 0x5e1e80
        animPose(actor);
    } else {
        animSet(actor, actor.anim, int(f.animKey)); // exe 0x5e1c30
        animPose(actor);
        f.animKey = 0;
    }
    // exe 0x5f20f0(actor, 0x30, 0x100, 0) for variants < 3: the shadow's entry is render output; its ground probe kept
    if (actor.variant < 3 && (actor.flags & kActorDrawn)) shadowGroundProbe(env, actor);
}

// ---- exe 0x4971c0's variants ----------------------------------------------------------------------------------------
// exe 0x4973d0: cued 0x37, then 0x38 / 0x39 as each ends
void cellVariant0(AnimatedActor& actor, CellBirdbotFields& f) {
    const uint8_t s = actor.subState;
    switch (s) {
    case 0: onCue(actor, f, 0x37); return;
    case 1:
        if (actor.animEnded()) {
            actor.anim = 0x38;
            ++actor.subState;
            f.cueSeen = f.cue;
        }
        return;
    case 2:
        if (actor.animEnded()) {
            actor.anim = 0x39;
            ++actor.subState;
        }
        return;
    default: return;
    }
}
// exe 0x497440: cued 0x3c, then 0x3d as it ends, and 0xc ticks later the parts 6 / 8 swapped
void cellVariant1(AnimatedActor& actor, CellBirdbotFields& f) {
    const uint8_t s = actor.subState;
    switch (s) {
    case 0: onCue(actor, f, 0x3c); return;
    case 1:
        if (actor.animEnded()) {
            actor.anim = 0x3d;
            ++actor.subState;
            f.cueSeen = f.cue;
            f.timer = 0xc;
        }
        return;
    case 2:
        --f.timer;
        if (f.timer != 0) return;
        ++actor.subState;
        actor.parts[6].flags &= uint8_t(~kPartHidden);
        actor.parts[8].flags |= kPartHidden;
        return;
    default: return;
    }
}
// exe 0x4974d0: cued 0x3f, cued 0x40, then 0x41 as it ends
void cellVariant2(AnimatedActor& actor, CellBirdbotFields& f) {
    const uint8_t s = actor.subState;
    switch (s) {
    case 0:
    case 1: onCue(actor, f, s == 0 ? 0x3f : 0x40); return;
    case 2:
        if (actor.animEnded()) {
            actor.anim = 0x41;
            ++actor.subState;
        }
        return;
    default: return;
    }
}
// exe 0x497540: cued 0x43, 0xc ticks later 0x44
void cellVariant3(AnimatedActor& actor, CellBirdbotFields& f) {
    const uint8_t s = actor.subState;
    if (s == 0) {
        if (f.take()) {
            actor.anim = 0x43;
            ++actor.subState;
            f.timer = 0xc;
        }
    } else if (s == 1) {
        --f.timer;
        if (f.timer != 0) return;
        actor.anim = 0x44;
        ++actor.subState;
    }
}
// exe 0x4973b0: the variant's routine [0x892d78]
void cellCued(AnimatedActor& actor, CellBirdbotFields& f) {
    switch (actor.variant) {
    case 0: cellVariant0(actor, f); return;
    case 1: cellVariant1(actor, f); return;
    case 2: cellVariant2(actor, f); return;
    case 3: cellVariant3(actor, f); return;
    default: notPortedAt(kSceneBirdbotPort, "class 0x4971c0 variant", actor.variant);
    }
}

// exe 0x4971c0's body
void cellBirdbot(const PlayerEnv& env, AnimatedActor& actor) {
    CellBirdbotFields f(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (SceneState(actor.state)) { // [0x892d6c]
    case SceneState::Init: {                 // exe 0x497270
        if (!bindActorModel(actor, cellModels[actor.variant])) { // exe 0x5e0bc0
            freeActor(actor);
            break;
        }
        actor.setDrawn(true);
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        actor.interactionBox = nullptr;
        actor.interaction = 0;
        actor.airborne = 0;
        actor.onPlatform = 0;
        actor.anim = cellFirstAnims[actor.variant];
        actor.prevAnim = 0xff;
        actor.collisionBox.pointAt(TableId::GlydeBaseSceneBirdbotsCellBox);
        actor.clearClassBlock();
        const uint8_t v = actor.variant; // the parts by variant (jump table 0x49739c)
        if (v == 1) {
            actor.parts[kBodyPart].flags &= uint8_t(~kPartHidden);
            actor.parts[6].flags |= kPartHidden;
            actor.parts[7].flags |= kPartHidden;
            actor.parts[8].flags &= uint8_t(~kPartHidden);
        } else if (v <= 3) {
            hideExtras(actor);
        }
        actor.enterState(uint8_t(SceneState::Cued));
        cellCued(actor, f);
        break;
    }
    case SceneState::Cued: cellCued(actor, f); break;
    default: notPortedAt(kSceneBirdbotPort, "class 0x4971c0 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) {
        animStep(actor); // exe 0x5e1e80
        animPose(actor);
    } else {
        animSet(actor, actor.anim, 0); // exe 0x5e1c30
        animPose(actor);
    }
    // exe 0x5f20f0(actor, 0x30, 0x100, 0): the shadow's entry is render output; its ground probe kept
    if (actor.flags & kActorDrawn) shadowGroundProbe(env, actor);
}

} // namespace

void glydeBaseSceneBirdbotUpdate(const PlayerEnv& env, uint32_t actor) { sceneBirdbot(env, animatedActorAt(actor)); }
void cellSceneBirdbotUpdate(const PlayerEnv& env, uint32_t actor) { cellBirdbot(env, animatedActorAt(actor)); }

} // namespace od::port
