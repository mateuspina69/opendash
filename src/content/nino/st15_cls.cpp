// The ST15 / ST16 group member, class 0x4b6280 (see st15_cls.hpp), one routine per exe routine. Render output (the
// shadow exe 0x5f20f0) is left out. Names are hypotheses from the observed behaviour.
//
// Its group is an e0 controller (the ST16 box spawner, content/nino/field_spawners): the group's bar (its lever slot,
// the "leader" the members ride), the call to it (the boxes missing) and the group's boxes (lift boxes, static type
// 0x25). The member walks the room's node graph, runs to the variant's points when its point bits allow and sprays there
// (pool 0x40 type 0x13, content/nino/st15_spray), takes the group's boxes to (0x500, 0x200) and throws them, or jumps
// onto the bar and rides it; knocked back by hits; lifted / carried / thrown by the player; out of hit points it runs
// off.
#include "content/nino/st15_cls.hpp"
#include "content/nino/stage_timer.hpp"
#include "content/nino/st15_cls_state.hpp"
#include "engine/data_source.hpp"
#include "game/loaded_data.hpp"
#include "game/stage_smoke.hpp"

#include "content/nino/field_spawners.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/carry.hpp"
#include "game/distances.hpp"
#include "game/vectors.hpp"
#include "game/game_state.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/lift_box.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/rewards.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"
#include "game/visibility.hpp"

