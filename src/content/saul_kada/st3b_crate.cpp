// The pirate base's breakable crate / barrel (static ST3B type 1, exe 0x590f30; hypothesis names): the hit points of
// its row (variant 0: 0x80); a hit flashes it and takes the damage; at 0 a blast (size 0xb4, 0xa0 higher) and it is
// freed (its riders dropped). Every tick: a hit-list entry 0xa0 above its base and a stand-on solid.
// States [0x8cddb4]: 0 bind 0x590f70, 1 by variant [0x8cddbc] (only 0: 0x591040), 2 0x591040 (hit unless disabled).
#include "content/saul_kada/st3b_crate.hpp"

#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/carry.hpp"
#include "game/explosions.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

namespace {

enum class CrateState : uint8_t { Bind = 0, Run = 1, Hit = 2 };
// The crate's parameter: its hits disabled
struct CrateParams {
    explicit CrateParams(const StaticActor& actor) : CrateParams(const_cast<StaticActor&>(actor)) {}
    explicit CrateParams(StaticActor& actor) : disabled(actor.params[0]) {}
    uint8_t& disabled;
};
// Its own bytes: its hit box
struct CrateFields {
    explicit CrateFields(const StaticActor& actor) : CrateFields(const_cast<StaticActor&>(actor)) {}
    explicit CrateFields(StaticActor& actor) : hitBox(actor.hitBox) {}
    HitBoxHead& hitBox;
};
constexpr int16_t kBarrelHitPoints = 0x80;
constexpr uint16_t kBlastSize = 0xb4;
constexpr int kHitHeight = 0xa0;
// its hit entry: a body (radius 0xa0), not hurting, a bump
constexpr uint32_t kCrateShape = hitGroup(kHitGroupBodies) | kHitNoLockOn | hitRadius(0xa0);
constexpr uint32_t kCrateAttack = kHitAttackHarmless | kHitAttackBump;

// exe 0x591060
void takeHits(StaticActor& actor) {
    CrateFields crate(actor);
    actor.tint = kTintNeutral;
    if (const uint32_t damage = crate.hitBox.result & kHitDamageMask) {
        actor.tint = kTintFlash;
        if (hitPointsAdd(actor, -int(damage)) != 0) {          // exe 0x5e52f0
            if (const uint32_t slot = allocActor60(0)) {     // exe 0x5e1300
                StaticActor& blast = staticActorAt(slot);
                blast.loopFlags |= kActorSweepGroup2;
                blast.flags = kActorLive;
                blast.type = typeByte(GlobalStaticType::Blast);
                blast.variant = 0;
                blast.pos = actor.pos;
                BlastParams params(actorHeaderAt(slot));
                params.size = kBlastSize;
                params.attack = 0;
                blast.pos.y.whole += 0xff60u;
            }
            actorDetachRiders(actor); // exe 0x414e70
            freeActor(actor);         // exe 0x5e1540
        }
    }
    const int16_t pos[3] = {actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole) - uint16_t(kHitHeight)),
                            actor.pos.z.whole};
    crate.hitBox.result = 0;
    crate.hitBox.previousX = actor.prevPos.x.whole;
    crate.hitBox.previousZ = actor.prevPos.z.whole;
    hitListSubmit(pos, crate.hitBox, hitOwner(actor.slot) | kCrateShape, kCrateAttack); // exe 0x5e4120
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0);                   // exe 0x414050
}
// exe 0x590f70
void bind(StaticActor& actor) {
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
        freeActor(actor);                         // exe 0x5e1540
        return;
    }
    const uint16_t hp = hasStaticStats(actor.type) ? uint16_t(staticStats(actor.type, 0).hitPoints) : uint16_t(0);
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.velocity.setXY(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.collisionBox.pointAt(TableId::St3bCrateBox);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.wallHit = 0;
    actor.actorHit = 0;
    actor.lastMover = nullptr;
    actor.clearClassBlock();
    actor.enterState(uint8_t(CrateState::Run));
    if (actor.variant == 0) {
        actor.maxHitPoints = kBarrelHitPoints;
        actor.hitPoints = kBarrelHitPoints;
    }
    actor.wasOnPlatform = actor.onPlatform;
}

// exe 0x590f30
void crate(StaticActor& actor) {
    actor.savePreviousPose();
    actor.wasAirborne = actor.airborne;
    switch (CrateState(actor.state)) {
    case CrateState::Bind: bind(actor); return;
    case CrateState::Run: // 0x591020: by variant [0x8cddbc] (only 0)
        if (actor.variant != 0) {
            notPortedAt(kStoryPort, "0x591020 variant (table 0x8cddbc)", actor.variant);
            return;
        }
        [[fallthrough]];
    case CrateState::Hit: // 0x591040
        if (CrateParams(actor).disabled == 0) takeHits(actor);
        return;
    default: notPortedAt(kStoryPort, "0x590f30 state (table 0x8cddb4)", actor.state);
    }
}

} // namespace

void breakableUpdate(const PlayerEnv&, uint32_t actor) { crate(staticActorAt(actor)); }

} // namespace od::port
