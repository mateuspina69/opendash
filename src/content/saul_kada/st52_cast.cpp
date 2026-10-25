// The ST52 cast (hypothesis names), one routine per exe routine; both keep the cast's class block (game/cutscene_actor
// CastFields: the face's texture tracks, the flags, the key their animation starts at).
//
// exe 0x4e2c40 (pool 0x20 type 0x5a): a standing cast member, its face tracks (0x895e30 / 0x895e54) stopped on cell 8.
// exe 0x4fb0e0 (pool 0x20 type 0x75): a cast member timed by the script's step tick -- its face tracks (0x897940 /
// 0x897964) switched at the ticks 1 / 0x21 / 0x47 / 0x9c / 0xab / 0xbd / 0x119 / 0x12d / 0x17d; animation 1 at 0x2a9,
// at 0x2cc animation 2, turned round and walking (speed -0x30); kept on the ground.
#include "content/saul_kada/st52_cast.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/cutscene_actor.hpp"
#include "game/keyframe_tracks.hpp"
#include "game/port_gap.hpp"
#include "game/script_state.hpp"
#include "game/story.hpp"

namespace od::port {

namespace {

enum class CastState : uint8_t { Bind = 0, Run = 1 };
constexpr uint8_t kHeldPoseBits = 3; // the cast flags that hold the pose (exe: & 3)

// the bind both share: the model, the box, the class block, animation 0, no motion, the face tracks on their tables
// stopped on the cell
bool bindCast(AnimatedActor& actor, CastFields& cast, uint32_t box, uint32_t eyesTable, uint32_t mouthTable,
              uint32_t cell) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);
        return false;
    }
    actor.collisionBox.pointAt(box);
    actor.clearClassBlock();
    actor.enterState(uint8_t(CastState::Run));
    actor.anim = 0;
    actor.prevAnim = kNoAnimShown;
    actor.velocity.z = 0;
    actor.velocity.y = 0;
    actor.velocity.x = 0;
    actor.acceleration.z = 0;
    actor.acceleration.y = 0;
    actor.acceleration.x = 0;
    cast.animKey = 0;
    cast.faceTracks.init(eyesTable, mouthTable);                                // exe 0x5e33a0
    textureTrackStop(actor, cast.faceTracks, FaceTrack::Eyes, cell);            // exe 0x5e3630
    textureTrackStop(actor, cast.faceTracks, FaceTrack::Mouth, cell);           // exe 0x5e3660
    return true;
}
// the head of both: the previous pose, the previous animation
void castHead(AnimatedActor& actor) {
    actor.savePreviousPose();
    actor.prevAnim = actor.anim;
}

// ---- exe 0x4e2c40 -------------------------------------------------------------------------------------------------
constexpr uint32_t kStandingCell = 8;

void standingCast(AnimatedActor& actor) {
    CastFields& cast = classState<CastFields>(actor);
    castHead(actor);
    if (actor.variant != 0) {
        notPortedAt(kStoryPort, "0x4e2c40 variant (table 0x895e18)", actor.variant);
        return;
    }
    if (CastState(actor.state) == CastState::Bind) { // 0x4e2d00
        if (!bindCast(actor, cast, stateAddress(TableId::St52CastStandingBox), stateAddress(TableId::St52CastStandingEyes), stateAddress(TableId::St52CastStandingMouth), kStandingCell)) return;
    }
    if (!(actor.flags & kActorLive)) return;
    textureTracksTick(actor, cast.faceTracks);                          // exe 0x5e34f0
    castAnimate(actor, cast.animKey, cast.castFlags, kHeldPoseBits); // exe 0x5e1c30 / 0x5e1f30 / 0x5e1e80
}

// ---- exe 0x4fb0e0 -------------------------------------------------------------------------------------------------
constexpr int16_t kWalkSpeed = -0x30;

void timedCast(const PlayerEnv& env, AnimatedActor& actor) {
    CastFields& cast = classState<CastFields>(actor);
    castHead(actor);
    if (actor.variant != 0) {
        notPortedAt(kStoryPort, "0x4fb0e0 variant (table 0x897920)", actor.variant);
        return;
    }
    if (CastState(actor.state) == CastState::Bind) { // 0x4fb1d0's bind
        if (!bindCast(actor, cast, stateAddress(TableId::St52CastTimedBox), stateAddress(TableId::St52CastTimedEyes), stateAddress(TableId::St52CastTimedMouth), 0)) return;
    }
    switch (scriptState().stepTick) {
    case 1: case 0x47: case 0xbd: case 0x12d: cast.faceTracks.second.select(0, 0); break;     // exe 0x5e33f0
    case 0x21: case 0x9c: case 0x119: textureTrackStop(actor, cast.faceTracks, FaceTrack::Mouth, 0); break;
    case 0xab: cast.faceTracks.first.select(0, 0); break; // exe 0x5e33d0
    case 0x17d:
        cast.faceTracks.first.select(0, 0);
        textureTrackStop(actor, cast.faceTracks, FaceTrack::Mouth, 0);
        break;
    case 0x2a9:
        actor.anim = 1;
        cast.animKey = 0;
        break;
    case 0x2cc:
        actor.anim = 2;
        cast.animKey = 0;
        actor.rotation.y += 0x800u;
        actor.velocity.z = kWalkSpeed;
        break;
    default: break;
    }
    if (!(actor.flags & kActorLive)) return;
    actor.velocity.z += uint16_t(actor.acceleration.z);
    if (actor.velocity.z != 0) integrateVelocity(actor, 0, 0, actor.velocity.z); // exe 0x5e3690
    actor.pos.y.whole = int16_t(actorGround(env, actor));                       // exe 0x40f190
    textureTracksTick(actor, cast.faceTracks);                                  // exe 0x5e34f0
    castAnimate(actor, cast.animKey, cast.castFlags, kHeldPoseBits);
}

} // namespace

void standingCastUpdate(const PlayerEnv&, uint32_t actor) { standingCast(animatedActorAt(actor)); }
void timedCastUpdate(const PlayerEnv& env, uint32_t actor) { timedCast(env, animatedActorAt(actor)); }

} // namespace od::port