namespace od::port {

using namespace st15_cls_state;

namespace {

constexpr uint8_t kMemberBounces = 0x01;   // the walls reflect its flight yaw
constexpr uint8_t kMemberThrown = 0x02;    // its hit entry hurts
constexpr uint8_t kMemberNoHitEntry = 0x04;
constexpr uint8_t kMemberKeepsOn = 0x08;   // a lost ground / platform does not start the fall
constexpr uint8_t kMemberNoDamage = 0x10;
constexpr uint8_t kMemberRunning = 0x20;   // its hit entry compares heights; the wall turn returns to the walk
constexpr uint8_t kMemberHoldsBox = 0x40;  // no solid / mover, no lift
constexpr uint8_t kMemberHitWall = 0x01;
constexpr uint8_t kMemberLanded = 0x02;
constexpr uint8_t kMemberGroundLost = 0x04;
constexpr uint8_t kMemberOffRoom = 0x08;   // the move test's water bit (0x20000000)
constexpr uint8_t kMemberOnPlatform = 0x10;
constexpr uint8_t kMemberBumped = 0x21;    // an actor's side (with the wall bit)

// The bar the members ride (the group's lever, a static type 1): its rider (2: a member on it), the member registered on
// it (the variants' init)
struct BarRide {
    explicit BarRide(const StaticActor& actor) : BarRide(const_cast<StaticActor&>(actor)) {}
    explicit BarRide(StaticActor& actor) : rider(actor.params[1]), member(actor.params[2]) {}
    uint8_t& rider;
    uint8_t& member;
};
constexpr uint8_t kBarFree = 0, kBarRidden = 2;

// A node of the room's graph (8 bytes): four links (bit 7: none), its point
struct RouteNode : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    std::array<int8_t, 4> links{}; // +0x0
    int16_t x = 0; // +0x4
    int16_t z = 0; // +0x6
    template <class R> static RouteNode load(const R& r) {
        RouteNode v;
        for (size_t i = 0; i < v.links.size(); ++i) v.links[i] = r.template read<int8_t>(0x0 + i * sizeof(int8_t));
        v.x = r.template read<int16_t>(0x4);
        v.z = r.template read<int16_t>(0x6);
        return v;
    }
};
RouteNode routeNode(int8_t n) {
    return dataRecord<RouteNode>(dataScalar<uint32_t>(stage().townRoutes) + uint32_t(int32_t(n)) * 8, RouteNode::kBytes);
}
// The variant's points (exe data, 0x894778 + v * 12): their count, their x / z table, their 16.16 positions
struct VariantPoints : DataRecord {
    static constexpr uint32_t kBytes = 0xc;
    uint32_t count = 0; // +0x0
    uint32_t xz = 0; // +0x4: s16 x / z pairs
    uint32_t positions = 0; // +0x8: 16.16 x / y / z
    template <class R> static VariantPoints load(const R& r) {
        VariantPoints v;
        v.count = r.template read<uint32_t>(0x0);
        v.xz = r.template read<uint32_t>(0x4);
        v.positions = r.template read<uint32_t>(0x8);
        return v;
    }
};
VariantPoints variantPoints(uint8_t v) { return dataRecord<VariantPoints>(stateAddress(TableId::St15ClsVariantPoints) + uint32_t(v) * 12, VariantPoints::kBytes); }

// [0x894790]
enum class MemberState : uint8_t { Init = 0, Active = 1, Lifted = 2 };
// [0x89479c]
enum class MemberSub : uint8_t {
    Walk = 0, GoTo = 1, WallTurn = 2, Watch = 3, Spray = 4, Ride = 5, CarryBox = 6, Fall = 7, Knocked = 8, Slide = 9,
    GetUp = 10, RunOff = 11,
};
// the lifted sub-states
enum class LiftedSub : uint8_t { Carried = 0, Thrown = 1 };
// the sub-state after the walk to a target (afterWalk)
constexpr uint8_t kAfterPoint = 4, kAfterBar = 5, kAfterBox = 6;
// the animations
constexpr uint8_t kAnimStand = 0, kAnimWalk = 1, kAnimRun = 2, kAnimCarried = 3, kAnimLift = 4, kAnimCarry = 5,
                  kAnimSpray = 6, kAnimSprayEnd = 7, kAnimKnocked = 8, kAnimGetUp = 9, kAnimKnockedFront = 0xa,
                  kAnimJump = 0xb, kAnimRide = 0xc, kAnimFall = 0xd, kAnimThrowBox = 0xe;
constexpr uint8_t kNoAnim = 0xff;
const DataTable<CollisionBoxRecord> memberBox{TableId::St15ClsMemberBox};
const DataTable<InteractionBoxRecord> memberLiftBox{TableId::St15ClsMemberLiftBox};
const DataTable<ScalarTable<int16_t, 4>> boxHold{TableId::St15ClsBoxHold}; // the box's offset from the bones 3 / 5 (and the spare short)
const DataTable<ScalarTable<uint8_t, 0x40>> struggleTicks{TableId::St15ClsStruggleTicks}; // (* 4: every 4th byte)
constexpr uint8_t kSprayType = 0x13;
constexpr uint8_t kTimerType = 0x1b;
constexpr int kFreedPenalty = 10000;
constexpr uint16_t kSoundSpray = 0x199, kSoundSprayTurn = 0x18e, kSoundJump = 0x19a, kSoundKnocked = 0x197,
                   kSoundKnockedDown = 0x19b, kSoundStruggle = 0x19c, kSoundBoxLift = 0x198;
constexpr int16_t kGravity = 0x40;
constexpr int16_t kBoxDropX = 0x500, kBoxDropZ = 0x200;
constexpr int16_t kBarX = 0x568, kBarZ = 0x690, kBarYaw = 0xc00;


void toSub(AnimatedActor& actor, MemberSub s) {
    actor.subState = uint8_t(s);
    actor.enterStep(0);
}
// the member's group (its e0 controller, by the exe's unchecked index arithmetic)
ControllerActor& groupController(const MemberFields& member) {
    return controllerPool().at(kControllerPool + (uint32_t(member.group) - kControllerClass) * ControllerPool::stride());
}
BoxSpawnerFields& groupOf(const MemberFields& member) { return classState<BoxSpawnerFields>(groupController(member)); }
// the target {x << 16, 0, z << 16} (the exe only writes the high words of its stack vector: the rest zero here)
void targetPoint(const MemberFields& member, int32_t out[3]) {
    out[0] = int32_t(uint32_t(uint16_t(member.targetX)) << 16);
    out[1] = 0;
    out[2] = int32_t(uint32_t(uint16_t(member.targetZ)) << 16);
}
int bearingFrom(const AnimatedActor& actor, const int32_t pt[3]) {
    return angleOf(pt[0], pt[2], actor.pos.x, actor.pos.z);
}

// exe 0x4a1b90(source, dy, variant): a static type 3 (exe 0x5e12f0) at the source's integer position, y + dy
void spawnSmoke(const AnimatedActor& actor, int dy, uint8_t v) {
    const uint32_t e = allocActor60(kActorStageClass);
    if (!e) return;
    StaticActor& smoke = staticActorAt(e);
    auto&& params = SmokeParams(actorHeaderAt(e));
    smoke.variant = v;
    smoke.loopFlags |= kActorSweepGroup2;
    smoke.flags = kActorLive;
    smoke.type = kSmokeType;
    smoke.pos.x.whole = actor.pos.x.whole;
    smoke.pos.y.whole = int16_t(uint16_t(actor.pos.y.whole) + uint32_t(dy));
    smoke.pos.z.whole = actor.pos.z.whole;
    params.driftX = 0;
    params.driftZ = 0;
    params.life = 4;
}

// the static of a slot id by the exe's unchecked index arithmetic (an invalid slot: no slot of the pool, an error)
StaticActor& staticAtSlot(int32_t slot) {
    return staticPool().at(kStaticPool + uint32_t(slot - int32_t(kStaticClass)) * StaticPool::stride());
}
// exe 0x4b7ff0: the group's bar (the static of the group's signed lever slot) while the group and it are alive; its
// address, 0 when none
uint32_t groupBar(const MemberFields& member) {
    const BoxSpawnerFields& group = groupOf(member);
    if (!(groupController(member).flags & kActorLive)) return 0;
    const StaticActor& bar = staticAtSlot(int8_t(group.lever));
    return (bar.flags & kActorLive) ? slotIdentityWord(bar) : 0;
}

// exe 0x4b82d0: freed while lifted (state 2): the balance - 10000 (exe 0x5e51d0) + an e0 timer 0x16
void freedPenalty(const AnimatedActor& actor) {
    if (MemberState(actor.state) != MemberState::Lifted) return;
    balanceAdd(-kFreedPenalty);
    const uint32_t e = allocActorE0(); // exe 0x5e1440
    if (!e) return;
    ControllerActor& timer = controllerActorAt(e);
    auto&& params = StageTimerParams(controllerActorAt(e));
    timer.flags = kActorLive;
    timer.type = kTimerType;
    timer.variant = 0;
    params.value = 0x16;
    params.limit = 0x7f;
    params.flag = -1;
}

// exe 0x4b7de0(actor, yaw): landed -> a bounce (the y speed - / 2, the z speed / 4), 1; else moved by (0, vy, vz)
// along the yaw (exe 0x5e36f0), gravity 0x40, 0
int fallMove(AnimatedActor& actor, const MemberFields& member, int yaw) {
    if (member.contact & kMemberLanded) {
        actor.velocity.y = int16_t(uint16_t(uint32_t(-int32_t(actor.velocity.y)) >> 1));
        actor.velocity.z = int16_t(actor.velocity.z >> 2);
        return 1;
    }
    moveAlongYaw(actor.pos, yaw, 0, actor.velocity.y, actor.velocity.z);
    actor.velocity.y += uint32_t(kGravity);
    return 0;
}

// exe 0x4b8040: the walls / the ground -> the contact bits; 1 when the ground is more than 0x20 below (walking)
int collide(const PlayerEnv& env, AnimatedActor& actor, MemberFields& member) {
    uint32_t p = 0;
    member.contact = 0;
    const uint32_t r = actorTestMove(env, actor); // exe 0x40f570
    if (r & 0x20000000) member.contact |= kMemberOffRoom;
    if (int32_t(r) < 0) {
        p = actorPushOut(env, actor); // exe 0x410960
        actor.wallHit = p;
        if (int32_t(p) < 0) actor.pos = actor.prevPos;
        if (p & 0xf00) member.contact = kMemberHitWall;
        if (member.behaviour & kMemberBounces) { // the exe's stack angles {pitch, flight yaw, roll}: the yaw reflected
            int16_t pitch = actor.rotation.x, yaw = member.flightYaw;
            const int16_t roll = actor.rotation.z;
            if (reflectRotationOffWalls(pitch, yaw, p) != 0) member.contact |= kMemberLanded; // exe 0x410740
            member.flightYaw = yaw;
            actor.rotation.z = roll;
        }
    }
    if (actor.onPlatform != 0) return 0;
    if (actor.airborne == 0) {
        const int g = actorGround(env, actor); // exe 0x40f190
        if (g - int(actor.pos.y.whole) > 0x20) {
            member.contact |= kMemberGroundLost;
            return 1;
        }
        actor.pos.y.whole = int16_t(g);
        return 0;
    }
    if (!(member.behaviour & kMemberBounces) && (p & 0x3000)) {
        actor.airborne = 0;
        actor.pos.y.whole = int16_t(actorGround(env, actor));
        member.contact |= kMemberLanded;
    }
    return 0;
}

// exe 0x4b8180: the actor-collision result (bumped): a side -> the bumped bits, reflected -> | landed; on a platform ->
// | on platform
void bumped(AnimatedActor& actor, MemberFields& member) {
    const uint32_t r = actor.actorHit;
    if (r) {
        if (r & 0xf00) member.contact = kMemberBumped;
        if ((member.behaviour & kMemberBounces) && reflectRotationOffWalls(actor.rotation, r) != 0)
            member.contact |= kMemberLanded;
    }
    if (actor.onPlatform) member.contact |= kMemberOnPlatform;
}

// exe 0x4b81d0: the hit: the stagger meter (0x10 or a strong hit -> 1), the balance on the player's hits (its row's byte
// 9, byte 10 on a strong push), the hit points (exe 0x5e52f0, dead -> 2); 0 = no reaction
int damage(AnimatedActor& actor, MemberFields& member) {
    const AnimatedActorStats row = animatedStats(actor.type);
    const uint32_t h = member.hitBox.result;
    const uint32_t d = h & kHitDamageMask;
    int res = 0;
    if (member.behaviour & kMemberNoDamage) return 0;
    if (!((d != 0 && (h & (kHitAttackStrong | kHitAttackByPlayer | kHitAttackSolid))) || (h & 0x6080000))) return 0;
    member.lastHitKind = uint8_t(h >> 16);
    const uint8_t m = member.stagger;
    if (int(m) + int(d) < 0x10 && !(h & 0x6120000)) {
        member.stagger = uint8_t(m + d);
    } else {
        res = 1;
        member.stagger = 0;
    }
    actor.tint = kTintFlash;
    if ((h & kHitAttackByPlayer) && res) {
        if (!(h & kHitAttackStrongPush)) {
            balanceAdd(-int(row.hitSize)); // exe 0x5e51d0 (its row's byte 9)
        } else {
            balanceAdd(-int(row.balanceLossThrown)); // (its row's byte 10)
            res = 1;
            member.stagger = 0;
        }
    }
    if (hitPointsAdd(actor, -int(d)) != 0) res = 2; // exe 0x5e52f0
    return res;
}

// exe 0x4b7e30: the nearest of the variant's points whose bit (0x10 << k, then 1 << k) is set in the point mask;
// 1-based, 0 none
uint32_t nearestPoint(const AnimatedActor& actor, const MemberFields& member) {
    const VariantPoints t = variantPoints(actor.variant);
    for (int pass = 0; pass < 2; ++pass) {
        uint32_t n = t.count;
        uint32_t bit = (pass == 0 ? 0x10u : 1u) << ((n - 1) & 31);
        int best = 0x7fffffff;
        uint32_t found = 0;
        for (; n != 0; --n, bit >>= 1) {
            if (!(bit & uint32_t(int32_t(member.pointMask.get())))) continue;
            const int dd = planarDistance(actor.pos, dataFixed16Vec3(t.positions + n * 12 - 0xc)); // exe 0x5e3b00
            if (dd < best) {
                best = dd;
                found = n;
            }
        }
        if (found || pass == 1) return found;
    }
    return 0;
}
// exe 0x4b68a0(block, v, k): the point k - 1, the target its x / z
void setPoint(MemberFields& member, uint8_t v, uint32_t k) {
    k -= 1;
    member.point = uint8_t(k);
    const uint32_t p = variantPoints(v).xz + k * 4;
    member.targetX = dataScalar<int16_t>(p);
    member.targetZ = dataScalar<int16_t>(p + 2);
}
// exe 0x4b6860: a point allowed -> walk there (the walk to a target, then the spray)
bool goPoint(AnimatedActor& actor, MemberFields& member) {
    const uint32_t k = nearestPoint(actor, member);
    if (!k) return false;
    setPoint(member, actor.variant, k);
    member.afterWalk = kAfterPoint;
    toSub(actor, MemberSub::GoTo);
    return true;
}
// exe 0x4b6910: the group's bar exists, the group calls (some boxes missing), it is farther than 0x200 from the player,
// alive and free
bool barCalls(const MemberFields& member) {
    const uint32_t b = groupBar(member);
    if (!b) return false;
    if (!groupOf(member).boxesMissing) return false;
    const StaticActor& bar = staticActorAt(b);
    if (planarDistance(player().pos, bar.pos) <= 0x200) return false;
    return (bar.flags & kActorLive) && BarRide(staticActorAt(b)).rider == kBarFree;
}
// exe 0x4b68d0: the bar calls -> walk to (0x568, 0x690), then the ride
bool goBar(AnimatedActor& actor, MemberFields& member) {
    if (!barCalls(member)) return false;
    member.targetX = kBarX;
    member.targetZ = kBarZ;
    member.afterWalk = kAfterBar;
    toSub(actor, MemberSub::GoTo);
    return true;
}
// the platform keys (& 0x1fffff) of the group's 4 boxes that ride a platform
int heldBoxes(const BoxSpawnerFields& group, uint32_t list[4]) {
    int n = 0;
    for (uint32_t k = 0; k < 4; ++k) {
        const uint32_t id = group.boxes[k];
        if (id == 0) continue;
        const StaticActor& s = staticAtSlot(int32_t(id));
        if (s.onPlatform == 0) continue;
        list[n++] = slotIdentityWord(s.platform) & 0x1fffff;
    }
    return n;
}
// the player stands on the static with this key
bool playerStandsOn(uint32_t key) {
    const Player& p = player();
    return p.onPlatform && (slotIdentityWord(p.platform) & 0x1fffff) == key;
}
// exe 0x4b69d0: the group's box of the frame (& 3) when it is alive, unheld, the player not on it, within 0x10 in y,
// not at x >= 0x500 with z <= 0x400 and no other member's platform; else 0
uint32_t freeBox(const AnimatedActor& actor, const MemberFields& member) {
    const BoxSpawnerFields& group = groupOf(member);
    if (!(groupController(member).flags & kActorLive)) return 0;
    uint32_t list[4];
    const int n = heldBoxes(group, list);
    const uint32_t id = group.boxes[gameState().frame & 3u];
    if (id == 0) return 0;
    const StaticActor& b = staticAtSlot(int32_t(id));
    const uint32_t key = slotIdentityWord(b) & 0x1fffff;
    if (!(b.flags & kActorLive)) return 0;
    if (!LiftBoxHolding(b).unheld()) return 0;
    if (playerStandsOn(key)) return 0;
    if (uint32_t((int32_t(uint32_t(b.pos.y) - uint32_t(actor.pos.y)) >> 16) + 0x10) >= 0x20) return 0;
    if (b.pos.x.whole >= 0x500 && uint16_t(b.pos.z.whole) <= 0x400) return 0;
    for (int k = n - 1; k >= 0; --k)
        if (list[k] == key) return 0;
    return id;
}
// exe 0x4b6970: a free box -> walk to it (the walk to a target, then the carry)
uint32_t goBox(AnimatedActor& actor, MemberFields& member) {
    const uint32_t id = freeBox(actor, member);
    if (!id) return 0;
    const StaticActor& b = staticAtSlot(int32_t(id));
    member.targetX = b.pos.x.whole;
    member.point = uint8_t(id);
    member.targetZ = b.pos.z.whole;
    member.afterWalk = kAfterBox;
    toSub(actor, MemberSub::GoTo);
    return id;
}
StaticActor& boxOfMember(const MemberFields& member) { return staticAtSlot(int32_t(member.point)); }
// exe 0x4b7620(actor, block): the box still takeable: 0; gone / held / out of y range / another's platform: -1 (the
// player standing on it: 0)
int boxLost(const AnimatedActor& actor, const MemberFields& member) {
    const BoxSpawnerFields& group = groupOf(member);
    uint32_t list[4];
    int n = 0;
    if (groupController(member).flags & kActorLive) n = heldBoxes(group, list);
    const StaticActor& b = boxOfMember(member);
    const uint32_t key = slotIdentityWord(b) & 0x1fffff;
    if (!(b.flags & kActorLive)) return -1;
    if (!LiftBoxHolding(b).unheld()) return -1;
    if (playerStandsOn(key)) return 0;
    if (uint32_t((int32_t(uint32_t(b.pos.y) - uint32_t(actor.pos.y)) >> 16) + 0x10) >= 0x20) return -1;
    for (int k = n - 1; k >= 0; --k)
        if (list[k] == key) return -1;
    return 0;
}
// exe 0x4b8310(actor, box, offset, yaw): the box between the bone-3 and bone-5 points offset by the box's hold (exe
// 0x5e1970 on stack copies), its previous pose saved, its yaw = the actor's + yaw
void holdBox(const AnimatedActor& actor, StaticActor& box, int yaw) {
    const int16_t v[3] = {boxHold[0], boxHold[1], boxHold[2]};
    int16_t p3[3], p5[3];
    bonePointOffset(actor, 3, v, p3);
    bonePointOffset(actor, 5, v, p5);
    box.savePreviousPose();
    box.rotation.x = 0;
    box.pos.x.whole = int16_t((int(p5[0]) + int(p3[0])) >> 1);
    box.pos.y.whole = int16_t((int(p5[1]) + int(p3[1])) >> 1);
    box.pos.z.whole = int16_t((int(p5[2]) + int(p3[2])) >> 1);
    box.rotation.y = int16_t(uint16_t(actor.rotation.y) + uint32_t(yaw));
}
// exe 0x4b7960(actor, block): holding a box -> let it go (speed (0, -0x200, -0x100))
void dropBox(const MemberFields& member) {
    if (!(member.behaviour & kMemberHoldsBox)) return;
    StaticActor& b = boxOfMember(member);
    auto&& hold = LiftBoxHolding(b);
    if (!(b.flags & kActorLive) || hold.carried != 0) return;
    hold.holder = kLiftBoxLetGo;
    b.velocity.y = -0x200;
    b.velocity.z = -0x100;
}
// exe 0x4b7f30: a spray (pool 0x40 type 0x13, exe 0x5e1280) at bone 0 offset (-0x3c, -0x54, -0x30), the actor's yaw
uint32_t spawnSpray(const AnimatedActor& actor) {
    const uint32_t e = allocActor40(kActorStageClass);
    if (!e) return 0;
    ProjectileActor& spray = projectileActorAt(e);
    spray.flags = kActorLive;
    spray.type = kSprayType;
    spray.variant = 0;
    const int16_t v[3] = {-0x3c, -0x54, -0x30};
    int16_t o[3];
    bonePointOffset(actor, 0, v, o); // exe 0x5e1970
    spray.pos.x.whole = o[0];
    spray.pos.y.whole = o[1];
    spray.pos.z.whole = o[2];
    spray.rotation.y = actor.rotation.y;
    return e;
}
// exe 0x4b7fb0(slot, yaw): the spray's yaw while it is still a live spray
void sprayYaw(uint32_t slot, int yaw) {
    if (slot == 0) return;
    ProjectileActor& spray = projectilePool()[size_t(slot - kProjectileClass)];
    if (!(spray.flags & kActorLive) || spray.type != kSprayType) return;
    spray.rotation.y = int16_t(yaw);
}
// exe 0x4b6b00: the player within 0x80 and in front (+-0x400) -> the watch
bool playerClose(AnimatedActor& actor) {
    if (planarDistance(actor.pos, player().pos) >= 0x80) return false;
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleOf(player().pos, actor.pos)));
    if (uint32_t(d + 0x400) > 0x800) return false;
    toSub(actor, MemberSub::Watch);
    return true;
}
// the current point still allowed
bool pointAllowed(const MemberFields& member) {
    return (int32_t(member.pointMask.get()) & (0x11 << (member.point & 31))) != 0;
}
Fixed16Vec3 pointPosition(const AnimatedActor& actor, const MemberFields& member) {
    return dataFixed16Vec3(variantPoints(actor.variant).positions + uint32_t(member.point) * 12);
}

