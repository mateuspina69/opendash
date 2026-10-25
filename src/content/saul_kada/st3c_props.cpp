// The placed models of ST3C (static type 1, exe 0x591160; hypothesis names). States [0x8cdde8]: 0 bind 0x5911a0, 1 by
// variant [0x8cddf4]: 0..2 nothing, 3 the re-bind 0x5912d0 (its height handed to the published class block of exe
// 0xa5a900), 4 the script step 0x591350, 5 freed. The script step reads [[0x8cde24 + variant * 4] + the script's cue *
// 4], a table only variants 0 / 1 have (variants 3 / 4 would read code as a table): a port gap if ever reached.
#include "content/saul_kada/st3c_props.hpp"
#include "content/saul_kada/st3c_props_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/game_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"

using namespace od::port::st3c_props_state;

namespace od::port {

namespace {

enum class PropState : uint8_t { Bind = 0, ByVariant = 1 };
enum class PropVariant : uint8_t { Plain0 = 0, Turned = 1, Plain2 = 2, Rebind = 3, ScriptStep = 4, Free = 5 };
constexpr uint8_t kTurnedFromStory = 0xd;
constexpr int16_t kTurnedRoll = 0xc00;
// by variant (exe table 0x8cde24: a pointer per variant to its step routines by the cue)
struct ScriptStepTables : DataRecord {
    static constexpr size_t kVariants = 8, kMaxCues = 0x80;
    static constexpr uint32_t kBytes = kVariants * 4;
    std::array<TableReader, kVariants> tables;
    // the routine of the variant's cue (past the tables: none; the caller's port gap names the cue's routine)
    RoutineRef routine(size_t variant, int32_t cue) const {
        if (variant >= kVariants || cue < 0 || size_t(cue) * 4 + 4 > tables[variant].size()) return RoutineRef();
        return tables[variant].routine(size_t(cue) * 4);
    }
    static ScriptStepTables load(const TableReader& r) {
        ScriptStepTables t;
        for (size_t v = 0; v < kVariants; ++v) t.tables[v] = r.follow(v * 4, kMaxCues * 4);
        return t;
    }
};
const DataTable<ScriptStepTables> scriptStepTables{TableId::St3cPropScriptStepTables};
// The class block a cast publishes in the exe global 0xa5a900 (content/forbidden_island/st1c_cast's ship: a static's
// block, +0xbc of its slot): the height this prop hands it (the block's +4, the static's hit box's previous x)
int16_t& publishedHeight(uint32_t block) { return staticActorAt(block - kStaticClassBlockOffset).hitBox.previousX; }
// The prop's spin's spare lane, cleared by the re-bind
struct PropFields {
    explicit PropFields(const StaticActor& actor) : PropFields(const_cast<StaticActor&>(actor)) {}
    explicit PropFields(StaticActor& actor) : spinSpare(actor.spin.spare) {}
    int16_t& spinSpare;
};

// exe 0x591350: the script step's routine (a table only variants 0 / 1 have): left visible if ever reached
void scriptStep(const StaticActor& actor) {
    const int8_t cue = int8_t(scriptState().part<CastTimelineStep>().stepSub);
    notPortedAt(kStoryPort, "0x591350 script step handler", scriptStepTables->routine(actor.variant, cue).pointerValue());
}

// exe 0x5911a0
void bind(StaticActor& actor) {
    if (!bindActorModel(actor, actor.variant)) {
        freeActor(actor);
        return;
    }
    const uint16_t hp = hasStaticStats(actor.type) ? uint16_t(staticStats(actor.type, 0).hitPoints) : uint16_t(0);
    actor.velocity.setXY(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.wallHit = 0;
    actor.actorHit = 0;
    actor.lastMover = nullptr;
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.collisionBox.pointAt(TableId::St3cPropsPropBox);
    actor.clearClassBlock();
    actor.enterState(uint8_t(PropState::ByVariant));
    if (PropVariant(actor.variant) == PropVariant::Turned && gameState().story >= kTurnedFromStory)
        actor.rotation.z = kTurnedRoll;
    actor.wasOnPlatform = actor.onPlatform;
}
// exe 0x5912d0
void rebind(StaticActor& actor) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    actor.flags |= kActorDrawn;
    actor.clearClassBlock();
    actor.velocity.z = 0;
    actor.velocity.y = 0;
    actor.velocity.x = 0;
    actor.acceleration.z = 0;
    actor.acceleration.y = 0;
    actor.acceleration.x = 0;
    actor.spin.y = 0;
    actor.spin.x = 0;
    PropFields(actor).spinSpare = 0;
    actor.spin.z = 0;
    publishedHeight(publishedLink().block) = actor.pos.y.whole;
    scriptStep(actor);
    actor.enterState(uint8_t(PropState::ByVariant));
}

void st3cProp(StaticActor& actor) {
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.wasAirborne = actor.airborne;
    switch (PropState(actor.state)) {
    case PropState::Bind: bind(actor); return;
    case PropState::ByVariant: // 0x591250
        switch (PropVariant(actor.variant)) {
        case PropVariant::Plain0:
        case PropVariant::Turned:
        case PropVariant::Plain2: return;
        case PropVariant::Rebind: rebind(actor); return;
        case PropVariant::ScriptStep: scriptStep(actor); return;
        case PropVariant::Free: freeActor(actor); return; // 0x5afdd0
        default: notPortedAt(kStoryPort, "0x591160 variant", actor.variant);
        }
    default: notPortedAt(kStoryPort, "0x591160 state", actor.state);
    }
}

} // namespace

void st3cPropUpdate(const PlayerEnv&, uint32_t actor) { st3cProp(staticActorAt(actor)); }

} // namespace od::port
