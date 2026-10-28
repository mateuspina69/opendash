// The bars the player hangs from on ST16 / ST17 (static type 1 of their stage tables) and the ST17 gate its lever
// opens, one routine per exe routine; the names are hypotheses from the observed behaviour.
//   0x58f040  ST16 static type 1, by the variant: 0 / 1 the halves of a gate that swing open once the script's step tick
//             reaches 0xf; 2 a bar that comes down when hung from (its pool-0xe0 timer started; back up when the timer
//             ends); 3 the mission lever (it rises once flag 0x682 is set -- the spawner 0x41dbe0 at 0xe10 ticks; hung
//             from, it comes down: sound 0x116 + flag 0x27, ST16's room case -> script 0x34)
//   0x58f820  ST17 static type 1, variant 1: the lever of the gate 0x5abe60 (spawned by it with the gate's slot): hung
//             from (the first time a pool-0xe0 type-0x1b message trigger, message 0x21) it comes down and opens the
//             gate; it rises back once the gate is idle
//   0x5abe60  ST17 static type 0x2f (record 0x8c747c): the gate -- enemy ids 0xa..0xc marked defeated; it waits for its
//             lever, slides (+-8 a tick toward its target height) and bounces; at story 6 the second opening sends a
//             wave of two statics (type 1 variant 2) before closing. A stand-on solid.
// A bar's hanger: 1 the player, 2 a Birdbot (pool 0x20 type 0x3d, ST16) hanging from it. Each bar carries a light
// (content/nino/stage_light) its states recolour.
#include "content/nino/bars_and_gate.hpp"
#include "content/nino/bars_and_gate_state.hpp"
#include "content/nino/stage_light.hpp"

#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/explosions.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/encounter.hpp"
#include "game/field_alias.hpp"
#include "game/game_state.hpp"
#include "game/player/player_hang.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace bars_and_gate_state;

