// The message triggers (see message_triggers.hpp).
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/message_triggers.hpp"

#include "game/actor_pools.hpp"
#include "game/distances.hpp"
#include "game/event_state.hpp"
#include "game/fade_state.hpp"
#include "game/game_state.hpp"
#include "game/message.hpp"
#include "game/event.hpp"
#include "game/player/player_script_control.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_engine.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

// ---- the message trigger (exe 0x4263f0)
// ------------------------------------------------------------------------------
enum class MessageTriggerState : uint8_t {
    Arm = 0,   // gone when its story is past; else Watch
    Watch = 1, // its condition tested while no script / event runs
    Fired = 2, // freed the next tick
};
// +9 of the planar reach variant: its first tick only arms it
constexpr uint8_t kReachArmed = 1;
// the window it shows its message in
constexpr int8_t kTriggerWindow = 4;
// the variants freed when the player is out of reach (the one-shot reach triggers)
constexpr uint8_t kPlanarReachOnce = 1, kReachOnce = 2;
// the line variants' gates (+0x12): the flag for the variants 3..6, the special weapon for 8..11 (variant + 5)
constexpr uint8_t kXUpward = 3, kXDownward = 4, kZUpward = 5, kZDownward = 6;
constexpr uint8_t kWeaponGateOffset = 5;
// the flags variant's mask that needs all three flags set
constexpr int8_t kAllFlagsSet = -1; // (0xff: every bit set)
constexpr uint32_t kFlagsTested = 3;
constexpr int kRangeUnit = 0x100;

// the conditions by variant (exe table 0x88bec0: a routine per variant, the named routines of engine/routines.csv; the
// variants past the eleven named ones read the table's further words, as the exe does)
const DataTable<RoutineTable<0x20>> triggerConditions{TableId::TriggerConditions};

// exe 0x4267e0: window 4 on the stage bank's message, freed next tick, its flag set
void triggerFire(ControllerActor& trigger) {
    const MessageTriggerFields& t = classState<MessageTriggerFields>(trigger);
    windowClose(kTriggerWindow, WindowClose::Force);
    windowOpen(kTriggerWindow, kMessageBank, t.message);
    trigger.state = uint8_t(MessageTriggerState::Fired);
    if (t.flag != kNoMessageFlag) setStoryFlag(uint32_t(int32_t(t.flag)));
}

// the player's point as s16 (the integer parts of its position)
void playerPoint(int16_t out[3]) {
    const Player& p = player();
    out[0] = p.pos.x.whole;
    out[1] = p.pos.y.whole;
    out[2] = p.pos.z.whole;
}

// the player's distance to the trigger's s16 point: exe 0x5e3b00 (planar) / 0x5e3a60 (3D) on the integer parts
// (game/distances: planarDistance16 / distance3 on the s16 points give the same)
int triggerDistance(const MessageTriggerFields& t, bool planar) {
    int16_t from[3];
    playerPoint(from);
    const int16_t to[3] = {t.params[0], t.params[1], t.params[2]};
    return planar ? planarDistance16(from, to) : distance3(from, to);
}

// the line variants' gate: variants 3..6 need their flag params[1] (unless kNoMessageFlag), 8..11 the saved special
// weapon == params[1]
bool triggerGate(const ControllerActor& trigger, const MessageTriggerFields& t, uint8_t flagVariant) {
    const int32_t v = t.params[1];
    if (trigger.variant == flagVariant) return v == kNoMessageFlag || storyFlag(uint32_t(v));
    if (trigger.variant == flagVariant + kWeaponGateOffset) return int32_t(player().specialWeapon) == v;
    return true;
}