// ---- the sub-states of state 1 ([0x89479c]) --------------------------------------------------------------------
enum class WalkStep : uint8_t { Start = 0, Go = 1, NextNode = 2 };

// exe 0x4b6680: walks the room's node graph (running when told); a wall -> the wall turn; the player close -> the
// watch; a point allowed -> there; with a variant: a free box, else the bar
void walk(AnimatedActor& actor, MemberFields& member) {
    switch (WalkStep(actor.step)) {
    case WalkStep::Start: {
        member.behaviour = 0;
        actor.anim = kAnimWalk;
        actor.velocity.z = -0x80;
        if (member.running != 0) {
            actor.anim = kAnimRun;
            actor.velocity.z = -0x140;
            member.behaviour |= kMemberRunning;
        }
        const RouteNode nd = routeNode(member.node);
        member.targetX = nd.x;
        member.targetZ = nd.z;
        actor.enterStep(uint8_t(WalkStep::Go));
        [[fallthrough]];
    }
    case WalkStep::Go: {
        int32_t t[3]; // the exe's stack vector: x / z set in full, y not written
        t[0] = int32_t(uint32_t(int32_t(member.targetX)) << 16);
        t[1] = 0;
        t[2] = int32_t(uint32_t(int32_t(member.targetZ)) << 16);
        turnToward(actor.rotation.y, bearingFrom(actor, t), 0x20); // exe 0x5e3bc0, 0x5e3db0
        integrateVelocity(actor, 0, 0, actor.velocity.z);            // exe 0x5e3690
        if (uint32_t(int(member.targetX) - int(actor.pos.x.whole) + 0x80) < 0x100 &&
            uint32_t(int(member.targetZ) - int(actor.pos.z.whole) + 0x80) < 0x100)
            actor.enterStep(uint8_t(WalkStep::NextNode));
        break;
    }
    case WalkStep::NextNode: { // the next node: one of the links (bit 7 clear), not straight back when there is a choice
        const RouteNode nd = routeNode(member.node);
        int c = 0;
        for (uint32_t k = 0; k < 4; ++k) c += (uint8_t(nd.links[k]) & 0x80) ? 0 : 1;
        int8_t next;
        if (c <= 1) {
            next = nd.links[0];
        } else {
            int i = int(rngStep() & 0xffff) % c;
            if (uint8_t(nd.links[size_t(i)]) == member.prevNode) i = (i + 1) % c;
            next = nd.links[size_t(i)];
        }
        member.prevNode = uint8_t(member.node);
        member.node = next;
        actor.enterStep(uint8_t(WalkStep::Start));
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        break;
    }
    default: break;
    }
    if (member.contact & kMemberHitWall) toSub(actor, MemberSub::WallTurn);
    playerClose(actor); // exe 0x4b6b00
    if (member.pointMask) goPoint(actor, member);
    if (actor.variant && !goBox(actor, member)) goBar(actor, member);
}

