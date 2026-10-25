// The pirates of the pirate base's cutscenes (ST3C sub 2, story 12: pool 0x20 type 0xc, exe 0x466f00; hypothesis
// names). Variant v: its first animation [0x88ff94 + v]; variants 3..5 wear the colour page 6 and puff smoke now and
// then (exe 0x467300). Each variant runs the cue chain of the script from its own entry (0..2: from the step sub-id 1
// -> 4 -> the face pages 8 / 9 / 10 at the step ticks 1 / 4 / 7 -> the two timed scenes 0x467120 / 0x467290; 3: from
// 0x467120; 4: from 0x467290; 5: nothing).
// States [0x88ff8c]: 0 bind 0x466fa0, 1 the cue chain by variant [0x88ff9c] 0x467040.
#include "content/saul_kada/st3c_pirate_cast.hpp"
#include "content/saul_kada/st3c_pirate_cast_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/cutscene_actor.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/spark_emitter.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st3c_pirate_cast_state;

namespace {

enum class PirateState : uint8_t { Bind = 0, Cues = 1 };
// the cue chain (the exe's routines in chain order)
enum class PirateCue : uint8_t {
    WaitFirst = 0,  // exe 0x4f4900: the step sub-id 1
    WaitFourth = 1, // exe 0x467080: the step sub-id 4
    FacePages = 2,  // exe 0x4670a0
    SceneA = 3,     // exe 0x467120
    SceneB = 4,     // exe 0x467290
    Idle = 5,       // exe 0x52ef00
    Count = 6,
};
const DataTable<ScalarTable<uint8_t, 8>> firstAnims{TableId::St3cPirateCastFirstAnims}; // by variant
constexpr uint8_t kSmokingFirst = 3, kSmokingLast = 5;
constexpr uint32_t kSmokingColourCell = 6;
constexpr uint8_t kSmokeVariant = 7;
constexpr uint32_t kSmokePattern = 0x15a200; // the RNG's low bits that puff (bit 31 - (r & 31))

void nextCue(AnimatedActor& actor, uint8_t cue) {
    actor.subState = cue;
    actor.restartStep();
}
// the scenes' gate: latched in the step once the script has started (its first byte)
bool sceneArmed(AnimatedActor& actor) {
    if (actor.step == 0) {
        if (scriptState().part<ScriptCastStart>().started == 0) return false;
        actor.step = 1;
    }
    return true;
}
void runCue(AnimatedActor& actor, PirateCue cue) {
    const uint32_t tick = uint32_t(scriptState().stepTick);
    switch (cue) {
    case PirateCue::WaitFirst: // exe 0x4f4900
        if (scriptState().stepSub == 1) nextCue(actor, 1);
        return;
    case PirateCue::WaitFourth: // exe 0x467080
        if (scriptState().stepSub == 4) nextCue(actor, 2);
        return;
    case PirateCue::FacePages: // exe 0x4670a0
        if (tick == 1) {
            actor.anim = 1;
            setTexturePageCell(actor.texturePage, 8);
        } else if (tick == 4) {
            setTexturePageCell(actor.texturePage, 9);
        } else if (tick == 7) {
            setTexturePageCell(actor.texturePage, 10);
        }
        return;
    case PirateCue::SceneA: { // exe 0x467120
        if (!sceneArmed(actor)) return;
        PirateFields& pirate = classState<PirateFields>(actor);
        if (tick == 0x19f) {
            actor.anim = 3;
            pirate.sceneFlag = 1;
            pirate.sceneByte = 0;
            pirate.sceneWord = 0;
        } else if (tick == 0x1e0 || tick == 0x208) {
            actor.prevAnim = kNoAnimShown; // the animation restarts
        } else if (tick == 0x255) {
            pirate.sceneFlag = 0;
        }
        return;
    }
    case PirateCue::SceneB: // exe 0x467290
        if (!sceneArmed(actor)) return;
        if (tick == 0x26c) actor.anim = 5;
        else if (tick == 0x2bc) actor.prevAnim = kNoAnimShown;
        return;
    case PirateCue::Idle: return; // exe 0x52ef00
    default: notPortedAt(kStoryPort, "0x466f00 cue", uint32_t(cue));
    }
}
// exe 0x467300: every 16 ticks, at random, a smoke puff (the stage's static type 3 variant 7) at bone 1, 0x20 up
void puffSmoke(const AnimatedActor& actor) {
    if (gameState().frame & 0xf) return;
    const uint32_t r = rngStep();
    if (int32_t(kSmokePattern << (r & 0x1f)) >= 0) return;
    const uint32_t slot = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!slot) return;
    StaticActor& smoke = staticActorAt(slot);
    SparkEmitterParams& params = classState<SparkEmitterParams>(staticActorAt(slot));
    smoke.loopFlags |= kActorSweepGroup2;
    smoke.flags = kActorLive;
    smoke.type = kSparkEmitterStageType;
    smoke.variant = kSmokeVariant;
    const ShortVec4& point = bonePoint(actor, 1, kStoryPort); // exe 0x5e1810 (none: the port gap)
    smoke.pos.x = int32_t(uint32_t(int32_t(point.x)) << 16);
    smoke.pos.y.whole = int16_t(uint16_t(point.y) - 0x20u);
    smoke.pos.z = int32_t(uint32_t(int32_t(point.z)) << 16);
    params.driftX = 0;
    params.driftZ = 0;
    params.lifetime = 1;
}

// exe 0x466fa0
void bind(AnimatedActor& actor) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    const uint8_t variant = actor.variant;
    actor.velocity.clearWords();
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.anim = firstAnims[variant];
    actor.prevAnim = kNoAnimShown;
    actor.maxHitPoints = 1;
    actor.hitPoints = 1;
    actor.collisionBox.pointAt(TableId::St3cPirateCastPirateBox);
    actor.clearClassBlock();
    if (variant >= kSmokingFirst && variant <= kSmokingLast) setTexturePageCell(actor.texturePage, kSmokingColourCell);
    actor.enterState(uint8_t(PirateState::Cues));
}
// exe 0x467040: the cue chain from the variant's entry [0x88ff9c]
void cues(AnimatedActor& actor) {
    const uint8_t variant = actor.variant;
    uint32_t first;
    if (variant <= 2) first = uint32_t(PirateCue::WaitFirst); // 0x467060 [0x88ffb4]
    else if (variant <= kSmokingLast) first = variant;         // 0x4670f0 / 0x467260 / 0x4672d0
    else {
        notPortedAt(kStoryPort, "0x467040 variant (table 0x88ff9c, direct cue call)", variant);
        return;
    }
    const uint32_t i = first + actor.subState;
    if (i >= uint32_t(PirateCue::Count)) {
        notPortedAt(kStoryPort, "0x466f00 sub-state", actor.subState);
        return;
    }
    runCue(actor, PirateCue(i));
    if (variant >= kSmokingFirst) puffSmoke(actor);
}

// exe 0x466f00
void pirateCast(AnimatedActor& actor) {
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (PirateState(actor.state)) {
    case PirateState::Bind: bind(actor); break;
    case PirateState::Cues: cues(actor); break;
    default: notPortedAt(kStoryPort, "0x466f00 state (table 0x88ff8c)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    animateAsked(actor, 0); // exe 0x5e1e80 / 0x5e1c30
}

} // namespace

void pirateCastUpdate(const PlayerEnv&, uint32_t actor) { pirateCast(animatedActorAt(actor)); }

} // namespace od::port