// exe 0x4263f0: the trigger's state, then its condition while no script / event runs
void messageTrigger(ControllerActor& trigger) {
    MessageTriggerFields& t = classState<MessageTriggerFields>(trigger);
    const Player& p = player();
    if (p.hitPoints < 0) {
        freeActor(trigger);
        return;
    }
    switch (MessageTriggerState(trigger.state)) {
    case MessageTriggerState::Arm:
        if (int32_t(t.lastStory) < int32_t(gameState().story)) {
            freeActor(trigger);
            return;
        }
        trigger.state = uint8_t(MessageTriggerState::Watch);
        break;
    case MessageTriggerState::Watch: break;
    case MessageTriggerState::Fired: freeActor(trigger); return;
    default: return;
    }
    if (scriptState().flags != 0 || eventState().active != 0) return;
    if (uint16_t(t.flag) != uint16_t(kNoMessageFlag) && storyFlag(uint16_t(t.flag))) {
        freeActor(trigger);
        return;
    }
    const int16_t x = p.pos.x.whole, z = p.pos.z.whole;
    const int16_t prevX = p.prevPos.x.whole, prevZ = p.prevPos.z.whole;
    if (trigger.variant >= triggerConditions.size())
        notPortedAt(kStoryPort, "story trigger controller variant", trigger.variant);
    const RoutineRef& condition = triggerConditions[trigger.variant];
    switch (condition.id()) {
    case RoutineId::TriggerAtOnce: triggerFire(trigger); return;
    case RoutineId::TriggerInPlanarReach:
    case RoutineId::TriggerInReach: {
        const bool planar = condition.is(RoutineId::TriggerInPlanarReach);
        if (planar && trigger.subState == 0) {
            trigger.subState = kReachArmed;
            return;
        }
        if (triggerDistance(t, planar) <= int32_t(t.range) * kRangeUnit)
            triggerFire(trigger);
        else if (trigger.variant == (planar ? kPlanarReachOnce : kReachOnce))
            freeActor(trigger);
        return;
    }
    case RoutineId::TriggerCrossedXUp:
        if (triggerGate(trigger, t, kXUpward) && t.params[0] <= x && prevX < t.params[0]) triggerFire(trigger);
        return;
    case RoutineId::TriggerCrossedXDown:
        if (triggerGate(trigger, t, kXDownward) && x <= t.params[0] && t.params[0] < prevX) triggerFire(trigger);
        return;
    case RoutineId::TriggerCrossedZUp:
        if (triggerGate(trigger, t, kZUpward) && t.params[2] <= z && prevZ < t.params[2]) triggerFire(trigger);
        return;
    case RoutineId::TriggerReachedZ:
        if (triggerGate(trigger, t, kZDownward) && z <= t.params[2] && prevZ < t.params[2]) triggerFire(trigger);
        return;
    case RoutineId::TriggerAllFlags: t.range = kAllFlagsSet; [[fallthrough]];
    case RoutineId::TriggerFlagsByMask: {
        uint32_t ok = 0;
        int32_t mask = t.range;
        for (size_t i = 0; i < kFlagsTested; ++i, mask >>= 1) {
            const int16_t f = t.params[i];
            if (uint16_t(f) == uint16_t(kNoMessageFlag)) {
                ++ok;
                continue;
            }
            const bool set = storyFlag(uint32_t(int32_t(f)));
            ok += (mask & 1) ? (set ? 1 : 0) : (set ? 0 : 1);
        }
        if (ok == kFlagsTested) triggerFire(trigger);
        return;
    }
    case RoutineId::TriggerSpecialWeapon:
        if (int16_t(p.specialWeapon) == t.params[0]) triggerFire(trigger);
        return;
    case RoutineId::TriggerCountdown:
        if (uint16_t(t.params[0]) == 0)
            triggerFire(trigger);
        else
            t.params[0] -= 1u;
        return;
    default: notPortedAt(kStoryPort, "story trigger controller variant", trigger.variant);
    }
}

// ---- the scripted message (exe 0x426950)
// -----------------------------------------------------------------------------
enum class ScriptedMessageState : uint8_t {
    WaitFree = 0, // until no script / event runs: the player on the replay pad
    TakeOver = 1, // until the player is script driven
    Show = 2,     // (after the fade, with kWaitsForFade) the message event, its flag
    WaitEvent = 3, // the event over: the player back, the controller freed
};
// The scripted message's bytes
struct ScriptedMessageFields {
    explicit ScriptedMessageFields(const ControllerActor& actor) : ScriptedMessageFields(const_cast<ControllerActor&>(actor)) {}
    explicit ScriptedMessageFields(ControllerActor& actor) : message(actor.params, 0), flags(actor.params[2]), flag(actor.recordPoint.spare) {}
    BytesField<uint16_t, 4> message;
    uint8_t& flags; // kWaitsForFade
    int16_t& flag; // the flag set with the event (kNoMessageFlag: none)
};
constexpr uint8_t kWaitsForFade = 0x01; // the event waits for the screen fade to end
constexpr uint32_t kScriptedEventFlags = 0x01; // the event's flags (bit 0: its reader is not traced)

// exe 0x426950: the takeover, the message event, the player back
void scriptedMessage(ControllerActor& controller) {
    const ScriptedMessageFields m(controller);
    switch (ScriptedMessageState(controller.state)) {
    case ScriptedMessageState::WaitFree:
        if (scriptState().flags == 0 && eventState().active == 0) {
            scriptReplayPadOn();
            ++controller.state;
        }
        return;
    case ScriptedMessageState::TakeOver:
        if (playerToScriptIfFree() == 0) ++controller.state;
        return;
    case ScriptedMessageState::Show:
        if (!(m.flags & kWaitsForFade) || fadeState().idle()) {
            eventStartAt(0, nullptr, kScriptedEventFlags, 0, m.message); // exe 0x426a00
            controller.state = uint8_t(ScriptedMessageState::Show);
            if (m.flag != kNoMessageFlag) setStoryFlag(uint32_t(int32_t(m.flag)));
            ++controller.state;
        }
        return;
    case ScriptedMessageState::WaitEvent:
        if (eventState().active == 0) {
            playerBackToPlay(); // exe 0x5723c0
            scriptReplayPadOff();
            player().airborne = 0;
            freeActor(controller); // exe 0x5e15c0
        }
        return;
    default: return;
    }
}

} // namespace

void messageTriggerUpdate(const PlayerEnv&, uint32_t actor) { messageTrigger(controllerActorAt(actor)); }
void scriptedMessageUpdate(const PlayerEnv&, uint32_t actor) { scriptedMessage(controllerActorAt(actor)); }

} // namespace od::port
