// The flight of the static gunship family (see gunship_flight.hpp), one routine per exe routine.
#include "game/actor_state.hpp"
#include "game/gunship_flight.hpp"
#include "game/stage_smoke.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_stats.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/breakable_walls.hpp"
#include "game/enemy_shot.hpp"
#include "game/explosions.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_list.hpp"
#include "game/hit_points.hpp"
#include "game/matrix.hpp"
#include "game/pickups.hpp"
#include "game/player/player_state.hpp"
#include "game/vectors.hpp"

namespace od::port {

namespace {

constexpr uint8_t kBombType = 0x22;  // the stage's bomb static
constexpr uint16_t kBombSound = 0x18a, kHurtSound = 0x8d, kStrongHurtSound = 0x8e;
constexpr uint32_t kHitByPlayerOrStrong = kHitAttackByPlayer | kHitAttackStrong;
constexpr uint8_t kShotVariant = 0x80, kStrongShotVariant = 0x84;
constexpr int16_t kShotTicks = 0x2d;
constexpr uint16_t kCrashBlastSize = 0xc0, kCrashBlastAttack = 4;
constexpr uint8_t kCrashTicks = 0x5a;


// the sub-states of a crash
enum class CrashSub : uint8_t { Start = 0, Fall = 1, Blast = 2 };

} // namespace

int steerRate(StaticActor& actor, uint32_t angle, int lim, int16_t acc) {
    int16_t& turnStep = actor.spin.spare; // GunshipFields::turnStep: a lane of the slot, whichever class of the family
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), angle);
    if (uint32_t(d * 0x10 + lim) <= uint32_t(lim * 2)) return 0;
    turnStep = 0;
    if (d < 0) turnStep = int16_t(-acc);
    if (d > 0) turnStep = acc;
    const int16_t r = actor.spin.y;
    actor.rotation.y = int16_t(actor.rotation.y + (r >> 4));
    actor.spin.y = int16_t(r + turnStep);
    if (actor.spin.y > lim) actor.spin.y = int16_t(lim);
    if (actor.spin.y < -lim) actor.spin.y = int16_t(-lim);
    return d;
}

int steerDamp(StaticActor& actor, int16_t acc) {
    int16_t& turnStep = actor.spin.spare; // GunshipFields::turnStep: a lane of the slot, whichever class of the family
    if (actor.spin.y < 0) turnStep = acc;
    if (actor.spin.y > 0) turnStep = int16_t(-acc);
    actor.rotation.y = int16_t(actor.rotation.y + (actor.spin.y >> 4));
    const int16_t r = int16_t(actor.spin.y + turnStep);
    actor.spin.y = r;
    if (r == turnStep) {
        actor.spin.y = 0;
        return 1;
    }
    return 0;
}

int verticalApproach(StaticActor& actor, int lim) {
    const int16_t v = actor.velocity.y;
    const int av = v < 0 ? -v : v;
    if (lim < av) {
        const int16_t nv = int16_t(uint16_t(v) - uint16_t(actor.acceleration.y));
        actor.velocity.y = nv;
        const int an = nv < 0 ? -nv : nv;
        if (an <= lim || int16_t(nv ^ v) < 0) return -1;
    } else {
        const int16_t nv = int16_t(actor.acceleration.y + v);
        actor.velocity.y = nv;
        const int an = nv < 0 ? -nv : nv;
        if (lim <= an) return 1;
    }
    return 0;
}

int steerToward(StaticActor& actor, const Fixed16Vec3& target, int lim, int16_t acc) {
    return steerRate(actor, uint32_t(angleOf(target, actor.pos)), lim, acc);
}

int steerToward(StaticActor& actor, int32_t targetX, int32_t targetZ, int lim, int16_t acc) {
    return steerRate(actor, uint32_t(angleOf(targetX, targetZ, actor.pos.x, actor.pos.z)), lim, acc);
}

void bobVertical(StaticActor& actor) {
    actor.velocity.y = int16_t(actor.velocity.y + actor.acceleration.y);
    if (actor.velocity.y > 0x60) {
        actor.velocity.y = 0x60;
        actor.acceleration.y = -6;
    }
    if (actor.velocity.y < -0x60) {
        actor.velocity.y = -0x60;
        actor.acceleration.y = 6;
    }
}