namespace {

// ---- the bars' shared parts ----------------------------------------------------------------------------------------
// The bar's spawn parameters: its other actor's slot (variant 2: its timer; the ST17 lever: its gate), its hanger, the
// Birdbot hanging from it
struct BarParams {
    explicit BarParams(const StaticActor& actor) : BarParams(const_cast<StaticActor&>(actor)) {}
    explicit BarParams(StaticActor& actor) : linked(actor.params[0]), hanger(actor.params[1]), birdbot(actor.params[2]) {}
    uint8_t& linked;
    FieldAlias<int8_t, uint8_t> hanger; // BarHanger
    FieldAlias<int8_t, uint8_t> birdbot; // its slot
};
enum BarHanger : int8_t { kNoHanger = 0, kPlayerHangs = 1, kBirdbotHangs = 2 };
// the bar's own interaction (the player's bar grab picks it)
constexpr uint8_t kInteractBarGrab = 0x08;
constexpr uint8_t kBirdbotType = 0x3d;
constexpr uint16_t kBarSound = 0x116;
constexpr int16_t kBarTop = -0x968, kBarTopWord = int16_t(0xf698), kBarBottom = -0x938, kBarBottomWord = int16_t(0xf6c8);
constexpr uint8_t kBarLightSize = 0x20;

// the static's row hit points (0 without a row)
int16_t rowHitPoints(const StaticActor& actor) {
    return hasStaticStats(actor.type) ? int16_t(staticStats(actor.type, 0).hitPoints) : int16_t(0);
}
// the class block's bytes +0xbc..+0xcb cleared (exe 0x40dc40(+0xbc, +0xc8): the dwords up to +0xc8 inclusive)
void clearBarBlock(BarFields& fields) {
    fields.clearBlockBytes();
}
// exe 0x58f7e0(slot, setting): the bar's light recoloured (a live stage light only)
void barLight(uint8_t slot, uint32_t setting) {
    if (slot == 0) return;
    EffectActor& light = effectActorAt(effectPoolSlot(slot));
    if (!(light.flags & kActorLive) || light.type != kStageLightType) return;
    const StageLightSetting s = dataRecord<StageLightSetting>(setting, StageLightSetting::kBytes);
    StageLightFields fields(light);
    fields.mode = s.mode;
    fields.colourA = s.colourA;
    fields.colourB = s.colourB;
}
// a light (pool 0xa0 stage type 6) on the bar, its slot kept, at the setting
void spawnBarLight(BarFields& fields, uint32_t setting) {
    const uint32_t e = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (e == 0) return;
    EffectActor& light = effectActorAt(e);
    StageLightFields params(effectActorAt(e));
    light.flags = kActorLive;
    light.type = kStageLightType;
    light.variant = 0;
    params.size = kBarLightSize;
    params.pulse = 0;
    fields.light = light.slot;
    barLight(light.slot, setting);
}
// exe 0x58f4e0: up by the speed (-0xc a tick); at the top 1
int barRise(StaticActor& bar) {
    bar.velocity.y = int16_t(bar.velocity.y - 0xc);
    bar.pos.y = int32_t(bar.pos.y + bar.velocity.y * 0x1000);
    if (bar.pos.y.whole < kBarTop) {
        bar.pos.y.whole = kBarTopWord;
        return 1;
    }
    return 0;
}
// exe 0x58f3e0: down by the speed (+0xc a tick); at the bottom 1
int barLower(StaticActor& bar) {
    bar.velocity.y = int16_t(bar.velocity.y + 0xc);
    bar.pos.y = int32_t(bar.pos.y + bar.velocity.y * 0x1000);
    if (bar.pos.y.whole > kBarBottom) {
        bar.pos.y.whole = kBarBottomWord;
        return 1;
    }
    return 0;
}
// exe 0x58f560(bar, block): the player hanging placed under it (its height kept); a Birdbot hanging moved with it.
// 1 while held; the player gone: no hanger
int barHold(StaticActor& bar, BarFields& fields) {
    BarParams params(bar);
    if (params.hanger == kPlayerHangs) {
        if (playerBarGrab(bar) == 2) {
            playerPlaceAt(bar, 0, 0, 0, 0x58);
            fields.hangY = player().pos.y.whole;
            return 1;
        }
        params.hanger = kNoHanger;
    } else if (params.hanger == kBirdbotHangs) {
        AnimatedActor& birdbot = animatedActorAt(animatedPoolSlot(params.birdbot));
        if ((birdbot.flags & kActorLive) && birdbot.type == kBirdbotType) {
            int16_t dx = 0, dz = 0;
            angleVector(int(uint16_t(bar.rotation.y)), 0x2c, dx, dz); // exe 0x5e40c0
            birdbot.pos.x.whole = int16_t(uint16_t(bar.pos.x.whole) + uint16_t(dx));
            const int16_t y = int16_t(uint16_t(bar.pos.y.whole) + 0xdc);
            birdbot.pos.y.whole = y;
            birdbot.pos.z.whole = int16_t(uint16_t(bar.pos.z.whole) + uint16_t(dz));
            birdbot.rotation.y = int16_t(uint16_t(bar.rotation.y) + 0x800);
            fields.hangY = y;
            return 1;
        }
    }
    return 0;
}

// ---- 0x58f040: the ST16 bars (states: 0 init, 1 by the variant) -----------------------------------------------------
enum class BarState : uint8_t { Init = 0, Run = 1 };
enum class St16BarVariant : uint8_t { GateLeft = 0, GateRight = 1, Timed = 2, Lever = 3 };
// the timed bar (exe 0x58f2b0 by the sub-state [0x8cdbfc])
enum class TimedBarPhase : uint8_t { Up = 0, Lower = 1, Down = 2 };
// the lever (exe 0x58f640 by the sub-state [0x8cdc08])
enum class LeverPhase : uint8_t { Rise = 0, Wait = 1, Pull = 2 };
// The timed bar's timer (a pool-0xe0 controller): started by the bar, its end
struct BarTimerFields {
    explicit BarTimerFields(const ControllerActor& actor) : BarTimerFields(const_cast<ControllerActor&>(actor)) {}
    explicit BarTimerFields(ControllerActor& actor) : started(actor.params[2]), ended(actor.params[3]) {}
    uint8_t& started;
    uint8_t& ended;
};
const DataTable<ScalarTable<uint8_t, 4>> st16BarModels{TableId::BarsAndGateSt16BarModels};
const DataTable<CollisionBoxRecord> st16BarBox{TableId::BarsAndGateSt16BarBox};
const DataTable<InteractionBoxRecord> st16BarGrabBox{TableId::BarsAndGateSt16BarGrabBox};
constexpr uint32_t kFlagBarHung = 0x683, kFlagLeverUp = 0x682, kFlagLeverPulled = 0x27;
constexpr uint8_t kPullCountdownType = 0x18, kPullCountdownVariant = 0x2c;
constexpr uint16_t kPullCountdownTicks = 300;
constexpr int16_t kGateTurnLimit = 8;

// exe 0x58f510: not hung from -> the grab (the player: flag 0x683, 1); the player 1, a Birdbot 2; else 0
int barGrab(StaticActor& bar) {
    BarParams params(bar);
    const int8_t hanger = params.hanger;
    if (hanger == kNoHanger) {
        if (playerBarGrab(bar) == 1) {
            params.hanger = kPlayerHangs;
            setStoryFlag(kFlagBarHung);
            return 1;
        }
    } else if (hanger == kPlayerHangs) {
        return 1;
    } else if (hanger == kBirdbotHangs) {
        return 2;
    }
    return 0;
}
int barGrabOrHold(StaticActor& bar, BarFields& fields) {
    return BarParams(bar).hanger == kNoHanger ? barGrab(bar) : barHold(bar, fields);
}
// exe 0x58f090: the model of the variant, the row's hit points, no motion; the bars (variants 2 / 3): the grab box,
// down at -0x938 (the timed bar starts lowered), their light
void st16BarInit(StaticActor& bar, BarFields& fields) {
    if (!bindActorModel(bar, st16BarModels[bar.variant])) {
        freeActor(bar);
        return;
    }
    const int16_t hp = rowHitPoints(bar);
    bar.maxHitPoints = hp;
    bar.hitPoints = hp;
    bar.velocity.clearBytes();
    bar.rotation.z = 0;
    bar.rotation.x = 0;
    bar.spin.y = 0;
    bar.airborne = 0;
    bar.onPlatform = 0;
    bar.collisionBox.pointAt(st16BarBox.stateAddress());
    bar.clearInteractionBytes();
    clearBarBlock(fields); // exe 0x40dc40(+0xbc, +0xc8)
    const St16BarVariant variant = St16BarVariant(bar.variant);
    bar.enterStateBytes(uint8_t(BarState::Run));
    bar.clearStepBytes();
    if (variant != St16BarVariant::Timed && variant != St16BarVariant::Lever) return;
    bar.collisionBox.pointAt(st16BarBox.stateAddress());
    bar.interactionBox.pointAt(st16BarGrabBox.stateAddress());
    bar.interaction = kInteractBarGrab;
    BarParams(bar).hanger = kNoHanger;
    bar.pos.y.whole = kBarBottomWord;
    if (variant == St16BarVariant::Timed) {
        bar.subState = uint8_t(TimedBarPhase::Down);
        bar.clearStepBytes();
    }
    spawnBarLight(fields, variant == St16BarVariant::Timed ? stateAddress(TableId::BarsAndGateTimedBarDown) : stateAddress(TableId::BarsAndGateLeverIdle));
}
// exe 0x58f210 / 0x58f260 (variants 0 / 1): once the step tick reaches 0xf the half swings (its turn speed -1 / +1 a
// tick, to 8) to yaw 0 / 0x800
void gateHalf(StaticActor& bar, bool left) {
    if (bar.subState == 0) {
        if (scriptState().stepTick < 0xf) return;
        bar.subState = 1;
    } else if (bar.subState != 1) {
        return;
    }
    if (left) {
        bar.spin.y = int16_t(bar.spin.y - 1);
        if (bar.spin.y < -kGateTurnLimit) bar.spin.y = -kGateTurnLimit;
        bar.rotation.y = int16_t(bar.rotation.y + bar.spin.y);
        if (bar.rotation.y < 0) bar.rotation.y = 0;
    } else {
        bar.spin.y = int16_t(bar.spin.y + 1);
        if (bar.spin.y > kGateTurnLimit) bar.spin.y = kGateTurnLimit;
        bar.rotation.y = int16_t(bar.rotation.y + bar.spin.y);
        if (bar.rotation.y > 0x800) bar.rotation.y = 0x800;
    }
}
// exe 0x58f2b0 (variant 2): hung from it comes down (its timer started), and rises back when the timer ends
void timedBar(StaticActor& bar, BarFields& fields) {
    BarParams params(bar);
    if (params.linked == 0) return;
    BarTimerFields timer(controllerActorAt(controllerPoolSlot(params.linked)));
    switch (TimedBarPhase(bar.subState)) {
    case TimedBarPhase::Up: // exe 0x58f2d0
        if (bar.step == 0) {
            bar.step = 1;
            params.hanger = kNoHanger;
            barLight(fields.light, stateAddress(TableId::BarsAndGateTimedBarReady));
        }
        if (barGrab(bar) != 0) {
            bar.subState = uint8_t(TimedBarPhase::Lower);
            bar.restartStep();
        }
        return;
    case TimedBarPhase::Lower: // exe 0x58f320
        if (bar.step == 0) {
            bar.velocity.y = 0;
            fields.hangY = player().pos.y.whole;
            bar.enterStep(1);
        }
        if (bar.step == 1 && barLower(bar) != 0) {
            timer.started = 1;
            if (params.hanger == kBirdbotHangs) params.hanger = kNoHanger;
            barLight(fields.light, stateAddress(TableId::BarsAndGateTimedBarDown));
            soundPlayAtPos(kBarSound, bar.pos);
            bar.subState = uint8_t(TimedBarPhase::Down);
            bar.restartStep();
        }
        barGrabOrHold(bar, fields);
        return;
    case TimedBarPhase::Down: // exe 0x58f410
        if (bar.step == 0) {
            if (timer.ended != 0) {
                barLight(fields.light, stateAddress(TableId::BarsAndGateTimedBarReady));
                bar.velocity.y = 0;
                bar.enterStep(1);
            }
            barGrabOrHold(bar, fields);
        } else if (bar.step == 1) {
            const int up = barRise(bar);
            const int held = barGrabOrHold(bar, fields);
            if (up != 0) {
                bar.subState = uint8_t(held != 0 ? TimedBarPhase::Lower : TimedBarPhase::Up);
                bar.restartStep();
            }
        }
        return;
    default: notPortedAt(kStoryPort, "ST16 bar variant 2 sub-state", bar.subState);
    }
}
// exe 0x58f640 (variant 3): waits for flag 0x682 and rises; hung from it comes down -> sound 0x116 + flag 0x27
void missionLever(StaticActor& bar, BarFields& fields) {
    switch (LeverPhase(bar.subState)) {
    case LeverPhase::Rise: { // exe 0x58f660
        if (bar.step == 0) {
            if (!storyFlag(kFlagLeverUp)) {
                barGrabOrHold(bar, fields);
                return;
            }
            barLight(fields.light, stateAddress(TableId::BarsAndGateLeverReady));
            bar.velocity.y = 0;
            bar.enterStep(1);
        }
        const int up = barRise(bar);
        const int held = barGrabOrHold(bar, fields);
        if (up != 0) {
            bar.subState = uint8_t(held != 0 ? LeverPhase::Pull : LeverPhase::Wait);
            bar.restartStep();
        }
        return;
    }
    case LeverPhase::Wait: // exe 0x58f720
        if (barGrab(bar) != 0) {
            bar.subState = uint8_t(LeverPhase::Pull);
            bar.restartStep();
        }
        return;
    case LeverPhase::Pull: // exe 0x58f740
        if (bar.step == 0) {
            bar.velocity.y = 0;
            if (const uint32_t e = allocActorE0()) { // exe 0x5e1440: a countdown controller
                ControllerActor& countdown = controllerActorAt(e);
                countdown.flags = kActorLive;
                countdown.type = kPullCountdownType;
                countdown.loopFlags |= kActorSweepGroup2 | kActorLateLoop;
                countdown.variant = kPullCountdownVariant;
                countdown.setParamsLowWord(kPullCountdownTicks);
            }
            bar.enterStep(1);
        } else if (bar.step != 1) {
            barGrabOrHold(bar, fields);
            return;
        }
        if (barLower(bar) != 0 && !storyFlag(kFlagLeverPulled)) {
            soundPlayAtPos(kBarSound, bar.pos);
            setStoryFlag(kFlagLeverPulled);
            return;
        }
        barGrabOrHold(bar, fields);
        return;
    default: notPortedAt(kStoryPort, "ST16 lever sub-state", bar.subState);
    }
}
// exe 0x58f040
void st16Bar(StaticActor& bar) {
    BarFields& fields = classState<BarFields>(bar);
    bar.savePreviousPose();
    bar.wasAirborne = bar.airborne;
    switch (BarState(bar.state)) {
    case BarState::Init: st16BarInit(bar, fields); break;
    case BarState::Run:
        switch (St16BarVariant(bar.variant)) {
        case St16BarVariant::GateLeft: gateHalf(bar, true); break;
        case St16BarVariant::GateRight: gateHalf(bar, false); break;
        case St16BarVariant::Timed: timedBar(bar, fields); break;
        case St16BarVariant::Lever: missionLever(bar, fields); break;
        default: notPortedAt(kStoryPort, "static 0x58f040 variant", bar.variant);
        }
        break;
    default: notPortedAt(kStoryPort, "static 0x58f040 state", bar.state);
    }
    if (bar.flags & kActorLive) bar.wasOnPlatform = bar.onPlatform;
}

// ---- 0x58f820: the ST17 statics of type 1 (states [0x8cdc50]: 0 init 0x58f860, 1 by the variant [0x8cdc5c]: 0 / 3
// none, 1 the lever, 2 not ported). The lever by the sub-state [0x8cdc6c].
enum class St17BarVariant : uint8_t { Still = 0, Lever = 1, Liftable = 2, Still3 = 3 };
enum class GateLeverPhase : uint8_t { Wait = 0, Lower = 1, Down = 2 };
// The gate's spawn parameters (read by its lever): busy (sliding / enemies left), the lever pulled
struct GateParams {
    explicit GateParams(const StaticActor& actor) : GateParams(const_cast<StaticActor&>(actor)) {}
    explicit GateParams(StaticActor& actor) : busy(actor.params[0]), leverPulled(actor.params[1]) {}
    uint8_t& busy;
    uint8_t& leverPulled;
};
// The message trigger the lever spawns the first time (pool 0xe0 type 0x1b): its message, a parameter and its flag
struct MessageTriggerParams {
    explicit MessageTriggerParams(const ControllerActor& actor) : MessageTriggerParams(const_cast<ControllerActor&>(actor)) {}
    explicit MessageTriggerParams(ControllerActor& actor)
        : message(actor.params, 0), param(actor.params[2]), flag(actor.recordPoint.spare) {}
    BytesField<uint16_t, 4> message;
    uint8_t& param;
    int16_t& flag;
};
const DataTable<CollisionBoxRecord> st17BarBox{TableId::BarsAndGateSt17BarBox};
const DataTable<InteractionBoxRecord> st17LeverGrabBox{TableId::BarsAndGateSt17LeverGrabBox};
const DataTable<InteractionBoxRecord> st17LiftBox{TableId::BarsAndGateSt17LiftBox};
constexpr uint8_t kMessageTriggerType = 0x1b;
constexpr uint16_t kLeverMessage = 0x21;

// exe 0x58fc00: the grab of the ST17 lever (no story flag)
int st17LeverGrab(StaticActor& bar) {
    BarParams params(bar);
    switch (params.hanger) {
    case kNoHanger:
        if (playerBarGrab(bar) == 1) {
            params.hanger = kPlayerHangs;
            return 1;
        }
        return 0;
    case kPlayerHangs: return 1;
    case kBirdbotHangs: return 2;
    default: return 0;
    }
}
int st17LeverGrabOrHold(StaticActor& bar, BarFields& fields) {
    return BarParams(bar).hanger == kNoHanger ? st17LeverGrab(bar) : barHold(bar, fields);
}
// exe 0x58f860: the model of the variant, the row's hit points, no motion; variant 2 liftable, variant 1 the lever's
// grab box and light
void st17BarInit(StaticActor& bar, BarFields& fields) {
    if (!bindActorModel(bar, bar.variant)) { // exe 0x5e0bc0(a, variant)
        freeActor(bar);                      // exe 0x5e1540
        return;
    }
    auto setHitPoints = [&] {
        const int16_t hp = rowHitPoints(bar);
        bar.maxHitPoints = hp;
        bar.hitPoints = hp;
    };
    setHitPoints();
    bar.velocity.setXY(0, 0);
    bar.rotation.z = 0;
    bar.rotation.x = 0;
    bar.airborne = 0;
    bar.onPlatform = 0;
    bar.collisionBox.pointAt(st17BarBox.stateAddress());
    bar.interactionBox = nullptr;
    bar.interaction = 0;
    bar.wallHit = 0;
    bar.actorHit = 0;
    bar.lastMover = nullptr;
    clearBarBlock(fields); // exe 0x40dc40(+0xbc, +0xc8)
    bar.enterState(uint8_t(BarState::Run));
    switch (St17BarVariant(bar.variant)) {
    case St17BarVariant::Liftable:
        setHitPoints();
        bar.flags |= kActorDrawn;
        bar.collisionBox.pointAt(st17BarBox.stateAddress());
        bar.interactionBox.pointAt(st17LiftBox.stateAddress());
        bar.interaction = kInteractLift;
        break;
    case St17BarVariant::Lever:
        bar.flags |= kActorDrawn;
        bar.collisionBox.pointAt(st17BarBox.stateAddress());
        bar.interactionBox.pointAt(st17LeverGrabBox.stateAddress());
        bar.interaction = kInteractBarGrab;
        BarParams(bar).hanger = kNoHanger;
        spawnBarLight(fields, stateAddress(TableId::BarsAndGateGateLeverReady));
        break;
    default: break;
    }
    bar.wasOnPlatform = bar.onPlatform;
}
// exe 0x58f9c0 (variant 1): hung from (the first time the message trigger) it comes down and opens the gate; it rises
// back once the gate is idle
void gateLever(StaticActor& bar, BarFields& fields) {
    BarParams params(bar);
    if (params.linked == 0) return;
    GateParams gate(staticActorAt(staticPoolSlot(params.linked)));
    switch (GateLeverPhase(bar.subState)) {
    case GateLeverPhase::Wait: // exe 0x58f9f0
        if (bar.step == 0) {
            bar.step = 1;
            params.hanger = kNoHanger;
            barLight(fields.light, stateAddress(TableId::BarsAndGateGateLeverReady));
        }
        if (st17LeverGrab(bar) != 0) {
            if (fields.triggerSpawned == 0) {
                if (const uint32_t e = allocActorE0()) { // exe 0x5e1440
                    ControllerActor& trigger = controllerActorAt(e);
                    MessageTriggerParams message(controllerActorAt(e));
                    trigger.flags = kActorLive;
                    trigger.type = kMessageTriggerType;
                    trigger.variant = 0;
                    message.message = kLeverMessage;
                    message.param = 6;
                    message.flag = -1;
                    fields.triggerSpawned = 1;
                }
            }
            bar.subState = uint8_t(GateLeverPhase::Lower);
            bar.restartStep();
        }
        break;
    case GateLeverPhase::Lower: // exe 0x58fa70
        if (bar.step == 0) {
            bar.velocity.y = 0;
            fields.hangY = player().pos.y.whole;
            bar.enterStep(1);
        }
        if (bar.step == 1 && barLower(bar) != 0) {
            gate.leverPulled = 1;
            if (params.hanger == kBirdbotHangs) params.hanger = kNoHanger;
            barLight(fields.light, stateAddress(TableId::BarsAndGateGateLeverDown));
            soundPlayAtPos(kBarSound, bar.pos);
            bar.subState = uint8_t(GateLeverPhase::Down);
            bar.restartStep();
        }
        st17LeverGrabOrHold(bar, fields);
        break;
    case GateLeverPhase::Down: // exe 0x58fb30
        if (bar.step == 0) {
            if (gate.busy == 0) {
                barLight(fields.light, stateAddress(TableId::BarsAndGateGateLeverReady));
                bar.velocity.y = 0;
                bar.enterStep(1);
            }
            st17LeverGrabOrHold(bar, fields);
        } else if (bar.step == 1) {
            const int up = barRise(bar);
            const int held = st17LeverGrabOrHold(bar, fields);
            if (up != 0) {
                bar.subState = uint8_t(held != 0 ? GateLeverPhase::Lower : GateLeverPhase::Wait);
                bar.restartStep();
            }
        }
        break;
    default: notPortedAt(kStoryPort, "ST17 lever sub-state", bar.subState);
    }
    if (bar.flags & kActorLive) bar.wasOnPlatform = bar.onPlatform;
}
// ---- 0x58fc40: the ST17 liftable static (variant 2; the gate's wave): sub-states [0x8cdc78] ----------------------------
enum class LiftableSub : uint8_t {
    Slide = 0,     // exe 0x58fdc0: spawned on the gate, it slides off forward
    Rest = 1,      // exe 0x58fe70: at rest, riding its gate until lifted
    InHands = 2,   // exe 0x58fed0: at the player's hands
    Flying = 3,    // exe 0x590aa0: thrown / dropped, until it hits something
    Destroyed = 4, // exe 0x590b00: a blast, freed
};
// BarFields::entry
constexpr uint8_t kLiftableNoEntry = 0x01; // in the hands: no hit entry
constexpr uint8_t kLiftableThrown = 0x02;  // flying thrown: the player's attack, a mover
constexpr uint8_t kLiftableFalling = 0x04; // sliding off the gate: its touch a thrown object's
constexpr int16_t kSlideSpeed = -0x180, kSlideBrake = 0x40;
constexpr int16_t kLiftableGravity = 0x40, kLiftableFallMax = 0x800;
constexpr int kLiftableHandsRaise = -0x10;
constexpr int16_t kLiftableThrowUp = -0x100, kLiftableThrowForward = -0x300;
constexpr int16_t kLiftableDropUp = -0x200, kLiftableDropForward = -0x100;
constexpr int32_t kLiftableLostY = 0x15800001; // fallen this far (16.16): freed
constexpr int16_t kLiftableHitHeight = 0x5a;   // its hit entry this far above its position
constexpr uint32_t kLiftableHitRadius = 0x60;
constexpr uint16_t kLiftableHitSound = 0x8d, kLiftableStrongHitSound = 0x8e;
constexpr uint16_t kLiftableBlastSize = 0x100;
constexpr uint32_t kLiftableBreaks = kHitAttackSolid | kHitAttackStrong; // a flying one bursts on these

// exe 0x58fe40: lifted by the player -> in the hands, no hit entry, off its platform, no longer riding its gate
void liftableLiftCheck(StaticActor& bar, BarFields& fields) {
    if (!playerLiftTargetCheck(bar, 0)) return; // exe 0x573a60
    bar.subState = uint8_t(LiftableSub::InHands);
    fields.entry |= kLiftableNoEntry;
    bar.onPlatform = 0;
    fields.lifted = 1;
}
void liftableSlide(StaticActor& bar, BarFields& fields) { // exe 0x58fdc0
    if (bar.step == 0) {
        fields.entry = kLiftableFalling;
        bar.velocity.z = kSlideSpeed;
        bar.enterStep(1);
    }
    if (bar.step <= 1) {
        BarParams p(bar);
        p.linked -= 1u; // its timer
        if (p.linked == 0) bar.enterStep(2);
    } else if (bar.step == 2) {
        bar.velocity.z = int16_t(bar.velocity.z + kSlideBrake);
        if (bar.velocity.z >= 0) {
            bar.velocity.z = 0;
            bar.subState = uint8_t(LiftableSub::Rest);
            bar.restartStep();
        }
    }
    integrateVelocity(bar, 0, 0, bar.velocity.z); // exe 0x5e3690
    liftableLiftCheck(bar, fields);
}
void liftableRest(StaticActor& bar, BarFields& fields) { // exe 0x58fe70
    if (bar.step == 0) {
        fields.entry = 0;
        bar.enterStep(1);
    }
    if (fields.lifted == 0) { // on its gate (the slot of its second parameter): its height
        const StaticActor& gate = staticActorAt(actorBySlot(bar.params[1]));
        if (gate.flags & kActorLive) bar.pos.y = gate.pos.y;
    }
    liftableLiftCheck(bar, fields);
}
void liftableFlying(const PlayerEnv& env, StaticActor& bar, BarFields& fields);
void liftableInHands(const PlayerEnv& env, StaticActor& bar, BarFields& fields) { // exe 0x58fed0
    if (bar.step == 0) {
        fields.entry = kLiftableNoEntry;
        bar.enterStep(1);
    }
    carryAtHands(bar, 0, kLiftableHandsRaise, 0); // exe 0x573ab0
    const CarryRelease r = carryReleased(); // exe 0x573950
    if (r == CarryRelease::Held) return;
    if (r == CarryRelease::Thrown) {
        int32_t aim[3];
        playerAimVector(0, kLiftableThrowUp, kLiftableThrowForward, aim); // exe 0x5e3e50
        bar.velocity.x = 0;
        bar.velocity.y = int16_t(aim[1]);
        bar.velocity.z = int16_t(aim[2]);
        fields.entry = kLiftableThrown;
    } else {
        bar.velocity.y = kLiftableDropUp;
        bar.velocity.z = kLiftableDropForward;
        fields.entry = 0;
    }
    bar.acceleration.y = kLiftableGravity;
    const int16_t yaw = player().rotation.y;
    bar.rotation.y = yaw;
    bar.rotation.spare = yaw;
    bar.airborne = 1;
    bar.actorHit = 0;
    carryDropFromPlayer(bar); // exe 0x573990
    bar.subState = uint8_t(LiftableSub::Flying);
    bar.restartStep();
    liftableFlying(env, bar, fields);
}
void liftableFlying(const PlayerEnv& env, StaticActor& bar, BarFields& fields) { // exe 0x590aa0
    bar.velocity.y = int16_t(bar.velocity.y + kLiftableGravity);
    if (bar.velocity.y > kLiftableFallMax) bar.velocity.y = kLiftableFallMax;
    integrateVelocity(bar, 0, bar.velocity.y, bar.velocity.z); // exe 0x5e3690
    bar.wallHit = actorTestMove(env, bar); // exe 0x40f570
    if (int32_t(bar.wallHit) >= 0 && bar.actorHit == 0 && !(fields.hitBox.result & kLiftableBreaks)) return;
    bar.subState = uint8_t(LiftableSub::Destroyed);
    bar.restartStep();
}
void liftableDestroyed(StaticActor& bar) { // exe 0x590b00: a blast (static type 7) of its row's power, freed
    if (const uint32_t address = allocActor60(0)) { // exe 0x5e1300
        StaticActor& blast = staticActorAt(address);
        BlastParams params(blast);
        blast.subtype = 0;
        blast.variant = 0;
        blast.loopFlags |= kActorSweepGroup2;
        blast.flags = kActorLive;
        blast.type = typeByte(GlobalStaticType::Blast);
        blast.pos.x = bar.pos.x;
        blast.pos.y = bar.pos.y;
        blast.pos.z = bar.pos.z;
        blast.pos.y.whole = int16_t(bar.pos.y.whole - kLiftableHitHeight);
        params.size = kLiftableBlastSize;
        params.attack = staticStats(bar.type, 0).power;
    }
    actorDetachRiders(bar); // exe 0x414e70
    freeActor(bar);         // exe 0x5e1540
}
// exe 0x58fc40: a hit by the player (its row's hit points: destroyed), the sub-state; fallen out: freed; its hit entry
// (in the hands none, flying the player's attack and a mover, else a liftable solid one stands on)
void liftableStatic(const PlayerEnv& env, StaticActor& bar, BarFields& fields) {
    bar.tint = kTintNeutral;
    const uint32_t hit = fields.hitBox.result;
    if ((hit & kHitDamageMask) && (hit & (kHitAttackByPlayer | kHitAttackStrong))) {
        bar.tint = kTintFlash;
        soundPlayAtPos((hit & kHitAttackStrongPush) ? kLiftableStrongHitSound : kLiftableHitSound, bar.pos);
        if (hitPointsAdd(bar, -int(hit & kHitDamageMask)) != 0) { // exe 0x5e52f0
            bar.subState = uint8_t(LiftableSub::Destroyed);
            bar.restartStep();
        }
    }
    switch (LiftableSub(bar.subState)) { // [0x8cdc78]
    case LiftableSub::Slide: liftableSlide(bar, fields); break;
    case LiftableSub::Rest: liftableRest(bar, fields); break;
    case LiftableSub::InHands: liftableInHands(env, bar, fields); break;
    case LiftableSub::Flying: liftableFlying(env, bar, fields); break;
    case LiftableSub::Destroyed: liftableDestroyed(bar); break;
    default: notPortedAt(kStoryPort, "static 0x58fc40 sub-state (table 0x8cdc78 has 5 entries)", bar.subState);
    }
    if (!(bar.flags & kActorLive)) return;
    if (int32_t(bar.pos.y) >= kLiftableLostY) {
        freeActor(bar); // exe 0x5e1540
        return;
    }
    bar.wasOnPlatform = bar.onPlatform;
    fields.hitBox.result = 0;
    fields.hitBox.previousX = bar.pos.x.whole;
    fields.hitBox.previousZ = bar.pos.z.whole;
    const uint8_t entry = fields.entry;
    if (entry & kLiftableNoEntry) return;
    const int16_t pos[3] = {bar.pos.x.whole, int16_t(bar.pos.y.whole - kLiftableHitHeight), bar.pos.z.whole};
    const uint32_t owner = hitOwner(bar.slot) | kHitNoLockOn | hitRadius(kLiftableHitRadius);
    if (entry & kLiftableThrown) {
        hitListSubmit(pos, fields.hitBox, owner | hitGroup(kHitGroupPlayerAttacks),
                      staticStats(bar.type, 0).damage | kHitAttackHarmless | kHitAttackByPlayer); // exe 0x5e4120
        actorCollisionSubmitMover(bar); // exe 0x4140b0
        return;                         // (exe 0x5f20f0: the shadow, render output)
    }
    hitListSubmit(pos, fields.hitBox, owner | hitGroup(kHitGroupLiftable),
                  ((entry & kLiftableFalling) ? kHitAttackThrown : 0u) | kHitAttackHarmless | kHitAttackSolid);
    actorCollisionSubmitSolid(bar, SolidType::StandOn, kNoBone, 0); // exe 0x414050
}

// exe 0x58f820
void st17Bar(const PlayerEnv& env, StaticActor& bar) {
    BarFields& fields = classState<BarFields>(bar);
    bar.savePreviousPose();
    bar.wasAirborne = bar.airborne;
    switch (BarState(bar.state)) {
    case BarState::Init: st17BarInit(bar, fields); return;
    case BarState::Run:
        switch (St17BarVariant(bar.variant)) {
        case St17BarVariant::Still:
        case St17BarVariant::Still3: return; // exe 0x52ef00
        case St17BarVariant::Lever: gateLever(bar, fields); return;
        case St17BarVariant::Liftable: liftableStatic(env, bar, fields); return; // exe 0x58fc40
        default: notPortedAt(kStoryPort, "static 0x58f820 variant", bar.variant);
        }
    default: notPortedAt(kStoryPort, "static 0x58f820 state", bar.state);
    }
}

// ---- 0x5abe60: the ST17 gate (states [0x8cf424]: 0 init 0x5abed0, 1 by the sub-state [0x8cf42c]) -------------------
enum class GateState : uint8_t { Init = 0, Run = 1 };
enum class GateSub : uint8_t { Wait = 0, Slide = 1, Wave = 2 };
enum class GateSlide : uint8_t { Move = 0, Land = 1, Bounce = 2 };
enum class GateWave : uint8_t { Start = 0, Spawn = 1, Pause = 2 };
const DataTable<CollisionBoxRecord> gateBox{TableId::BarsAndGateBox};
// the wave statics' first parameter, by the statics left (exe data, read at 0x8cf43a - left)
const DataTable<ScalarTable<uint8_t, 3>> gateWaveParams{TableId::BarsAndGateWaveParams};
constexpr uint8_t kGateLeverType = 1, kGateLeverVariant = 1, kWaveVariant = 2;
constexpr uint32_t kGateEnemyA = 0xa, kGateEnemyB = 0xb, kGateEnemyC = 0xc, kWaveIdBase = 9;
constexpr int16_t kOpenY = int16_t(0xfa00), kClosedY = int16_t(0xf800);
constexpr int16_t kSlideStep = 8, kSlideSpeedMax = 0x100;
constexpr uint8_t kWaveStory = 6;

// exe 0x5abed0: the model (scenery), the row's hit points, no motion; its lever spawned at (-0x510, -0x968, 0x880)
// facing 0x800; enemy ids 0xa..0xc marked defeated
void gateInit(StaticActor& gate, GateFields& fields) {
    if (!bindActorModel(gate, 0)) { // exe 0x5e0ba0
        freeActor(gate);           // exe 0x5e1540
        return;
    }
    gate.flags |= kActorScenery;
    const int16_t hp = rowHitPoints(gate);
    gate.maxHitPoints = hp;
    gate.hitPoints = hp;
    gate.velocity.setXY(0, 0);
    gate.rotation.z = 0;
    gate.rotation.x = 0;
    gate.airborne = 0;
    gate.onPlatform = 0;
    gate.collisionBox.pointAt(gateBox.stateAddress());
    gate.interactionBox = nullptr;
    gate.interaction = 0;
    fields.clearBlockBytes(); // exe 0x40dc40(+0xbc, +0xc8)
    GateParams(gate).busy = 0;
    if (const uint32_t e = allocActor60(kActorStageClass)) { // exe 0x5e12f0
        StaticActor& lever = staticActorAt(e);
        lever.flags = kActorLive;
        lever.type = kGateLeverType;
        lever.subtype = 0;
        lever.variant = kGateLeverVariant;
        lever.pos.x.whole = int16_t(0xfaf0);
        lever.pos.y.whole = kBarTopWord;
        lever.pos.z.whole = 0x880;
        lever.rotation.y = 0x800;
        BarParams(lever).linked = gate.slot;
    }
    enemyMarkDefeated(kGateEnemyA); // exe 0x5e63c0
    enemyMarkDefeated(kGateEnemyB);
    enemyMarkDefeated(kGateEnemyC);
    gate.enterState(uint8_t(GateState::Run));
}
// exe 0x5abfd0: busy while enemies 0xa / 0xb live; the lever pulled -> the slide (up to open, down to closed)
void gateWait(StaticActor& gate, GateFields& fields) {
    GateParams params(gate);
    params.busy = enemyDefeated(kGateEnemyA) && enemyDefeated(kGateEnemyB) ? 0 : 1; // exe 0x5e6420
    if (params.leverPulled == 0) return;
    params.busy = 1;
    params.leverPulled = 0;
    gate.velocity.y = 0;
    if (fields.open == 0) {
        fields.targetY = kOpenY;
        gate.acceleration.y = kSlideStep;
    } else {
        fields.targetY = kClosedY;
        gate.acceleration.y = -kSlideStep;
    }
    fields.moved = 1;
    gate.subState = uint8_t(GateSub::Slide);
    gate.restartStep();
}
// exe 0x5ac060: the slide to the target height, then a bounce of 0xf ticks; open toggled: opened -> the wave, closed ->
// waiting
void gateSlide(StaticActor& gate, GateFields& fields) {
    switch (GateSlide(gate.step)) {
    case GateSlide::Move: {
        gate.velocity.y = int16_t(uint16_t(gate.velocity.y) + uint16_t(gate.acceleration.y));
        const int16_t v = gate.velocity.y;
        if (gate.acceleration.y > 0) {
            if (v > kSlideSpeedMax) gate.velocity.y = kSlideSpeedMax;
            if (gate.pos.y.whole > fields.targetY) {
                gate.pos.y.whole = fields.targetY;
                gate.enterStep(uint8_t(GateSlide::Land));
                return;
            }
        } else {
            if (v < -kSlideSpeedMax) gate.velocity.y = -kSlideSpeedMax;
            if (gate.pos.y.whole < fields.targetY) {
                gate.pos.y.whole = fields.targetY;
                gate.enterStep(uint8_t(GateSlide::Land));
                return;
            }
        }
        gate.pos.y = int32_t(gate.pos.y + int32_t(uint32_t(int32_t(gate.velocity.y)) << 12));
        return;
    }
    case GateSlide::Land:
        fields.ticks = 0xf;
        gate.velocity.y = 0;
        gate.enterStep(uint8_t(GateSlide::Bounce));
        [[fallthrough]];
    case GateSlide::Bounce: {
        const uint32_t c = fields.ticks;
        const uint32_t bob = (c & 1) ? c >> 1 : 0;
        gate.pos.y.whole = int16_t(uint16_t(uint32_t(uint16_t(fields.targetY)) + bob));
        fields.ticks = uint8_t(c - 1u);
        if (fields.ticks != 0) return;
        gate.pos.y.whole = fields.targetY;
        fields.open ^= 1u;
        if (fields.open != 0) {
            gate.restartStep();
            gate.subState = uint8_t(GateSub::Wave);
        } else {
            gate.subState = uint8_t(GateSub::Wait);
            gate.restartStep();
        }
        return;
    }
    default: return;
    }
}
// exe 0x5ac170: opened -- at story 6 two statics (type 1 variant 2, tracked as the enemy ids 0xa / 0xb), 0x1e ticks
// apart; then the slide back down
void gateWave(StaticActor& gate, GateFields& fields) {
    switch (GateWave(gate.step)) {
    case GateWave::Start:
        fields.waveLeft = 2;
        gate.enterStep(uint8_t(GateWave::Spawn));
        [[fallthrough]];
    case GateWave::Spawn:
        if (gameState().story == kWaveStory) {
            if (const uint32_t e = allocActor60(kActorStageClass)) { // exe 0x5e12f0
                StaticActor& wave = staticActorAt(e);
                wave.flags = kActorLive;
                wave.type = kGateLeverType;
                wave.subtype = 0;
                wave.variant = kWaveVariant;
                wave.pos.x = int32_t(0xf9800000u);
                wave.pos.y = int32_t(0xfa000000u);
                wave.pos.z = 0x7000000;
                wave.rotation.y = 0xc00;
                wave.params[0] = gateWaveParams[size_t(2u - fields.waveLeft)];
                wave.params[1] = gate.slot;
                const uint32_t id = uint32_t(fields.waveLeft) + kWaveIdBase;
                enemyClearDefeated(id);                         // exe 0x5e63f0
                encounterTrackInScriptSlot(wave, uint16_t(id)); // exe 0x5e6500
            }
        }
        fields.ticks = 0x1e;
        gate.enterStep(uint8_t(GateWave::Pause));
        return;
    case GateWave::Pause:
        fields.ticks -= 1u;
        if (fields.ticks != 0) return;
        fields.waveLeft -= 1u;
        if (fields.waveLeft != 0) {
            gate.enterStep(uint8_t(GateWave::Spawn));
        } else {
            fields.targetY = kClosedY;
            gate.acceleration.y = -kSlideStep;
            gate.subState = uint8_t(GateSub::Slide);
            gate.restartStep();
        }
        return;
    default: return;
    }
}
// exe 0x5abe60
void st17Gate(StaticActor& gate) {
    GateFields& fields = classState<GateFields>(gate);
    gate.tint = kTintNeutral;
    gate.savePreviousPose();
    gate.wasAirborne = gate.airborne;
    switch (GateState(gate.state)) {
    case GateState::Init: gateInit(gate, fields); break;
    case GateState::Run:
        switch (GateSub(gate.subState)) {
        case GateSub::Wait: gateWait(gate, fields); break;
        case GateSub::Slide: gateSlide(gate, fields); break;
        case GateSub::Wave: gateWave(gate, fields); break;
        default: notPortedAt(kStoryPort, "ST17 gate sub-state", gate.subState);
        }
        break;
    default: notPortedAt(kStoryPort, "static 0x5abe60 state", gate.state);
    }
    gate.wasOnPlatform = gate.onPlatform;
    if (gate.flags & kActorLive) actorCollisionSubmitSolid(gate, SolidType::StandOn, kNoBone, 0); // exe 0x414050
}

} // namespace

void st16BarUpdate(const PlayerEnv&, uint32_t actor) { st16Bar(staticActorAt(actor)); }
void st17LeverUpdate(const PlayerEnv& env, uint32_t actor) { st17Bar(env, staticActorAt(actor)); }
void st17GateUpdate(const PlayerEnv&, uint32_t actor) { st17Gate(staticActorAt(actor)); }

} // namespace od::port
