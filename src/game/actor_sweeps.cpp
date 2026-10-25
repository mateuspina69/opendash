// The sweeps over every actor pool (see actor_sweeps.hpp).
#include "game/actor_state.hpp"
#include "game/actor_sweeps.hpp"

#include "game/actor_pools.hpp"
#include "game/carry.hpp"
#include "game/player/player_state.hpp"
#include "game/sky_layers.hpp"

namespace od::port {

namespace {

// a slot's group (loop flags & 3: kActorSweepGroup1 | kActorSweepGroup2)
constexpr uint8_t kGroupMask = kActorSweepGroup1 | kActorSweepGroup2;

// The s16 point exe 0x5e0b80 moves in a controller's slot (a tail of the slot: the controller classes keep a point
// there)
struct ControllerPoint {
    explicit ControllerPoint(const ControllerActor& actor) : ControllerPoint(const_cast<ControllerActor&>(actor)) {}
    explicit ControllerPoint(ControllerActor& actor) : x(actor.recordPoint.x), z(actor.recordPoint.z) {}
    int16_t& x;
    int16_t& z;
};

// a slot of a live mask word (bit 31 - index) is live
bool liveIn(uint32_t mask, uint32_t index) { return int32_t(mask << index) < 0; }

// exe 0x5e0890(actor, op): the run / shown flags changed by op
void changeRunFlags(ActorHeader& actor, uint8_t op) {
    if (op & kSweepToggleRun) actor.runFlags ^= kActorRun;
    if (op & kSweepToggleShown) actor.runFlags ^= kActorShown;
    if (op & kSweepSetRun) actor.runFlags |= kActorRun;
    if (op & kSweepSetShown) actor.runFlags |= kActorShown;
}

// exe 0x5e0b30: a model-bearing actor (and its previous position) moved by (dx, -, dz)
void shiftActor(Actor& actor, int16_t dx, int16_t dz) {
    const uint32_t ox = uint32_t(int32_t(dx)) << 16, oz = uint32_t(int32_t(dz)) << 16;
    actor.pos.x += ox;
    actor.pos.z += oz;
    actor.prevPos.x += ox;
    actor.prevPos.z += oz;
}

} // namespace

void actorsSweep(uint32_t classes, uint8_t op) {
    const bool free = (op & kSweepFree) != 0;
    ActorSystemState& s = actorSystem();
    if (classes & kSweepPlayer) {
        if (!free) changeRunFlags(player(), op);
        else player().flags = 0;
    }
    if (classes & (kSweepAnimatedGroup0 | kSweepAnimatedGroup2)) {
        const uint32_t mask = s.animatedLive;
        for (uint32_t i = 0; i < kAnimatedFieldSlots; ++i) {
            if (!liveIn(mask, i)) continue;
            AnimatedActor& a = animatedPool()[i];
            const uint8_t group = a.loopFlags & kGroupMask;
            if (((classes & kSweepAnimatedGroup0) && group == 0) || ((classes & kSweepAnimatedGroup2) && group == 2)) {
                if (!free) changeRunFlags(a, op);
                else {
                    actorDetachRiders(a);
                    freeActor(a);
                }
            }
        }
    }
    if (classes & kSweepShots) {
        const uint32_t mask = s.shotsLive;
        for (uint32_t i = 0; i < ShotPool::size(); ++i) {
            if (!liveIn(mask, i)) continue;
            ShotActor& a = shotPool()[i];
            if (!free) changeRunFlags(a, op);
            else {
                actorDetachRiders(a);
                freeActor(a);
            }
        }
    }
    if (classes & kSweepProjectiles) {
        const uint32_t mask = s.projectilesLive;
        for (uint32_t i = 0; i < ProjectilePool::size(); ++i) {
            if (!liveIn(mask, i)) continue;
            ProjectileActor& a = projectilePool()[i];
            if (!free) changeRunFlags(a, op);
            else {
                actorDetachRiders(a);
                freeActor(a);
            }
        }
    }
    if (classes & (kSweepStaticsGroup0 | kSweepStaticsGroup1 | kSweepStaticsGroup2)) {
        for (uint32_t w = 0; w < 2; ++w) {
            const uint32_t mask = s.staticsLive[w];
            for (uint32_t i = 0; i < 32; ++i) {
                if (!liveIn(mask, i)) continue;
                StaticActor& a = staticPool()[w * 32 + i];
                const uint8_t group = a.loopFlags & kGroupMask;
                const bool picked = ((classes & kSweepStaticsGroup0) && group == 0) ||
                                    ((classes & kSweepStaticsGroup1) && group == 1) ||
                                    ((classes & kSweepStaticsGroup2) && group == 2);
                if (picked) {
                    if (!free) changeRunFlags(a, op);
                    else {
                        actorDetachRiders(a);
                        freeActor(a);
                    }
                }
            }
        }
    }
    if (classes & (kSweepEffectsGroup0 | kSweepEffectsGroup1 | kSweepEffectsGroup2)) {
        for (uint32_t w = 0; w < 2; ++w) {
            const uint32_t mask = s.effectsLive[w];
            for (uint32_t i = 0; i < 32; ++i) {
                if (!liveIn(mask, i)) continue;
                EffectActor& a = effectPool()[w * 32 + i];
                const uint8_t group = a.loopFlags & kGroupMask;
                const bool picked = ((classes & kSweepEffectsGroup0) && group == 0) ||
                                    ((classes & kSweepEffectsGroup1) && group == 1) ||
                                    ((classes & kSweepEffectsGroup2) && group == 2);
                if (picked) {
                    if (!free) changeRunFlags(a, op);
                    else freeActor(a);
                }
            }
        }
    }
    if (classes & kSweepControllers) {
        const uint32_t mask = s.controllersLive;
        for (uint32_t i = 0; i < ControllerPool::size(); ++i) {
            if (!liveIn(mask, i)) continue;
            ControllerActor& a = controllerPool()[i];
            if (!free) changeRunFlags(a, op);
            else freeActor(a);
        }
    }
    if (classes & kSweepSkyLayers) {
        for (size_t i = 0; i < kSkyLayers; ++i) {
            auto&& layer = skyLayers()[i];
            uint8_t v = layer.flags;
            if ((v & kSkyLayerInUse) == 0) continue;
            if (!free) {
                if (op & kSweepToggleRun) v ^= kSkyLayerRun;
                if (op & kSweepToggleShown) v ^= kSkyLayerShown;
                if (op & kSweepSetRun) v |= kSkyLayerRun;
                if (op & kSweepSetShown) v |= kSkyLayerShown;
            } else {
                v = 0;
            }
            layer.flags = v;
        }
    }
}

void actorsWarp(uint32_t classes, const int16_t* offset) {
    ActorSystemState& s = actorSystem();
    // the warp of one actor: kept ones move (when there is an offset), the others are freed
    auto warpModel = [&](auto& actor) {
        if (!(actor.runFlags & kActorWarpKept)) freeActor(actor);
        else if (offset) shiftActor(actor, offset[0], offset[2]);
    };
    if (classes & (kSweepAnimatedGroup0 | kSweepAnimatedGroup2)) {
        const uint32_t mask = s.animatedLive;
        for (uint32_t i = 0; i < kAnimatedFieldSlots; ++i) {
            if (!liveIn(mask, i)) continue;
            AnimatedActor& a = animatedPool()[i];
            const uint8_t group = a.loopFlags & kGroupMask;
            if (((classes & kSweepAnimatedGroup0) && group == 0) || ((classes & kSweepAnimatedGroup2) && group == 2))
                warpModel(a);
        }
    }
    if (classes & kSweepShots) {
        const uint32_t mask = s.shotsLive;
        for (uint32_t i = 0; i < ShotPool::size(); ++i)
            if (liveIn(mask, i)) warpModel(shotPool()[i]);
    }
    if (classes & kSweepProjectiles) {
        const uint32_t mask = s.projectilesLive;
        for (uint32_t i = 0; i < ProjectilePool::size(); ++i)
            if (liveIn(mask, i)) warpModel(projectilePool()[i]);
    }
    // the exe's class tests for the next two pools are inverted ("not (bit and class)" chains): an actor is skipped
    // only when every requested class bit excludes it
    if (classes & (kSweepStaticsGroup0 | kSweepStaticsGroup1 | kSweepStaticsGroup2)) {
        for (uint32_t w = 0; w < 2; ++w) {
            const uint32_t mask = s.staticsLive[w];
            for (uint32_t i = 0; i < 32; ++i) {
                if (!liveIn(mask, i)) continue;
                StaticActor& a = staticPool()[w * 32 + i];
                const uint8_t group = a.loopFlags & kGroupMask;
                if (!(classes & kSweepStaticsGroup0) || group == 0 || !(classes & kSweepStaticsGroup1) || group == 1 ||
                    !(classes & kSweepStaticsGroup2) || group == 2)
                    warpModel(a);
            }
        }
    }
    if (classes & (kSweepEffectsGroup0 | kSweepEffectsGroup1 | kSweepEffectsGroup2)) {
        for (uint32_t w = 0; w < 2; ++w) {
            const uint32_t mask = s.effectsLive[w];
            for (uint32_t i = 0; i < 32; ++i) {
                if (!liveIn(mask, i)) continue;
                EffectActor& a = effectPool()[w * 32 + i];
                const uint8_t group = a.loopFlags & kGroupMask;
                if (!(classes & kSweepEffectsGroup0) || group == 0 || !(classes & kSweepEffectsGroup1) || group == 1 ||
                    !(classes & kSweepEffectsGroup2) || group == 2) {
                    if (!(a.runFlags & kActorWarpKept)) freeActor(a);
                    else if (offset) { // exe 0x5e0b80
                        a.pos.x = int16_t(a.pos.x + offset[0]);
                        a.pos.z = int16_t(a.pos.z + offset[2]);
                    }
                }
            }
        }
    }
    if (classes & kSweepControllers) {
        const uint32_t mask = s.controllersLive;
        for (uint32_t i = 0; i < ControllerPool::size(); ++i) {
            if (!liveIn(mask, i)) continue;
            ControllerActor& a = controllerPool()[i];
            if (!(a.runFlags & kActorWarpKept)) freeActor(a);
            else if (offset) { // exe 0x5e0b80
                ControllerPoint point(a);
                point.x = int16_t(point.x + offset[0]);
                point.z = int16_t(point.z + offset[2]);
            }
        }
    }
}

void killAllActors() {
    actorsSweep(kSweepAllButPlayer, kSweepFree);
    applyActorFrees();
}

} // namespace od::port
