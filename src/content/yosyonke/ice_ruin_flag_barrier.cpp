// The ice ruin's ST2D / ST2E barrier with its guard (static type 0x2a, exe 0x5aa770; hypothesis names): a semi-transparent stand-on
// solid; at its bind (unless its flag is already set) a guard (pool 0x20 type 5 sub 1, variant 0x85) at its place,
// handed the same flag. Its flag set -> sound 0x255, the riders dropped, freed.
// States [0x8cf2bc]: 0 bind 0x5aa7c0, 1 stand 0x5aa8b0, 2 gone 0x5aa900.
#include "content/yosyonke/ice_ruin_flag_barrier.hpp"

#include "content/saul_kada/field_reaverbot.hpp"

#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/field_alias.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"
#include "game/story_flags.hpp"

namespace od::port {

namespace {

enum class BarrierState : uint8_t { Bind = 0, Stand = 1, Gone = 2 };
// The barrier's parameter: the flag that opens it
struct BarrierParams {
    explicit BarrierParams(const StaticActor& actor) : BarrierParams(const_cast<StaticActor&>(actor)) {}
    explicit BarrierParams(StaticActor& actor) : flag(actor.params, 2) {}
    BytesField<uint16_t, 4> flag;
};
// Its own bytes: its hit box
struct BarrierFields {
    explicit BarrierFields(const StaticActor& actor) : BarrierFields(const_cast<StaticActor&>(actor)) {}
    explicit BarrierFields(StaticActor& actor) : hitBox(actor.hitBox) {}
    HitBoxHead& hitBox;
};
constexpr uint8_t kGuardSubtype = 1, kGuardVariant = 0x85, kGuardSpawnByte = 4, kGuardMode = 2;
constexpr uint16_t kOpenSound = 0x255;

// exe 0x5aa7c0: already open -> freed; else the guard at its place
void bind(StaticActor& actor, BarrierFields& barrier) {
    const BarrierParams params(actor);
    if (storyFlag(params.flag) || !bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);                                       // exe 0x5e1540
        return;
    }
    actor.enterState(uint8_t(BarrierState::Stand));
    actor.collisionBox.pointAt(TableId::IceRuinFlagBarrierBox);
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.semiTransparency = semiTransparencyBits(1, 0); // exe 0x5e1a70(a, 1, 0)
    actor.clearClassBlock();
    barrier.hitBox.previousX = actor.pos.x.whole;
    barrier.hitBox.previousZ = actor.pos.z.whole;
    barrier.hitBox.result = 0;
    if (const uint32_t slot = allocActor20()) { // exe 0x5e1160
        AnimatedActor& guard = animatedActorAt(slot);
        FieldReaverbotParams guardParams(animatedActorAt(slot));
        guard.loopFlags |= kActorSweepGroup2;
        guard.flags = kActorLive;
        guard.type = kFieldReaverbotType;
        guard.subtype = kGuardSubtype;
        guard.variant = kGuardVariant;
        guard.spawnByte = kGuardSpawnByte;
        guard.pos = actor.pos;
        guard.rotation.setWords(actor.rotation.xyWord(), actor.rotation.zSpareWord());
        guardParams.mode = kGuardMode;
        guardParams.second = 0;
        guardParams.flag = params.flag;
    }
}

void flagBarrier(StaticActor& actor) {
    BarrierFields barrier(actor);
    actor.prevPos = actor.pos;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (BarrierState(actor.state)) {
    case BarrierState::Bind: bind(actor, barrier); return;
    case BarrierState::Stand: // 0x5aa8b0: the flag -> gone, sound 0x255; a solid
        if (storyFlag(BarrierParams(actor).flag)) {
            actor.enterState(uint8_t(BarrierState::Gone));
            soundPlayAtPos(kOpenSound, actor.pos);
        }
        actorCollisionSubmitSolid(actor, SolidType::StandOn, 0, 0); // exe 0x414050
        return;
    case BarrierState::Gone: // 0x5aa900
        actorDetachRiders(actor); // exe 0x414e70
        freeActor(actor);
        return;
    default: notPortedAt(kStoryPort, "0x5aa770 state (table 0x8cf2bc)", actor.state);
    }
}

} // namespace

void flagBarrierUpdate(const PlayerEnv&, uint32_t actor) { flagBarrier(staticActorAt(actor)); }

} // namespace od::port