void rockInit(StaticActor& actor) {
    int16_t& rollSpeed = actor.acceleration.x; // GunshipFields::rollSpeed: a lane of the slot, whichever class of the family
    actor.rotation.z = int16_t(actor.rotation.z >> 2);
    actor.spin.z = 0x400;
    rollSpeed = int16_t((uint16_t(actor.rotation.z) & 0xfff) < 0x800 ? 0x400 : 0xfc00);
}

int rock(StaticActor& actor) {
    int16_t& rollSpeed = actor.acceleration.x; // GunshipFields::rollSpeed: a lane of the slot, whichever class of the family
    const int16_t amp = actor.spin.z;
    const uint32_t r = uint16_t(actor.rotation.z) & 0xfff;
    actor.rotation.z = int16_t((rollSpeed >> 4) + uint16_t(actor.rotation.z));
    if (r < 0x800) rollSpeed = int16_t(rollSpeed - 0x200);
    if (r > 0x800) rollSpeed = int16_t(rollSpeed + 0x200);
    if (amp < rollSpeed) rollSpeed = amp;
    if (rollSpeed < -int(amp)) rollSpeed = int16_t(-amp);
    if ((uint16_t(actor.prevRotation.z) ^ uint16_t(actor.rotation.z)) & 0x800) {
        const int16_t n = int16_t(amp - (amp >> 2));
        actor.spin.z = n;
        if (uint16_t(n + 0x40) < 0x80) return 1;
    }
    return 0;
}

void sway(StaticActor& actor) {
    const uint32_t before = uint16_t(actor.prevRotation.y) & 0xfff, now = uint16_t(actor.rotation.y) & 0xfff;
    const int s1 = fixedSin(int(now)), s2 = fixedSin(int(before));
    const int c1 = fixedCos(int(now)), c2 = fixedCos(int(before));
    actor.pos.z.whole = int16_t(actor.pos.z.whole - int16_t(((c2 - c1) * 0x118) >> 12));
    actor.pos.x.whole = int16_t(actor.pos.x.whole - int16_t(((s2 - s1) * 0x118) >> 12));
}

void rollToTurn(StaticActor& actor) {
    GunshipFields& ship = classState<GunshipFields>(actor);
    if (ship.flight & kFlightRocking) return;
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(int32_t(actor.prevRotation.y)));
    const int16_t v = actor.velocity.z;
    if (v == 0) {
        ship.rollTarget = int16_t(int16_t(d) * 2);
    } else {
        const int16_t k = int16_t(((v >> 6) ^ (v >> 15)) - (v >> 15));
        ship.rollTarget = int16_t((k + 2) * int16_t(d));
    }
    const int16_t r = ship.rollTarget;
    const int d2 = angleDiff(uint32_t(int32_t(actor.rotation.z)), uint32_t(int32_t(r)));
    if (uint32_t(d2 + 8) < 0x10) actor.rotation.z = r;
    else actor.rotation.z = int16_t(actor.rotation.z + (d2 < 1 ? -8 : 8));
}

void attitude(StaticActor& actor) {
    GunshipFields& ship = classState<GunshipFields>(actor);
    const int t = int(ship.pitchTarget) << 2;
    const int d = angleDiff(uint32_t(int32_t(actor.rotation.x)), uint32_t(t));
    if (uint32_t(d + 8) < 0x10) actor.rotation.x = int16_t(t);
    else actor.rotation.x = int16_t(actor.rotation.x + (d < 1 ? -8 : 8));
    rollToTurn(actor);
}

void gunshipSmoke(const StaticActor& actor) {
    const uint32_t s = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!s) return;
    StaticActor& smoke = staticActorAt(s);
    auto&& params = SmokeParams(actorHeaderAt(s));
    smoke.flags = kActorLive;
    smoke.type = kSmokeType;
    smoke.variant = 0;
    smoke.loopFlags |= kActorSweepGroup2;
    smoke.pos.x.whole = actor.pos.x.whole;
    smoke.pos.y.whole = actor.pos.y.whole;
    smoke.pos.z.whole = actor.pos.z.whole;
    params.driftX = 0;
    params.driftZ = 0;
    params.life = 4;
}

