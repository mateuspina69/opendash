// The ST13 spinning blade (pool 0x20 type 0x20, exe 0x481980; hypothesis names): it spins up to its variant's top
// speed (exe 0x891b04, +-2 a tick), a hit slows it to a stop, then 0x5a ticks later it starts again; sound 0x145 when
// the blade passes in front of the player. Hit capsules: bone 0 -> bone 1 (radius 0xc) and a column 0x100..0x2bc above
// bone 1 (radius 0x20). States [0x891b0c]: 0 bind 0x481b90, 1 spin 0x481c60.
#include "content/saul_kada/st13_spin_blade.hpp"
#include "content/saul_kada/st13_spin_blade_state.hpp"

#include <cstdlib>

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/cutscene_actor.hpp"
#include "game/vectors.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace st13_spin_blade_state;

namespace {

enum class BladeState : uint8_t { Bind = 0, Spin = 1 };
enum class BladeSub : uint8_t { Spinning = 0, Stopping = 1, Stopped = 2 };
const DataTable<ScalarTable<int16_t, 8>> topSpeeds{TableId::St13SpinBladeTopSpeeds}; // by variant
constexpr uint32_t kBladePart = 1; // the blade's part (the decode buffer's entry 1 + 1)
constexpr uint16_t kPassSound = 0x145;
constexpr uint16_t kSoundCooldown = 0xf;
constexpr uint8_t kRestartTicks = 0x5a;
// the hit capsules: the arm (not hurting, deflecting) and the column (hurting the thrown, deflecting)
constexpr uint32_t kArmShape = hitGroup(kHitGroupBodies) | kHitNoLockOn | hitCapsule(3);
constexpr uint32_t kArmAttack = kHitAttackHarmless | kHitAttackDeflects | 1;
constexpr uint32_t kColumnShape = hitGroup(kHitGroupBodies) | kHitNoLockOn | hitCapsule(1);
constexpr uint32_t kColumnAttack = kHitAttackHarmless | kHitAttackDeflects | kHitAttackThrown;

// exe 0x481b90
void bind(AnimatedActor& actor, BladeFields& blade) {
    if (!bindActorModel(actor, 0)) {
        freeActor(actor);
        return;
    }
    actor.flags |= kActorScenery;
    actor.anim = 0;
    actor.prevAnim = kNoAnimShown;
    const uint16_t hp = hasAnimatedStats(actor.type) ? uint16_t(animatedStats(actor.type).hitPoints) : uint16_t(0);
    actor.maxHitPoints = int16_t(hp);
    actor.hitPoints = int16_t(hp);
    actor.velocity.clearWords();
    actor.spin.setXY(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    actor.collisionBox.pointAt(TableId::St13SpinBladeBox);
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.clearClassBlock();
    const int16_t top = topSpeeds[actor.variant];
    blade.topSpeed = top;
    blade.spinAccel = top <= 0 ? int16_t(-2) : int16_t(2);
    actor.enterState(uint8_t(BladeState::Spin));
}
// exe 0x481c60
void spin(AnimatedActor& actor, BladeFields& blade) {
    const int speed = actor.spin.y, accel = blade.spinAccel;
    switch (BladeSub(actor.subState)) {
    case BladeSub::Spinning: {
        if (std::abs(speed) <= std::abs(int(blade.topSpeed))) actor.spin.y = int16_t(speed + accel);
        else actor.spin.y = blade.topSpeed;
        if (blade.hitBox.result & kHitAttackBump) actor.subState = uint8_t(BladeSub::Stopping);
        const int d = angleDiff(uint32_t(actor.rotation.y + 0x400), uint32_t(angleOf(player().pos, actor.pos)));
        if (blade.soundTicks == 0 && uint32_t(d + 0x100) < 0x200) {
            soundPlayAtPos(kPassSound, actor.pos);
            blade.soundTicks = kSoundCooldown;
        }
        if (blade.soundTicks) --blade.soundTicks;
        break;
    }
    case BladeSub::Stopping:
        if (std::abs(speed) >= std::abs(accel)) {
            actor.spin.y = int16_t(speed - accel);
        } else {
            actor.spin.y = 0;
            blade.restartTicks = kRestartTicks;
            actor.subState = uint8_t(BladeSub::Stopped);
        }
        break;
    case BladeSub::Stopped:
        --blade.restartTicks;
        if (blade.restartTicks == 0) actor.subState = uint8_t(BladeSub::Spinning);
        break;
    default: break;
    }
    actor.rotation.y += uint16_t(actor.spin.y);
    blade.bladeTurn = uint16_t(blade.bladeTurn + uint16_t(-0x80 - actor.spin.y));
}

// exe 0x481980
void spinBlade(AnimatedActor& actor) {
    BladeFields& blade = classState<BladeFields>(actor);
    actor.prevAnim = actor.anim;
    actor.prevPos = actor.pos;
    actor.tint = kTintNeutral;
    actor.prevRotation = actor.rotation;
    actor.savePreviousContact();
    switch (BladeState(actor.state)) {
    case BladeState::Bind: bind(actor, blade); break;
    case BladeState::Spin: spin(actor, blade); break;
    default: notPortedAt(kStoryPort, "0x481980 state (table 0x891b0c)", actor.state);
    }
    if (!(actor.flags & kActorLive)) return;
    if (actor.anim == actor.prevAnim) animStep(actor); // exe 0x5e1ea0
    else animSet(actor, actor.anim, 0);                // exe 0x5e1c50
    decodedFrame().entries[1 + kBladePart].y += blade.bladeTurn;
    animPose(actor); // exe 0x5e2d90
    if (!(actor.flags & kActorDrawn)) return;
    blade.hitBox.result = 0;
    if (!hasAnimatedStats(actor.type)) notPorted(kStoryPort, "0x481980: type row null (the exe reads through it)");
    const AnimatedActorStats stats = animatedStats(actor.type);
    const ShortVec4& base = bonePoint(actor, 0, kStoryPort), tip = bonePoint(actor, 1, kStoryPort); // exe 0x5e1810
    const int16_t arm0[3] = {base.x, int16_t(uint16_t(base.y) + 0x40u), base.z};
    const int16_t arm1[3] = {tip.x, int16_t(uint16_t(tip.y) + 0x40u), tip.z};
    hitListSubmitCapsule(arm0, 0xc, arm1, 0xc, blade.hitBox, hitOwner(actor.slot) | kArmShape, kArmAttack); // 0x5e4190
    const int16_t column0[3] = {tip.x, int16_t(uint16_t(tip.y) + 0x100u), tip.z};
    const int16_t column1[3] = {tip.x, int16_t(uint16_t(tip.y) + 0x2bcu), tip.z};
    hitListSubmitCapsule(column0, 0x20, column1, 0x20, blade.hitBox, hitOwner(actor.slot) | kColumnShape,
                         uint32_t(stats.attack) | kColumnAttack);
    blade.hitBox.previousX = column1[0];
    blade.hitBox.previousZ = column1[2];
    // the shadow at the column top (exe 0x5f20f0, the position swapped in and restored): render output
}

} // namespace

void spinBladeUpdate(const PlayerEnv&, uint32_t actor) { spinBlade(animatedActorAt(actor)); }

} // namespace od::port
