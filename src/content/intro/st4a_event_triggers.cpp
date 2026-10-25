// The event triggers of ST4A (see st4a_event_triggers.hpp). State 0: freed once the story is past the trigger's limit;
// state 1, while no script or event runs: freed once its flag is set, else its variant's test fires it (the event of
// its message, state 2, its flag set); state 2: freed. A trigger of variant 0xff refills the player's hit points.
#include "content/intro/st4a_event_triggers.hpp"
#include "content/intro/st4a_event_triggers_state.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/distances.hpp"
#include "game/event.hpp"
#include "game/event_state.hpp"
#include "game/game_state.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/story_flags.hpp"

namespace od::port {

using namespace st4a_event_triggers_state;

namespace {

inline constexpr const char* kTriggersArea = "enemy spawner port"; // the area of its port-gap errors

constexpr int16_t kNoTriggerFlag = -1;
constexpr int kRadiusShift = 8;

// +6: the variants (exe table 0x88bad0: a test per variant; Phase 1: by the variant)
enum class TriggerVariant : uint8_t {
    AtOnce = 0,          // exe 0x422470
    NearPlanar = 1,      // exe 0x422480 (waits one frame first)
    Near = 2,            // exe 0x422500
    CrossXUp = 3,        // exe 0x422570 (the gate: a story flag)
    CrossXDown = 4,      // exe 0x4225d0
    CrossZUp = 5,        // exe 0x422630
    CrossZDown = 6,      // exe 0x422690
    Flags = 7,           // exe 0x4226f0
    CrossXUpArmed = 8,   // exe 0x422570 (the gate: the weapon)
    CrossXDownArmed = 9, // exe 0x4225d0
    CrossZUpArmed = 10,  // exe 0x422630
    CrossZDownArmed = 11, // exe 0x422690
    Weapon = 12,         // exe 0x422740
    Refill = 0xff,       // the player's hit points refilled (no state machine)
};
// +8: the states
enum class TriggerState : uint8_t { Init = 0, Watch = 1, Done = 2 };
constexpr uint8_t kNearPlanarFirst = 0, kNearPlanarWaited = 1; // variant 1's sub-state: its first frame, waited

// exe 0x422760: the event of the trigger's message (exe 0x543b40(0, 0, 0, 0, message)), done, its flag set
void triggerFire(ControllerActor& a) {
    EventTriggerFields& f = classState<EventTriggerFields>(a);
    eventStartAt(0, nullptr, 0, 0, uint16_t(f.message.get()));
    a.state = uint8_t(TriggerState::Done);
    const int16_t flag = f.flag;
    if (flag != kNoTriggerFlag) setStoryFlag(uint32_t(int32_t(flag)));
}

// the gate of the line triggers: the flag variant needs the story flag args[1] (kNoTriggerFlag: none), the armed one
// the weapon args[1]
bool lineGate(ControllerActor& a, TriggerVariant flagVariant, TriggerVariant weaponVariant) {
    EventTriggerFields& f = classState<EventTriggerFields>(a);
    const TriggerVariant v = TriggerVariant(a.variant);
    const int32_t gate = f.args[1];
    if (v == flagVariant) return gate == kNoTriggerFlag || storyFlag(uint32_t(gate));
    if (v == weaponVariant) return int32_t(player().specialWeapon) == gate;
    return true;
}

// variants 1 / 2: the player within the radius of the point (planar / in 3D) fires the trigger, else it is freed
void triggerNear(ControllerActor& a, bool planar) {
    EventTriggerFields& f = classState<EventTriggerFields>(a);
    if (planar && a.subState == kNearPlanarFirst) {
        a.subState = kNearPlanarWaited;
        return;
    }
    const Player& p = player();
    const int32_t dx = int16_t(uint16_t(uint16_t(f.args[0]) - uint16_t(uint32_t(p.pos.x) >> 16)));
    const int32_t dz = int16_t(uint16_t(uint16_t(f.args[2]) - uint16_t(uint32_t(p.pos.z) >> 16)));
    uint32_t sq = uint32_t(dx) * uint32_t(dx) + uint32_t(dz) * uint32_t(dz);
    if (!planar) { // exe 0x5e3a60: + dy
        const int32_t dy = int16_t(uint16_t(uint16_t(f.args[1]) - uint16_t(p.pos.y.whole)));
        sq = uint32_t(dx) * uint32_t(dx) + uint32_t(dy) * uint32_t(dy) + uint32_t(dz) * uint32_t(dz);
    }
    const int d = isqrt(int32_t(sq));
    if (d > (int32_t(f.radius) << kRadiusShift)) freeActor(a);
    else triggerFire(a);
}

// exe 0x4223c0
void eventTrigger(ControllerActor& a) {
    EventTriggerFields& f = classState<EventTriggerFields>(a);
    if (TriggerVariant(a.variant) == TriggerVariant::Refill) {
        Player& p = player();
        p.hitPoints = p.maxHitPoints;
        return;
    }
    switch (TriggerState(a.state)) {
    case TriggerState::Init:
        if (int32_t(gameState().story) > int32_t(f.storyLimit)) {
            freeActor(a);
            return;
        }
        a.state = uint8_t(TriggerState::Watch);
        [[fallthrough]];
    case TriggerState::Watch: {
        if (scriptState().inUse() || eventState().active != 0) return;
        const uint16_t flag = uint16_t(f.flag);
        if (flag != uint16_t(kNoTriggerFlag) && storyFlag(flag)) {
            freeActor(a);
            return;
        }
        const Player& p = player();
        const int16_t px = p.pos.x.whole, ppx = p.prevPos.x.whole, pz = p.pos.z.whole, ppz = p.prevPos.z.whole;
        switch (TriggerVariant(a.variant)) {
        case TriggerVariant::AtOnce: triggerFire(a); return;
        case TriggerVariant::NearPlanar: triggerNear(a, true); return;
        case TriggerVariant::Near: triggerNear(a, false); return;
        case TriggerVariant::CrossXUp:
        case TriggerVariant::CrossXUpArmed:
            if (lineGate(a, TriggerVariant::CrossXUp, TriggerVariant::CrossXUpArmed) && px >= f.args[0] &&
                ppx < f.args[0])
                triggerFire(a);
            return;
        case TriggerVariant::CrossXDown:
        case TriggerVariant::CrossXDownArmed:
            if (lineGate(a, TriggerVariant::CrossXDown, TriggerVariant::CrossXDownArmed) && px <= f.args[0] &&
                ppx > f.args[0])
                triggerFire(a);
            return;
        case TriggerVariant::CrossZUp:
        case TriggerVariant::CrossZUpArmed:
            if (lineGate(a, TriggerVariant::CrossZUp, TriggerVariant::CrossZUpArmed) && pz >= f.args[2] &&
                ppz < f.args[2])
                triggerFire(a);
            return;
        case TriggerVariant::CrossZDown:
        case TriggerVariant::CrossZDownArmed: // z <= line and the previous z < line, as the exe tests
            if (lineGate(a, TriggerVariant::CrossZDown, TriggerVariant::CrossZDownArmed) && pz <= f.args[2] &&
                ppz < f.args[2])
                triggerFire(a);
            return;
        case TriggerVariant::Flags: {
            uint32_t n = 0;
            for (size_t k = 0; k < f.args.size(); ++k) {
                const uint16_t w = uint16_t(f.args[k]);
                if (w == uint16_t(kNoTriggerFlag)) ++n;
                else n += storyFlag(uint32_t(int32_t(int16_t(w)))) ? 1u : 0u;
            }
            if (n == f.args.size()) triggerFire(a);
            return;
        }
        case TriggerVariant::Weapon:
            if (int16_t(player().specialWeapon) == f.args[0]) triggerFire(a);
            return;
        default: notPortedAt(kTriggersArea, "ST4A trigger variant", a.variant);
        }
    }
    case TriggerState::Done: freeActor(a); return;
    default: return;
    }
}

} // namespace

// the class's update (st4a_event_triggers.hpp)
void st4aEventTriggerUpdate(const PlayerEnv&, uint32_t address) { eventTrigger(controllerActorAt(address)); }

} // namespace od::port
