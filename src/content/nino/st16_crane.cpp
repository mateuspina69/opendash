// The ST16 crane, class 0x4c7400 (see st16_crane.hpp). States [0x894f04]: 0 init 0x4c7510, 1 0x4c75b0 (sub-states
// [0x894f0c]: 0 0x4c75d0 parked at (0x800, -0xb80, 0x540), hidden; 1 0x4c7620 brings the box; 2 0x4c7810 turns round
// and flies back to 0x800; 3 0x4c7890 (anims 3 / 4, then 2)).
#include "content/nino/st16_crane.hpp"
#include "content/nino/st16_crane_state.hpp"

#include "content/nino/field_spawners.hpp"
#include "engine/data_source.hpp"
#include "game/actor_state.hpp"
#include "game/lift_box.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/fixed_point.hpp"
#include "game/hit_list.hpp"
#include "game/port_gap.hpp"

namespace od::port {

using namespace st16_crane_state;

namespace {

constexpr const char* kCranePort = "st16 crane port"; // the area of its port-gap errors

// The box it brings (game/lift_box: its holder's bytes) and its controller's list of the boxes (the ST16 box spawner,
// content/nino/field_spawners)
using CraneBoxParams = LiftBoxHolding;
constexpr uint8_t kBoxLetGo = kLiftBoxLetGo;
using BoxListOwner = BoxSpawnerFields;

enum class CraneState : uint8_t { Init = 0, Main = 1 };
enum class CraneSub : uint8_t { Parked = 0, Bring = 1, Return = 2, Wave = 3 };

const DataTable<CollisionBoxRecord> craneBox{TableId::St16CraneBox};
constexpr uint8_t kBoxType = 0x25, kBoxVariant = 0x80;
constexpr int32_t kParkX = 0x08000000, kParkY = int32_t(0xf4800000), kParkZ = 0x05400000; // (0x800, -0xb80, 0x540)
constexpr int16_t kDropX = 0x610, kHomeX = 0x800, kSpeed = -0xa0;
constexpr uint16_t kSoundBring = 0x19d;

void toSub(AnimatedActor& a, CraneSub s) {
    a.subState = uint8_t(s);
    a.enterStep(0);
}
// the hung box while it is live and not taken, else nullptr
StaticActor* hungBox(const CraneFields& f) {
    const uint32_t slot = f.box;
    if (slot == 0) return nullptr;
    StaticActor& b = staticPool()[slot - kStaticClass];
    return (b.flags & kActorLive) && CraneBoxParams(b).carried == 0 ? &b : nullptr;
}
void bring(AnimatedActor& a, CraneFields& f) { // exe 0x4c7620
    switch (a.step) {
    case 0: {
        a.anim = 5;
        a.flags |= kActorDrawn;
        const uint32_t address = allocActor60(kActorStageClass); // exe 0x5e12f0
        f.box = 0;
        if (address) {
            StaticActor& s = staticActorAt(address);
            s.flags = kActorLiveDrawn;
            s.type = kBoxType;
            s.subtype = 0;
            s.variant = kBoxVariant;
            s.setParamsWord(0);
            s.rotation.y = a.rotation.y;
            f.box = s.slot;
            BoxListOwner& owner = classState<BoxListOwner>(controllerPool()[size_t(uint32_t(f.controller) - kControllerClass)]);
            for (size_t k = 0; k < owner.boxes.size(); ++k)
                if (owner.boxes[k] == 0) {
                    owner.boxes[k] = s.slot;
                    break;
                }
        }
        soundPlayAtPos(kSoundBring, a.pos);
        a.enterStep(1);
        [[fallthrough]];
    }
    case 1:
        integrateVelocity(a, 0, 0, kSpeed);
        if (a.pos.x.whole < kDropX) {
            a.pos.x.whole = kDropX;
            a.enterStep(2);
        }
        if (StaticActor* b = hungBox(f)) { // the box hangs below bone 3
            int16_t dx = 0, dz = 0;
            angleVector(a.rotation.y, 0x6e, dx, dz); // exe 0x5e40c0
            const ShortVec4& bp = bonePoint(a, 3, kCranePort); // exe 0x5e1810
            b->savePreviousPose();
            b->pos.x.whole = int16_t(dx + a.pos.x.whole);
            b->pos.y.whole = int16_t(uint16_t(bp.y) + 0x40u);
            b->pos.z.whole = int16_t(a.pos.z.whole + dz);
        }
        return;
    case 2:
        a.anim = 1;
        a.enterStep(3);
        if (StaticActor* b = hungBox(f)) { // let go
            CraneBoxParams(*b).holder = kBoxLetGo;
            b->velocity.y = 0;
            b->velocity.z = 0;
        }
        [[fallthrough]];
    case 3:
        if (a.animEnded()) toSub(a, CraneSub::Return);
        return;
    default: return;
    }
}
void parked(AnimatedActor& a, CraneFields& f) { // exe 0x4c75d0
    if (a.step == 0) {
        a.rotation.y = f.homeYaw;
        a.pos.x = kParkX;
        a.pos.y = kParkY;
        a.pos.z = kParkZ;
        a.setDrawn(false);
        f.called = 0;
        a.step = 1;
        a.stepCounter = 0;
    }
    if (f.called != 0) toSub(a, CraneSub::Bring);
}
void flyBack(AnimatedActor& a, CraneFields& f) { // exe 0x4c7810: a half turn 0x40 a tick, then back to x 0x800
    const uint8_t s = a.step;
    if (s == 0) {
        a.anim = 2;
        f.turnLeft = kHalfTurn;
        a.enterStep(1);
    } else if (s != 1) {
        if (s != 2) return;
        integrateVelocity(a, 0, 0, kSpeed);
        if (a.pos.x.whole < kHomeX + 1) return;
        a.pos.x.whole = kHomeX;
        toSub(a, CraneSub::Parked);
        return;
    }
    a.rotation.y = int16_t(uint16_t(a.rotation.y) + 0x40u);
    f.turnLeft = int16_t(uint16_t(f.turnLeft) - 0x40u);
    if (f.turnLeft < 1) a.enterStep(2);
}
void wave(AnimatedActor& a) { // exe 0x4c7890
    const uint8_t s = a.step;
    if (s == 0) {
        a.anim = 3;
        a.enterStep(1);
    } else if (s == 1) {
        if (a.animEnded()) {
            a.anim = 4;
            a.enterStep(2);
        }
    } else if (s == 2 && a.animEnded()) {
        toSub(a, CraneSub::Return);
    }
}
void init(AnimatedActor& a, CraneFields& f) { // exe 0x4c7510
    if (!bindActorModel(a, 0)) {
        freeActor(a);
        return;
    }
    a.anim = 0;
    a.prevAnim = 0xff;
    const uint16_t hp = hasAnimatedStats(a.type) ? animatedStats(a.type).hitPoints : 0;
    a.velocity.setXY(0, 0);
    a.velocity.setZSpare(0, 0);
    a.spin.setXY(0, 0);
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.interactionBox = nullptr;
    a.airborne = 0;
    a.onPlatform = 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    f.homeYaw = a.rotation.y;
    a.collisionBox.pointAt(craneBox.stateAddress());
    a.interaction = kInteractLift;
    a.clearClassBlock();
    a.enterState(uint8_t(CraneState::Main));
}

void crane(AnimatedActor& a) { // exe 0x4c7400
    CraneFields& f = classState<CraneFields>(a);
    a.prevAnim = a.anim;
    a.savePreviousPose();
    a.savePreviousContact();
    a.tint = a.hitPoints < 0 ? kTintSpent : kTintNeutral;
    switch (CraneState(a.state)) { // [0x894f04]
    case CraneState::Init:
        init(a, f);
        if (!(a.flags & kActorLive)) return;
        break;
    case CraneState::Main: // exe 0x4c75b0: [0x894f0c]
        switch (CraneSub(a.subState)) {
        case CraneSub::Parked: parked(a, f); break;
        case CraneSub::Bring: bring(a, f); break;
        case CraneSub::Return: flyBack(a, f); break;
        case CraneSub::Wave: wave(a); break;
        default: notPortedAt(kCranePort, "class 0x4c7400 sub-state", a.subState);
        }
        break;
    default: notPortedAt(kCranePort, "class 0x4c7400 state", a.state);
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim == a.prevAnim) animStepPose(a); // exe 0x5e1e80
    else animSetPose(a, a.anim, 0);            // exe 0x5e1c30
    f.hitBox.result = 0;
    f.hitBox.previousX = a.prevPos.x.whole;
    f.hitBox.previousZ = a.prevPos.z.whole;
    const int16_t pos[3] = {a.pos.x.whole, wrap16(a.pos.y.whole - 100), a.pos.z.whole};
    hitListSubmit(pos, f.hitBox, hitOwner(a.slot) | 0xa88010u, kHitAttackSolid);
}

} // namespace

void st16CraneUpdate(const PlayerEnv&, uint32_t actor) { crane(animatedActorAt(actor)); }

} // namespace od::port
