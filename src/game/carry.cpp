// The lift / carry system, actor side: see carry.hpp.
#include "game/carry.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "game/actor_collision.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera_state.hpp"
#include "game/encounter.hpp"
#include "game/hud.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/shop.hpp"
#include "game/sky_layers.hpp"
#include "game/stage_state.hpp"
#include "game/state_array.hpp"
#include "game/title.hpp"

namespace od::port {

namespace {

// the player's sub-states the carry reads
constexpr int kLiftSteps = 0xc;        // the lift animation's steps (the hands' offset shrinks over them)
constexpr uint32_t kLeftHand = 4, kRightHand = 7; // the player's bone points the actor is held between

// exe 0x414f00(platform, rider): the rider stops standing on the platform
void stopRiding(const ActorHeader& platform, Actor& rider) {
    if (rider.onPlatform != 0 && rider.platform.get() == &platform) {
        rider.onPlatform = 0;
        rider.platform = nullptr;
    }
}

} // namespace

bool playerLiftTargetCheck(const Actor& actor, uint8_t mode) { // exe 0x573a60
    Player& p = player();
    if (p.airborne != 0 || !p.interactionTarget.is(actor, kInteractLiftMode)) return false;
    if (mode != kLiftQueryOnly) {
        p.liftMode = mode;
        p.subState = uint8_t(PlayerSub::Lift);
        p.enterStep(0);
    }
    return true;
}

void carryAtHands(Actor& actor, int x, int y, int z) { // exe 0x573ab0
    const Player& p = player();
    const int yaw = uint16_t(p.rotation.y) & kAngleMask;
    const int sn = fixedSin(yaw), c = fixedCos(yaw);
    int32_t ox = 0, oz = 0;
    if (p.sub() == PlayerSub::Lift) {
        const int k = kLiftSteps - p.body.key.frame;
        ox = int32_t(uint32_t((int32_t(uint32_t(c * x + sn * z) * uint32_t(k))) / kLiftSteps) << 4);
        // the exe's "sn + x"
        oz = int32_t(uint32_t((int32_t(uint32_t(c * z + sn + x) * uint32_t(k))) / kLiftSteps) << 4);
    }
    const uint32_t sh = (0xfu - stage().coordinateShift) & 0x1f;
    const ShortVec3& left = p.bonePoints[kLeftHand];
    const ShortVec3& right = p.bonePoints[kRightHand];
    actor.pos.x = int32_t((uint32_t(int32_t(left.x) + right.x) << sh) + uint32_t(ox));
    actor.pos.y = int32_t((uint32_t(int32_t(left.y) + right.y) << sh) -
                          (uint32_t(int32_t(actor.collisionBox->maxY)) << 15) + (uint32_t(y) << 16));
    actor.pos.z = int32_t((uint32_t(int32_t(left.z) + right.z) << sh) + uint32_t(oz));
    actor.rotation.y = int16_t(uint16_t(uint16_t(p.rotation.y) - uint16_t(p.directionOffset.y)));
}

CarryRelease carryReleased() { // exe 0x573950
    const Player& p = player();
    const PlayerSub sub = p.sub();
    if (sub == PlayerSub::KnockBack || sub == PlayerSub::KnockDown || (sub == PlayerSub::Lift && p.airborne != 0))
        return CarryRelease::Dropped;
    return p.interactionTarget.none() ? CarryRelease::Thrown : CarryRelease::Held;
}

void carryDropFromPlayer(Actor& actor) { // exe 0x573990
    const Player& p = player();
    const BoxAccess box = *actor.collisionBox, playerBox = *p.collisionBox;
    const int16_t bx = box.maxX, px = playerBox.maxX;
    if (px >= bx && playerBox.maxZ >= box.maxZ) {
        actor.prevPos.x = p.pos.x;
        actor.prevPos.z = p.pos.z;
        actor.prevPos.y = actor.pos.y;
        return;
    }
    const int sx = (uint16_t(p.rotation.y) & kAngleMask) < kHalfTurn ? 1 : -1;
    actor.prevPos.x = int32_t((uint32_t(sx * (int(bx) - int(px))) << 16) + uint32_t(p.pos.x));
    const int sz = ((uint16_t(p.rotation.y) - uint32_t(kQuarterTurn)) & kAngleMask) < uint32_t(kHalfTurn) ? 1 : -1;
    actor.prevPos.z =
        int32_t((uint32_t((int(playerBox.maxZ) - int(box.maxZ)) * sz) << 16) + uint32_t(p.pos.z));
    actor.prevPos.y = actor.pos.y;
}

bool playerRides(const ActorHeader& actor) {
    const Player& p = player();
    return p.onPlatform != 0 && p.platform.get() == &actor;
}

void actorDetachRiders(const ActorHeader& platform) { // exe 0x414e70
    stopRiding(platform, player());
    ActorSystemState& s = actorSystem();
    for (uint32_t i = 0; i < kAnimatedFieldSlots; ++i)
        if (int32_t(s.animatedLive << i) < 0) stopRiding(platform, animatedPool()[i]);
    // the statics: the exe walks 0x40 slots per mask word testing bit (i & 0x1f), the first word over the whole pool,
    // the second from two slots past its end; the port walks the pool's slots only (the bytes past it are no actor's:
    // the walk past the pool is not reproduced)
    const uint32_t mask = s.staticsLive[0];
    for (uint32_t i = 0; i < StaticPool::size(); ++i)
        if (int32_t(mask << (i & 0x1f)) < 0) stopRiding(platform, staticPool()[i]);
}

void riderFollowUpdate(const PlayerEnv&, uint32_t address) { // exe 0x4250d0
    ControllerActor& follower = controllerActorAt(address);
    RiderFollowerFields& f = classState<RiderFollowerFields>(follower);
    uint32_t riderAddress = f.rider;
    if (follower.state == 0) {
        riderAddress = actorBySlot(uint32_t(int32_t(f.riderSlot)));
        f.rider = riderAddress;
        f.riderStageClass = f.riderLoopFlags & kActorStageClass;
        ++follower.state;
    }
    // the rider and its platform wherever the slot arithmetic and the platform pointer point (the exe's reads)
    auto&& rider = actorLayoutAt(riderAddress);
    if (!(rider.flags & kActorLive) || uint32_t(rider.type) != uint32_t(int32_t(f.riderType)) ||
        (rider.loopFlags & kActorStageClass) != f.riderStageClass) {
        freeActor(follower); // exe 0x5e15c0
    } else if (rider.onPlatform != 0) {
        actorCarry(rider, *rider.platform);
    }
}

} // namespace od::port
