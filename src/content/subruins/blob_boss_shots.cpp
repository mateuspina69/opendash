// The two shots of the ST2F / ST58 blob boss (see blob_boss_shots.hpp; hypothesis names from the behaviour).
// Render output: the falling shot's 0x28 billboard (word 0 unwritten) through dc::emit as the exe writes it; the lobbed
// blob's model is the generic pool-0x40 mesh draw (model variant 0, bound at its start).
#include "content/subruins/blob_boss_shots.hpp"
#include "content/subruins/blob_boss_shots_state.hpp"

#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/draw_commands.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace blob_boss_shots_state;

namespace {

// ---- the falling shot (exe 0x5c5d20) --------------------------------------------------------------------------------
// States [0x8d1258]: 0 0x5c5d40 start, 1 0x5c5d70 fall, 2 0x5c5e90 burst.
enum class FallingShotState : uint8_t { Start = 0, Fall = 1, Burst = 2 };
constexpr int16_t kFallingShotTicks = 0x64;

// exe 0x5c5ee0: the 0x28 billboard at the draw cursor (word 0 unwritten), small / big
void fallingShotBillboard(const ProjectileActor& shot) {
    const bool big = FallingShotParams(shot).big != 0;
    const uint32_t e = big ? 0x1fu : 0x17u, s = big ? 0x28u : 0x14u;
    dc::emit({0, 0x2c808080u,
              uint32_t(uint16_t(shot.pos.x.whole)) | uint32_t(uint16_t(shot.pos.y.whole)) << 16,
              big ? 0x3c388000u : 0x7cd18000u, uint32_t(uint16_t(shot.pos.z.whole)), e | 0xe8000u, s, (e + 0x80) << 8,
              0u, ((e + 0x80) << 8) | e},
             shot.at, false);
}
void fallingShotStart(ProjectileActor& shot, FallingShotFields& f) { // exe 0x5c5d40
    f.hit.result = 0;
    fallingShotBillboard(shot);
    f.ticks = kFallingShotTicks;
    shot.collisionBox.pointAt(TableId::BlobBossShotsFallingShotBox);
    shot.state = uint8_t(FallingShotState::Fall);
}
// exe 0x5c5d70: gravity for its ticks; a hit (0x50000 in its first ticks, then 0x70000) or a wall -> the burst
void fallingShotFall(const PlayerEnv& env, ProjectileActor& shot, FallingShotFields& f) {
    shot.savePreviousPose();
    --f.ticks;
    if (uint16_t(f.ticks) == 0) shot.state = uint8_t(FallingShotState::Burst);
    shot.velocity.y = int16_t(uint16_t(shot.velocity.y) + uint16_t(shot.acceleration.y));
    integrateVelocity(shot, 0, shot.velocity.y, shot.velocity.z); // exe 0x5e3690
    fallingShotBillboard(shot);
    const uint32_t h = f.hit.result;
    if (f.ticks > 0x5f) {
        if (h & (kHitAttackByPlayer | kHitAttackBump)) shot.state = uint8_t(FallingShotState::Burst);
    } else if (h & (kHitAttackByPlayer | kHitAttackSolid | kHitAttackBump)) {
        shot.state = uint8_t(FallingShotState::Burst);
    }
    if (int32_t(actorTestMove(env, shot)) < 0) shot.state = uint8_t(FallingShotState::Burst); // exe 0x40f570
    f.hit.result = 0;
    f.hit.previousX = shot.prevPos.x.whole;
    f.hit.previousZ = shot.prevPos.z.whole;
    const int16_t pos[3] = {shot.pos.x.whole, shot.pos.y.whole, shot.pos.z.whole};
    const bool big = FallingShotParams(shot).big != 0;
    const uint32_t lo = big ? 0x14u : 0x10u, hi = big ? 0x600000u : 0x400000u;
    hitListSubmit(pos, f.hit, ((lo | 0x200000u) >> 2) | uint32_t(shot.slot) << 24 | hi, 0x20180008u); // exe 0x5e4120
}
void fallingShotBurst(ProjectileActor& shot) { // exe 0x5c5e90: the global spark (variant 0) at the shot, freed
    if (const uint32_t address = allocActorA0(0)) { // exe 0x5e13b0
        EffectActor& spark = effectActorAt(address);
        spark.type = typeByte(GlobalEffectType::HitSpark);
        spark.variant = 0;
        spark.flags = kActorLive;
        spark.loopFlags |= kActorSweepGroup2;
        spark.pos.x = shot.pos.x.whole;
        spark.pos.y = shot.pos.y.whole;
        spark.pos.z = shot.pos.z.whole;
        classState<HitSparkFields>(spark).size = 0x80;
    }
    freeActor(shot); // exe 0x5e1500
}
void fallingShot(const PlayerEnv& env, ProjectileActor& shot) {
    FallingShotFields& f = classState<FallingShotFields>(shot);
    switch (FallingShotState(shot.state)) {
    case FallingShotState::Start: fallingShotStart(shot, f); return;
    case FallingShotState::Fall: fallingShotFall(env, shot, f); return;
    case FallingShotState::Burst: fallingShotBurst(shot); return;
    default: notPortedAt(kStoryPort, "0x5c5d20 state (table 0x8d1258)", shot.state);
    }
}

// ---- the lobbed blob (exe 0x5caa00) ---------------------------------------------------------------------------------
// States [0x8d15a0]: 0 0x5caa20 start, 1 0x5caaa0 fly, 2 0x5d0bc0 free. Its variant: the landing height (the ground
// - 0x20 / - 0x80).
enum class BlobState : uint8_t { Start = 0, Fly = 1, Free = 2 };
constexpr uint32_t kBlobFlightTicks = 0x12c, kBlobFlattenTicks = 0x40;
constexpr uint16_t kSoundBlobLands = 0x25d;

// exe 0x5caa20: the model variant 0 (none: freed), its flight's ticks, the rider follower of its slot (game/carry)
void blobStart(ProjectileActor& blob, LobbedBlobFields& f) {
    if (!bindActorModel(blob, 0)) { // exe 0x5e0ba0
        freeActor(blob);            // exe 0x5e1500
        return;
    }
    f.ticks = kBlobFlightTicks;
    f.landed = 0;
    blob.onPlatform = 0;
    blob.airborne = 1;
    blob.collisionBox.pointAt(TableId::BlobBossShotsLobbedBlobBox);
    blob.actorHit = 0;
    if (const uint32_t address = allocActorE0()) { // exe 0x5e1440: the rider follower (pool 0xe0 type 0x14)
        ControllerActor& c = controllerActorAt(address);
        RiderFollowerFields& follower = classState<RiderFollowerFields>(c);
        c.flags = kActorLive;
        c.loopFlags |= uint8_t(kActorSweepGroup2 | kActorLateLoop);
        c.type = kRiderFollowerType;
        follower.riderSlot = int8_t(blob.slot);
        follower.riderType = int8_t(blob.type);
        follower.riderLoopFlags = blob.loopFlags;
    }
    blob.subState = 0;
    ++blob.state;
}
// exe 0x5caaa0: ballistic (the gravity, forward along its yaw); landing (on an actor, or below the ground minus its
// variant's height: the sound, snapped up) stops it and arms its flattening; freed when its ticks run out
void blobFly(const PlayerEnv& env, ProjectileActor& blob, LobbedBlobFields& f) {
    LobbedBlobParams params(blob);
    blob.savePreviousPose();
    blob.wasOnPlatform = blob.onPlatform;
    blob.velocity.y = int16_t(uint16_t(blob.velocity.y) + uint16_t(blob.acceleration.y));
    integrateVelocity(blob, 0, blob.velocity.y, -int(blob.velocity.z)); // exe 0x5e3690
    bool landed = false;
    if (f.landed & 1) {
        ++params.hitSize;
        ++blob.pos.y.whole;
        blob.scale.y -= 8u;
        blob.scale.x += 8u;
        blob.scale.z += 8u;
    } else if (blob.onPlatform != 0) {
        soundPlayAtPos(kSoundBlobLands, blob.pos);
        blob.pos = blob.prevPos;
        landed = true;
    } else {
        const int g = actorGround(env, blob); // exe 0x40f190
        const int lift = blob.variant == 0 ? 0x20 : blob.variant == 1 ? 0x80 : -1;
        if (lift >= 0) {
            if (blob.pos.y.whole > int(int16_t(g)) - lift) {
                soundPlayAtPos(kSoundBlobLands, blob.pos);
                blob.pos.y.whole = int16_t(uint16_t(g - lift));
                landed = true;
            } else {
                actorCollisionSubmitMover(blob); // exe 0x4140b0
            }
        }
    }
    if (landed) {
        blob.velocity.y = 0;
        blob.velocity.z = 0;
        blob.acceleration.y = 0;
        f.ticks = kBlobFlattenTicks;
        f.landed |= 1u;
    }
    --f.ticks;
    if (f.ticks == 0) ++blob.state;
    f.hit.result = 0;
    f.hit.previousX = blob.prevPos.x.whole;
    f.hit.previousZ = blob.prevPos.z.whole;
    const int16_t pos[3] = {blob.pos.x.whole, blob.pos.y.whole, blob.pos.z.whole};
    hitListSubmit(pos, f.hit, uint32_t(blob.slot) << 24 | (uint32_t(params.hitSize) >> 2) | 0x680000u,
                  uint32_t(params.attack) | kHitAttackStrong); // exe 0x5e4120
}
void lobbedBlob(const PlayerEnv& env, ProjectileActor& blob) {
    LobbedBlobFields& f = classState<LobbedBlobFields>(blob);
    switch (BlobState(blob.state)) {
    case BlobState::Start: blobStart(blob, f); return;
    case BlobState::Fly: blobFly(env, blob, f); return;
    case BlobState::Free: freeActor(blob); return; // exe 0x5d0bc0 -> 0x5e1500
    default: notPortedAt(kStoryPort, "0x5caa00 state (table 0x8d15a0)", blob.state);
    }
}

} // namespace

void fallingShotUpdate(const PlayerEnv& env, uint32_t actor) { fallingShot(env, projectileActorAt(actor)); }
void lobbedBlobUpdate(const PlayerEnv& env, uint32_t actor) { lobbedBlob(env, projectileActorAt(actor)); }

} // namespace od::port
