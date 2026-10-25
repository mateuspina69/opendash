// The lock-on candidates of the hit pass (see lock_on_targets.hpp).
#include "game/lock_on_targets.hpp"

#include "game/actor_pools.hpp"
#include "game/game_state.hpp"
#include "game/geometry_scratch.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/projection.hpp"
#include "game/projection_state.hpp"

namespace od::port {

namespace {

// The selection's share of the geometry scratch (game/geometry_scratch: +0x60..+0x87, over the room collision
// queries' state and the carried point): the candidate's screen point, its point (s16 x / y / z and the entry's shape
// word, the 8 bytes the exe copies whole) and the points of the three best candidates
struct LockOnScratch {
    Lane16 screenX = geometryScratch().lane(0x60);
    Lane16 screenY = geometryScratch().lane(0x62);
    LaneVec4 point = geometryScratch().vec4(0x68);           // the candidate's point (a capsule's: the midpoint of its ends)
    LaneVec4 nearestOnScreen = geometryScratch().vec4(0x70); // the nearest candidate on the screen
    LaneVec4 nearestInReach = geometryScratch().vec4(0x78);  // the nearest one within kNearDistanceSq
    LaneVec4 nearestToCentre = geometryScratch().vec4(0x80); // the one nearest to the screen's aim centre
};
// the scratch's one instance
LockOnScratch& scratch() {
    static LockOnScratch s;
    return s;
}

constexpr uint32_t kNearDistanceSq = 0x9c4000;   // a candidate this close (squared, x / z) is "near" (the HUD's radar)
constexpr uint32_t kReachDistanceSq = 0xa900000; // the candidates farther than this are not targets
constexpr uint16_t kScreenWidth = 0x140, kScreenHeight = 0xf0;
constexpr uint8_t kLockHoldTicks = 15; // OpenDash (modern aim): a locked target sending no point keeps the lock this long
constexpr int kAimCentreX = 0xa0, kAimCentreY = 0x3c; // the screen point the centre candidate is measured from
constexpr int8_t kLockSoundDelay = 8;                 // the lock-on sound's countdown, set with every new candidate
constexpr int kNone = -1;

} // namespace

void selectLockOnTargets(uint32_t count, uint32_t groupMask) {
    ActorSystemState& sys = actorSystem();
    LockOnScratch& s = scratch();
    Player& p = player();
    const int px = p.pos.x.whole, pz = p.pos.z.whole;
    uint32_t minAll = 0xffffffff, minScreen = 0xffffffff, minNear = 0xffffffff, minVis = 0xffffffff;
    uint32_t screenSlot = 0;
    int closest = kNone, nearest = kNone, visible = kNone;
    bool anyNear = false;
    loadCameraProjection();
    const uint32_t remembered = p.lockRemembered;
    uint32_t candidates = 0;
    const bool locked = p.lockedActor != nullptr;
    const uint32_t groups = groupMask >> kHitGroupShift;
    // the candidate's point from entry i (a capsule: its midpoint, the entry its second half); returns that entry
    auto point = [&](uint32_t i) -> uint32_t {
        const HitEntry& e = sys.hitList[i];
        if ((uint16_t(e.shape()) & kHitCapsuleMask) == 0) {
            s.point.setWords(e.pos.xyWord(), e.pos.zSpareWord());
            return i;
        }
        const HitEntry& end = sys.hitList[i + 1];
        s.point.x = int16_t((int(e.pos.x) + end.pos.x) >> 1);
        s.point.y = int16_t((int(end.pos.y) + e.pos.y) >> 1);
        s.point.z = int16_t((int(end.pos.z) + e.pos.z) >> 1);
        --count;
        return i + 1;
    };
    auto projectPoint = [&]() {
        const int16_t v[3] = {s.point.x, s.point.y, s.point.z};
        const Projected r = projectThrough(projection().camera, v);
        s.screenX = r.sx;
        s.screenY = r.sy;
        return r;
    };
    auto screenDistance = [&]() {
        const int sx = s.screenX - kAimCentreX, sy = s.screenY - kAimCentreY;
        return uint32_t(sy * sy + sx * sx);
    };
    auto inScreenX = [&]() { return uint16_t(s.screenX) < kScreenWidth; };
    auto onScreen = [&]() { return uint16_t(s.screenX) < kScreenWidth && uint16_t(s.screenY) < kScreenHeight; };
    bool found = false;
    if (count == 0) {
        if (!locked) p.homingTarget = 0;
    } else {
        uint32_t i = 0;
        while (count != 0) {
            --count;
            bool nearFlag = false;
            i = point(i);
            const HitEntry& e = sys.hitList[i];
            const uint32_t owner = e.owner;
            const uint32_t slot = owner >> kHitSlotShift;
            if (hitGroupPairs((owner >> kHitGroupShift) & groups) & kHitGroupLockOn) {
                const int dx = s.point.x - px, dz = s.point.z - pz;
                const uint32_t d = uint32_t(dz * dz) + uint32_t(dx * dx);
                if (d < minAll) {
                    minAll = d;
                    closest = int(i);
                }
                if (d < kNearDistanceSq && !(e.attack & kHitAttackHarmless)) nearFlag = true;
                if (!locked) {
                    if (owner & kHitNoLockOn) {
                        const Projected r = projectPoint();
                        if (!r.clipped && inScreenX()) nearFlag = false;
                    } else if (d < kReachDistanceSq) {
                        const Projected r = projectPoint();
                        bool counted = false;
                        if (!r.clipped && inScreenX()) {
                            if (d < minVis) {
                                ++candidates;
                                counted = true;
                                if (remembered != slot) {
                                    minVis = d;
                                    visible = int(i);
                                    s.nearestOnScreen = s.point;
                                    p.lockSoundTicks = kLockSoundDelay;
                                }
                            }
                            const uint32_t sd = screenDistance();
                            if (sd < minScreen) {
                                minScreen = sd;
                                screenSlot = slot;
                                s.nearestToCentre = s.point;
                            }
                            nearFlag = false;
                        }
                        if (d < kNearDistanceSq && d < minNear) {
                            if (!counted) ++candidates;
                            if (remembered != slot) {
                                minNear = d;
                                nearest = int(i);
                                s.nearestInReach = s.point;
                                p.lockSoundTicks = kLockSoundDelay;
                            }
                        }
                    }
                } else {
                    projectPoint();
                    if (owner & kHitNoLockOn) {
                        if (onScreen()) nearFlag = false;
                    } else {
                        if (slot == p.lockTarget) {
                            e.box->result |= kHitLockedTarget;
                            if (onScreen() && d < minVis) {
                                minVis = d;
                                visible = int(i);
                                s.nearestOnScreen = s.point;
                            }
                            if (d < minNear) {
                                minNear = d;
                                nearest = int(i);
                                s.nearestInReach = s.point;
                            }
                        }
                        if (onScreen()) {
                            const uint32_t sd = screenDistance();
                            if (sd < minScreen) {
                                screenSlot = slot;
                                minScreen = sd;
                                s.nearestToCentre = s.point;
                            }
                            nearFlag = false;
                        }
                    }
                }
            }
            anyNear |= nearFlag;
            ++i;
        }
        if (!locked && candidates < 2) p.homingTarget = 0;
        // exe 0x5e50b2: the target
        found = true;
        if (visible != kNone) {
            p.lockTarget = uint8_t(sys.hitList[uint32_t(visible)].owner >> kHitSlotShift);
            p.lockAimPoint = s.nearestOnScreen;
            p.lockHeldTicks = 0;
        } else if (nearest != kNone) {
            p.lockTarget = uint8_t(sys.hitList[uint32_t(nearest)].owner >> kHitSlotShift);
            p.lockAimPoint = s.nearestInReach;
            p.lockHeldTicks = 0;
        } else if (const ActorRef<ActorHeader> t = p.lockedActor;
                   t && (actorLayoutOf(*t).flags & kActorLive) && actorLayoutOf(*t).slot == remembered &&
                   p.lockHeldTicks < kLockHoldTicks) {
            // OpenDash (modern aim, not the exe): a locked target that is alive but submitted no lock-on point this
            // frame (a boss between phases, a part swapping its hit boxes) keeps the lock a moment -- aimed at the
            // actor's position -- instead of dropping it and turning Megaman / his shots away while the lock button
            // is held. A target that stays without a point is dropped after kLockHoldTicks, as the exe drops it at
            // once, and the lock goes to the next one; a target that is done (a fire put out) releases it at once
            // (releaseLockOnTarget)
            ++p.lockHeldTicks;
            const Actor& target = actorLayoutOf(*t);
            p.lockTarget = uint8_t(remembered);
            p.lockAimPoint.x = target.pos.x.whole;
            p.lockAimPoint.y = target.pos.y.whole;
            p.lockAimPoint.z = target.pos.z.whole;
        } else {
            found = false;
        }
        if (found && p.lockedActor != nullptr) {
            p.homingTarget = p.lockedActor;
            p.lockRemembered = p.lockTarget;
            p.softRemembered = p.softTarget;
        }
    }
    if (!found) {
        p.lockHeldTicks = 0;
        p.lockedActor = 0;
        p.clearTargetSlots();
    }
    p.softTarget = uint8_t(screenSlot);
    p.screenAimPoint = s.nearestToCentre;
    if (closest != kNone) sys.hitList[uint32_t(closest)].box->result |= kHitClosestCandidate;
    GameState& game = gameState();
    if (!anyNear)
        game.enemyNear = 0;
    else if (game.enemyNear == 0)
        game.enemyNear = 1;
}

void releaseLockOnTarget(uint8_t slot) {
    Player& p = player();
    if (const ActorRef<ActorHeader> t = p.lockedActor; !t || actorLayoutOf(*t).slot != slot) return;
    p.lockHeldTicks = 0;
    p.lockedActor = 0;
    p.clearTargetSlots();
}

} // namespace od::port
