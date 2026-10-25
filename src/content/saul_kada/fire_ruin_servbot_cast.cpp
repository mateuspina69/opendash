// A Servbot in the fire ruin's cutscenes (pool 0x20 type 0xb sub 2: ST28 sub 5, story 13, scripts 0x42..0x45, exe
// 0x465b20; hypothesis names), one routine per exe routine. It follows the script's step sub-id and tick: walks in,
// faces / poses, its face pages (exe 0x5e3280) and a blink cycle (exe 0x466040, rows [0x88fe14]); exe 0x465fe0 poses it
// with its head turned (part 1's decoded yaw).
// States [0x88fe00]: 0 bind 0x465ba0, 1 by variant 0x465c60 [0x88fe0c] (0: 0x465c80, 1: 0x465dd0), 2 freed 0x468fa0,
// 3 / 4 the variants' routines.
#include "content/saul_kada/fire_ruin_servbot_cast.hpp"
#include "content/saul_kada/fire_ruin_servbot_cast_state.hpp"

#include "game/loaded_data.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/cutscene_actor.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace fire_ruin_servbot_cast_state;

namespace {

enum class ServbotState : uint8_t { Bind = 0, ByVariant = 1, Free = 2, Watcher = 3, Speaker = 4 };
enum class ServbotVariant : uint8_t { Watcher = 0, Speaker = 1 };
enum class WatcherStep : uint8_t { WalkIn = 0, Wait = 1, Pose = 2, Last = 3 };
// The Servbot's parameter: the side it stands on (its turns and its cue)
struct ServbotParams {
    explicit ServbotParams(const AnimatedActor& actor) : ServbotParams(const_cast<AnimatedActor&>(actor)) {}
    explicit ServbotParams(AnimatedActor& actor) : side(actor.params[0]) {}
    uint8_t& side;
};
// A row of the blink cycle (exe 0x88fe14, 4 bytes): the texture cell, its ticks, the end (kBlinkLoop: back to row 0)
struct BlinkRow : DataRecord {
    static constexpr uint32_t kBytes = 0x3;
    int8_t cell = 0; // +0x0
    uint8_t ticks = 0; // +0x1
    uint8_t end = 0; // +0x2
    template <class R> static BlinkRow load(const R& r) {
        BlinkRow v;
        v.cell = r.template read<int8_t>(0x0);
        v.ticks = r.template read<uint8_t>(0x1);
        v.end = r.template read<uint8_t>(0x2);
        return v;
    }
};
constexpr uint8_t kBlinkLoop = 2;
constexpr uint32_t kHeadPart = 1; // the head's part (the decode buffer's entry 1 + 1)
constexpr int16_t kWalkSpeed = int16_t(0xff00);

BlinkRow blinkRow(int8_t row) { return dataRecord<BlinkRow>(stateAddress(TableId::FireRuinServbotCastBlinkRows) + uint32_t(int32_t(row)) * 4, BlinkRow::kBytes); }

// exe 0x466040: one tick of the blink cycle
void blink(AnimatedActor& actor, ServbotFields& servbot) {
    if (servbot.blinking == 0) {
        servbot.blinkTick = 0;
        servbot.blinkRow = 0;
        servbot.blinking = 1;
    }
    const BlinkRow row = blinkRow(servbot.blinkRow);
    if (servbot.blinkTick != row.ticks) {
        ++servbot.blinkTick;
        return;
    }
    if (row.end == kBlinkLoop) servbot.blinkRow = 0;
    else ++servbot.blinkRow;
    servbot.blinkTick = 0;
    setTexturePageCell(actor.texturePage, uint32_t(int32_t(blinkRow(servbot.blinkRow).cell)));
}
// exe 0x465fe0: the pose with the head turned
void headPose(AnimatedActor& actor, ServbotFields& servbot) {
    servbot.posed = 1;
    if (actor.anim != actor.prevAnim) animSet(actor, actor.anim, 0); // exe 0x5e1c50
    else animStep(actor);                                            // exe 0x5e1ea0
    decodedFrame().entries[1 + kHeadPart].y += uint16_t(int32_t(servbot.headTurn) << 4);
    animPose(actor); // exe 0x5e2d90
}
// exe 0x465c80 (variant 0)
void watcher(AnimatedActor& actor, ServbotFields& servbot) {
    const int32_t tick = scriptState().stepTick;
    switch (WatcherStep(actor.subState)) {
    case WatcherStep::WalkIn:
        if (tick > 0x4c) {
            actor.anim = 0;
            actor.subState = uint8_t(WatcherStep::Wait);
        } else if (tick > 5) {
            integrateVelocity(actor, 0, 0, actor.velocity.z); // exe 0x5e3690
        }
        break;
    case WatcherStep::Wait:
        if (scriptState().stepSub != 4) break;
        if (ServbotParams(actor).side == 0) {
            actor.subState = uint8_t(WatcherStep::Pose);
            break;
        }
        if (tick == 5) {
            actor.anim = 0x1a;
        } else if (tick == 0x64) {
            setTexturePageCell(actor.texturePage, 2);
            actor.anim = 0x1d;
            actor.subState = uint8_t(WatcherStep::Pose);
        }
        if (tick > 5 && tick < 0x64) blink(actor, servbot);
        break;
    case WatcherStep::Pose:
        if (tick == 0x154) {
            actor.anim = 0x1b;
            setTexturePageCell(actor.texturePage, 6);
            actor.subState = uint8_t(WatcherStep::Last);
        }
        break;
    case WatcherStep::Last:
        if (scriptState().stepSub != 5) break;
        if (tick == 1) setTexturePageCell(actor.texturePage, 2);
        if (tick == 0x5a) actor.anim = 0x1c;
        break;
    default: break;
    }
    animateAsked(actor, 0); // exe 0x5e1e80 / 0x5e1c30
}
// exe 0x465dd0 (variant 1)
void speaker(AnimatedActor& actor, ServbotFields& servbot) {
    const int32_t tick = scriptState().stepTick;
    const bool otherSide = ServbotParams(actor).side != 0;
    switch (scriptState().stepSub) {
    case 1:
        if (tick == 0x3c) {
            actor.anim = 0;
            actor.rotation.y = 0xc00;
        } else if (tick < 0x3c) {
            integrateVelocity(actor, 0, 0, actor.velocity.z);
        } else if (tick > 0x64 && tick < 0x8c) {
            servbot.headTurn = int8_t(servbot.headTurn - 1);
            headPose(actor, servbot);
        } else if (tick >= 0x8c) {
            headPose(actor, servbot);
        }
        break;
    case 2:
        if (tick == 1) servbot.headTurn = int8_t(otherSide ? 0xe0 : 0);
        break;
    case 3:
        if (tick < 0x438) {
            bool blinks;
            if (otherSide) {
                if (tick > 0x12e && tick < 0x140) servbot.headTurn = int8_t(servbot.headTurn + 4);
                else if (tick > 0x1f4 && tick < 0x206) servbot.headTurn = int8_t(servbot.headTurn - 4);
                blinks = tick > 0x212 && tick < 0x2a8;
            } else {
                if (tick > 0x12e && tick < 0x132) servbot.headTurn = int8_t(servbot.headTurn - 4);
                else if (tick > 0x1f4 && tick < 0x1f8) servbot.headTurn = int8_t(servbot.headTurn + 4);
                blinks = tick > 0x2bc && tick < 0x2f8;
            }
            if (blinks) blink(actor, servbot);
            headPose(actor, servbot);
        } else if (tick == 0x438) {
            servbot.posed = 0;
            servbot.headTurn = 0;
            actor.velocity.z = kWalkSpeed;
            actor.anim = 4;
            actor.rotation.y = 0xc00;
        } else {
            integrateVelocity(actor, 0, 0, actor.velocity.z);
        }
        if (tick > 0x41a) blink(actor, servbot);
        break;
    case 4: actor.state = uint8_t(ServbotState::Free); break;
    default: break;
    }
    if (servbot.posed != 0) return;
    animateAsked(actor, 0);
}
// exe 0x465ba0
void bind(AnimatedActor& actor, ServbotFields& servbot) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return;
    }
    actor.flags |= kActorDrawn;
    actor.anim = ServbotVariant(actor.variant) == ServbotVariant::Watcher ? 2 : 4;
    actor.prevAnim = kNoAnimShown;
    actor.velocity.x = 0;
    actor.velocity.y = 0;
    actor.velocity.z = kWalkSpeed;
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    servbot.headTurn = 0;
    servbot.posed = 0;
    servbot.blinkTick = 0;
    servbot.blinkRow = 0;
    actor.state = uint8_t(ServbotState::ByVariant);
    actor.subState = 0;
    actor.restartStep();
    animateAsked(actor, 0);
}

// exe 0x465b20
void servbotScriptCast(AnimatedActor& actor) {
    ServbotFields& servbot = classState<ServbotFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (ServbotState(actor.state)) {
    case ServbotState::Bind: bind(actor, servbot); return;
    case ServbotState::ByVariant: // 0x465c60 [0x88fe0c]
        if (ServbotVariant(actor.variant) == ServbotVariant::Watcher) watcher(actor, servbot);
        else if (ServbotVariant(actor.variant) == ServbotVariant::Speaker) speaker(actor, servbot);
        else notPortedAt(kStoryPort, "0x465c60 variant (table 0x88fe0c)", actor.variant);
        return;
    case ServbotState::Free: freeActor(actor); return; // 0x468fa0
    case ServbotState::Watcher: watcher(actor, servbot); return;
    case ServbotState::Speaker: speaker(actor, servbot); return;
    default: notPortedAt(kStoryPort, "0x465b20 state (table 0x88fe00)", actor.state);
    }
}

} // namespace

void servbotScriptCastUpdate(const PlayerEnv&, uint32_t actor) { servbotScriptCast(animatedActorAt(actor)); }

} // namespace od::port
