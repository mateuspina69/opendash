// The ST24 story structure, class 0x49d110 (pool 0x20, ST24 type 0x30): an animated stand-on solid spawned by the
// story's room script (record 0 of 0x8c8e80 at (6760, -960, 5024); the player stops at its box). Moved here from
// game/actors. State 0 binds the model (variant +6, none -> freed), the collision box 0x893908, the class block
// cleared, animation 0 (exe 0x5e1c30 = set + pose), +0x50 = 1 (airborne: it keeps its height); later states only step
// the animation (exe 0x5e1e80 = step + pose). A solid of type 1 every tick.
#include "content/calbania/st24_structure.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_state.hpp"

namespace od::port {

namespace {

constexpr uint8_t kAnimStill = 0, kNoAnim = 0xff;
const DataTable<CollisionBoxRecord> st24StructureBox{TableId::St24StructureBox};

// exe 0x49d110's body: bound once, then its animation stepped, a stand-on solid
void st24Structure(AnimatedActor& actor) {
    actor.savePreviousPose();
    if (actor.state != 0) {
        animStepPose(actor);
        actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0);
        return;
    }
    if (!bindActorModel(actor, actor.variant)) {
        freeActor(actor);
        return;
    }
    actor.collisionBox.pointAt(st24StructureBox.stateAddress());
    actor.clearClassBlock();
    actor.enterState(1);
    actor.anim = kAnimStill;
    actor.prevAnim = kNoAnim;
    actor.airborne = 1;
    animSetPose(actor, kAnimStill, 0);
    actorCollisionSubmitSolid(actor, SolidType::StandOn, kNoBone, 0);
}

} // namespace

void st24StructureUpdate(const PlayerEnv&, uint32_t address) { st24Structure(animatedActorAt(address)); }

} // namespace od::port