// exe 0x4b6b60: runs to the target, then the sub-state after the walk (the spray, the ride, the carry)
void goTo(AnimatedActor& actor, MemberFields& member) {
    int32_t t[3];
    targetPoint(member, t);
    const int ang = bearingFrom(actor, t);
    if (actor.step == 0) {
        actor.anim = kAnimRun;
        member.behaviour = kMemberRunning;
        actor.velocity.z = -0x140;
        member.running = 1;
        actor.enterStep(1);
    }
    const int d = turnYawTo(actor.rotation.y, uint32_t(ang), 0x40); // exe 0x5e3df0
    if (uint32_t(d + 0x200) <= 0x400) integrateVelocity(actor, 0, 0, -0x140);
    if (member.contact & kMemberHitWall) toSub(actor, MemberSub::WallTurn);
    if (planarDistance(actor.pos, t) <= 0x20) {
        actor.enterStep(0);
        actor.subState = member.afterWalk;
    }
    switch (member.afterWalk) {
    case kAfterPoint: {
        if (planarDistance(actor.pos, pointPosition(actor, member)) < 0x200) {
            actor.enterStep(0);
            actor.subState = member.afterWalk;
        }
        if (uint32_t(d + 0x100) >= 0x200) return;
        if (pointAllowed(member)) return;
        if (member.pointMask == 0) {
            toSub(actor, MemberSub::Walk);
            return;
        }
        goPoint(actor, member);
        return;
    }
    case kAfterBar:
        if (!barCalls(member)) toSub(actor, MemberSub::Walk);
        return;
    case kAfterBox: {
        const StaticActor& b = boxOfMember(member);
        if (!(b.flags & kActorLive) || b.type != kLiftBoxType) {
            toSub(actor, MemberSub::Walk);
            return;
        }
        if (((slotIdentityWord(b.lastMover) ^ slotIdentityWord(actor)) & 0x1fffff) == 0) toSub(actor, MemberSub::CarryBox);
        member.targetX = b.pos.x.whole;
        member.targetZ = b.pos.z.whole;
        return;
    }
    default: return;
    }
}

