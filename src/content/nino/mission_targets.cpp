// The mission targets (ST15 / ST16 static type 0x2d, exe 0x5ab8b0; hypothesis names) and the lava plume 0x5abc30 that
// runs their smoke states, one routine per exe routine.
//
// A target's row (0x8cf3b8 + variant * 0xc): its model variant, the damaged one, whether it has a model, the hit kinds
// that hurt it (<< 16), its capsules (0x8cf348, 16 bytes: two points + a radius), the flag set when destroyed (with
// 0x6f4), the flag at a quarter of its hit points (and the "damaged" message, window 4). States [0x8cf3e8]: 0 init
// 0x5ab9a0, 1 0x5aba60, 2 0x5abc50 (the smoke columns' start), 3 0x5abca0 (8 smoke columns, draw commands only), 4
// freed 0x5afdd0.
#include "content/nino/mission_targets.hpp"
#include "content/nino/mission_targets_state.hpp"

#include "content/saul_kada/lava_vents.hpp"

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/audio_logic.hpp"
#include "game/draw_commands.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/hud.hpp"
#include "game/message.hpp"
#include "game/message_state.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace mission_targets_state;

namespace {

// A target's row (exe data, 0xc bytes)
struct TargetRow : DataRecord {
    static constexpr uint32_t kBytes = 0xc;
    uint8_t model = 0; // +0x0
    uint8_t damagedModel = 0; // +0x1
    uint8_t hasModel = 0; // +0x2
    uint8_t hurtKinds = 0; // +0x3: the attack bits (>> 16) that hurt it (0: no hit entry)
    uint8_t firstCapsule = 0; // +0x4
    uint8_t capsules = 0; // +0x5
    uint16_t destroyedFlag = 0; // +0x6
    uint16_t damagedFlag = 0; // +0x8
    int16_t damagedMessage = 0; // +0xa
    template <class R> static TargetRow load(const R& r) {
        TargetRow v;
        v.model = r.template read<uint8_t>(0x0);
        v.damagedModel = r.template read<uint8_t>(0x1);
        v.hasModel = r.template read<uint8_t>(0x2);
        v.hurtKinds = r.template read<uint8_t>(0x3);
        v.firstCapsule = r.template read<uint8_t>(0x4);
        v.capsules = r.template read<uint8_t>(0x5);
        v.destroyedFlag = r.template read<uint16_t>(0x6);
        v.damagedFlag = r.template read<uint16_t>(0x8);
        v.damagedMessage = r.template read<int16_t>(0xa);
        return v;
    }
};
TargetRow targetRow(const StaticActor& actor) { return dataRecord<TargetRow>(stateAddress(TableId::MissionTargetsTargetRows) + uint32_t(actor.variant) * 0xc, TargetRow::kBytes); }

// A hit capsule (exe data, 0x10 bytes): its two ends and its radius
struct TargetCapsule : DataRecord {
    static constexpr uint32_t kBytes = 0xe;
    int16_t x0 = 0; // +0x0
    int16_t y0 = 0; // +0x2
    int16_t z0 = 0; // +0x4
    int16_t radius = 0; // +0x6
    int16_t x1 = 0; // +0x8
    int16_t y1 = 0; // +0xa
    int16_t z1 = 0; // +0xc
    template <class R> static TargetCapsule load(const R& r) {
        TargetCapsule v;
        v.x0 = r.template read<int16_t>(0x0);
        v.y0 = r.template read<int16_t>(0x2);
        v.z0 = r.template read<int16_t>(0x4);
        v.radius = r.template read<int16_t>(0x6);
        v.x1 = r.template read<int16_t>(0x8);
        v.y1 = r.template read<int16_t>(0xa);
        v.z1 = r.template read<int16_t>(0xc);
        return v;
    }
};

// The target's own bytes: the hurt flash's ticks (the velocity's spare lane), its hit box
struct TargetFields {
    explicit TargetFields(const StaticActor& actor) : TargetFields(const_cast<StaticActor&>(actor)) {}
    explicit TargetFields(StaticActor& actor) : hurtTicks(actor.velocity.spare), hitBox(actor.hitBox) {}
    int16_t& hurtTicks;
    HitBox& hitBox; // with its contact output (the hit entry asks for the contact point)
};
const DataTable<ScalarTable<uint8_t, 8>> smokeCells{TableId::MissionTargetsSmokeCells}; // the puff sprite's texture u by its age

// [0x8cf3e8]
enum class TargetState : uint8_t { Init = 0, Main = 1, SmokeStart = 2, Smoke = 3, Gone = 4 };
// the sub-state of Main
enum class TargetSub : uint8_t { Whole = 0, Damaged = 1 };
// the lava plume's states ([0x8cf3f0])
enum class PlumeState : uint8_t { Start = 0, Smoke = 1, Gone = 2 };

constexpr uint32_t kFlagTargetLost = 0x6f4;
constexpr uint16_t kHurtSound = 399;
constexpr int16_t kHurtTicks = 0xf;
constexpr int8_t kTargetWindow = 4;
constexpr uint8_t kPuffLife = 0x40;
constexpr int8_t kPuffRise = 0x18, kPuffGrey = 0x1f;
constexpr int16_t kPuffRestart = -0x800;

// exe 0x5ab9a0: the row's model (when it has one), drawn; the hit points of its stats row * 4, no motion, no box; the
// target gauge
void targetInit(StaticActor& actor) {
    const TargetRow row = targetRow(actor);
    if (row.hasModel != 0) {
        if (!bindActorModel(actor, row.model)) {
            freeActor(actor);
            return;
        }
        actor.flags |= kActorDrawn;
    }
    actor.enterState(uint8_t(TargetState::Main));
    const int16_t hp = hasStaticStats(actor.type)
                           ? int16_t(staticStats(actor.type, actor.variant).hitPoints << 2)
                           : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.velocity.clearWords();
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.collisionBox = nullptr;
    actor.interactionBox = nullptr;
    actor.interaction = kInteractLift;
    actor.clearClassBlock();
    hudAddTargetGauge(actor, 0, 2);
}

// exe 0x5aba60: the hits (a flash of 0xf ticks between them): destroyed -> its flags; a quarter left -> the damaged
// model, its flag and the message (the player alive)
void targetMain(StaticActor& actor, TargetFields& target) {
    const uint32_t h = target.hitBox.result, n = h & kHitDamageMask;
    const TargetRow row = targetRow(actor);
    if (target.hurtTicks != 0) {
        target.hurtTicks = int16_t(target.hurtTicks - 1);
        return;
    }
    if (n == 0 || !(h & (uint32_t(row.hurtKinds) << 16))) return;
    actor.tint = kTintFlash;
    target.hurtTicks = kHurtTicks;
    soundPlayFlatForced(kHurtSound); // exe 0x5d5260
    if (hitPointsAdd(actor, -int(n)) != 0) {
        setStoryFlag(row.destroyedFlag);
        setStoryFlag(kFlagTargetLost);
        return;
    }
    if (TargetSub(actor.subState) != TargetSub::Whole || actor.hitPoints > (actor.maxHitPoints >> 2)) return;
    if (row.hasModel != 0 && !bindActorModel(actor, row.damagedModel)) {
        freeActor(actor);
        return;
    }
    actor.subState = uint8_t(TargetSub::Damaged);
    setStoryFlag(row.damagedFlag);
    windowClose(kTargetWindow, WindowClose::Force); // exe 0x5576b0
    if (player().hitPoints >= 0) windowOpen(kTargetWindow, kMessageBank, row.damagedMessage); // exe 0x557390
}

// exe 0x5abc50: the 8 smoke columns started (staggered by 4 ticks); state 1
void smokeStart(StaticActor& actor) {
    auto&& smoke = classState<SmokeColumnsFields>(actor);
    actor.state = 1;
    smoke.unusedC8 = 0;
    for (uint32_t i = 0; i < 8; ++i) {
        smoke.puffY[i] = actor.pos.y.whole;
        smoke.puffTick[i] = int16_t(int8_t(i) * -4);
        smoke.puffLife[i] = kPuffLife;
        smoke.puffRise[i] = kPuffRise;
        smoke.puffGrey[i] = kPuffGrey;
    }
}

// exe 0x5abca0: 8 rising puffs (billboard draw commands), restarting; the last one's end -> state 2
void smokeColumns(StaticActor& actor) {
    auto&& smoke = classState<SmokeColumnsFields>(actor);
    for (uint32_t i = 0; i < 8; ++i) {
        const int16_t t = smoke.puffTick[i];
        if (t < 0) {
            smoke.puffTick[i] = int16_t(t + 1);
            continue;
        }
        smoke.puffLife[i] -= 1u;
        if (t % 5 == 0) smoke.puffGrey[i] -= 1u;
        const int8_t sp = smoke.puffRise[i];
        if (sp > 4 && t % 2 == 0) smoke.puffRise[i] = int8_t(sp - 1);
        const uint32_t g = uint32_t(int32_t(smoke.puffGrey[i]));
        const int k = 0x40 - int8_t(smoke.puffLife[i]);
        const int8_t c7 = int8_t(k >> 3);
        const uint32_t u = smokeCells[size_t(c7)];
        const uint32_t v = c7 < 4 ? 0 : 0x30;
        const uint32_t v1 = (v + 0x2f) * 0x100;
        const uint32_t w6 = uint32_t((u == 0 && v == 0) ? k * 0x20 : k * 8) + 0x30u;
        dc::emit({0, ((g << 8 | g) << 8 | g) | 0x2e000000u,
                  uint32_t(uint16_t(actor.pos.x.whole)) | uint32_t(uint16_t(smoke.puffY[i])) << 16,
                  (v | 0x7c9100u) << 8 | u, uint32_t(int32_t(actor.pos.z.whole)), (v | 0x2e00u) << 8 | (u + 0x2f),
                  w6 & 0xffff, v1 | u, 0, v1 | (u + 0x2f)},
                 actor.at, false);
        smoke.puffY[i] = int16_t(smoke.puffY[i] - smoke.puffRise[i]);
        if (uint8_t(t) == 0x3c) {
            if (i == 7) actor.state = 2;
            else smoke.puffTick[i] = kPuffRestart;
        } else {
            smoke.puffTick[i] = int16_t(t + 1);
        }
    }
}

// exe 0x5ab8b0
void target(StaticActor& actor) {
    auto&& target = TargetFields(actor);
    actor.tint = kTintNeutral;
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (TargetState(actor.state)) {
    case TargetState::Init: targetInit(actor); break;
    case TargetState::Main: targetMain(actor, target); break;
    case TargetState::SmokeStart: smokeStart(actor); break;
    case TargetState::Smoke: smokeColumns(actor); break;
    case TargetState::Gone: freeActor(actor); break; // exe 0x5afdd0
    default: notPortedAt(kStoryPort, "static 0x5ab8b0 state", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    const TargetRow row = targetRow(actor);
    uint32_t cap = stateAddress(TableId::MissionTargetsTargetCapsules) + uint32_t(row.firstCapsule) * 0x10;
    target.hitBox.result = 0;
    target.hitBox.previousX = actor.prevPos.x.whole;
    target.hitBox.previousZ = actor.prevPos.z.whole;
    if (row.hurtKinds == 0) return;
    for (uint32_t k = row.capsules; k != 0; --k, cap += 0x10) { // exe 0x5e4190
        const TargetCapsule c = dataRecord<TargetCapsule>(cap, TargetCapsule::kBytes);
        const int16_t p0[3] = {c.x0, c.y0, c.z0};
        const int16_t p1[3] = {c.x1, c.y1, c.z1};
        const uint16_t r = uint16_t(c.radius >> 2);
        hitListSubmitCapsule(p0, r, p1, r, target.hitBox,
                             hitOwner(actor.slot) | hitGroup(kHitGroupPlayer) | kHitContactPoint | hitCapsule(1), 0);
    }
}

} // namespace

void missionTargetUpdate(const PlayerEnv&, uint32_t actor) { target(staticActorAt(actor)); }

// exe 0x5abc30 (pool 0x60 type 0x2e, the lava plume of content/saul_kada/lava_vents): the target's smoke start (-> state
// 1) / its 8 rising puffs (the last -> state 2) / freed (exe 0x5afdd0)
void lavaPlumeUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    switch (PlumeState(actor.state)) {
    case PlumeState::Start: smokeStart(actor); return;
    case PlumeState::Smoke: smokeColumns(actor); return;
    case PlumeState::Gone: freeActor(actor); return;
    default: notPortedAt(kStoryPort, "0x5abc30 state", actor.state);
    }
}

} // namespace od::port
