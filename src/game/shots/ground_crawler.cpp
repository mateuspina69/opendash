// The ground crawler (weapon 7, pool 0x10 type 6, exe 0x56df60): falls from the hand with its speed split by the
// player's aim pitch, then rides the ground homing on its target (its turn rate growing): a step up is climbed, a wall
// or a pit edge stops it; a blast where it ends.
#include "engine/data_source.hpp"
#include "game/geometry_scratch.hpp"
#include "game/shots/ground_crawler_state.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/shots/player_shots.hpp"

#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/collision.hpp"
#include "game/fixed_point.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/shots/shot_common.hpp"

namespace od::port {

using namespace ground_crawler_state;

namespace {

inline constexpr const char* kCrawlerArea = "ground crawler";

// [0x8c49e4]
enum class CrawlerState : uint8_t { Spawn = 0, Crawl = 1, Free = 2 }; // exe 0x56e010 / 0x56e2e0 / 0x56dea0
enum class CrawlerEnd { Crawling = 0, HitBody = 1, Stopped = 2, OutOfRange = 3, Gone = -1 };

const DataTable<CollisionBoxRecord> crawlerBox{TableId::GroundCrawlerBox};
constexpr int16_t kCrawlerScale = 0x300;
constexpr uint16_t kCrawlerSound = 0xfa;
constexpr int kLaunchSpeed = 0x400; // split by the aim pitch (<< 10 >> 12)
constexpr int16_t kCrawlerGravity = 0x30;
constexpr int16_t kCrawlerHop = -0x180; // the hop up a wall it may climb
constexpr int16_t kLandSpeed = 0x100;   // a fall slower than this lands
constexpr uint16_t kMaxTurnRate = 0x30;
constexpr uint32_t kTilt = 0x200;                               // the drawn pitch
constexpr uint32_t kActorWalls = 0x3f00;                        // the actor collision's wall / floor bits
constexpr uint32_t kMoveOffMap = kMoveOutsideMap | kMoveNoCell; // the test move left the grid / found no cell
constexpr uint32_t kMoveStopping = 0x6000; // the test move's bits 13..14 (hypothesis: a surface it cannot ride)

// the shape codes the crawler stopped by a wall still bounces off (a step it climbs otherwise): below 0x10 and the
// ranges 0x1b..0x22, 0x27..0x32, 0x37..0x42, 0x47..0x52 (the collision volumes' ramp / stair codes)
bool climbsOver(uint32_t shape) {
    if (shape < 0x10) return false;
    if (shape > 0x1a && shape < 0x23) return false;
    if (shape > 0x26 && shape < 0x33) return false;
    if (shape > 0x36 && shape < 0x43) return false;
    if (shape > 0x46 && shape < 0x53) return false;
    return true;
}

CrawlerEnd spawn(const PlayerEnv& env, ShotActor& shot, CrawlerFields& fields) { // exe 0x56e010
    const Player& p = player();
    shot.flags |= kActorDrawn | kActorMeshModel;
    shot.runFlags |= kActorShown | kActorMeshModel | kActorWarpKept;
    if (!bindActorModel(shot, 0)) {
        freeActor(shot);
        return CrawlerEnd::Gone;
    }
    aimRelativeToTarget(shot);
    PlayerShotFields(shot).hitBox.result = 0;
    const int pitch = uint16_t(p.rotation.x) & kAngleMask;
    int z = -((fixedCos(pitch) * kLaunchSpeed) >> 12);
    if (p.onPlatform != 0) {
        int x;
        platformMove(x, z);
        z -= (fixedCos(pitch) * kLaunchSpeed) >> 12;
    }
    shot.velocity.z = wrap16(z);
    shot.velocity.x = 0;
    shot.velocity.y = wrap16((fixedSin(pitch) * kLaunchSpeed) >> 12);
    placeAtBone(shot, kLeftHandBone, 0);
    shot.rotation.x = 0;
    shot.rotation.y = p.rotation.y;
    shot.scale.x = kCrawlerScale;
    shot.scale.y = kCrawlerScale;
    shot.scale.z = kCrawlerScale;
    shot.rotation.z = 0;
    moveAlongRotation(shot.prevPos, shot.rotation, 0, -int(shot.velocity.y) * 2, -int(shot.velocity.z) * 2);
    shot.collisionBox.pointAt(crawlerBox.stateAddress());
    shot.actorHit = 0;
    soundPlayAtPos(kCrawlerSound, shot.pos);
    fillShotStats(fields.stats);
    paySpecialEnergy(player());
    fields.turnRate = 0;
    fields.turnStep = uint16_t(weaponLevel(p, p.activeSpecial, WeaponStat::Extra) + 1);
    ++shot.state;
    if (shot.onPlatform == 0) {
        if (actorTestMove(env, shot) != 0) {
            shot.pos = shot.prevPos;
            shot.velocity.y = 0;
        } else {
            shot.velocity.y = wrap16(shot.velocity.y + kCrawlerGravity);
        }
    }
    return CrawlerEnd((actorPushOut(env, shot) >> 12) & 2);
}

CrawlerEnd crawl(const PlayerEnv& env, ShotActor& shot, CrawlerFields& fields) { // exe 0x56e2e0
    auto&& shotFields = PlayerShotFields(shot);
    if (shotFields.hitBox.result & kHitAttackSolid) {
        stopHalfway(shot);
        return CrawlerEnd::HitBody;
    }
    if (shot.onPlatform == 0 && (shot.actorHit & kActorWalls)) return CrawlerEnd::Stopped;
    shot.savePreviousPose();
    shot.rotation.x = 0;
    fields.turnRate = uint16_t(fields.turnRate + fields.turnStep);
    if (fields.turnRate > kMaxTurnRate) fields.turnRate = kMaxTurnRate;
    if (shotFields.target != nullptr) homeOnTarget(shot, slotIdentityWord(shotFields.target), fields.turnRate >> 1, kHomingAlwaysTurn);
    CrawlerEnd end = CrawlerEnd::OutOfRange;
    --fields.stats.range;
    if (fields.stats.range != 0) {
        moveShot(shot, 0, shot.velocity.y, shot.velocity.z);
        if (shot.onPlatform == 0) {
            const uint32_t r = actorTestMove(env, shot);
            if (int32_t(r) >= 0) {
                if (r & kMoveOffMap) return CrawlerEnd::OutOfRange;
                if (r & kMoveStopping) return CrawlerEnd::Stopped;
                shot.velocity.y = wrap16(shot.velocity.y + kCrawlerGravity);
            } else {
                const uint32_t shape = uint8_t(geometryScratch().collision.hitShape);
                const int32_t px = shot.pos.x, py = shot.pos.y, pz = shot.pos.z;
                const uint32_t pushed = actorPushOut(env, shot); // only its result: the position is kept
                shot.pos.x = px;
                shot.pos.y = py;
                shot.pos.z = pz;
                const bool floor = (pushed & kWallOnYPlus) != 0;
                if (!floor && !climbsOver(shape) && (pushed & kWallOnSides)) { // a wall: back, a hop up
                    shot.pos.x = shot.prevPos.x;
                    shot.pos.z = shot.prevPos.z;
                    shot.velocity.y = kCrawlerHop;
                    shot.prevPos.y = shot.pos.y;
                } else if (floor && shot.velocity.y > 0 && shot.velocity.y < kLandSpeed) { // landed
                    shot.velocity.y = 0;
                    shot.prevPos.y = shot.pos.y;
                } else {
                    if (floor && shot.velocity.y > 0) shot.velocity.y = wrap16(-(shot.velocity.y >> 1)); // a bounce
                    shot.prevPos.y = shot.pos.y;
                }
            }
        }
        if (actorPushOut(env, shot) & kWallOnYMinus) {
            shot.rotation.x = int16_t((uint16_t(shot.prevRotation.x) + kTilt) & kAngleMask);
            return CrawlerEnd::Stopped;
        }
        markFlying(shot);
        end = CrawlerEnd::Crawling;
    }
    shot.rotation.x = int16_t((uint16_t(shot.prevRotation.x) + kTilt) & kAngleMask);
    return end;
}

// exe 0x56e550: the hit entry at the shot (radius 8)
void submitCrawlerHit(ShotActor& shot, const ShotStats& stats) {
    submitShotHit(shot, hitGroup(kHitGroupPlayerAttacks) | kHitContactPoint | hitRadius(0x20),
                  stats.attack | kHitAttackByPlayer);
}

} // namespace

void groundCrawlerShotUpdate(const PlayerEnv& env, uint32_t address) {
    ShotActor& shot = shotActorAt(address);
    auto&& fields = classState<CrawlerFields>(shot);
    dropDeadTarget(shot);
    CrawlerEnd end;
    switch (CrawlerState(shot.state)) {
    case CrawlerState::Spawn: end = spawn(env, shot, fields); break;
    case CrawlerState::Crawl: end = crawl(env, shot, fields); break;
    case CrawlerState::Free: freeActor(shot); return; // exe 0x56dea0
    default: notPortedAt(kCrawlerArea, "0x56df60 state", shot.state);
    }
    switch (end) {
    case CrawlerEnd::Crawling:
        // exe 0x5f20f0(shot, 0x30, 0x100, 0): a shadow list entry (render output)
        actorCollisionSubmitMover(shot);
        submitCrawlerHit(shot, fields.stats);
        return;
    case CrawlerEnd::HitBody:
    case CrawlerEnd::OutOfRange:
        spawnShotBlast(shot, fields.stats.level);
        ++shot.state;
        return;
    case CrawlerEnd::Stopped:
        spawnShotBlast(shot, fields.stats.level);
        submitCrawlerHit(shot, fields.stats);
        ++shot.state;
        return;
    default: return;
    }
}

} // namespace od::port