// exe 0x4b6d20: turns away from the wall / the bumper while walking, then the walk / the run to its target (dead: runs
// off)
void wallTurn(AnimatedActor& actor, MemberFields& member) {
    if (actor.step == 0) {
        uint32_t dx = 0, dz = 0;
        if (actor.actorHit == 0) {
            const uint32_t p = actor.wallHit;
            if (p & 0x100) dx = 0xff000000u;
            if (p & 0x200) dx = 0x1000000u;
            if (p & 0x400) dz = 0xff000000u;
            if (p & 0x800) dz = 0x1000000u;
        } else {
            dz = uint32_t(int32_t(fixedCos(actor.rotation.y))) << 12;
            dx = uint32_t(int32_t(fixedSin(actor.rotation.y))) << 12;
        }
        // the point ahead (y: stack contents in the exe, zero here)
        const int32_t t[3] = {int32_t(dx + uint32_t(actor.pos.x)), 0, int32_t(dz + uint32_t(actor.pos.z))};
        const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(bearingFrom(actor, t)));
        if (!(member.behaviour & kMemberRunning)) {
            member.behaviour = 0;
            actor.spin.y = int16_t(d >= 0 ? 0x20 : -0x20);
        } else {
            member.behaviour = kMemberRunning;
            actor.spin.y = int16_t(d >= 0 ? 0x40 : -0x40);
        }
        actor.enterStep(1);
    }
    integrateVelocity(actor, 0, 0, actor.velocity.z);
    actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + uint16_t(actor.spin.y));
    if (member.contact & kMemberHitWall) return;
    if (actor.hitPoints >= 0)
        toSub(actor, (member.behaviour & kMemberRunning) ? MemberSub::GoTo : MemberSub::Walk);
    else
        toSub(actor, MemberSub::RunOff);
}

// exe 0x4b6e60: stands facing the player while it is close and in front
void watch(AnimatedActor& actor, MemberFields& member) {
    switch (actor.step) {
    case 0: {
        member.behaviour = 0;
        actor.anim = kAnimStand;
        const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleOf(player().pos, actor.pos)));
        actor.spin.y = int16_t(d <= 0 ? 0x20 : -0x20);
        member.timer = 0;
        actor.enterStep(1);
        [[fallthrough]];
    }
    case 1: {
        const int dist = planarDistance(actor.pos, player().pos);
        const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleOf(player().pos, actor.pos)));
        if (uint32_t(dist + 0x100) > 0x200 || uint32_t(d + 0x480) > 0x900) toSub(actor, MemberSub::Walk);
        break;
    }
    default: break;
    }
    if (member.pointMask) goPoint(actor, member);
    if (actor.variant && !goBar(actor, member)) goBox(actor, member);
}

// exe 0x4b6f50 (jump table 0x4b7148): at the point, turns to it and sprays (a spray every 3 ticks, swinging every 0x10)
enum class SprayStep : uint8_t { Start = 0, Turn = 1, Aim = 2, Spray = 3, End = 4 };
void spray(AnimatedActor& actor, MemberFields& member) {
    const int ang = angleOf(pointPosition(actor, member), actor.pos);
    switch (SprayStep(actor.step)) {
    case SprayStep::Start: {
        member.behaviour = 0;
        const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(ang));
        if (uint32_t(d + 0x400) > 0x800) {
            actor.enterStep(uint8_t(SprayStep::Turn));
            return;
        }
        actor.anim = kAnimSpray;
        actor.enterStep(uint8_t(SprayStep::Aim));
        return;
    }
    case SprayStep::Turn: // exe 0x4b6fc6
        if (uint32_t(turnYawTo(actor.rotation.y, uint32_t(ang), 0x40) + 0x40) > 0x80) return;
        actor.anim = kAnimSpray;
        actor.enterStep(uint8_t(SprayStep::Aim));
        return;
    case SprayStep::Aim: { // exe 0x4b6ff2
        const int d = turnYawTo(actor.rotation.y, uint32_t(ang), 0x40);
        if (actor.animEnded()) {
            actor.enterStep(uint8_t(SprayStep::Spray));
            actor.spin.y = 0x10;
            soundPlayAtPos(kSoundSpray, actor.pos); // exe 0x5d5380
            member.timer = 1;
        }
        if (uint32_t(d + 0x100) >= 0x200) return;
        if (pointAllowed(member)) return;
        if (member.pointMask == 0) {
            toSub(actor, MemberSub::Walk);
            return;
        }
        goPoint(actor, member);
        return;
    }
    case SprayStep::Spray: { // exe 0x4b706b: a spray every 3 ticks, the last one turned with the actor
        if (actor.stepCounter == 0) {
            const uint32_t e = spawnSpray(actor);
            member.spraySlot = e ? projectileActorAt(e).slot : uint8_t(0);
            actor.stepCounter = 1;
        } else {
            sprayYaw(member.spraySlot, actor.rotation.y);
            const uint8_t n = uint8_t(actor.stepCounter + 1);
            actor.stepCounter = n;
            if (n == 3) actor.stepCounter = 0;
        }
        actor.rotation.y = int16_t(uint16_t(actor.rotation.y) + uint16_t(actor.spin.y));
        const uint8_t t = uint8_t(member.timer - 1);
        member.timer = t;
        if (t == 0) {
            actor.spin.y = int16_t(-int32_t(actor.spin.y));
            soundPlayAtPos(kSoundSprayTurn, actor.pos);
            member.timer = 0x10;
        }
        if (pointAllowed(member)) return;
        actor.enterStep(uint8_t(SprayStep::End));
        return;
    }
    case SprayStep::End: // exe 0x4b7106
        if (actor.stepCounter == 0) {
            actor.anim = kAnimSprayEnd;
            actor.stepCounter = 1;
            return;
        }
        if (!actor.animEnded()) return;
        if (member.pointMask) {
            goPoint(actor, member);
            return;
        }
        toSub(actor, MemberSub::Walk);
        return;
    default: return;
    }
}

// exe 0x4b7160 (jump table 0x4b732c): at the bar, faces 0xc00, jumps onto it and rides it (the exe computes the target's
// bearing and discards it)
enum class RideStep : uint8_t { Start = 0, Face = 1, Jump = 2, Leap = 3, Land = 4, Ride = 5, Riding = 6 };
void ride(AnimatedActor& actor, MemberFields& member) {
    switch (RideStep(actor.step)) {
    case RideStep::Start:
        actor.anim = kAnimRun;
        member.behaviour = kMemberKeepsOn | kMemberNoDamage;
        actor.velocity.z = -0x140;
        member.running = 1;
        actor.enterStep(uint8_t(RideStep::Face));
        [[fallthrough]];
    case RideStep::Face: // exe 0x4b71d1
        if (uint32_t(turnYawTo(actor.rotation.y, kBarYaw, 0x40) + 0x40) <= 0x80) {
            actor.rotation.y = kBarYaw;
            actor.enterStep(uint8_t(RideStep::Jump));
        }
        if (!barCalls(member)) toSub(actor, MemberSub::Walk);
        return;
    case RideStep::Jump: // exe 0x4b7218
        actor.anim = kAnimJump;
        actor.velocity.y = -0x200;
        actor.velocity.z = 0;
        actor.enterStep(uint8_t(RideStep::Leap));
        return;
    case RideStep::Leap: // exe 0x4b7239
        if (uint8_t(actor.animKey.frame) != 9) return;
        actor.airborne = 1;
        member.behaviour |= kMemberBounces;
        actor.enterStep(uint8_t(RideStep::Land));
        return;
    case RideStep::Land: { // exe 0x4b7264
        if (fallMove(actor, member, actor.rotation.y) && actor.velocity.y > 0) toSub(actor, MemberSub::Fall);
        if (actor.lastMover) return;
        const uint32_t b = groupBar(member);
        if (!b) return;
        if (uint32_t(int(staticActorAt(b).pos.y.whole) - int(actor.pos.y.whole) + 0xec) >= 0x20) return;
        if (BarRide(staticActorAt(b)).rider != kBarFree) return;
        BarRide(staticActorAt(b)).rider = kBarRidden;
        member.behaviour &= uint8_t(~kMemberBounces);
        actor.enterStep(uint8_t(RideStep::Ride));
        soundPlayAtPos(kSoundJump, actor.pos);
        return;
    }
    case RideStep::Ride: // exe 0x4b72e3
        actor.anim = kAnimRide;
        actor.enterStep(uint8_t(RideStep::Riding));
        [[fallthrough]];
    case RideStep::Riding: { // exe 0x4b72f0
        const uint32_t b = groupBar(member);
        if (!b) return;
        if (BarRide(staticActorAt(b)).rider == kBarFree) toSub(actor, MemberSub::Fall);
        if (!actor.lastMover) return;
        if (BarRide(staticActorAt(b)).rider == kBarRidden) BarRide(staticActorAt(b)).rider = kBarFree;
        toSub(actor, MemberSub::Fall);
        return;
    }
    default: return;
    }
}