void dropBomb(const StaticActor& actor, int16_t v) {
    int16_t dx, dz;
    angleVector(int(int16_t(uint16_t(actor.rotation.y) + 0x800u)), 0x90, dx, dz); // exe 0x5e40c0
    const uint32_t s = allocActor60(kActorStageClass);                          // exe 0x5e12f0
    if (!s) return;
    StaticActor& bomb = staticActorAt(s);
    bomb.subtype = 0;
    bomb.variant = 0;
    bomb.flags = kActorLive;
    bomb.type = kBombType;
    bomb.loopFlags |= kActorSweepGroup2;
    bomb.pos.x.whole = int16_t(actor.pos.x.whole + dx);
    bomb.pos.y.whole = int16_t(actor.pos.y.whole + 0x80);
    bomb.pos.z.whole = int16_t(actor.pos.z.whole + dz);
    bomb.rotation.y = actor.rotation.y;
    bomb.velocity.z = v;
    soundPlayAtPos(kBombSound, actor.pos);
}

bool gunshipHitTaken(const StaticActor& actor) {
    const uint32_t v = classState<GunshipFields>(const_cast<StaticActor&>(actor)).hitBox.result;
    return (v & kHitDamageMask) != 0 && (v & kHitByPlayerOrStrong) != 0;
}

GunshipHit gunshipHits(StaticActor& actor) {
    GunshipFields& ship = classState<GunshipFields>(actor);
    const uint32_t v = ship.hitBox.result, n = v & kHitDamageMask;
    if (n == 0 || !(v & kHitByPlayerOrStrong)) return GunshipHit::None;
    GunshipHit r = GunshipHit::None;
    actor.tint = kTintFlash;
    if (int(n) < (actor.maxHitPoints >> 2) && !(v & kHitAttackStrongPush)) {
        soundPlayAtPos(kHurtSound, actor.pos);
    } else {
        r = GunshipHit::Strong;
        soundPlayAtPos(kStrongHurtSound, actor.pos);
    }
    if (hitPointsAdd(actor, -int(n)) != 0) return GunshipHit::Dead;
    return r;
}

void gunshipCrash(const PlayerEnv& env, StaticActor& actor, int16_t minFall, bool drops) {
    GunshipFields& ship = classState<GunshipFields>(actor);
    const CrashSub sub = CrashSub(actor.subState);
    if (sub == CrashSub::Start) {
        ship.flight = kFlightNoRock | kFlightRocking | kFlightSway;
        countDefeat(); // exe 0x5e53e0
        ship.turnStep = int16_t(actor.spin.y < 1 ? -0x80 : 0x80);
        actor.spin.x = int16_t(actor.rotation.z < 1 ? -0x40 : 0x40);
        actor.velocity.y = 0;
        ship.heading = actor.rotation.y;
        actor.acceleration.y = 4;
        ship.timer = kCrashTicks;
        actor.subState = uint8_t(CrashSub::Fall);
        ship.behaviour &= uint8_t(~kBehaveNoPushOut);
    } else if (sub == CrashSub::Blast) {
        if (const uint32_t s = allocActor60(0)) { // exe 0x5e1300
            StaticActor& blast = staticActorAt(s);
            auto&& params = BlastParams(actorHeaderAt(s));
            blast.flags = kActorLive;
            blast.type = typeByte(GlobalStaticType::Blast);
            blast.loopFlags |= kActorSweepGroup2;
            blast.subtype = 0;
            blast.variant = 0;
            blast.pos.x = actor.pos.x;
            blast.pos.y = actor.pos.y;
            params.size = kCrashBlastSize;
            params.attack = kCrashBlastAttack;
            blast.pos.z = actor.pos.z;
        }
        if (drops) spawnDrops(actor.pos, staticStats(actor.type, 0).dropTable); // exe 0x5f1270
        freeActor(actor);
        return;
    }
    if (sub == CrashSub::Start || sub == CrashSub::Fall) {
        actor.velocity.y = int16_t(actor.velocity.y + actor.acceleration.y);
        if (actor.velocity.y < minFall) actor.velocity.y = minFall;
        actor.spin.y = int16_t(actor.spin.y + ship.turnStep);
        actor.rotation.y = int16_t(actor.rotation.y + (actor.spin.y >> 4));
        if (actor.spin.y > 0x800) actor.spin.y = 0x800;
        if (actor.spin.y < -0x800) actor.spin.y = -0x800;
        if (uint16_t(actor.rotation.z + 0x200) < 0x400) actor.rotation.z = int16_t(actor.spin.x + actor.rotation.z);
        moveAlongYaw(actor.pos, ship.heading, 0, actor.velocity.y, actor.velocity.z); // exe 0x5e36f0
        uint32_t r = actorTestMove(env, actor);                                       // exe 0x40f570
        if (int32_t(r) < 0) {
            queueBreakableWallHit(r, 0xff); // exe 0x58d4e0
            r = actorPushOut(env, actor);   // exe 0x410960
            if (int32_t(r) < 0) actor.pos = actor.prevPos;
            if (r & 0xf00) actor.velocity.z = 0;
            if (r & 0x3000) actor.velocity.y = 0;
            if (0x200 < uint16_t(actor.rotation.z + 0x100)) actor.subState = uint8_t(CrashSub::Blast);
        }
        const uint8_t t = uint8_t(ship.timer - 1);
        ship.timer = t;
        if (t == 0) actor.subState = uint8_t(CrashSub::Blast);
    }
    if (!(ship.timer & 1)) gunshipSmoke(actor);
    if (ship.timer < 0x4b) ship.flight |= kFlightDown;
}

