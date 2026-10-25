// ST0E: the Servbots of the vehicle chase (see st0e_servbots.hpp).
//
// The crate's Servbot (exe 0x464a30; pool 0x20 type 0xb sub 0). States (the exe's table 0x88fd34): 0 bind 0x464ac0, 1
// run 0x464bc0, 2 free 0x468fa0; its animation set on a change, else stepped (variant != 0). Variant 0 (inside): a
// random face (a texture cell of the table 0x88fd40); the faces 3..6 say their voice (0x88fd80) once after 0x2d ticks.
// Variant 1 (jumped out): hops (exe 0x464d50 the walls / the ground), lands (animation 0x11), off screen or far for 0x3c
// ticks (exe 0x5e59a0) -> freed.
// The riders (exe 0x464dc0; pool 0x20 type 0xb sub 1, spawned by the boss vehicle's exe 0x503330). Flag 0x582 (once) or
// a hit -> knocked off (anim 0x10, the held bomb burst, a cry of 0x88fdc4). States (the exe's table 0x88fdd4): 0 bind
// 0x465030, 1 run 0x465110 (the sub-states 0x88fde0), 2 free. Its animation (held: exe 0x5e1f30 instead of a step), the
// held bomb kept at bone 2 (0x11, 0x35, 0) until the key 10; two hit entries (y - 0x30 / - 0x80) unless knocked off.
//   Its position = its local point + the boss vehicle's bone 2 point of the offset of its variant (0x88fda8).
//   Sub-states: Walk (exe 0x4653b0) -0x30 a tick for its walk ticks, then face the player -> Throw (its side toggled);
//   Throw (exe 0x4654f0) face the point 0x400 / -0x4000 from the defended vehicle (y 0xf980), then a bomb is picked up
//   and it hops (-0x280); on the key 0xc the bomb is lobbed (speed 0x600, gravity 0x40, at the point +-0x3f), a shout
//   (0x88fdf0); landed -> turn back along the vehicle's yaw (+ a half turn on the other side) -> Walk; KnockedOff (exe
//   0x465960) flung backwards, bounces (exe 0x465ab0), lands (anim 0x11), freed off screen; DebugThrow (exe 0x465140)
//   a throw on the second pad's bit 0x800.
// The flung Servbot (exe 0x492f20; pool 0x20 type 0x2d). States (the exe's table 0x892b54): 0 bind 0x493110, 1 run
// 0x493230 (the sub-states 0x892b64: FlyOut 0x4932e0, Run 0x492880, Hit 0x493450, TurnAround 0x493580, Fall 0x4935e0,
// HopOff 0x493650), 2 carried 0x4936f0 (0x892b8c: Carried 0x493710, Thrown 0x493850). Freed when out of the room and
// far from the player, below the far distance, or off screen for 0x1e ticks.
// The shadows (exe 0x5f20f0) are render output and left out. The exe writes through a held bomb pointer of -1 (the
// debug throw, the knock-off): port gaps there.
#include "content/yosyonke/st0e_servbots.hpp"
#include "content/yosyonke/st0e_servbots_state.hpp"

#include "content/yosyonke/st0e_turret.hpp"
#include "content/yosyonke/st0e_vehicles.hpp"
#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_input.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"
#include "game/story_flags.hpp"
#include "game/visibility.hpp"

