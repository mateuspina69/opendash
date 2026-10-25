// The room's trigger volumes (see room_triggers.hpp).
#include "game/room_triggers.hpp"

#include <optional>

#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/collision.hpp"
#include "game/fade.hpp"
#include "game/game_state.hpp"
#include "game/message.hpp"
#include "game/event.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_status.hpp"
#include "game/player/player_weapons.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"
#include "game/transition_state.hpp"

namespace od::port {

namespace {

// the trigger types (TriggerRecord type)
constexpr uint32_t kTriggerWarp = kTriggerWarpType; // an in-stage warp (a relative move)
constexpr uint32_t kTriggerDoorA = 0x2, kTriggerDoorB = 0x3, kTriggerDoorC = 0x8, kTriggerDoorD = 0x9; // the use button, facing
constexpr uint32_t kTriggerPassageA = 0x4, kTriggerPassageB = 0x5, kTriggerPassageC = 0x6, kTriggerPassageD = 0x7,
                   kTriggerPassageE = 0xb;       // crossed in a direction with the use button
constexpr uint32_t kTriggerPassageCeiling = 0xa; // a passage's ceiling: clamps, or arms
constexpr uint32_t kTriggerUseExit = 0xc;        // the use button
constexpr uint32_t kTriggerUseExitFacing = 0xd;  // the use button, facing
constexpr uint32_t kTriggerWalkOff = 0xf;        // walked into
constexpr uint32_t kTriggerEvent = 0x10;         // the use button: its event
constexpr uint32_t kTriggerStatusFloor = 0x11;   // a status floor
constexpr uint32_t kTransitionKindBase = 2;      // the transition kind = the trigger's type - this
// the trigger's flags
constexpr uint16_t kTriggerStartsEvent = 0x4000; // an event volume's event (its message: flags & 0xff)
constexpr uint16_t kFloorKindMask = 3;

// the event flags the triggers read / set
constexpr uint32_t kFlagNoWarps = 0x6f0;
constexpr uint32_t kFlagFloorStatusOff = 0x240; // + the floor's kind: the floor gives no status
constexpr uint32_t kExitIdMask = 0x1f;

// the fades a transition asks for (in after the load / out before it)
constexpr uint8_t kExitFadeIn = kFadeInRequest | 2, kExitFadeOut = kWipe28Request;
constexpr uint8_t kPassageFadeOut = kWipe26Request;
constexpr uint8_t kFacingExitFadeIn = kFadeInRequest | kFadeWhiteRequest | 2;
constexpr uint8_t kFacingExitFadeOut = kFadeOutRequest | kFadeWhiteRequest | 2;
constexpr int16_t kFloorStatusTicks = 0x200;
constexpr int8_t kStatusFloorWade = 0x40;       // the burning floor's sink depth
constexpr uint32_t kFacingWindow = 0x3ff;       // the facing within +-0x200 of the exit's
constexpr uint32_t kFacingBias = 0x200;
constexpr uint32_t kQuadrantBits = 0xc00;      // an angle's quarter-turn quadrant (a passage: crossed in its own quadrant)

// exe 0x413c80 (game/collision triggerScan) into the trigger record
uint32_t scanTriggers(const PlayerEnv& env, const Actor& actor) {
    TriggerHit hit;
    const uint32_t r = withCollisionState([&](CollisionState& st) {
        return triggerScan(env.room, st, {actor.pos.x.whole, actor.pos.y.whole, actor.pos.z.whole}, actor.rotation.y,
                           player().rotation.y, hit);
    });
    TriggerRecord& t = trigger();
    t.type = hit.type;
    t.flags = hit.flags;
    if (r) {
        t.centre.x = hit.x;
        t.centre.y = hit.y;
        t.centre.z = hit.z;
        t.centre.yaw = int16_t(hit.yaw);
    }
    return r;
}

// exe 0x413f40: the player may use an exit now: no transition pending, standing / walking / turning, the use button
// held, no weapon or carry busy
bool playerMayUseExit(const Player* p) {
    if (!p || transitionRequest().state != kTransitionNone) return false;
    const PlayerSub sub = p->sub();
    if (sub != PlayerSub::Idle && sub != PlayerSub::Walk && sub != PlayerSub::TurnInPlace) return false;
    if (!p->holds(PlayerButton::Use)) return false;
    return weaponBusy(*p) == 0;
}

// exe 0x413f90: the door is locked (flag 0x710 or 0x710 + its id) -> its event (exe 0x543b40 at the trigger's centre)
bool exitLocked(const ExitEntry& e) {
    if (!storyFlag(kFlagLocked) && !storyFlag((e.id & kExitIdMask) + kFlagLocked)) return false;
    if (e.lockedMessage >= 0) eventStartAtPoint(trigger().centre, nullptr, 0, 0, uint16_t(e.lockedMessage));
    return true;
}

// exe 0x413ff0: the request's state: asked, or none when flag 0x730 (+ its id) holds the transition for a script
uint8_t exitRequestState(const ExitEntry& e) {
    return (!storyFlag(kFlagTransitionHeld) && !storyFlag((e.id & kExitIdMask) + kFlagTransitionHeld))
               ? kTransitionAsked
               : kTransitionNone;
}

// the exit entry at the trigger's centre (and its facing), none when the table has none
std::optional<ExitEntry> exitAt(bool withFacing) {
    const RoomPlace centre = trigger().centre;
    for (uint32_t i = 0;; ++i) {
        const ExitEntry e = exitEntry(i);
        if (e.id == kExitEnd) return std::nullopt;
        if (centre.x != e.trigger.x || centre.y != e.trigger.y || centre.z != e.trigger.z) continue;
        if (withFacing &&
            kFacingWindow < ((uint32_t(uint16_t(centre.yaw) - uint16_t(e.trigger.yaw)) + kFacingBias) & kAngleMask))
            continue;
        return e;
    }
}

// the exit's ids and destination into the request (raw: its trigger and destination places too)
void requestFrom(const ExitEntry& e, bool withPlaces) {
    TransitionRequest& r = transitionRequest();
    r.exitId = e.id;
    r.exitByte1 = e.byte1;
    r.exitByte2 = e.byte2;
    r.destStage = e.destStage;
    r.destRoom = e.destRoom;
    if (withPlaces) {
        r.trigger = e.trigger;
        r.destination = e.destination;
    }
}

// the request armed with its fades and kind (the trigger's type), flag 0x700
void arm(uint32_t type, uint8_t fadeIn, uint8_t fadeOut) {
    TransitionRequest& r = transitionRequest();
    r.fadeIn = fadeIn;
    r.fadeOut = fadeOut;
    r.kind = uint8_t(type - kTransitionKindBase);
    r.keepPosition = 0;
    setStoryFlag(kFlagTransitionArmed);
}

// the status floors' kinds (the trigger's flags & kFloorKindMask): the status each gives (its timer's index)
enum class FloorKind : uint32_t { Drain = 0, Burn = 1, Slowed = 2, Orbit = 3 };

// the status floor (+ the kind's flag off): the player's status, its floor form, its timer
void statusFloor(Player& p, uint16_t flags) {
    if ((p.airborne & p.wasAirborne) || p.onPlatform != 0) return;
    const FloorKind kind = FloorKind(flags & kFloorKindMask);
    const bool off = storyFlag(kFlagFloorStatusOff + uint32_t(kind));
    auto& ticks = p.statusTicks;
    switch (kind) {
    case FloorKind::Drain:
        if (!off) {
            p.setStatusBits(uint16_t(kStatusDrain | kStatusDrainFloor));
            ticks[size_t(kind)] = kFloorStatusTicks;
        }
        break;
    case FloorKind::Burn:
        if (!off) {
            if (!storyFlag(kFlagBurnArmour)) {
                p.setStatusBits(uint16_t(kStatusBurn | kStatusBurnFloor));
                ticks[size_t(kind)] = kFloorStatusTicks;
            } else {
                p.setStatusBits(kStatusBurnFloor);
            }
            if (uint8_t(p.legPart) != kLegsNoBurn) p.wadeDepth = kStatusFloorWade;
        }
        break;
    case FloorKind::Slowed:
        if (!off && uint8_t(p.legPart) != kLegsSlowProof) {
            if (!storyFlag(kFlagSlowArmour)) {
                p.setStatusBits(uint16_t(kStatusSlowed | kStatusSlowedFloor));
                ticks[size_t(kind)] = kFloorStatusTicks;
            } else {
                p.setStatusBits(kStatusSlowedFloor);
            }
        }
        break;
    case FloorKind::Orbit:
        if (!off) {
            p.setStatusBits(uint16_t(kStatusOrbit | kStatusOrbitFloor));
            ticks[size_t(kind)] = kFloorStatusTicks;
        }
        break;
    }
}

} // namespace

uint32_t roomTriggers(const PlayerEnv& env, Actor& actor) {
    if (scanTriggers(env, actor) == 0) return 0;
    Player* p = nullptr; // the player, when it is the actor (exe 0x5e0f70: class 0)
    if (actorSlotClass(actor.slot) == kPlayerClass) {
        p = &asPlayer(actor);
        if (p->inputMode & kInputOff) return 0;
    }
    const uint32_t type = trigger().type;
    const uint16_t flags = trigger().flags;
    TransitionRequest& request = transitionRequest();
    const RoomPlace centre = trigger().centre;
    switch (type) {
    case kTriggerWarp: {
        if (!p || storyFlag(kFlagNoWarps) || exitEntry(0).id == kExitEnd) break;
        if (request.state != kTransitionNone) break;
        const auto e = exitAt(false);
        if (!e) break;
        request.state = kTransitionWarp;
        request.keepPosition = 0;
        request.exitId = e->id;
        request.exitByte1 = e->byte1;
        request.destStage = e->destStage;
        request.destRoom = e->destRoom;
        const int16_t dx = int16_t(e->destination.x - e->trigger.x), dz = int16_t(e->destination.z - e->trigger.z);
        request.trigger.x = dx; // the move's offsets
        request.trigger.y = 0;
        request.trigger.z = dz;
        request.destination.x = int16_t(p->pos.x.whole + dx);
        request.destination.y = p->pos.y.whole;
        request.destination.z = int16_t(p->pos.z.whole + dz);
        request.destination.yaw = p->rotation.y;
        return type;
    }
    case kTriggerDoorA:
    case kTriggerDoorB:
    case kTriggerDoorC:
    case kTriggerDoorD: {
        if (!playerMayUseExit(p) || exitEntry(0).id == kExitEnd) break;
        const auto e = exitAt(true);
        if (!e || exitLocked(*e)) break;
        request.state = exitRequestState(*e);
        request.keepPosition = 0;
        requestFrom(*e, true);
        request.kind = uint8_t(type - kTransitionKindBase);
        if (gameState().stage != e->destStage) { // another stage: faded
            request.fadeOut = kExitFadeOut;
            request.fadeIn = kExitFadeIn;
        } else {
            request.fadeOut = kNoFade;
            request.fadeIn = kNoFade;
        }
        setStoryFlag(kFlagTransitionArmed);
        return type;
    }
    case kTriggerPassageA:
    case kTriggerPassageB:
    case kTriggerPassageC:
    case kTriggerPassageD:
    case kTriggerPassageE: {
        if (!playerMayUseExit(p)) break;
        for (uint32_t i = 0;; ++i) {
            const ExitEntry x = exitEntry(i);
            if (x.id == kExitEnd) break;
            if (centre.x != x.trigger.x || centre.y != x.trigger.y || centre.z != x.trigger.z) continue;
            if (((uint32_t(int(uint16_t(centre.yaw)) - int(uint16_t(x.trigger.yaw))) + kFacingBias) & kQuadrantBits) != 0) continue;
            const uint32_t dir = ((uint16_t(centre.yaw) + kFacingBias) >> 10) & 3;
            const int16_t tx = centre.x, tz = centre.z;
            bool past; // the actor past the trigger's centre in its direction
            switch (dir) {
            case 0: past = tz <= actor.pos.z.whole; break;
            case 1: past = tx <= actor.pos.x.whole; break;
            case 2: past = actor.pos.z.whole <= tz; break;
            default: past = actor.pos.x.whole <= tx; break;
            }
            if (!past) continue;
            if (exitLocked(x)) return type;
            request.state = exitRequestState(x);
            requestFrom(x, true);
            arm(type, kExitFadeIn, kPassageFadeOut);
            return type;
        }
        break;
    }
    case kTriggerPassageCeiling: {
        if (!p) return type;
        const auto e = exitAt(false);
        if (!e) return type;
        if (request.state != kTransitionNone) { // a transition runs: the ceiling holds the player under it
            if (p->pos.y.whole <= e->trigger.y) return type;
            p->pos.y.whole = e->trigger.y;
            return type;
        }
        request.state = exitRequestState(*e);
        requestFrom(*e, false);
        request.trigger = e->trigger;
        request.destination.x = e->destination.x;
        request.destination.y = e->destination.y;
        request.destination.z = e->destination.z;
        request.destination.yaw = p->rotation.y;
        arm(type, kExitFadeIn, kExitFadeOut);
        return type;
    }
    case kTriggerUseExit: {
        if (!playerMayUseExit(p)) return type;
        const auto e = exitAt(false);
        if (!e || exitLocked(*e)) return type;
        request.state = exitRequestState(*e);
        requestFrom(*e, true);
        arm(type, kExitFadeIn, kExitFadeOut);
        return type;
    }
    case kTriggerUseExitFacing: {
        if (!playerMayUseExit(p)) return type;
        const auto e = exitAt(true);
        if (!e || exitLocked(*e)) return type;
        request.state = exitRequestState(*e);
        requestFrom(*e, true);
        arm(type, kFacingExitFadeIn, kFacingExitFadeOut);
        return type;
    }
    case kTriggerWalkOff: {
        if (!p || request.state != kTransitionNone) return type;
        const auto e = exitAt(false);
        if (!e) return type;
        request.state = exitRequestState(*e);
        requestFrom(*e, true);
        arm(type, kExitFadeIn, kExitFadeOut);
        return type;
    }
    case kTriggerEvent:
        if (playerMayUseExit(p) && (flags & kTriggerStartsEvent)) // exe 0x543b40 at the centre, message flags & 0xff
            eventStartAtPoint(trigger().centre, nullptr, 0, 0, uint16_t(flags & 0xff));
        break;
    case kTriggerStatusFloor:
        if (p) statusFloor(*p, flags);
        break;
    default:
        break;
    }
    return type;
}

} // namespace od::port
