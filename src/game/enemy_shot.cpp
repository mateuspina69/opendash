// The enemy shot (see enemy_shot.hpp).
#include "game/actor_state.hpp"
#include "game/enemy_shot.hpp"

#include "engine/data_source.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/field_alias.hpp"
#include "game/fixed_point.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"

namespace od::port {

namespace {

inline constexpr const char* kEnemyShotArea = "enemy shot";

// how a tick ended
enum class ShotEnd { Flying, HitBody, OutOfTicks, Blocked };

const DataTable<CollisionBoxRecord> shotBox{TableId::EnemyShotBox};
constexpr uint16_t kLaunchSound = 0xb1;
constexpr uint32_t kEnemyShotGroup = 3; // the hit group (the heavy thrown box's too: it hits the player)
constexpr uint8_t kPopType = 7; // the stage table's effect type 7, a pop (stage types stay per class: game/global_class_types.hpp)
// the pop's spawn parameter: its size
struct PopParams {
    explicit PopParams(const EffectActor& actor) : PopParams(const_cast<EffectActor&>(actor)) {}
    explicit PopParams(EffectActor& actor) : size(actor.params, 0) {}
    BytesField<uint16_t, 4> size;
};

// exe 0x5c55f0(shot, variant): a pop (the stage's effect type 7, variant v, size by the shot's size) at the shot, the
// shot freed
void pop(ProjectileActor& shot, uint8_t variant) {
    if (const uint32_t address = allocActorA0(kActorStageClass)) { // exe 0x5e13a0
        EffectActor& e = effectActorAt(address);
        e.flags = kActorLive | kActorDrawn;
        e.type = kPopType;
        e.loopFlags |= kActorLateLoop;
        e.variant = variant;
        e.pos.x = shot.pos.x.whole;
        e.pos.y = shot.pos.y.whole;
        e.pos.z = shot.pos.z.whole;
        PopParams(e).size = uint16_t(uint32_t((shot.variant & kShotSizeMask) + 2) * 0x40);
    }
    freeActor(shot);
}

// exe 0x5d0710(shot, 0): a hit spark (global effect type 0, size by the shot's size), the shot freed
void spark(ProjectileActor& shot) {
    if (const uint32_t address = allocActorA0(0)) { // exe 0x5e13b0
        EffectActor& e = effectActorAt(address);
        e.flags = kActorLive | kActorDrawn;
        e.type = typeByte(GlobalEffectType::HitSpark);
        e.variant = 0;
        e.pos.x = shot.pos.x.whole;
        e.pos.y = shot.pos.y.whole;
        e.pos.z = shot.pos.z.whole;
        auto&& fields = classState<HitSparkFields>(e);
        fields.frameTicks = 1;
        fields.size = uint16_t(uint32_t((shot.variant & kShotSizeMask) + 8) * 0x10);
    }
    freeActor(shot);
}

} // namespace

void enemyShotUpdate(const PlayerEnv& env, uint32_t address) {
    ProjectileActor& shot = projectileActorAt(address);
    auto&& fields = EnemyShotFields(shot);
    ShotEnd end = ShotEnd::Flying;
    if (EnemyShotState(shot.state) == EnemyShotState::Launch) {
        // speed (-7 - (variant >> 2 & 3)) * 0x100 along the rotation, the previous position one step behind
        shot.runFlags |= kActorWarpKept;
        shot.flags &= uint8_t(~kActorDrawn);
        fields.hitBox.result = 0;
        const int16_t speed = wrap16((-7 - int((shot.variant >> 2) & 3)) * 0x100);
        shot.velocity.z = speed;
        shot.velocity.x = 0;
        shot.velocity.y = 0;
        shot.prevPos = shot.pos;
        moveAlongRotation(shot.prevPos, shot.rotation, 0, 0, -2 * int(speed)); // exe 0x5e3810
        shot.collisionBox.pointAt(shotBox.stateAddress());
        shot.actorHit = 0;
        soundPlayAtPos(kLaunchSound, shot.pos);
        ++shot.state;
        // exe 0x5c6fc0: the glow sprite (render output)
    } else if (EnemyShotState(shot.state) == EnemyShotState::Fly) {
        shot.savePreviousPose();
        if (shot.actorHit != 0 || (fields.hitBox.result & kHitAttackBump)) {
            end = ShotEnd::HitBody;
        } else {
            --fields.ticks;
            if (fields.ticks == 0) {
                end = ShotEnd::OutOfTicks;
            } else {
                moveAlongRotation(shot.pos, shot.rotation, 0, 0, shot.velocity.z);
                const uint32_t radius = (shot.variant & kShotSizeMask) * 8 + 0x20;
                if (playerShieldBlocks(shot, radius)) { // exe 0x5737b0
                    end = ShotEnd::Blocked;
                } else {
                    // the shot sprite: render output
                    const int16_t pos[3] = {shot.pos.x.whole, shot.pos.y.whole, shot.pos.z.whole};
                    fields.hitBox.result = 0;
                    fields.hitBox.previousX = shot.prevPos.x.whole;
                    fields.hitBox.previousZ = shot.prevPos.z.whole;
                    hitListSubmit(pos, fields.hitBox,
                                  hitOwner(shot.slot) | hitGroup(kEnemyShotGroup) | hitRadius(radius),
                                  fields.attack | kHitAttackStrong);
                    shot.airborne = kAirborneFalling;
                    shot.onPlatform = 0;
                    shot.platform = nullptr;
                }
            }
        }
    } else {
        notPortedAt(kEnemyShotArea, "enemy shot state", shot.state);
    }
    switch (end) {
    case ShotEnd::Flying: {
        const uint32_t t = actorTestMove(env, shot);
        if (int32_t(t) < 0) {
            queueBreakableWallHit(t, int(uint32_t(fields.attack) << 4));
            actorPushOut(env, shot);
            pop(shot, 0);
            return;
        }
        if (!(shot.variant & kShotNoMover)) actorCollisionSubmitMover(shot); // exe 0x4140b0
        return;
    }
    case ShotEnd::HitBody: spark(shot); return;
    case ShotEnd::OutOfTicks: pop(shot, 1); return;
    default: freeActor(shot); return; // exe 0x5e1500
    }
}

} // namespace od::port