namespace od::port {

using namespace st0e_servbots_state;

namespace {

int16_t t16(int64_t v) { return int16_t(uint16_t(uint64_t(v))); }

enum class ServbotState : uint8_t { Bind = 0, Run = 1, Free = 2 };
// the hops' collision bits (exe 0x464d50 / 0x465ab0)
constexpr uint8_t kHopWall = 0x01, kHopLanded = 0x04;
constexpr uint32_t kPushedOutSides = 0xf00, kPushedOutFloor = 0x3000;
constexpr uint32_t kFlagFrontBroken = 0x582;

// exe 0x464d50 (as 0x465ab0): the wall test / push-out (back to the previous position when stuck; kHopWall on a side),
// the ground (kHopLanded)
uint32_t hopCollide(const PlayerEnv& env, AnimatedActor& a, uint8_t& hits) {
    uint32_t r = 0;
    if (int32_t(actorTestMove(env, a)) < 0) { // exe 0x40f570
        r = actorPushOut(env, a);             // exe 0x410960
        if (int32_t(r) < 0) a.pos = a.prevPos;
        if (r & kPushedOutSides) hits |= kHopWall;
    }
    const int g = actorGround(env, a); // exe 0x40f190
    if (r & kPushedOutFloor) {
        a.airborne = 0;
        a.pos.y.whole = int16_t(g);
        hits |= kHopLanded;
    }
    return r;
}
// the hop's arc: the move, the gravity, the fall speed capped at 0x380
void hopArc(AnimatedActor& a) {
    integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
    a.velocity.y = int16_t(a.velocity.y + a.acceleration.y);
    if (a.velocity.y > 0x380) a.velocity.y = 0x380;
}

enum class HopStep : uint8_t { Rise = 0, Fall = 1, Lie = 2 };
constexpr uint8_t kInsideCrate = 0;
const DataTable<CollisionBoxRecord> crateServbotBox{TableId::St0eServbotsCrateServbotBox};
const DataTable<ScalarTable<uint32_t, 16>> faceCells{TableId::St0eServbotsFaceCells};
const DataTable<ScalarTable<uint32_t, 7>> faceVoices{TableId::St0eServbotsFaceVoices};
constexpr uint8_t kFirstTalkingFace = 3, kLastTalkingFace = 6;

void crateServbotBind(AnimatedActor& a, CrateServbotFields& f) { // exe 0x464ac0
    if (!bindActorModel(a, 0)) {                                 // exe 0x5e0ba0
        freeActor(a);                                            // exe 0x5e1490
        return;
    }
    const uint8_t v = a.variant;
    a.runFlags |= kActorWarpKept;
    a.flags |= kActorDrawn;
    a.velocity.setXY(0, 0);
    a.anim = 0x18;
    a.prevAnim = 0xff;
    a.collisionBox.pointAt(crateServbotBox.stateAddress());
    a.interactionBox = nullptr;
    a.interaction = kInteractLift;
    a.velocity.setZSpare(0, 0);
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.velocity.y = 0;
    a.acceleration.y = 0x40;
    a.acceleration.z = 0x10;
    a.state = uint8_t(ServbotState::Run);
    a.step = 0;
    if (v != kInsideCrate) {
        a.airborne = 1;
        a.velocity.y = -0x200;
        a.velocity.z = -0x100;
        a.acceleration.y = 0x40;
        a.anim = 0x10;
    }
    f.lieTicks = 0x3c;
    f.voiceDelay = 0x2d;
    f.voiceSaid = 0;
    const uint8_t face = uint8_t(rngStep() & 0xf);
    f.face = face;
    setTexturePageCell(a.texturePage, faceCells[face]); // exe 0x5e3280 -> 0x5e32c0
}
void crateServbotRun(const PlayerEnv& env, AnimatedActor& a, CrateServbotFields& f) { // exe 0x464bc0
    f.hits = 0;
    const uint8_t v = a.variant;
    a.tint = v == kInsideCrate ? kTintNeutral : kTintSpent;
    if (v == kInsideCrate) {
        const uint8_t face = f.face;
        if (face < kFirstTalkingFace || face > kLastTalkingFace) return;
        if (f.voiceDelay != 0) {
            --f.voiceDelay;
            return;
        }
        if (f.voiceSaid != 0) return;
        soundPlayAtPos(uint16_t(faceVoices[face]), a.pos);
        f.voiceSaid = 1;
        return;
    }
    switch (HopStep(a.step)) {
    case HopStep::Rise:
        hopCollide(env, a, f.hits);
        if ((f.hits & kHopLanded) || a.pos.y.whole > 0) {
            a.velocity.z = int16_t(a.velocity.z >> 1);
            a.velocity.y = int16_t((-int32_t(a.velocity.y)) >> 1);
            ++a.step;
        }
        break;
    case HopStep::Fall:
        hopCollide(env, a, f.hits);
        if (f.hits & kHopLanded) {
            a.anim = 0x11;
            ++a.step;
            f.lieTicks = 0x3c;
            return;
        }
        if (a.pos.y.whole > 0) {
            a.anim = 0x11;
            ++a.step;
            f.lieTicks = 0x3c;
        }
        break;
    default:
        if (pointVisibleOrNear(a.pos, 0x200) == 0) return; // exe 0x5e59a0
        --f.lieTicks;
        if (f.lieTicks < 0) a.state = uint8_t(ServbotState::Free);
        return;
    }
    hopArc(a);
}
void crateServbot(const PlayerEnv& env, AnimatedActor& a) { // exe 0x464a30
    CrateServbotFields& f = classState<CrateServbotFields>(a);
    a.prevAnim = a.anim;
    a.tint = kTintNeutral;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (ServbotState(a.state)) { // the exe's table 0x88fd34
    case ServbotState::Bind:
        crateServbotBind(a, f);
        if (a.flags & kActorLive) crateServbotRun(env, a, f); // the bind ends with 0x464bc0
        break;
    case ServbotState::Run: crateServbotRun(env, a, f); break;
    case ServbotState::Free: freeActor(a); break; // exe 0x468fa0 -> 0x5e1490
    default: notPortedAt(kSt0ePort, "0x464a30 state", a.state);
    }
    if (a.anim != a.prevAnim) animSetPose(a, a.anim, 0); // exe 0x5e1c30
    else if (a.variant != kInsideCrate) animStepPose(a); // exe 0x5e1e80
}

constexpr uint32_t kNoBomb = 0xffffffffu;
constexpr uint8_t kRiderHoldPose = 0x01, kRiderKnockedOff = 0x02, kRiderHoldsBomb = 0x04;
enum class RiderSub : uint8_t { Walk = 0, Throw = 1, KnockedOff = 2, DebugThrow = 3 };
enum class ThrowStep : uint8_t { Face = 0, Lob = 1, TurnBack = 2 };
constexpr uint8_t kThrowKey = 0xc, kDebugThrowKey = 0xb, kBombHeldUntilKey = 10, kHoldPoseKey = 6;
constexpr uint16_t kDebugThrowButton = 0x800; // the second pad's pressed bit of the debug throw
const DataTable<CollisionBoxRecord> riderBox{TableId::St0eServbotsRiderBox};
const DataTable<ScalarTable<uint32_t, 4>> riderCries{TableId::St0eServbotsRiderCries};
const DataTable<ScalarTable<uint32_t, 4>> riderShouts{TableId::St0eServbotsRiderShouts};
// the riders' offsets on the boss vehicle's bone 2 (by variant, 8 bytes apart)
struct RiderAnchorOffset : DataRecord {
    static constexpr uint32_t kBytes = 0x6;
    int16_t x = 0; // +0x0
    int16_t y = 0; // +0x2
    int16_t z = 0; // +0x4
    template <class R> static RiderAnchorOffset load(const R& r) {
        RiderAnchorOffset v;
        v.x = r.template read<int16_t>(0x0);
        v.y = r.template read<int16_t>(0x2);
        v.z = r.template read<int16_t>(0x4);
        return v;
    }
};

// exe 0x5e1970(the boss vehicle, 2, the variant's offset): the riders' anchor on the boss vehicle
BonePoint riderAnchor(const AnimatedActor& a) {
    const RiderAnchorOffset o = dataRecord<RiderAnchorOffset>(stateAddress(TableId::St0eServbotsRiderAnchorOffsets) + uint32_t(a.variant) * 8, RiderAnchorOffset::kBytes);
    return bonePointAt(bossVehicle(), 2, o.x, o.y, o.z);
}
// the position: its local point + the anchor
void riderPlace(AnimatedActor& a, RiderFields& f, const BonePoint& o) {
    a.pos.x = int32_t(uint32_t(int32_t(f.local.x) + o.v[0]) << 16);
    a.pos.y = int32_t(uint32_t(int32_t(f.local.y) + o.v[1]) << 16);
    a.pos.z = int32_t(uint32_t(int32_t(f.local.z) + o.v[2]) << 16);
}
// the local point into the position (<< 16), moved by (0, y, z), back into the local point
void riderMoveLocal(AnimatedActor& a, RiderFields& f, int y, int z) {
    a.pos.x = int32_t(uint32_t(int32_t(f.local.x)) << 16);
    a.pos.y = int32_t(uint32_t(int32_t(f.local.y)) << 16);
    a.pos.z = int32_t(uint32_t(int32_t(f.local.z)) << 16);
    integrateVelocity(a, 0, y, z);
}
void riderStoreLocal(AnimatedActor& a, RiderFields& f) {
    f.local.x = a.pos.x.whole;
    f.local.y = a.pos.y.whole;
    f.local.z = a.pos.z.whole;
}
// the player's position (or a point) with the integer x / z jittered by (rng & 0x7f) - 0x3f (x first): the bombs'
// target
void jitterTarget(int32_t t[3]) {
    for (int k = 0; k < 3; k += 2)
        t[k] = int32_t((uint32_t(uint16_t(uint32_t(t[k]) >> 16) + uint16_t((rngStep() & 0x7f) - 0x3f)) << 16) |
                       (uint32_t(t[k]) & 0xffff));
}
void playerTarget(int32_t t[3]) {
    const Player& p = player();
    t[0] = p.pos.x;
    t[1] = p.pos.y;
    t[2] = p.pos.z;
}
// a bomb (pool 0x40 stage type 0x24 = content/yosyonke/st0e_turret's) picked up by a rider (the callers set the rest)
ProjectileActor* pickUpBomb() {
    const uint32_t at = allocActor40(kActorStageClass); // exe 0x5e1280
    if (!at) return nullptr;
    ProjectileActor& e = projectileActorAt(at);
    e.flags = kActorLive;
    e.type = kBombType;
    e.variant = 0;
    e.loopFlags |= kActorSweepGroup2;
    return &e;
}
// the bomb held still in the rider's hands (its pose, its velocities cleared), its damage the boss vehicle's row
void holdBomb(AnimatedActor& a, ProjectileActor& e, const AnimatedActorStats& bossRow) {
    e.rotation.x = 0;
    e.rotation.z = 0;
    e.rotation.y = a.rotation.y;
    e.velocity.x = 0;
    e.acceleration.y = 0;
    e.velocity.z = 0;
    e.velocity.y = 0;
    BombSpawnParams(e).damage = int8_t(bossRow.attack);
}
// the launch of a held bomb at the target t (exe 0x5e3ee0 speed 0x600, gravity 0x40)
void lobBomb(ProjectileActor& bomb, const int32_t t[3]) {
    bomb.velocity.x = 0;
    const int32_t from[3] = {bomb.pos.x, bomb.pos.y, bomb.pos.z};
    bomb.velocity.y = int16_t(launchSpeed(from, t, 0x600, 0x40, 0xffffffffu));
    bomb.velocity.z = -0x600;
    bomb.acceleration.y = 0x40;
}
// the bomb at the rider's bone 2 point
void bombAtHands(AnimatedActor& a, ProjectileActor& bomb, int x, int y) {
    int32_t p[3];
    fixedOfPoint(bonePointAt(a, 2, x, y, 0), p);
    bomb.pos.x = p[0];
    bomb.pos.y = p[1];
    bomb.pos.z = p[2];
}
void riderWalk(AnimatedActor& a, RiderFields& f) { // exe 0x4653b0
    const BonePoint o = riderAnchor(a);
    if (f.walkTicks != 0) {
        riderMoveLocal(a, f, 0, a.velocity.z);
        riderStoreLocal(a, f);
        --f.walkTicks;
    } else {
        f.walkTicks = uint16_t(a.spin.x);
        turnYawTo(a.rotation.y, uint32_t(int32_t(t16(angleOf(player().pos, a.pos)))), 0x20); // exe 0x5e3bc0 / 0x5e3df0
        ++a.subState;
        a.step = 0;
        f.side ^= 1;
    }
    riderPlace(a, f, o);
}
void riderThrow(AnimatedActor& a, RiderFields& f) { // exe 0x4654f0
    const AnimatedActor& boss = bossVehicle();
    const StaticActor& defended = defendedVehicle();
    const AnimatedActorStats bossRow = animatedStats(boss.type);
    // the point 0x400 / -0x4000 along the defended vehicle's yaw, at y 0xf980
    int32_t t[3] = {defended.pos.x, int32_t(0xf9800000u | defended.pos.y.frac), defended.pos.z};
    int32_t d[3];
    rotateByYaw(defended.rotation.y, 0x400, 0, -0x4000, d); // exe 0x5e36f0
    for (int k = 0; k < 3; ++k) t[k] = int32_t(uint32_t(t[k]) + uint32_t(d[k]));
    switch (ThrowStep(a.step)) {
    case ThrowStep::Face:
        turnYawTo(a.rotation.y, uint32_t(int32_t(t16(angleOf(t[0], t[2], a.pos.x, a.pos.z)))),
                  0x20);              // exe 0x5e3bc0 / 0x5e3df0
        if (facesAwayFrom(a, t, 0x20)) { // exe 0x5036e0
            a.anim = 0xc;
            ++a.step;
            f.walkTicks = 0xf;
            a.velocity.y = -0x280;
            a.acceleration.y = 0x40;
            f.flags |= kRiderHoldPose;
            f.bomb = kNoBomb;
            if (ProjectileActor* e = pickUpBomb()) {
                f.flags |= kRiderHoldsBomb;
                holdBomb(a, *e, bossRow);
                BombSpawnParams(projectileActorAt(e->at)).held = 1;
                f.bomb = e->at;
            }
        }
        break;
    case ThrowStep::Lob: {
        if (a.animEnded()) {
            riderMoveLocal(a, f, a.velocity.y, 0);
            a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
            riderStoreLocal(a, f);
        } else {
            bool released = false;
            if (f.walkTicks != 0) {
                --f.walkTicks;
                released = f.walkTicks == 0;
            } else {
                if (a.keyIndex == kHoldPoseKey) f.flags |= kRiderHoldPose;
                riderMoveLocal(a, f, a.velocity.y, 0);
                a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
                riderStoreLocal(a, f);
                released = a.velocity.y >= 0;
            }
            if (released) f.flags &= uint8_t(~kRiderHoldPose);
            if (a.keyIndex == kThrowKey) { // the throw frame
                if (f.bomb != kNoBomb) {
                    ProjectileActor& e = projectileActorAt(f.bomb);
                    jitterTarget(t);
                    lobBomb(e, t);
                    BombSpawnParams(e).flying = 1;
                }
                soundPlayAtPos(uint16_t(riderShouts[rngStep() & 3]), a.pos);
                f.flags &= uint8_t(~kRiderHoldsBomb);
            }
        }
        const BonePoint o = riderAnchor(a);
        if (int(f.local.y) + o.v[1] > int(o.v[1])) { // below the anchor: landed
            f.local.y = 0;
            ++a.step;
        }
        break;
    }
    default: {
        const int yaw = boss.rotation.y;
        const int left = f.side == 0 ? turnYawTo(a.rotation.y, uint32_t(yaw), 0x20)
                                     : turnYawTo(a.rotation.y, uint32_t(yaw + kHalfTurn), 0x14); // exe 0x5e3df0
        if (left == 0) {
            f.walkTicks = uint16_t(a.spin.x);
            a.anim = 1;
            a.subState = uint8_t(RiderSub::Walk);
            a.step = 0;
        }
        break;
    }
    }
    riderPlace(a, f, riderAnchor(a));
}
void riderKnockedOff(const PlayerEnv& env, AnimatedActor& a, RiderFields& f) { // exe 0x465960
    switch (HopStep(a.step)) {
    case HopStep::Rise:
        hopCollide(env, a, f.hits); // exe 0x465ab0
        if ((f.hits & kHopLanded) || a.pos.y.whole > 0) {
            a.velocity.y = int16_t((-int32_t(a.velocity.y)) >> 1);
            a.velocity.z = int16_t(a.velocity.z >> 1);
            ++a.step;
        }
        break;
    case HopStep::Fall:
        hopCollide(env, a, f.hits); // exe 0x465ab0
        if (f.hits & kHopLanded) {
            a.anim = 0x11;
            ++a.step;
            f.lieTicks = 0x3c;
            return;
        }
        if (a.pos.y.whole > 0) {
            a.anim = 0x11;
            ++a.step;
            f.lieTicks = 0x3c;
        }
        break;
    default:
        if (pointVisibleOrNear(a.pos, 0x200) == 0) return; // exe 0x5e59a0
        --f.lieTicks;
        if (f.lieTicks < 0) a.state = uint8_t(ServbotState::Free);
        return;
    }
    moveAlongYaw(a.pos, a.rotation.y + kHalfTurn, 0, a.velocity.y, a.velocity.z); // exe 0x5e36f0
    a.velocity.y = int16_t(a.velocity.y + a.acceleration.y);
    if (a.velocity.y > 0x380) a.velocity.y = 0x380;
}
void riderDebugThrow(AnimatedActor& a, RiderFields& f) { // exe 0x465140 (the second pad's bit 0x800)
    const AnimatedActorStats bossRow = animatedStats(bossVehicle().type);
    int32_t t[3];
    if (pad1().pressed & kDebugThrowButton) {
        if (f.flags & kRiderHoldPose) {
            f.flags &= uint8_t(~kRiderHoldPose);
        } else {
            f.flags |= kRiderHoldPose;
            a.anim = 0xc;
            a.prevAnim = 0xff;
            if (ProjectileActor* e = pickUpBomb()) {
                bombAtHands(a, *e, 0x22, 0x6a);
                playerTarget(t);
                jitterTarget(t); // computed and unused (the RNG advances twice)
                holdBomb(a, *e, bossRow);
                f.bomb = e->at;
            }
        }
    }
    const uint32_t bomb = f.bomb;
    if (a.keyIndex == kDebugThrowKey) {
        if (bomb == 0) return;
        if (bomb == kNoBomb) notPorted(kSt0ePort, "0x465140: the held bomb pointer is -1 (the exe writes through it)");
        playerTarget(t);
        jitterTarget(t);
        lobBomb(projectileActorAt(bomb), t);
        return;
    }
    if (bomb == 0 || int8_t(a.keyIndex) >= int8_t(kDebugThrowKey)) return;
    if (bomb == kNoBomb) notPorted(kSt0ePort, "0x465140: the held bomb pointer is -1 (the exe writes through it)");
    bombAtHands(a, projectileActorAt(bomb), 0x22, 0x6a);
}
void riderBind(AnimatedActor& a, RiderFields& f) { // exe 0x465030
    if (!bindActorModel(a, 0)) {                   // exe 0x5e0ba0
        freeActor(a);                              // exe 0x5e1490
        return;
    }
    a.anim = 1;
    a.state = uint8_t(ServbotState::Run);
    a.airborne = 1;
    a.runFlags |= kActorWarpKept;
    a.prevAnim = 0xff;
    a.prevPos.x = a.pos.x;
    a.flags |= kActorDrawn;
    a.prevPos.y = a.pos.y;
    a.velocity.setXY(0, 0);
    a.velocity.setZSpare(0, 0);
    a.collisionBox.pointAt(riderBox.stateAddress());
    a.interactionBox = nullptr;
    a.interaction = kInteractLift;
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.velocity.y = 0;
    a.velocity.z = -0x30;
    a.acceleration.y = 0x40;
    a.acceleration.z = 0x10;
    a.subState = uint8_t(RiderSub::Walk);
    a.step = 0;
    a.prevPos.z = a.pos.z;
    f.lieTicks = 0x3c;
    f.walkTicks = uint16_t(a.spin.x);
    a.spin.x = 0x32;
    f.local.z = 0;
    f.local.y = 0;
    f.local.x = 0;
    f.flags = 0;
    f.box.result = 0;
}
void riderRun(const PlayerEnv& env, AnimatedActor& a, RiderFields& f) { // exe 0x465110
    f.hits = 0;
    a.tint = kTintNeutral;
    switch (RiderSub(a.subState)) { // the exe's table 0x88fde0
    case RiderSub::Walk: riderWalk(a, f); return;
    case RiderSub::Throw: riderThrow(a, f); return;
    case RiderSub::KnockedOff: riderKnockedOff(env, a, f); return;
    case RiderSub::DebugThrow: riderDebugThrow(a, f); return;
    default: notPortedAt(kSt0ePort, "0x465110 sub-state", a.subState);
    }
}
// knocked off the vehicle: flung (anim 0x10)
void riderKnockOff(AnimatedActor& a) {
    a.subState = uint8_t(RiderSub::KnockedOff);
    a.step = 0;
    a.airborne = 1;
    a.velocity.y = -0x200;
    a.velocity.z = -0x100;
    a.acceleration.y = 0x40;
    a.anim = 0x10;
}
void riderServbot(const PlayerEnv& env, AnimatedActor& a) { // exe 0x464dc0
    RiderFields& f = classState<RiderFields>(a);
    a.prevAnim = a.anim;
    a.tint = kTintNeutral;
    a.savePreviousPose();
    a.savePreviousContact();
    bool cry = false;
    if (storyFlag(kFlagFrontBroken)) {
        if (!(f.flags & kRiderKnockedOff)) {
            f.flags |= kRiderKnockedOff;
            riderKnockOff(a);
            cry = true;
        }
    } else if (f.box.result & kHitAttackByPlayer) {
        riderKnockOff(a);
        f.flags |= kRiderKnockedOff;
        cry = true;
    }
    if (cry) {
        if (f.flags & kRiderHoldsBomb) { // the held bomb bursts
            if (f.bomb == kNoBomb)
                notPorted(kSt0ePort, "0x464dc0: the held bomb pointer is -1 (the exe writes through it)");
            projectileActorAt(f.bomb).state = kBombBlast;
        }
        soundPlayAtPos(uint16_t(riderCries[rngStep() & 3]), a.pos);
    }
    switch (ServbotState(a.state)) { // the exe's table 0x88fdd4
    case ServbotState::Bind:
        riderBind(a, f);
        if (a.flags & kActorLive) riderRun(env, a, f); // the bind ends with 0x465110
        break;
    case ServbotState::Run: riderRun(env, a, f); break;
    case ServbotState::Free: freeActor(a); break; // exe 0x468fa0 -> 0x5e1490
    default: notPortedAt(kSt0ePort, "0x464dc0 state", a.state);
    }
    if (a.anim != a.prevAnim) animSet(a, a.anim, 0);      // exe 0x5e1c50
    else if (f.flags & kRiderHoldPose) animDecodeCurrent(a); // exe 0x5e1f30
    else animStep(a);                                        // exe 0x5e1ea0
    animPose(a);                                             // exe 0x5e2d90
    if ((f.flags & kRiderHoldsBomb) && f.bomb != kNoBomb && int8_t(a.keyIndex) <= int8_t(kBombHeldUntilKey))
        bombAtHands(a, projectileActorAt(f.bomb), 0x11, 0x35); // the held bomb at the hands
    f.box.result = 0;
    f.box.previousX = a.prevPos.x.whole;
    f.box.previousZ = a.prevPos.z.whole;
    if (f.flags & kRiderKnockedOff) return;
    const uint32_t body = hitOwner(a.slot) | hitGroup(kHitGroupBodies) | hitHeightLevel(1) | hitRadius(0x30);
    const uint32_t attack = kHitAttackSolid | 1;
    const int16_t p1[3] = {a.pos.x.whole, t16(a.pos.y.whole - 0x30), a.pos.z.whole};
    hitListSubmit(p1, f.box, body | kHitNoLockOn, attack); // exe 0x5e4120
    const int16_t p2[3] = {a.pos.x.whole, t16(a.pos.y.whole - 0x80), a.pos.z.whole};
    hitListSubmit(p2, f.box, body, attack);
}

// the behaviour bits
constexpr uint8_t kFlungBounces = 0x01;       // bounces off the walls (its arc ends on a bounce)
constexpr uint8_t kFlungNoEntry = 0x04;       // no hit entry (carried, or thrown and spent)
constexpr uint8_t kFlungAirborne = 0x08;      // an arc of its own: no fall when it leaves the ground
constexpr uint8_t kFlungHurtsEnemies = 0x10;  // thrown: its entry hurts the enemies
constexpr uint8_t kFlungIgnoresHits = 0x20;
// the contact bits
constexpr uint8_t kContactWall = 0x01, kContactBounced = 0x02, kContactFellOff = 0x04, kContactOutOfRoom = 0x08,
                  kContactRiding = 0x10, kContactActorWall = 0x20;
constexpr uint32_t kTestOutOfRoom = 0x20000000u;
constexpr uint32_t kTakesHits = kHitAttackByPlayer | kHitAttackThrown | kHitAttackSolid | kHitDamageMask;
// the variant's bits: 0x40 stays down after landing, 0x80 runs at the walls (no turn back)
constexpr uint8_t kFlungStaysDown = 0x40, kFlungNoTurnBack = 0x80;
enum class FlungState : uint8_t { Bind = 0, Run = 1, Carried = 2 };
enum class FlungSub : uint8_t { FlyOut = 0, Run = 1, Hit = 2, TurnAround = 3, Fall = 4, HopOff = 5 };
enum class CarriedSub : uint8_t { Held = 0, Thrown = 1 };
const DataTable<CollisionBoxRecord> flungBox{TableId::St0eServbotsFlungBox};
const DataTable<InteractionBoxRecord> flungLiftBox{TableId::St0eServbotsFlungLiftBox};
// the carried Servbot's wriggle timers (u32 entries; the exe reads their low bytes)
struct WriggleEntry : DataRecord {
    uint8_t ticks = 0;
    static WriggleEntry load(const TableReader& r) {
        WriggleEntry rec;
        rec.ticks = r.u8(0);
        return rec;
    }
};
const DataTable<RecordTable<WriggleEntry, 16, 4>> wriggleTimers{TableId::St0eServbotsWriggleTimers};

// exe 0x492ed0: the actor collision result: a side -> kContactWall | kContactActorWall; bouncing off walls (exe 0x410740)
// -> kContactBounced; riding -> kContactRiding
void flungContacts(AnimatedActor& a, FlungFields& f) {
    const uint32_t r = a.actorHit;
    if (r != 0) {
        if (r & kPushedOutSides) f.contacts |= uint8_t(kContactWall | kContactActorWall);
        if ((f.behaviour & kFlungBounces) && reflectRotationOffWalls(a.rotation, r) != 0) f.contacts |= kContactBounced;
    }
    if (a.onPlatform != 0) f.contacts |= kContactRiding;
}
// exe 0x492e70(a, yaw): blocked (bounced / riding) -> bounce (the fall speed back at half, the speed / 4, the pitch 0),
// true; else the arc along the yaw and the gravity, false
bool flungArc(AnimatedActor& a, FlungFields& f, int yaw) {
    if (f.contacts & (kContactBounced | kContactRiding)) {
        a.velocity.y = int16_t((-int32_t(a.velocity.y)) >> 1);
        a.velocity.z = int16_t(a.velocity.z >> 2);
        a.rotation.x = 0;
        return true;
    }
    moveAlongYaw(a.pos, yaw, 0, a.velocity.y, a.velocity.z); // exe 0x5e36f0
    a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
    return false;
}
// exe 0x493910: the walls (out of the room, pushed by a side, bounced), the ground (on foot: a drop > 0x20 -> fell off,
// true; else snapped; airborne and not bouncing: landed below -> bounced)
bool flungCollide(const PlayerEnv& env, AnimatedActor& a, FlungFields& f) {
    f.contacts = 0;
    uint32_t r = actorTestMove(env, a); // exe 0x40f570
    if (r & kTestOutOfRoom) f.contacts |= kContactOutOfRoom;
    if (int32_t(r) < 0) {
        r = actorPushOut(env, a); // exe 0x410960
        a.wallHit = r;
        if (int32_t(r) < 0) a.pos = a.prevPos;
        if (r & kPushedOutSides) f.contacts = kContactWall;
        if ((f.behaviour & kFlungBounces) && reflectRotationOffWalls(a.rotation, r) != 0) f.contacts |= kContactBounced;
    }
    if (a.onPlatform != 0) return false;
    const int g = actorGround(env, a); // exe 0x40f190
    if (a.airborne == 0) {
        if (g - a.pos.y.whole > 0x20) {
            f.contacts |= kContactFellOff;
            return true;
        }
        a.pos.y.whole = int16_t(g);
        return false;
    }
    if (!(f.behaviour & kFlungBounces) && a.pos.y.whole > g) {
        a.airborne = 0;
        a.pos.y.whole = int16_t(g);
        f.contacts |= kContactBounced;
    }
    return false;
}
// exe 0x493a00: a hit on the ground (unless it ignores hits): its kind (2 from far below); 2 for a strong push (knocked
// down), else 1
int flungHit(AnimatedActor& a, FlungFields& f) {
    const uint32_t v = f.box.result;
    if (f.behaviour & kFlungIgnoresHits) return 0;
    f.hitKind = 0;
    if (!(v & kTakesHits) || a.airborne != 0) return 0;
    f.hitKind = (v & kHitLevelBelow) ? 2 : 1;
    return (v & kHitAttackStrongPush) ? 2 : 1;
}
void flyOutSub(AnimatedActor& a, FlungFields& f) { // exe 0x4932e0: flying out, spinning (smoke), lands, lies, gets up
    switch (a.step) {
    case 0:
        f.behaviour = uint8_t(kFlungIgnoresHits | kFlungAirborne | kFlungBounces);
        a.anim = 0x11;
        a.airborne = 1;
        a.acceleration.y = 0x40;
        a.spin.y = -0x1c7;
        f.flightYaw = a.rotation.y;
        f.timer = 0;
        a.enterStep(1);
        [[fallthrough]];
    case 1:
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + uint16_t(a.spin.y));
        if (flungArc(a, f, f.flightYaw)) {
            a.anim = 0x12;
            a.enterStep(2);
            a.airborne = 0;
        }
        if (f.smoking != 0 && !(f.timer & 1)) {
            if (const uint32_t at = allocActor60(kActorStageClass)) { // exe 0x5e12f0: a smoke puff (stage 3 variant 6)
                StaticActor& e = staticActorAt(at);
                FireEmitterParams p(e);
                e.flags = kActorLive;
                e.loopFlags |= kActorSweepGroup2;
                e.type = kFireEmitterType;
                e.variant = 6;
                e.pos.x.whole = a.pos.x.whole;
                e.pos.y.whole = a.pos.y.whole;
                e.pos.z.whole = a.pos.z.whole;
                p.paramA = 0;
                p.paramB = 0;
                p.lifetime = 4;
            }
        }
        ++f.timer;
        return;
    case 2:
        moveAlongYaw(a.pos, f.flightYaw, 0, 0, a.velocity.z); // exe 0x5e36f0
        if (a.animEnded()) {
            f.timer = 0x1e;
            a.enterStep(3);
        }
        return;
    case 3:
        if (a.variant & kFlungStaysDown) return;
        --f.timer;
        if (f.timer != 0) return;
        a.anim = 0x13;
        a.enterStep(4);
        return;
    case 4:
        if (!a.animEnded()) return;
        f.smoking = 0;
        a.subState = uint8_t(FlungSub::Run);
        a.restartStep();
        return;
    default: return;
    }
}
void runSub(AnimatedActor& a, FlungFields& f) { // exe 0x492880: runs around the player (+-0x600 of the angle to it)
    switch (a.step) {
    case 0: {
        a.anim = 0x14;
        a.velocity.setXY(0, 0);
        a.velocity.z = -0x140;
        f.behaviour = 0;
        const uint32_t r = rngStep() & 0x1f;
        a.enterStep(1);
        a.spin.y = ((0xaaaaaaaau >> r) & 1) ? 0x600 : -0x600;
        break;
    }
    case 1: break;
    case 2:
        --f.turnTimer;
        if (f.turnTimer == 0) {
            a.enterStep(1);
            a.spin.y = int16_t(-a.spin.y);
        }
        integrateVelocity(a, 0, 0, a.velocity.z);
        return;
    default: integrateVelocity(a, 0, 0, a.velocity.z); return;
    }
    if (f.contacts == 0 && !(a.actorHit & kPushedOutSides)) {
        const int ang = angleOf(player().pos, a.pos) + a.spin.y; // exe 0x5e3bc0
        // exe 0x5e3db0(actor, angle, 0x20): the yaw += 0x20 toward the angle when at least 0x20 away
        const int d = angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(ang));
        if (d >= 0x20) a.rotation.y = int16_t(a.rotation.y + 0x20);
        if (d <= -0x20) a.rotation.y = int16_t(a.rotation.y - 0x20);
        integrateVelocity(a, 0, 0, a.velocity.z);
        return;
    }
    if (!(a.variant & kFlungNoTurnBack)) {
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + kHalfTurn);
        f.turnTimer = 0x1e;
        a.enterStep(2);
        integrateVelocity(a, 0, 0, a.velocity.z);
        return;
    }
    a.subState = uint8_t(FlungSub::HopOff);
    a.restartStep();
    integrateVelocity(a, 0, 0, a.velocity.z);
}
void hitSub(AnimatedActor& a, FlungFields& f) { // exe 0x493450: knocked back (hit from the front: anim 0x18) or forward
    switch (a.step) {
    case 0: {
        f.behaviour = kFlungIgnoresHits;
        a.velocity.z = 0x300;
        a.acceleration.z = -0x30;
        if (f.hitKind == 2) a.velocity.z = 0x500;
        // exe 0x5e3c00(the contact point, the position): the actor's direction seen from the hit's point
        const int ang = atanAngle(float(int(a.pos.x.whole) - f.box.contact.x),
                                  float(int(a.pos.z.whole) - f.box.contact.z)) &
                        kAngleMask;
        const int d = angleDiff(uint32_t(int32_t(a.rotation.y)), uint32_t(ang)); // exe 0x5e3d80
        if (uint32_t(d + kQuarterTurn) < uint32_t(kHalfTurn)) {
            a.anim = 0x18;
            f.timer = 8;
        } else {
            a.anim = 0x17;
            f.timer = 9;
            a.velocity.z = int16_t(-a.velocity.z);
            a.acceleration.z = int16_t(-a.acceleration.z);
        }
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + 0x20);
        a.enterStep(1);
        return;
    }
    case 1:
        integrateVelocity(a, 0, 0, a.velocity.z);
        a.velocity.z = int16_t(uint16_t(a.velocity.z) + uint16_t(a.acceleration.z));
        --f.timer;
        if (f.timer == 0 || int16_t(uint16_t(a.velocity.z) ^ uint16_t(a.acceleration.z)) >= 0) a.enterStep(2);
        break;
    case 2: break;
    default: return;
    }
    if (!a.animEnded()) return;
    a.subState = uint8_t(FlungSub::Run);
    a.restartStep();
}
void turnAroundSub(AnimatedActor& a, FlungFields& f) { // exe 0x493580: anim 0x12 then 0x13
    switch (a.step) {
    case 0:
        a.anim = 0x12;
        f.startKey = 9;
        f.behaviour = 0;
        a.rotation.y = int16_t(uint16_t(a.rotation.y) + kQuarterTurn);
        a.enterStep(1);
        return;
    case 1:
        if (!a.animEnded()) return;
        a.anim = 0x13;
        a.enterStep(2);
        return;
    case 2:
        if (!a.animEnded()) return;
        a.subState = uint8_t(FlungSub::Run);
        a.restartStep();
        return;
    default: return;
    }
}
void fallSub(AnimatedActor& a, FlungFields& f) { // exe 0x4935e0: fall (anim 0xf), land (0x10)
    switch (a.step) {
    case 0:
        a.anim = 0xf;
        a.acceleration.y = 0x18;
        a.velocity.y = 0;
        f.behaviour = uint8_t(kFlungAirborne | kFlungBounces);
        a.enterStep(1);
        [[fallthrough]];
    case 1:
        if (!flungArc(a, f, a.rotation.y)) return;
        a.anim = 0x10;
        a.enterStep(2);
        a.airborne = 0;
        return;
    case 2:
        if (!a.animEnded()) return;
        a.subState = uint8_t(FlungSub::Run);
        a.restartStep();
        return;
    default: return;
    }
}
void hopOffSub(AnimatedActor& a, FlungFields& f) { // exe 0x493650: hop off (anim 0xe), land (0x10)
    switch (a.step) {
    case 0:
        a.anim = 0xe;
        a.acceleration.y = 0x18;
        a.velocity.y = -0x140;
        a.velocity.z = 0;
        a.airborne = 1;
        f.behaviour = uint8_t(kFlungAirborne | kFlungBounces);
        a.enterStep(1);
        [[fallthrough]];
    case 1:
        if (flungArc(a, f, a.rotation.y)) {
            a.anim = 0x10;
            a.enterStep(2);
            a.airborne = 0;
        }
        if (a.velocity.y < 0) return;
        a.velocity.z = int16_t(uint16_t(a.velocity.z) - 0x20);
        if (a.velocity.z < -0x140) a.velocity.z = -0x140;
        return;
    case 2:
        if (!a.animEnded()) return;
        a.subState = uint8_t(FlungSub::Run);
        a.restartStep();
        return;
    default: return;
    }
}
void flungRun(const PlayerEnv& env, AnimatedActor& a, FlungFields& f) { // exe 0x493230
    if (a.actorHit != 0) flungContacts(a, f);
    if (a.wasOnPlatform != 0 && a.onPlatform == 0 && !(f.behaviour & kFlungAirborne)) { // left the platform it rode
        a.subState = uint8_t(FlungSub::Fall);
        a.restartStep();
    }
    switch (flungHit(a, f)) {
    case 1:
        a.subState = uint8_t(FlungSub::Hit);
        a.restartStep();
        break;
    case 2:
        a.velocity.y = -0x300;
        a.velocity.z = -0x80;
        a.subState = uint8_t(FlungSub::FlyOut);
        a.restartStep();
        break;
    default: break;
    }
    switch (FlungSub(a.subState)) { // the exe's table 0x892b64
    case FlungSub::FlyOut: flyOutSub(a, f); break;
    case FlungSub::Run: runSub(a, f); break;
    case FlungSub::Hit: hitSub(a, f); break;
    case FlungSub::TurnAround: turnAroundSub(a, f); break;
    case FlungSub::Fall: fallSub(a, f); break;
    case FlungSub::HopOff: hopOffSub(a, f); break;
    default: notPortedAt(kSt0ePort, "0x493230 sub-state", a.subState);
    }
    if (flungCollide(env, a, f) && !(f.behaviour & kFlungAirborne)) {
        a.subState = uint8_t(FlungSub::Fall);
        a.restartStep();
    }
    if (!playerLiftTargetCheck(a, 0)) return; // exe 0x573a60
    a.enterState(uint8_t(FlungState::Carried));
    f.behaviour |= kFlungNoEntry;
    a.onPlatform = 0;
}
void flungBind(const PlayerEnv& env, AnimatedActor& a, FlungFields& f) { // exe 0x493110
    if (!bindActorModel(a, f.model)) {                                   // exe 0x5e0bc0(a, &model)
        freeActor(a);                                                    // exe 0x5e1490
        return;
    }
    a.flags |= kActorDrawn;
    a.anim = 0;
    a.prevAnim = 0xff;
    const uint16_t hp = hasAnimatedStats(a.type) ? uint16_t(animatedStats(a.type).hitPoints) : 0;
    a.maxHitPoints = int16_t(hp);
    a.hitPoints = int16_t(hp);
    a.rotation.z = 0;
    a.rotation.x = 0;
    a.collisionBox.pointAt(flungBox.stateAddress());
    a.interactionBox.pointAt(flungLiftBox.stateAddress());
    a.interaction = kInteractLift;
    a.airborne = 0;
    a.onPlatform = 0;
    a.clearClassBlock(); // exe 0x40dc40
    f.offscreenTicks = 0x1e;
    a.parts[2].flags &= uint8_t(~kPartHidden);
    a.parts[6].flags |= kPartHidden;
    a.parts[8].flags |= kPartHidden;
    a.parts[7].flags |= kPartHidden;
    a.enterState(uint8_t(FlungState::Run));
    switch (a.variant & 0xf) {
    case 0:
        a.velocity.y = -0x300;
        a.velocity.z = -0x80;
        f.smoking = 1;
        break;
    case 1: f.smoking = 1; break;
    case 2: a.subState = uint8_t(FlungSub::Run); break;
    default: break;
    }
    flungRun(env, a, f);
}
void flungThrown(const PlayerEnv& env, AnimatedActor& a, FlungFields& f) { // exe 0x493850
    if (f.box.result & kHitAttackSolid) {
        if (a.hitPoints >= 0) {
            f.flightYaw = int16_t(uint16_t(f.flightYaw) + kHalfTurn);
            a.hitPoints = -1;
            f.behaviour |= kFlungNoEntry;
        }
    } else if (a.actorHit != 0) {
        flungContacts(a, f);
        a.hitPoints = -1;
    }
    a.rotation.x = int16_t(uint16_t(a.rotation.x) + 0x100);
    if (flungArc(a, f, a.rotation.y)) {
        if (a.step == 0 && !(f.contacts & kContactRiding)) { // the first bounce: one more hop
            integrateVelocity(a, 0, a.velocity.y, a.velocity.z);
            a.velocity.y = int16_t(uint16_t(a.velocity.y) + uint16_t(a.acceleration.y));
            a.airborne = 1;
            a.enterStep(1);
            flungCollide(env, a, f);
            return;
        }
        a.airborne = 0;
        a.enterState(uint8_t(FlungState::Run), uint8_t(FlungSub::TurnAround));
    }
    flungCollide(env, a, f);
}
void flungCarried(const PlayerEnv& env, AnimatedActor& a, FlungFields& f) { // exe 0x493710
    if (a.step == 0) {
        f.timer = wriggleTimers[rngStep() & 0xf].ticks;
        a.anim = 0x16;
        a.enterStep(1);
    }
    --f.timer;
    if (f.timer == 0) f.timer = wriggleTimers[rngStep() & 0xf].ticks;
    carryAtHands(a, 0, 0, 0);               // exe 0x573ab0
    const CarryRelease r = carryReleased(); // exe 0x573950
    if (r == CarryRelease::Held) return;
    if (r == CarryRelease::Thrown) {
        int32_t v[3];
        playerAimVector(0, -0x100, -0x400, v); // exe 0x5e3e50
        a.velocity.x = 0;
        a.velocity.y = int16_t(v[1]);
        a.velocity.z = int16_t(v[2]);
        f.behaviour = uint8_t(kFlungHurtsEnemies | kFlungBounces);
    } else {
        a.velocity.y = -0x200;
        a.velocity.z = -0x100;
        f.behaviour = kFlungBounces;
    }
    a.acceleration.y = 0x40;
    a.rotation.y = player().rotation.y;
    f.flightYaw = player().rotation.y;
    a.airborne = 1;
    a.hitPoints = 1;
    carryDropFromPlayer(a); // exe 0x573990
    a.subState = uint8_t(CarriedSub::Thrown);
    a.restartStep();
    flungThrown(env, a, f);
}
void flungServbot(const PlayerEnv& env, AnimatedActor& a) { // exe 0x492f20
    FlungFields& f = classState<FlungFields>(a);
    a.prevAnim = a.anim;
    a.tint = kTintSpent;
    a.savePreviousPose();
    a.savePreviousContact();
    switch (FlungState(a.state)) { // the exe's table 0x892b54
    case FlungState::Bind: flungBind(env, a, f); break;
    case FlungState::Run: flungRun(env, a, f); break;
    case FlungState::Carried: // exe 0x4936f0: the table 0x892b8c
        if (CarriedSub(a.subState) == CarriedSub::Held) flungCarried(env, a, f);
        else if (CarriedSub(a.subState) == CarriedSub::Thrown) flungThrown(env, a, f);
        else notPortedAt(kSt0ePort, "0x4936f0 sub-state", a.subState);
        break;
    default: notPortedAt(kSt0ePort, "0x492f20 state", a.state);
    }
    const int far = int(uint32_t(stage().depthRange) << 2);
    if ((f.contacts & kContactOutOfRoom) && distance3(a.pos, player().pos) > far) { // exe 0x5e3a60
        freeActor(a);                                                               // exe 0x5e1490
        return;
    }
    if (int(a.pos.y.whole) > far) {
        freeActor(a);
        return;
    }
    if (pointVisibleOrNear(a.pos, 0x200) != 0) { // exe 0x5e59a0
        --f.offscreenTicks;
        if (f.offscreenTicks == 0) {
            freeActor(a);
            return;
        }
    } else {
        f.offscreenTicks = 0x1e;
    }
    if (!(a.flags & kActorLive)) return;
    if (a.anim == a.prevAnim) {
        animStepPose(a); // exe 0x5e1e80
    } else {
        animSetPose(a, a.anim, int(f.startKey)); // exe 0x5e1c30
        f.startKey = 0;
    }
    f.box.result = 0;
    f.box.previousX = a.prevPos.x.whole;
    f.box.previousZ = a.prevPos.z.whole;
    const AnimatedActorStats row = animatedStats(a.type);
    const uint8_t b = f.behaviour;
    if (!(b & kFlungNoEntry)) {
        const int16_t p[3] = {a.pos.x.whole, t16(a.pos.y.whole - 0x40), a.pos.z.whole};
        if (b & kFlungHurtsEnemies) // thrown: hurts the enemies
            hitListSubmit(p, f.box,
                          hitOwner(a.slot) | hitGroup(kHitGroupPlayerAttacks) | kHitContactPoint | hitRadius(0x30),
                          uint32_t(row.bodyAttack) | kHitAttackByPlayer | kHitAttackThrown);
        else
            hitListSubmit(p, f.box,
                          hitOwner(a.slot) | hitHeightLevel(row.hitHeight) | hitGroup(kHitGroupLiftable) |
                              kHitCompareHeights | kHitNoLockOn | kHitContactPoint | hitRadius(0x1c),
                          kHitAttackSolid);
    }
    actorCollisionSubmitMover(a); // exe 0x4140b0
}

} // namespace

void crateServbotUpdate(const PlayerEnv& env, uint32_t actor) { crateServbot(env, animatedActorAt(actor)); }
void riderServbotUpdate(const PlayerEnv& env, uint32_t actor) { riderServbot(env, animatedActorAt(actor)); }
void flungServbotUpdate(const PlayerEnv& env, uint32_t actor) { flungServbot(env, animatedActorAt(actor)); }

} // namespace od::port