void gunshipFire(const StaticActor& actor, int side, int lim, GunshipGun gun) {
    const int16_t rot[3] = {actor.rotation.x, actor.rotation.y, actor.rotation.z};
    int32_t o[3];
    rotateVector(rot, (side != 0 ? 0x400 : 0) - 0x200, 0xf00, -0xa00, o); // exe 0x5e3970
    const uint32_t s = allocActor40(kActorStageClass);                    // exe 0x5e1280
    if (!s) return;
    ProjectileActor& shot = projectileActorAt(s);
    auto&& params = EnemyShotFields(projectileActorAt(s));
    shot.flags = kActorLive;
    shot.type = kEnemyShotType;
    if (gun == GunshipGun::Square) shot.variant = actor.variant == 2 ? kStrongShotVariant : kShotVariant;
    else shot.variant = actor.variant < 2 ? kShotVariant : kStrongShotVariant;
    shot.loopFlags |= kActorSweepGroup2;
    shot.pos.x = int32_t(uint32_t(actor.pos.x) + uint32_t(o[0]));
    shot.pos.y = int32_t(uint32_t(actor.pos.y) + uint32_t(o[1]));
    shot.pos.z = int32_t(uint32_t(actor.pos.z) + uint32_t(o[2]));
    const Player& p = player();
    int16_t aim[3] = {p.pos.x.whole, p.pos.y.whole, p.pos.z.whole};
    aim[1] = int16_t(aim[1] - 0x40);
    const int16_t from[3] = {shot.pos.x.whole, shot.pos.y.whole, shot.pos.z.whole};
    int dy = angleDiff(uint32_t(int32_t(actor.rotation.y)), uint32_t(angleOf(p.pos, shot.pos)));
    if (lim < dy) dy = lim;
    if (dy < -lim) dy = -lim;
    int dp = angleDiff(uint32_t(int32_t(actor.rotation.x)), uint32_t(0x800 - pitchOf(aim, from)));
    if (lim < dp) dp = lim;
    if (dp < -lim) dp = -lim;
    shot.rotation.x = int16_t(dp + actor.rotation.x);
    shot.rotation.y = int16_t(actor.rotation.y + dy);
    shot.rotation.z = actor.rotation.z;
    params.ticks = kShotTicks;
    const StaticActorStats row = staticStats(actor.type, gun == GunshipGun::Mission ? 0 : 1);
    params.attack = row.power;
}

void gunshipFireAsked(StaticActor& actor, int lim, GunshipGun gun) {
    GunshipFields& ship = classState<GunshipFields>(actor);
    const uint8_t asked = ship.flight;
    if (asked & kGunLeft) gunshipFire(actor, 0, lim, gun);
    if (asked & kGunRight) gunshipFire(actor, 1, lim, gun);
    ship.flight &= uint8_t(~kGuns);
}

} // namespace od::port
