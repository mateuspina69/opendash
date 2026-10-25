// ST54's puff emitter (static, exe 0x5c2040). Its sprites go through the draw-command buffer (game/draw_commands
// dc::emit).
//
// 18 puffs {x, y, z, life} in the slot from +0x10, their live bits, their brightness. Its row [0x8d0e88 + variant * 4]:
// flags (bit 7: born at the owner's bone 0 + (0, 0xa0, 0), else at the owner's position; bit 0: the owner kept), a byte
// it keeps, the spawn interval (random r..2r-1 ticks), the size factor. Variants 0 / 2: short grey puffs (life 0x10);
// the others smoke that darkens by age (0x40, 0x80 when started long). Script step 1 / 1 of a cast whose first member's
// step is 1 starts the fade (puffs dim 2 every 4th frame). Each live puff = one semi-transparent billboard (random
// rotation). Phases [0x8d0ea4]: 0 -> 1, 1 spawn 0x5c23f0, 2 freed once every puff is gone.
#include "content/late_game/st54_puffs.hpp"
#include "content/late_game/st54_puffs_state.hpp"

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/draw_commands.hpp"
#include "game/game_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st54_puffs_state;

namespace {

enum class EmitterState : uint8_t { Init = 0, Run = 1, Start = 2, Spawn = 3, Drain = 4 };
enum class EmitterPhase : uint8_t { Start = 0, Spawn = 1, Drain = 2 };
constexpr uint8_t kRowAtBone = 0x80, kRowKeepsOwner = 0x01;
constexpr int8_t kPuffBrightness = 0x60;

// the emitter's row (exe data 0x8d0e88, by variant)
struct PuffRow : DataRecord {
    static constexpr uint32_t kBytes = 0x4;
    uint8_t flags = 0; // +0x0: kRowAtBone | kRowKeepsOwner
    uint8_t kept = 0; // +0x1
    uint8_t interval = 0; // +0x2
    uint8_t sizeFactor = 0; // +0x3
    template <class R> static PuffRow load(const R& r) {
        PuffRow v;
        v.flags = r.template read<uint8_t>(0x0);
        v.kept = r.template read<uint8_t>(0x1);
        v.interval = r.template read<uint8_t>(0x2);
        v.sizeFactor = r.template read<uint8_t>(0x3);
        return v;
    }
};
PuffRow puffRow(const StaticActor& actor) { return dataRecord<PuffRow>(stateAddress(TableId::St54PuffsPuffRows) + uint32_t(actor.variant) * 4, PuffRow::kBytes); }
// the smoke's grey by age / 8 (exe data 0x8d0eb4: 0x1b bytes, up to the pointer table at 0x8d0ed0; the long smoke's
// age 0x80 reads entry 0x10)
const DataTable<ScalarTable<uint8_t, 0x1b>> smokeShades{TableId::St54PuffsSmokeShades};

bool shortPuffs(uint8_t variant) { return variant == 0 || variant == 2; }
uint32_t grey(uint32_t g) { return (g << 8 | g) << 8 | g; }

// exe 0x5c23f0: a puff born every interval..2 * interval - 1 ticks in the first free slot
void spawnPuff(StaticActor& actor, PuffEmitterFields& emitter) {
    const PuffRow row = puffRow(actor);
    const uint32_t owner = emitter.owner;
    if (emitter.spawnDelay != 0) {
        emitter.spawnDelay = uint16_t(emitter.spawnDelay - 1u);
        return;
    }
    uint32_t k = 0;
    while (k < kPuffs && ((emitter.liveMask >> k) & 1)) ++k;
    if (k == kPuffs) return;
    Puff& puff = emitter.puffs[k];
    if (row.flags & kRowAtBone) {
        const int16_t off[3] = {0, 0xa0, 0};
        int16_t p[3];
        bonePointOffset(animatedActorAt(owner), 0, off, p); // exe 0x5e1970(owner, 0, off, out)
        puff.x = p[0];
        puff.y = p[1];
        puff.z = p[2];
    } else {
        const Actor& from = actorLayoutAt(owner); // the owner's position (the Actor layout at its address)
        puff.x = from.pos.x.whole;
        puff.y = from.pos.y.whole;
        puff.z = from.pos.z.whole;
    }
    puff.life = shortPuffs(actor.variant) ? 0x10 : ((emitter.fadeFlags & kLongSmoke) ? 0x80 : 0x40);
    emitter.brightness[k] = kPuffBrightness;
    emitter.liveMask = uint16_t(emitter.liveMask | (1u << k));
    const uint32_t rnd = rngStep();
    const int32_t r = int32_t(row.interval);
    emitter.spawnDelay = uint16_t(uint32_t(int32_t(uint16_t(rnd)) % r) + uint32_t(r));
}
void runPhase(StaticActor& actor, PuffEmitterFields& emitter, uint8_t phase) {
    switch (EmitterPhase(phase)) {
    case EmitterPhase::Start: actor.subState = uint8_t(EmitterPhase::Spawn); return; // 0x5c2720
    case EmitterPhase::Spawn: spawnPuff(actor, emitter); return;
    case EmitterPhase::Drain:                                                         // 0x592560
        if (emitter.liveMask == 0) freeActor(actor); // exe 0x5e1540
        return;
    default: notPortedAt(kStoryPort, "0x5c2040 sub-state (table 0x8d0ea4)", phase);
    }
}
// the fade's cue: script step 1 / 1 while the first cast member's step is 1
bool fadeCue() {
    ScriptState& script = scriptState();
    return script.stepSub == 1 && script.stepId == 1 && (*script.actors[0]).step == 1;
}
// exe 0x5c20f0: the puffs aged (or dropped), each drawn as a billboard; the phase
void runEmitter(StaticActor& actor, PuffEmitterFields& emitter) {
    const PuffRow row = puffRow(actor);
    uint8_t shade = 0; // kept from the last smoke puff
    for (uint32_t k = 0; k < kPuffs; ++k) {
        Puff& puff = emitter.puffs[k];
        const uint32_t mask = emitter.liveMask;
        if (!((mask >> k) & 1)) continue;
        const uint8_t v = actor.variant;
        const auto ageOrDrop = [&]() {
            if (puff.life > 0) puff.life = int16_t(puff.life - 1);
            else if (shortPuffs(v)) emitter.liveMask = uint16_t((mask & ~(1u << k)) & 0x3fff);
        };
        if (fadeCue()) {
            emitter.fadeFlags |= kFading;
        } else if (!(emitter.fadeFlags & kFading)) {
            ageOrDrop();
        } else if (emitter.brightness[k] < 0x28) {
            ageOrDrop();
        }
        int32_t n;
        uint32_t col;
        if (!shortPuffs(v)) {
            n = ((emitter.fadeFlags & kLongSmoke) ? 0x80 : 0x40) - puff.life;
            shade = uint8_t(n >> 3);
            uint8_t c = uint8_t(uint8_t((n % 8) * 2) + smokeShades[shade]);
            if (c > 0x60) c = 0x60;
            col = grey(c);
        } else {
            n = 0x10 - puff.life;
            col = 0x606060;
        }
        if ((emitter.fadeFlags & kFading) && emitter.fadedPuffs > k) {
            if (!(gameState().frame & 3)) emitter.brightness[k] -= 2;
            if (emitter.brightness[k] < 0) emitter.brightness[k] = 0;
            col = grey(uint32_t(int32_t(emitter.brightness[k])));
        }
        const uint32_t size = uint32_t((int32_t(uint32_t(row.sizeFactor) * uint32_t(n + 0x10)) << 2) >> 6) & 0xffff;
        const uint32_t rot = (rngStep() & 0xfff) << 16;
        dc::emit({0, dc::billboardColour(col), uint32_t(uint16_t(puff.x)) | uint32_t(int32_t(puff.y)) << 16,
                  0x7d800000u, uint32_t(int32_t(puff.z)), 0x3c003fu, rot | size, 0x3f00u, 0, 0x3f3fu},
                 actor.at, false);
    }
    if (emitter.fadeFlags & kFading) {
        ++emitter.fadedPuffs;
        if (emitter.fadedPuffs > kMaxFaded) emitter.fadedPuffs = kMaxFaded;
    }
    runPhase(actor, emitter, actor.subState);
}
// exe 0x5c2060
void init(StaticActor& actor, PuffEmitterFields& emitter) {
    const PuffRow row = puffRow(actor);
    actor.setDrawn(false);
    actor.runFlags |= kActorWarpKept;
    actor.prevPos.x = 0;
    actor.prevPos.y = 0;
    actor.prevPos.z = 0;
    emitter.liveMask = 0;
    emitter.spawnDelay = 0;
    emitter.keptWord = emitter.spawnWord;
    emitter.rowByte = row.kept;
    if (scriptState().stepId == 1) {
        emitter.fadeFlags = kLongSmoke;
        emitter.fadedPuffs = 1;
    } else {
        emitter.fadeFlags = 0;
    }
    if (!(row.flags & kRowKeepsOwner)) emitter.owner = 0;
    actor.state = uint8_t(EmitterState::Run);
}
// exe 0x5c2040 (states [0x8d0e9c])
void puffEmitter(StaticActor& actor) {
    PuffEmitterFields& emitter = classState<PuffEmitterFields>(actor);
    switch (EmitterState(actor.state)) {
    case EmitterState::Init: init(actor, emitter); return;
    case EmitterState::Run: runEmitter(actor, emitter); return;
    case EmitterState::Start: runPhase(actor, emitter, uint8_t(EmitterPhase::Start)); return;
    case EmitterState::Spawn: runPhase(actor, emitter, uint8_t(EmitterPhase::Spawn)); return;
    case EmitterState::Drain: runPhase(actor, emitter, uint8_t(EmitterPhase::Drain)); return;
    default: notPortedAt(kStoryPort, "0x5c2040 state (table 0x8d0e9c)", actor.state);
    }
}

} // namespace

void puffEmitterUpdate(const PlayerEnv&, uint32_t actor) { puffEmitter(staticActorAt(actor)); }

} // namespace od::port
