// The door actor class 0x59ce50 (see door_actor.hpp).
#include "game/actor_state.hpp"
#include "game/door_actor.hpp"

#include "engine/data_source.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/event_state.hpp"
#include "game/message.hpp"
#include "game/event.hpp"
#include "game/player/player_state.hpp"
#include "game/story_flags.hpp"
#include "game/transition_state.hpp"

namespace od::port {

namespace {


constexpr int kFacingWindow = 0x400;        // the player faces the door within this of its back ...
constexpr uint32_t kFacingBias = 0x200;     // ... the player's yaw biased by this first (the exe's test)

const DataTable<CollisionBoxRecord> doorCollisionBox{TableId::DoorActorCollisionBox};
const DataTable<InteractionBoxRecord> doorTalkBoxYaw0{TableId::DoorActorTalkBoxYaw0}; // the talk box of a door at yaw 0 ...
const DataTable<InteractionBoxRecord> doorTalkBoxYawQuarter{TableId::DoorActorTalkBoxYawQuarter}; // ... at a quarter turn
const DataTable<InteractionBoxRecord> doorTalkBoxYawHalf{TableId::DoorActorTalkBoxYawHalf}; // ... at a half turn
const DataTable<InteractionBoxRecord> doorTalkBoxYawOther{TableId::DoorActorTalkBoxYawOther}; // ... at any other yaw

// exe 0x59ce50's body: the state, then a stand-on solid every tick
void doorUpdate(StaticActor& actor) {
    const DoorFields door(actor);
    actor.prevPos = actor.pos;
    actor.wasAirborne = actor.airborne;
    actor.prevRotation = actor.rotation;
    actor.wasOnPlatform = actor.onPlatform;
    switch (DoorState(actor.state)) {
    case DoorState::Init: {
        if (!bindActorModel(actor, actor.variant)) { // exe 0x5e1540: freed
            freeActor(actor);
            return;
        }
        const int16_t yaw = actor.rotation.y;
        actor.airborne = 0;
        actor.onPlatform = 0;
        actor.lastMover = nullptr;
        actor.collisionBox.pointAt(doorCollisionBox.stateAddress());
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        actor.velocity.z = 0;
        actor.velocity.y = 0;
        actor.velocity.x = 0;
        actor.interactionBox.pointAt((yaw == 0              ? doorTalkBoxYaw0
                                      : yaw == kQuarterTurn ? doorTalkBoxYawQuarter
                                      : yaw == kHalfTurn    ? doorTalkBoxYawHalf
                                                            : doorTalkBoxYawOther)
                                         .stateAddress());
        actor.interaction = kInteractTalk;
        actor.enterState(uint8_t(DoorState::Closed));
        [[fallthrough]];
    }
    case DoorState::Closed: {
        const uint32_t facing = (uint16_t(player().rotation.y) + kFacingBias) & kAngleMask;
        const uint32_t back = (uint16_t(actor.rotation.y) - uint32_t(kHalfTurn)) & kAngleMask;
        int d = int(facing) - int(back);
        if (d < 0) d = int(back) - int(facing);
        if (d < kFacingWindow && player().talksTo(actor) && eventState().active == 0) {
            const bool locked = storyFlag(uint32_t(int32_t(door.lock) + int32_t(kFlagLocked)));
            const int8_t message = locked ? door.lockedMessage : door.message;
            eventStartAt(0, nullptr, 0, 0, uint16_t(int16_t(message))); // exe 0x543b40
            actor.state = uint8_t(DoorState::Talk);
        }
        break;
    }
    case DoorState::Talk:
        if (eventState().active != 0) break;
        if (storyFlag(kFlagDoorYes)) {
            transitionRequest().kind = uint8_t(TransitionKind::DoorActor);
            setStoryFlag(kFlagTransitionArmed);
            clearStoryFlag(kFlagDoorYes);
        }
        actor.state = uint8_t(DoorState::Closed);
        actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0);
        return;
    default: break;
    }
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0);
}

} // namespace

void doorActorUpdate(const PlayerEnv&, uint32_t address) { doorUpdate(staticActorAt(address)); }

} // namespace od::port