// exe 0x4b7350 (jump table 0x4b75f8): takes the box, carries it to (0x500, 0x200) and throws it
enum class CarryStep : uint8_t { Start = 0, Face = 1, Lift = 2, Lifting = 3, ToDrop = 4, Carry = 5, Throw = 6, Throwing = 7, End = 8 };
void carryBox(AnimatedActor& actor, MemberFields& member) {
    StaticActor& b = boxOfMember(member);
    int32_t t[3];
    targetPoint(member, t);
    const int ang = bearingFrom(actor, t);
    switch (CarryStep(actor.step)) {
    case CarryStep::Start:
        actor.anim = kAnimRun;
        member.behaviour = kMemberKeepsOn;
        actor.velocity.z = -0x140;
        member.running = 1;
        actor.enterStep(uint8_t(CarryStep::Face));
        [[fallthrough]];
    case CarryStep::Face: // exe 0x4b73dc
        if (boxLost(actor, member) != 0) {
            toSub(actor, MemberSub::Walk);
            return;
        }
        if (uint32_t(turnYawTo(actor.rotation.y, uint32_t(ang), 0x40) + 0x40) > 0x80) return;
        LiftBoxHolding(b).holder = kLiftBoxHolds | kLiftBoxHolderMover;
        member.behaviour |= kMemberRunning | kMemberHoldsBox;
        actor.enterStep(uint8_t(CarryStep::Lift));
        return;
    case CarryStep::Lift: // exe 0x4b7426
        actor.anim = kAnimLift;
        actor.enterStep(uint8_t(CarryStep::Lifting));
        member.boxYaw = int16_t(((uint16_t(b.rotation.y) + 0x1ffu) & 0x400u) - uint16_t(actor.rotation.y));
        return;
    case CarryStep::Lifting: // exe 0x4b7452
        if (actor.animKey.events & 1) holdBox(actor, b, member.boxYaw);
        if (actor.keyIndex == 0x11) soundPlayAtPos(kSoundBoxLift, actor.pos);
        if (actor.animEnded()) actor.enterStep(uint8_t(CarryStep::ToDrop));
        return;
    case CarryStep::ToDrop: // exe 0x4b74a1
        member.targetX = kBoxDropX;
        member.targetZ = kBoxDropZ;
        actor.anim = kAnimCarry;
        actor.enterStep(uint8_t(CarryStep::Carry));
        return;
    case CarryStep::Carry: // exe 0x4b74c4
        if (uint32_t(turnYawTo(actor.rotation.y, uint32_t(ang), 0x20) + 0x200) <= 0x400)
            integrateVelocity(actor, 0, 0, -0x80);
        if (planarDistance(actor.pos, t) <= 0x20) actor.enterStep(uint8_t(CarryStep::Throw));
        if (b.actorHit != 0 || b.lastMover) actor.enterStep(uint8_t(CarryStep::Throw));
        holdBox(actor, b, member.boxYaw);
        return;
    case CarryStep::Throw: // exe 0x4b7535
        actor.anim = kAnimThrowBox;
        actor.velocity.z = 0x30;
        actor.enterStep(uint8_t(CarryStep::Throwing));
        if (!(member.behaviour & kMemberHoldsBox)) actor.enterStep(uint8_t(CarryStep::End));
        return;
    case CarryStep::Throwing: // exe 0x4b755f
        if (actor.stepCounter == 0) {
            if (actor.animKey.frame <= 0x1c) {
                auto&& hold = LiftBoxHolding(b);
                if ((b.flags & kActorLive) && hold.carried == 0) {
                    hold.holder = kLiftBoxLetGo;
                    b.velocity.y = 0x40;
                    b.velocity.z = 0;
                }
                member.behaviour = uint8_t((member.behaviour & uint8_t(~(kMemberRunning | kMemberHoldsBox))) |
                                           kMemberNoDamage);
                actor.stepCounter = 1;
            } else {
                holdBox(actor, b, member.boxYaw);
            }
        } else {
            integrateVelocity(actor, 0, 0, actor.velocity.z);
            if (uint16_t(actor.velocity.z) != 0) actor.velocity.z -= 2u;
        }
        [[fallthrough]];
    case CarryStep::End: // exe 0x4b75db
        if (actor.animEnded()) toSub(actor, MemberSub::Walk);
        return;
    default: return;
    }
}

// exe 0x4b7730: falls until landed, then walks (dead: runs off)
void fall(AnimatedActor& actor, MemberFields& member) {
    switch (actor.step) {
    case 0:
        actor.anim = kAnimFall;
        member.behaviour = kMemberBounces | kMemberKeepsOn;
        actor.acceleration.y = kGravity;
        actor.velocity.y = 0;
        actor.airborne = 1;
        actor.enterStep(1);
        [[fallthrough]];
    case 1:
        if (!fallMove(actor, member, actor.rotation.y)) return;
        actor.airborne = 0;
        toSub(actor, actor.hitPoints >= 0 ? MemberSub::Walk : MemberSub::RunOff);
        return;
    default: return;
    }
}

// exe 0x4b77a0: hit -- the box dropped, knocked back (dead: launched, once), sliding, then it gets up
enum class KnockedStep : uint8_t { Start = 0, Air = 1, Slide = 2 };
void knocked(AnimatedActor& actor, MemberFields& member) {
    if (member.contact & kMemberHitWall) {
        actor.acceleration.z = 0;
        actor.velocity.z = 0;
    }
    switch (KnockedStep(actor.step)) {
    case KnockedStep::Start: { // exe 0x4b7828
        dropBox(member); // exe 0x4b7960
        member.behaviour = kMemberBounces | kMemberNoHitEntry | kMemberKeepsOn | kMemberNoDamage;
        const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(int32_t(member.hitBox.contact.x)));
        actor.anim = kAnimKnocked;
        actor.velocity.z = 0x180;
        actor.acceleration.z = -0x10;
        actor.velocity.y = int16_t((member.hitBox.result & kHitAttackStrongPush) ? 0xfe80u : 0xff40u);
        if (actor.hitPoints < 0 && member.downKnocked == 0) {
            actor.velocity.y = -0x300;
            actor.velocity.z = 0x100;
            member.timer = 0;
            member.offScreenTicks = 0x1e;
            soundPlayAtPos(kSoundKnockedDown, actor.pos);
        } else {
            soundPlayAtPos(kSoundKnocked, actor.pos);
        }
        if (uint32_t(d + 0x400) < 0x800 && member.hitBox.contact.y != 0) {
            actor.anim = kAnimKnockedFront;
            actor.velocity.z = int16_t(-int32_t(actor.velocity.z));
            actor.acceleration.z = int16_t(-int32_t(actor.acceleration.z));
        }
        actor.acceleration.y = kGravity;
        actor.airborne = 1;
        actor.enterStep(uint8_t(KnockedStep::Air));
        [[fallthrough]];
    }
    case KnockedStep::Air: // exe 0x4b78f1
        if (fallMove(actor, member, actor.rotation.y)) {
            actor.velocity.z = int16_t(uint16_t(actor.velocity.z) << 1);
            actor.airborne = 0;
            actor.enterStep(uint8_t(KnockedStep::Slide));
        }
        break;
    case KnockedStep::Slide: // exe 0x4b77d8
        integrateVelocity(actor, 0, 0, actor.velocity.z);
        if (member.contact & kMemberGroundLost) {
            actor.airborne = 1;
            actor.enterStep(uint8_t(KnockedStep::Air));
            break;
        }
        if (!actor.animEnded()) break;
        actor.airborne = 0;
        if (actor.hitPoints < 0) member.downKnocked = 1;
        member.lastHitKind = 0;
        toSub(actor, MemberSub::GetUp);
        break;
    default: break;
    }
    if (actor.hitPoints < 0 && member.downKnocked == 0 && !(member.timer & 3)) spawnSmoke(actor, 0, 8); // exe 0x4a1b90
    member.timer += 1u;
    if (actor.animKey.events & 1) {
        const uint16_t step = uint16_t(actor.acceleration.z);
        const uint16_t v = uint16_t(uint16_t(actor.velocity.z) + step);
        actor.velocity.z = int16_t(v);
        if (int16_t(v ^ step) >= 0) actor.velocity.z = 0;
    }
}

