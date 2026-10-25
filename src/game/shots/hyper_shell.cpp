// The hyper shell (weapon 5, pool 0x10 type 4, exe 0x56d1b0): an accelerating shell (3 steps a tick, up to 0xa00) with
// a dark trail (static global type 5, exe 0x563400: a ring of points following the shell, drawn as smoke sprites),
// homing on its target after a delay; at its target's closest approach or a hit a blast, then blasts every other tick
// spreading out around it.
#include "engine/data_source.hpp"
#include "game/shots/hyper_shell_state.hpp"
#include "game/actor_state.hpp"
#include "game/shots/player_shots.hpp"

#include "game/actor_movement.hpp"
#include "game/global_class_types.hpp"
#include "game/player/player_weapons.hpp"

#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/distances.hpp"
#include "game/explosions.hpp"
#include "game/fixed_point.hpp"
#include "game/draw_commands.hpp"
#include "game/game_state.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/shots/shot_common.hpp"

namespace od::port {

using namespace hyper_shell_state;

namespace {

inline constexpr const char* kShellArea = "hyper shell";

// [0x8c498c]
enum class ShellState : uint8_t {
    Spawn = 0,       // exe 0x56d230
    Fly = 1,         // exe 0x56d580
    Blast = 2,       // exe 0x56d760: a blast, the shell freed
    DirectHit = 3,   // exe 0x56d760: a blast, the trail freed, then the after-blasts
    AfterBlasts = 4, // exe 0x56d7e0
};
// how a tick ended (the jump table 0x56d220)
enum class ShellEnd { Flying = 0, Burst = 1, HitWall = 2, OutOfRange = 3, Gone = -1 };

// ---- the dark trail (static global type 5) ---------------------------------------------------------------------------
// [0x8c4204]
enum class TrailState : uint8_t { Start = 0, Follow = 1 };
// the sprites: the smoke cells (2 x 4 of 0x30 texels by the age) of tpage 0x4e (subtractive) / 0x6e, CLUT 0x7c10
constexpr uint16_t kTrailClut = 0x7c10, kTrailShadeTpage = 0x4e, kTrailGlowTpage = 0x6e;
constexpr uint32_t kTrailShadeRgb = 0x3f0000, kTrailGlowRgb = 0xffffff, kTrailGlowFade = 0x202020;
constexpr uint32_t kTrailCell = 0x30, kTrailCellEdge = 0x2f, kTrailSpriteSize = 0x40;

// the point i of the trail set to the shell's position
void trailPointAtParent(TrailPoint& p, const Actor& parent) {
    p.point.x = parent.pos.x.whole;
    p.point.y = parent.pos.y.whole;
    p.point.z = parent.pos.z.whole;
}
// exe 0x563430: kept through the warps, its position cleared, no point drawn, the first at the shell
void trailStart(StaticActor& actor, DarkTrailFields& f) {
    actor.runFlags |= kActorWarpKept;
    actor.pos.x = 0;
    actor.pos.y = 0;
    actor.pos.z = 0;
    f.head = 0;
    f.length = 0;
    const Actor& parent = actorLayoutAt(f.parent);
    auto&& first = f.points[0];
    first.point.x = parent.pos.x.whole;
    first.point.y = parent.pos.y.whole;
    first.age = 0;
    first.point.z = parent.pos.z.whole;
    actor.state += 1u;
}
// exe 0x563490: one more point drawn (up to kTrailPoints); a warp's move added to the points; the points carried by the
// shell's velocity (along its rotation); the head takes the shell's position; drawn from the head back: per point two
// sprites (draw command billboards: the shade on tpage 0x4e, then the glow on 0x6e fading by kTrailGlowFade from white)
// of the cell by its age, size kTrailSpriteSize + age / 4, rotation = the frame << 10
void trailFollow(StaticActor& actor, DarkTrailFields& f) {
    ++f.length;
    if (f.length > int16_t(kTrailPoints)) f.length = int16_t(kTrailPoints);
    if (actor.pos.x != 0 || actor.pos.z != 0) {
        for (int i = 0; i < f.length; ++i) {
            auto&& p = f.points[size_t(i)];
            p.point.x += uint32_t(uint16_t(actor.pos.x.whole));
            p.point.z += uint32_t(uint16_t(actor.pos.z.whole));
        }
        actor.pos.x = 0;
        actor.pos.z = 0;
    }
    for (int i = 0; i < f.length; ++i) {
        const Actor& parent = actorLayoutAt(f.parent);
        auto&& p = f.points[size_t(i)];
        moveAlongRotation16(p.point, parent.rotation, parent.velocity.x, 0, parent.velocity.z); // exe 0x5e3870
    }
    const uint32_t h = (uint32_t(uint8_t(f.head)) + 1u) & (kTrailPoints - 1);
    f.head = int16_t(h);
    {
        const Actor& parent = actorLayoutAt(f.parent);
        auto&& p = f.points[h];
        trailPointAtParent(p, parent);
        p.age = 0;
    }
    uint32_t glow = dc::billboardColour(kTrailGlowRgb);
    uint32_t i = uint32_t(int32_t(f.head));
    for (int n = f.length - 1; n > 0; --n) {
        auto&& p = f.points[i];
        const int age = p.age;
        p.age = int16_t(age + 1);
        const uint32_t cy = (age > 3 ? 1u : 0u) * kTrailCell, cx = uint32_t(age & 3) * kTrailCell;
        dc::Billboard b;
        b.xy = dc::packXY(p.point.x, p.point.y);
        b.z = dc::zWord(p.point.z);
        b.clut = kTrailClut;
        b.uv[0] = dc::texel(cx, cy);
        b.uv[1] = dc::texel(cx + kTrailCellEdge, cy);
        b.uv[2] = dc::texel(cx, cy + kTrailCellEdge);
        b.uv[3] = dc::texel(cx + kTrailCellEdge, cy + kTrailCellEdge);
        b.sizeRotation = dc::sizeRotation(uint32_t((age >> 2) + int(kTrailSpriteSize)),
                                          uint32_t(gameState().frame) << 10);
        b.colour = dc::billboardColour(kTrailShadeRgb);
        b.tpage = kTrailShadeTpage;
        dc::emit(b, actor.at);
        b.colour = glow;
        b.tpage = kTrailGlowTpage;
        dc::emit(b, actor.at);
        glow -= kTrailGlowFade;
        i = (i - 1) & (kTrailPoints - 1);
    }
}

// ---- the shell ------------------------------------------------------------------------------------------------------
const DataTable<CollisionBoxRecord> shellBox{TableId::HyperShellBox};
constexpr int16_t kShellSpeed = -0x180, kShellAcceleration = 0x40, kShellTopSpeed = -0xa00;
constexpr int16_t kShellScale = 0x400;
constexpr uint16_t kShellSound = 0xbe;
constexpr uint16_t kHomingDelay = 8;
constexpr uint32_t kBurstReach = 0x400; // a closest approach within this bursts the shell
constexpr int kSteps = 3;

// x86 idiv-by-3 idiom (the 0x55555556 multiply): truncation toward zero
int div3(int v) {
    const int hi = int((int64_t(v) * 0x55555556LL) >> 32);
    return hi + int(uint32_t(hi) >> 31);
}

// a blast (static global type 7) at the shell
void blastAt(const ShotActor& shot, uint8_t variant, uint16_t size, uint16_t attack) {
    const uint32_t address = allocActor60(0);
    if (address == 0) return;
    StaticActor& s = staticActorAt(address);
    s.loopFlags |= kActorSweepGroup1;
    s.flags = kActorLive;
    s.type = typeByte(GlobalStaticType::Blast);
    s.variant = variant;
    s.pos = shot.pos;
    auto&& blast = BlastParams(s);
    blast.size = size;
    blast.attack = attack;
}

ShellEnd spawn(const PlayerEnv& env, ShotActor& shot, ShellFields& fields) { // exe 0x56d230
    const Player& p = player();
    shot.flags |= kActorDrawn | kActorMeshModel;
    shot.runFlags |= kActorShown | kActorMeshModel | kActorWarpKept;
    if (!bindActorModel(shot, 0)) {
        freeActor(shot);
        return ShellEnd::Gone;
    }
    int x = 0, z = 0;
    if (p.platform) platformMove(x, z);
    const int16_t sx = wrap16(x), sz = wrap16(z);
    const uint32_t tr = allocActor60(0);
    if (tr == 0) {
        freeActor(shot);
        return ShellEnd::Gone;
    }
    StaticActor& trail = staticActorAt(tr);
    trail.flags = kActorLive;
    trail.type = typeByte(GlobalStaticType::DarkTrail);
    trail.variant = 0;
    trail.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    trail.velocity.x = sx;
    trail.velocity.z = sz;
    classState<DarkTrailFields>(trail).parent = shot.at;
    fields.trail = tr;
    auto&& shotFields = PlayerShotFields(shot);
    shotFields.hitBox.result = 0;
    if (p.onPlatform == 0) {
        shot.velocity.x = 0;
        shot.velocity.z = kShellSpeed;
    } else {
        shot.velocity.x = wrap16(div3(sx));
        shot.velocity.z = wrap16(div3(sz) + kShellSpeed);
    }
    shot.velocity.y = 0;
    placeAtBone(shot, kLeftHandBone, 0);
    aimAlongPlayer(shot);
    moveAlongRotation(shot.prevPos, shot.rotation, 0, 0, -int(shot.velocity.z));
    if (shotFields.target != nullptr) {
        aimRelativeToTarget(shot);
        int32_t aim[3];
        targetAimPoint(shot, slotIdentityWord(shotFields.target), aim);
        fields.closest = uint32_t(distance3(shot.pos, aim));
    }
    shot.scale.x = kShellScale;
    shot.scale.y = kShellScale;
    shot.scale.z = kShellScale;
    shot.collisionBox.pointAt(shellBox.stateAddress());
    shot.actorHit = 0;
    soundPlayAtPos(kShellSound, shot.pos);
    fillShotStats(fields.stats);
    paySpecialEnergy(player());
    fields.homingDelay = kHomingDelay;
    fields.locked = p.lockTarget;
    const uint32_t n = uint32_t(int32_t(weaponLevel(p, p.activeSpecial, WeaponStat::Extra)) * 4 + 4) & 0xffff;
    fields.blastTicks = uint16_t(n);
    fields.blastHalf = uint16_t(n >> 1);
    ++shot.state;
    return shotHitWall(env, shot, fields.stats.attack) ? ShellEnd::HitWall : ShellEnd::Flying;
}

ShellEnd fly(const PlayerEnv& env, ShotActor& shot, ShellFields& fields) { // exe 0x56d580
    auto&& shotFields = PlayerShotFields(shot);
    if (shot.actorHit != 0 || (shotFields.hitBox.result & (kHitAttackSolid | kHitAttackDeflects))) {
        stopHalfway(shot);
        return shot.actorHit != 0 ? ShellEnd::OutOfRange : ShellEnd::Burst;
    }
    shot.savePreviousPose();
    const uint32_t t = slotIdentityWord(shotFields.target);
    if (fields.locked != 0 && t != 0) {
        if (fields.homingDelay != 0)
            --fields.homingDelay;
        else
            homeOnTarget(shot, t, 4, kHomingKeepTarget | kHomingAlwaysTurn);
        int32_t aim[3];
        targetAimPoint(shot, t, aim);
        const uint32_t d = uint32_t(distance3(shot.pos, aim));
        if (fields.closest <= d) {
            if (fields.homingDelay == 0 && fields.closest < kBurstReach)
                return ShellEnd::Burst; // the closest approach passed
        } else {
            fields.closest = d;
        }
    }
    --fields.stats.range;
    if (fields.stats.range == 0)
        return (fields.locked != 0 && shotFields.target != nullptr) ? ShellEnd::Burst : ShellEnd::OutOfRange;
    for (int k = 0; k < kSteps; ++k) {
        moveShot(shot, shot.velocity.x, 0, shot.velocity.z);
        if (shotHitWall(env, shot, fields.stats.attack)) return ShellEnd::HitWall;
    }
    shot.velocity.z = wrap16(shot.velocity.z - kShellAcceleration);
    if (shot.velocity.z < kShellTopSpeed) shot.velocity.z = kShellTopSpeed;
    markFlying(shot);
    return ShellEnd::Flying;
}

ShellEnd blast(ShotActor& shot, ShellFields& fields) { // exe 0x56d760 (states 2 and 3)
    const uint32_t level = uint16_t(fields.stats.level);
    blastAt(shot, 1, uint16_t((level + 6) << 5), uint16_t((level + 1) << 3));
    if (ShellState(shot.state) == ShellState::DirectHit) {
        shot.state = uint8_t(ShellState::AfterBlasts);
        freeActor(staticActorAt(fields.trail));
        return ShellEnd::Gone;
    }
    freeActor(shot);
    return ShellEnd::Gone;
}

ShellEnd afterBlasts(ShotActor& shot, ShellFields& fields) { // exe 0x56d7e0: two blasts every other tick, spreading out
    shot.flags &= uint8_t(~kActorDrawn);
    uint32_t angle;
    const uint32_t t = slotIdentityWord(PlayerShotFields(shot).target);
    if (fields.locked != 0 && t != 0) {
        int32_t aim[3];
        targetAimPoint(shot, t, aim);
        angle = uint32_t(angleOf(shot.pos.x, shot.pos.z, aim[0], aim[2]) - kQuarterTurn); // exe 0x5e3bc0 - a quarter
    } else {
        angle = uint16_t(shot.rotation.y);
    }
    --fields.blastTicks;
    const uint32_t n = fields.blastTicks;
    if (n == 0) {
        freeActor(shot);
        return ShellEnd::Gone;
    }
    if (!(n & 1)) return ShellEnd::Gone;
    const int ring = int(fields.blastHalf) - int(n >> 1);
    const uint32_t level = uint16_t(fields.stats.level);
    for (uint32_t k = 0; k < 2; ++k) {
        const uint32_t i = ((rngStep() & 0x1ff) + angle + k * kHalfTurn - 0x500) & kAngleMask;
        const uint32_t address = allocActor60(0);
        if (address == 0) return ShellEnd::Gone;
        StaticActor& s = staticActorAt(address);
        s.flags = kActorLive;
        s.type = typeByte(GlobalStaticType::Blast);
        s.variant = 0;
        s.loopFlags |= kActorSweepGroup1;
        s.pos.x = int32_t(uint32_t(fixedSin(int(i)) * (ring + 1)) * 0x1000u + uint32_t(shot.pos.x));
        s.pos.y = int32_t((((rngStep() & 0x1ff) - 0x100u) << 16) + uint32_t(shot.pos.y));
        s.pos.z = int32_t(uint32_t(fixedCos(int(i)) * (ring + 1)) * 0x1000u + uint32_t(shot.pos.z));
        const int f = 0xc - ring;
        auto&& params = BlastParams(s);
        params.size = uint16_t((int32_t((level + 8) * uint32_t(f)) << 4) >> 3);
        params.attack = uint16_t((int32_t((level + 2) * uint32_t(f)) << 1) >> 3);
    }
    return ShellEnd::Gone;
}

// exe 0x56d9c0: the capsule from the previous position (radius (level * 8 + 0x20) / 4)
void submitShellHit(ShotActor& shot, const ShotStats& stats) {
    submitShotCapsule(shot, uint16_t(int32_t(uint32_t(uint16_t(stats.level)) * 8 + 0x20) >> 2),
                      hitGroup(kHitGroupPlayerAttacks) | kHitContactMotion | hitCapsule(1),
                      stats.attack | kHitAttackByPlayer | kHitAttackStrongPush);
}

} // namespace

void darkTrailUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    auto&& f = classState<DarkTrailFields>(actor);
    if (actorLayoutAt(f.parent).flags == 0) { // the shell is gone
        freeActor(actor);
        return;
    }
    switch (TrailState(actor.state)) {
    case TrailState::Start: trailStart(actor, f); return;
    case TrailState::Follow: trailFollow(actor, f); return;
    default: notPortedAt(kShellArea, "0x563400 state", actor.state);
    }
}

void hyperShellShotUpdate(const PlayerEnv& env, uint32_t address) {
    ShotActor& shot = shotActorAt(address);
    auto&& fields = classState<ShellFields>(shot);
    dropDeadTarget(shot);
    ShellEnd end;
    switch (ShellState(shot.state)) {
    case ShellState::Spawn: end = spawn(env, shot, fields); break;
    case ShellState::Fly: end = fly(env, shot, fields); break;
    case ShellState::Blast:
    case ShellState::DirectHit: end = blast(shot, fields); break;
    case ShellState::AfterBlasts: end = afterBlasts(shot, fields); break;
    default: notPortedAt(kShellArea, "0x56d1b0 state", shot.state);
    }
    switch (end) {
    case ShellEnd::Flying:
        actorCollisionSubmitMover(shot);
        submitShellHit(shot, fields.stats);
        return;
    case ShellEnd::Burst: shot.state = uint8_t(shot.state + 2u); return; // a direct hit: the after-blasts
    case ShellEnd::HitWall:
        submitShellHit(shot, fields.stats);
        ++shot.state;
        return;
    case ShellEnd::OutOfRange: ++shot.state; return;
    default: return;
    }
}

} // namespace od::port
