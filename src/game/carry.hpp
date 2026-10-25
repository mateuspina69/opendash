// The lift / carry system, actor side. The player side is in game/player/player_interaction: the lift query (exe 0x57b3b0
// mode 1: the actors with the lift interaction, the triangle button with no special weapon equipped) sets the player's
// interaction target to the actor in lift mode; the actor's own update sees it (playerLiftTargetCheck, exe 0x573a60 ->
// player sub-state 16, the lift exe 0x578b10) and goes to its "carried" state, which every tick places it at the player's
// hands (carryAtHands) until carryReleased says it was let go (thrown: the throw exe 0x573870 cleared the target;
// dropped: the player hurt / knocked down, or airborne while still lifting). Also the riders: an actor standing on
// another (+0x52 / +0x54) is carried by it (game/actor_collision actorCarry, the rider follower below) until the platform
// detaches its riders. The classes you lift: game/lift_box, game/bomb, ...
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"
#include "game/actor_movement.hpp"
#include "game/field_alias.hpp"

namespace od::port {

// carryReleased's answer
enum class CarryRelease : int {
    Held = 0,    // still at the player's hands
    Thrown = 1,  // the throw cleared the player's interaction target
    Dropped = 2, // the player was hurt / knocked down, or is airborne while still lifting
};

// playerLiftTargetCheck's mode that only asks (the player's lift is not started)
constexpr uint8_t kLiftQueryOnly = 0x80;

// exe 0x573a60(actor, mode): the player is not airborne and its interaction target is this actor in lift mode ->
// true; unless mode is kLiftQueryOnly the player's lift starts (sub-state 16, step 0, its lift mode = mode)
bool playerLiftTargetCheck(const Actor& actor, uint8_t mode);
// exe 0x573ab0(actor, x, y, z): the carried actor at the player's hands -- the midpoint of the player's bone points 4
// and 7 (<< 15 - the stage shift), its box's maxY down, y up; while the player still lifts (sub-state 16) the x / z
// offsets rotated by the player's yaw shrink with the lift animation's step; its yaw = the player's
void carryAtHands(Actor& actor, int x, int y, int z);
// exe 0x573950: held, thrown (the interaction target cleared) or dropped (player sub-state 14 / 15, or 16 while
// airborne)
CarryRelease carryReleased();
// exe 0x573990(actor): the previous position (x / z) of a released actor put at the player's (pushed out of the
// player's box when the actor's box is wider), its previous y = its y
void carryDropFromPlayer(Actor& actor);
// exe 0x414e70(platform): nobody stands on the platform any more (the player, the field's animated actors and the
// statics stop riding it)
void actorDetachRiders(const ActorHeader& platform);
// exe 0x59beb0(actor): the player stands on the actor (it is on a platform whose address matches the actor's in the
// exe's low 21 bits)
bool playerRides(const ActorHeader& actor);

// The rider follower (controller pool type 0x14, exe 0x4250d0): the actor of a slot, while it is the same one (type,
// stage-class flag), follows the platform it stands on (actorCarry); spawned by the classes you stand things on
struct RiderFollowerFields : ActorClassState {
    explicit RiderFollowerFields(ControllerActor& actor) : riderSlot(actor.params[0]), riderType(actor.params[1]), riderLoopFlags(actor.params[2]) {}
    FieldAlias<int8_t, uint8_t> riderSlot; // the slot id of the actor that rides (read as a signed byte)
    FieldAlias<int8_t, uint8_t> riderType; // its type: the follower ends when the slot holds another one
    uint8_t& riderLoopFlags; // its +3 at the spawn (& kActorStageClass)
    uint32_t rider = 0; // the rider's address (the slot arithmetic's, any value)
    uint8_t riderStageClass = 0;
};
constexpr uint8_t kRiderFollowerType = 0x14;

// exe 0x4250d0: the rider follower's update (ActorUpdateFn)
void riderFollowUpdate(const PlayerEnv& env, uint32_t actor);

} // namespace od::port