// exe 0x4b79b0: the thrown landing's slide along its flight yaw, then it gets up
void slide(AnimatedActor& actor, MemberFields& member) {
    if (actor.step == 0) {
        member.behaviour = 0;
        actor.rotation.x = 0;
        if (actor.anim != kAnimKnocked) {
            actor.anim = kAnimKnockedFront;
            member.animKeyStart = 0xa;
        }
        actor.enterStep(1);
    } else if (actor.animEnded()) {
        toSub(actor, MemberSub::GetUp);
    }
    moveAlongYaw(actor.pos, member.flightYaw, 0, 0, actor.velocity.z); // exe 0x5e36f0
}

// exe 0x4b7a10: gets up, then walks (dead: runs off)
void getUp(AnimatedActor& actor) {
    if (actor.step == 0) {
        actor.anim = kAnimGetUp;
        actor.enterStep(1);
        return;
    }
    if (!actor.animEnded()) return;
    toSub(actor, actor.hitPoints >= 0 ? MemberSub::Walk : MemberSub::RunOff);
}

// exe 0x4b7a50: out of hit points -- runs off at +-0x600 from the player's bearing, smoke every 8 ticks by chance
void runOff(AnimatedActor& actor, MemberFields& member) {
    switch (actor.step) {
    case 0: {
        actor.anim = kAnimRun;
        actor.velocity.setXY(0, 0);
        actor.velocity.z = -0x140;
        member.running = 1;
        member.behaviour = 0;
        const uint32_t r = rngStep();
        actor.enterStep(1);
        actor.spin.y = int16_t(((0xaaaaaaaau >> (r & 0x1f)) & 1) ? 0x600u : 0xfa00u);
        [[fallthrough]];
    }
    case 1:
        turnToward(actor.rotation.y, int(int(actor.spin.y) + angleOf(player().pos, actor.pos)), 0x40);
        break;
    default: break;
    }
    integrateVelocity(actor, 0, 0, actor.velocity.z);
    if (member.contact & kMemberHitWall) toSub(actor, MemberSub::WallTurn);
    if (!(member.point & 7)) {
        const uint32_t r = rngStep();
        if (int32_t(0x15c200u << (r & 0x1f)) < 0) spawnSmoke(actor, -0x8c, 8);
    }
    member.point += 1u;
}

// ---- the states ([0x894790]) ------------------------------------------------------------------------------------

// exe 0x4b64f0: the model, drawn, its hit points, no motion, the boxes (liftable), the class block cleared; with a
// variant: its slot to the group's bar
void init(AnimatedActor& actor, MemberFields& member) {
    if (!bindActorModel(actor, 0)) { // exe 0x5e0ba0
        freeActor(actor);           // exe 0x5e1490
        return;
    }
    actor.flags |= kActorDrawn;
    actor.anim = kAnimStand;
    actor.prevAnim = kNoAnim;
    const int16_t hp = hasAnimatedStats(actor.type) ? int16_t(animatedStats(actor.type).hitPoints) : int16_t(0);
    actor.maxHitPoints = hp;
    actor.hitPoints = hp;
    actor.velocity.clearWords();
    actor.spin.setXY(0, 0);
    actor.rotation.z = 0;
    actor.rotation.x = 0;
    member.tiltY = 0;
    actor.collisionBox.pointAt(memberBox.stateAddress());
    actor.interactionBox.pointAt(memberLiftBox.stateAddress());
    actor.interaction = kInteractLift;
    actor.airborne = 0;
    actor.onPlatform = 0;
    member.running = 0;
    actor.clearClassBlock(); // exe 0x40dc40
    if (actor.variant != 0) {
        if (const uint32_t bar = groupBar(member)) BarRide(staticActorAt(bar)).member = actor.slot; // exe 0x4b7ff0
    }
    actor.enterState(uint8_t(MemberState::Active));
}

// exe 0x4b65c0: bumped, off its platform -> the fall, the hit, the sub-state, the collision, lifted -> state 2, the
// stagger meter down on odd ticks
void active(const PlayerEnv& env, AnimatedActor& actor, MemberFields& member) {
    if (actor.actorHit != 0) bumped(actor, member); // exe 0x4b8180
    if (actor.wasOnPlatform && !actor.onPlatform && !(member.behaviour & kMemberKeepsOn)) toSub(actor, MemberSub::Fall);
    if (damage(actor, member) != 0) toSub(actor, MemberSub::Knocked); // exe 0x4b81d0
    switch (MemberSub(actor.subState)) {
    case MemberSub::Walk: walk(actor, member); break;
    case MemberSub::GoTo: goTo(actor, member); break;
    case MemberSub::WallTurn: wallTurn(actor, member); break;
    case MemberSub::Watch: watch(actor, member); break;
    case MemberSub::Spray: spray(actor, member); break;
    case MemberSub::Ride: ride(actor, member); break;
    case MemberSub::CarryBox: carryBox(actor, member); break;
    case MemberSub::Fall: fall(actor, member); break;
    case MemberSub::Knocked: knocked(actor, member); break;
    case MemberSub::Slide: slide(actor, member); break;
    case MemberSub::GetUp: getUp(actor); break;
    case MemberSub::RunOff: runOff(actor, member); break;
    default: notPortedAt(kStoryPort, "0x4b65c0 sub-state (table 0x89479c runs into other data)", actor.subState);
    }
    if (collide(env, actor, member) != 0 && !(member.behaviour & kMemberKeepsOn)) toSub(actor, MemberSub::Fall); // 0x4b8040
    if (!(member.behaviour & kMemberHoldsBox) && playerLiftTargetCheck(actor, 0)) // exe 0x573a60
        actor.enterState(uint8_t(MemberState::Lifted));
    if ((gameState().frame & 1) && member.stagger) member.stagger -= 1u;
}

// exe 0x4b7ce0: let go by the player -- flies along its flight yaw (pitching), a wall / a solid hit turns it back (its
// row's power as damage), landed -> the slide
void thrown(const PlayerEnv& env, AnimatedActor& actor, MemberFields& member) {
    const AnimatedActorStats row = animatedStats(actor.type);
    if (actor.step == 0) {
        actor.rotation.x += 0x40u;
        if (actor.rotation.x > 0x200) actor.rotation.x = 0x200;
        if (member.contact & kMemberHitWall) {
            actor.anim = kAnimKnocked;
            member.animKeyStart = 0x10;
            actor.velocity.z = int16_t(actor.velocity.z >> 1);
            actor.rotation.x = 0;
            actor.enterStep(1);
        }
    }
    if ((member.hitBox.result & kHitAttackSolid) && actor.anim != kAnimKnocked) {
        actor.anim = kAnimKnocked;
        member.animKeyStart = 0x10;
        actor.velocity.z = int16_t(actor.velocity.z >> 1);
        member.flightYaw += 0x800u;
        actor.rotation.x = 0;
        member.behaviour |= kMemberNoHitEntry;
        actor.enterStep(1);
        if (actor.hitPoints >= 0 && hitPointsAdd(actor, -int(row.strongAttack)) != 0) { // exe 0x5e52f0
            spawnSmoke(actor, 0, 0);                                                     // exe 0x4a1b90
            member.downKnocked = 1;
        }
    }
    if (fallMove(actor, member, member.flightYaw)) {
        actor.airborne = 0;
        actor.enterState(uint8_t(MemberState::Active), uint8_t(MemberSub::Slide));
    }
    collide(env, actor, member); // exe 0x4b8040
}

// exe 0x4b7b80: at the player's hands (a struggle sound 0x19c on a random timer) until let go: thrown along the aim or
// dropped
void carried(const PlayerEnv& env, AnimatedActor& actor, MemberFields& member) {
    if (actor.step == 0) {
        member.behaviour = kMemberKeepsOn;
        member.timer = struggleTicks[(rngStep() & 0xf) * 4];
        actor.anim = kAnimCarried;
        actor.enterStep(1);
    }
    const uint8_t t = uint8_t(member.timer - 1);
    member.timer = t;
    if (t == 0) {
        soundPlayAtPos(kSoundStruggle, 0);
        member.timer = struggleTicks[(rngStep() & 0xf) * 4];
    }
    carryAtHands(actor, 0, 0x10, 0); // exe 0x573ab0
    const CarryRelease rel = carryReleased(); // exe 0x573950
    if (rel == CarryRelease::Held) return;
    soundPlayAtPos(kSoundKnocked, 0);
    if (rel == CarryRelease::Thrown) { // thrown: Rx(the player's pitch) * (0, -0xc0, -0x300) (exe 0x5e3e50)
        int32_t v[3];
        playerAimVector(0, -0xc0, -0x300, v);
        actor.velocity.x = 0;
        actor.velocity.y = int16_t(v[1]);
        actor.velocity.z = int16_t(v[2]);
        member.behaviour = kMemberBounces | kMemberThrown | kMemberKeepsOn;
    } else {
        actor.velocity.y = -0x1c0;
        actor.velocity.z = -0x100;
        member.behaviour = kMemberBounces | kMemberKeepsOn;
    }
    actor.acceleration.y = kGravity;
    const int16_t yaw = player().rotation.y;
    actor.rotation.y = yaw;
    member.flightYaw = yaw;
    actor.rotation.x = 0;
    actor.airborne = 1;
    carryDropFromPlayer(actor); // exe 0x573990
    actor.subState = uint8_t(LiftedSub::Thrown);
    actor.enterStep(0);
    thrown(env, actor, member);
}

// exe 0x4b6280
void groupMember(const PlayerEnv& env, AnimatedActor& actor) {
    MemberFields& member = classState<MemberFields>(actor);
    actor.prevAnim = actor.anim;
    actor.savePreviousPose();
    actor.savePreviousContact();
    actor.tint = actor.hitPoints < 0 ? kTintSpent : kTintNeutral;
    switch (MemberState(actor.state)) {
    case MemberState::Init: init(actor, member); break;
    case MemberState::Active: active(env, actor, member); break;
    case MemberState::Lifted: // exe 0x4b7b60
        if (LiftedSub(actor.subState) == LiftedSub::Carried) carried(env, actor, member);
        else thrown(env, actor, member);
        break;
    default:
        notPortedAt(kStoryPort, "0x4b6280 state (table 0x894790 runs into the sub-state table)", actor.state);
        return;
    }
    if (!(actor.flags & kActorLive)) return;
    auto despawn = [&]() {
        freedPenalty(actor); // exe 0x4b82d0
        freeActor(actor);    // exe 0x5e1490
    };
    const int dist = distance3(actor.pos, player().pos); // exe 0x5e3a60
    const uint32_t far = stage().depthRange;
    if ((member.contact & kMemberOffRoom) && (dist > int(far << 2) || pointVisibleOrNear(actor.pos, 0) != 0)) { // 0x5e59a0
        despawn();
        return;
    }
    if (int(actor.pos.y.whole) > int(far << 1)) {
        despawn();
        return;
    }
    if (actor.hitPoints < 0) {
        if (pointVisibleOrNear(actor.pos, 0x200) != 0) {
            const uint8_t t = uint8_t(member.offScreenTicks - 1);
            member.offScreenTicks = t;
            if (t == 0) {
                despawn();
                return;
            }
        } else {
            member.offScreenTicks = 0x1e;
        }
    }
    if (actor.anim == actor.prevAnim) {
        animStep(actor); // exe 0x5e1ea0
    } else {
        animSet(actor, actor.anim, member.animKeyStart); // exe 0x5e1c50
        member.animKeyStart = 0;
    }
    LaneVec4 partOne = decodedFrame().entries[2]; // part 1's angles, tilted
    partOne.y = int16_t(uint16_t(partOne.y) + uint16_t(member.tiltY));
    animPose(actor); // exe 0x5e2d90
    member.hitBox.result = 0;
    member.hitBox.previousX = actor.prevPos.x.whole;
    member.hitBox.previousZ = actor.prevPos.z.whole;
    const AnimatedActorStats row = animatedStats(actor.type);
    const int16_t pos[3] = {actor.pos.x.whole, int16_t(uint16_t(actor.pos.y.whole) - 0x64u), actor.pos.z.whole};
    const uint8_t bits = member.behaviour;
    if (!(bits & kMemberNoHitEntry)) { // exe 0x5e4120
        if (bits & kMemberThrown)
            hitListSubmit(pos, member.hitBox,
                          hitOwner(actor.slot) | hitGroup(kHitGroupPlayerAttacks) | kHitContactMotion | hitRadius(0x48),
                          uint32_t(row.attack) | kHitAttackByPlayer);
        else
            hitListSubmit(pos, member.hitBox,
                          hitOwner(actor.slot) | ((bits & kMemberRunning) ? kHitCompareHeights : 0u) |
                              hitGroup(kHitGroupLiftable) | kHitNoLockOn | kHitContactMotion | hitRadius(0x48),
                          kHitAttackHarmless | kHitAttackSolid);
    }
    if (!(MemberState(actor.state) == MemberState::Lifted && actor.subState == 0)) {
        // exe 0x5f20f0(a, 0x40, 0x100, 0): the shadow (render output)
        if (!(bits & kMemberHoldsBox)) actorCollisionSubmitSolid(actor, SolidType::PushOut, kNoBone, 0); // exe 0x414050
    }
    if (!(bits & kMemberHoldsBox)) actorCollisionSubmitMover(actor); // exe 0x4140b0
}

} // namespace

void groupMemberUpdate(const PlayerEnv& env, uint32_t actor) { groupMember(env, animatedActorAt(actor)); }

} // namespace od::port
