// The reflector (weapon 9, pool 0x10 type 8, exe 0x56e730): a shot that bounces off walls and actors (a spray of
// sparks, static global type 0xf, exe 0x566d40, and a sound each time; skidding along a floor), turns away from a
// deflecting body, homes on its target; its sprite and glow are draw commands, and so are the spray's 12 sparks.
#include "engine/data_source.hpp"
#include "game/shots/reflector_state.hpp"
#include "game/actor_state.hpp"
#include "game/shots/player_shots.hpp"

#include "game/actor_movement.hpp"
#include "game/global_class_types.hpp"
#include "game/player/player_weapons.hpp"

#include "game/actor_collision.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/draw_commands.hpp"
#include "game/fixed_point.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/shots/shot_common.hpp"

namespace od::port {

using namespace reflector_state;

namespace {

inline constexpr const char* kReflectorArea = "reflector";

// ---- the spray of sparks (static global type 0xf) --------------------------------------------------------------------
// [0x8c4710]
enum class SprayState : uint8_t { Start = 0, Fly = 1 }; // exe 0x566d60 / 0x566e40
constexpr uint8_t kSprayTicks = 6;
constexpr int16_t kSparkFall = 0x20;          // the pitch's step toward the ground each tick
constexpr int16_t kSparkGrounded = 0xc00;     // the pitch once within kSparkFall of it
constexpr uint32_t kSprayBlueDisc = 0x80, kSprayGreyDisc = 0x20, kSparkDisc = 4;
constexpr uint32_t kGreyScale = 0x10101;

// exe 0x566e70(actor, b): at the spray (<< the stage shift; in front and on the screen): two discs (ring records: blue
// and grey by the ticks) and the sparks (their pitch falling toward the ground), each a disc at the spray + the spark's
// direction * speed * (9 - ticks) * 2
void sparkSprayDraw(StaticActor& actor, SparkSprayFields& f) {
    const int sh = stage().coordinateShift & 0x1f;
    const int16_t P[3] = {wrap16(int(actor.pos.x.whole) << sh), wrap16(int(actor.pos.y.whole) << sh),
                          wrap16(int(actor.pos.z.whole) << sh)};
    int16_t sxy[2];
    if (projectOnScreen(P, sxy) < 0) return; // exe 0x5f2dd0 (shifts again, as the exe)
    if (!effectOnScreen(sxy[0], sxy[1])) return;
    const uint32_t c = (uint32_t(f.ticks) + 1) << 4;
    const uint32_t blue = dc::ringColour(c << 16), grey = dc::ringColour(c * kGreyScale);
    dc::Ring disc;
    disc.inner = blue;
    disc.xy = dc::packXY(P[0], P[1]);
    disc.zStep = dc::zWord(P[2]) | dc::ringStep(2);
    disc.radii = dc::ringRadii(0, kSprayBlueDisc);
    dc::emit(disc, actor.at);
    disc.inner = grey;
    disc.radii = dc::ringRadii(0, kSprayGreyDisc);
    dc::emit(disc, actor.at);
    for (uint32_t k = 0; k < kSpraySparks; ++k) {
        auto&& spark = f.sparks[k];
        const int16_t d = spark.pitch;
        const int e = int(uint32_t(d + kQuarterTurn) & kAngleMask);
        if (e > kSparkFall) spark.pitch = int16_t(uint16_t(uint32_t(e > kHalfTurn ? d + kSparkFall : d - kSparkFall)));
        else spark.pitch = kSparkGrounded;
        const int16_t rot[3] = {spark.pitch, spark.yaw, spark.roll};
        int32_t o[3];
        rotateVector(rot, 0, 0, spark.speed * (9 - int(f.ticks)) * 2, o); // exe 0x5e3970
        const int16_t qx = wrap16(P[0] + (o[0] >> 16)), qy = wrap16(P[1] + (o[1] >> 16)), qz = wrap16(P[2] + (o[2] >> 16));
        dc::Ring s;
        s.inner = grey;
        s.xy = dc::packXY(qx, qy);
        s.zStep = dc::zWord(qz) | dc::ringStep(4);
        s.radii = dc::ringRadii(0, kSparkDisc);
        dc::emit(s, actor.at);
    }
}
// exe 0x566d60: kept through the warps, the sparks around its direction, kSprayTicks
void sparkSprayStart(StaticActor& actor, SparkSprayFields& f) {
    actor.runFlags |= kActorWarpKept;
    for (uint32_t k = 0; k < kSpraySparks; ++k) {
        auto&& spark = f.sparks[k];
        spark.pitch = int16_t(((rngStep() & 0x7ff) + uint32_t(f.pitch) - 0x300u) & kAngleMask);
        spark.yaw = int16_t(((rngStep() & 0x7ff) + uint32_t(f.yaw) - 0x400u) & kAngleMask);
        spark.roll = 0;
        spark.speed = int16_t((rngStep() & 0x7f) + 0x20);
    }
    f.ticks = kSprayTicks;
    sparkSprayDraw(actor, f);
    actor.state += 1u;
}
// exe 0x566e40: a tick less (none left: freed)
void sparkSprayFly(StaticActor& actor, SparkSprayFields& f) {
    if (f.ticks == 0) {
        freeActor(actor);
        return;
    }
    --f.ticks;
    sparkSprayDraw(actor, f);
}

// [0x8c4a04]
enum class ReflectorState : uint8_t { Spawn = 0, Fly = 1 }; // exe 0x56e7a0 / 0x56e9f0
enum class ReflectorEnd { Flying = 0, HitBody = 1, OutOfRange = 2 };

const DataTable<CollisionBoxRecord> reflectorBox{TableId::ReflectorBox};
constexpr int16_t kReflectorSpeed = -0x600;
constexpr uint16_t kReflectorSound = 0xff, kSprayedSound = 0x100, kDeflectSound = 0x8f;
constexpr uint16_t kDeflectTicks = 2, kSkidTicks = 4;
constexpr uint32_t kActorSideHit = 0x3000; // the actor collision result's top / bottom bits (cleared by a bounce)
constexpr uint32_t kActorHitKept = 0xf0ff;

// a spray of sparks (static global type 0xf) at the shot along its rotation, and its sound
void spray(const ShotActor& shot) {
    if (const uint32_t address = allocActor60(0)) {
        StaticActor& s = staticActorAt(address);
        s.flags = kActorLive;
        s.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
        s.type = typeByte(GlobalStaticType::SparkSpray);
        s.variant = 0;
        s.pos = shot.pos;
        SparkSprayFields& params = classState<SparkSprayFields>(s);
        params.pitch = uint16_t(shot.rotation.x);
        params.yaw = uint16_t(shot.rotation.y);
    }
    soundPlayAtPos(kSprayedSound, shot.pos);
}

ReflectorEnd spawn(const PlayerEnv& env, ShotActor& shot, ReflectorFields& fields) { // exe 0x56e7a0
    shot.flags &= uint8_t(~kActorDrawn);
    shot.runFlags |= kActorWarpKept;
    aimRelativeToTarget(shot);
    PlayerShotFields(shot).hitBox.result = 0;
    if (player().onPlatform == 0) {
        shot.velocity.x = 0;
        shot.velocity.z = kReflectorSpeed;
    } else {
        int x, z;
        platformMove(x, z);
        shot.velocity.x = wrap16(x);
        shot.velocity.z = wrap16(z + kReflectorSpeed);
    }
    shot.velocity.y = 0;
    placeAtBone(shot, kLeftHandBone, 0);
    aimAlongPlayer(shot);
    moveAlongRotation(shot.prevPos, shot.rotation, 0, 0, -int(shot.velocity.z) * 2);
    fillShotStats(fields.stats);
    fields.deflectTicks = 0;
    fields.skidTicks = 0;
    paySpecialEnergy(player());
    shot.collisionBox.pointAt(reflectorBox.stateAddress());
    shot.actorHit = 0;
    soundPlayAtPos(kReflectorSound, shot.pos);
    ++shot.state;
    if (int32_t(actorTestMove(env, shot)) < 0)
        bounceRotationOffWalls(shot.rotation, actorPushOut(env, shot)); // exe 0x4107d0
    return ReflectorEnd::Flying;
}

ReflectorEnd fly(const PlayerEnv& env, ShotActor& shot, ReflectorFields& fields) { // exe 0x56e9f0
    auto&& shotFields = PlayerShotFields(shot);
    if (shotFields.hitBox.result & kHitAttackSolid) {
        stopHalfway(shot);
        return ReflectorEnd::HitBody;
    }
    const uint32_t hit = shot.actorHit;
    if (hit != 0) { // bounced off an actor
        if (hit & kActorSideHit) shot.actorHit = hit & kActorHitKept;
        shot.rotation.y = int16_t((uint16_t(shot.rotation.y) - uint32_t(kHalfTurn)) & kAngleMask);
        spray(shot);
    }
    shot.savePreviousPose();
    if (fields.deflectTicks != 0) {
        --fields.deflectTicks;
    } else if (shotFields.hitBox.result & kHitAttackDeflects) { // deflected: turn away from the contact point
        const int dx = int(shot.pos.x.whole) - shotFields.hitBox.contact.x;
        const int dz = -(int(shot.pos.z.whole) - shotFields.hitBox.contact.z);
        shot.rotation.y = int16_t(uint32_t(-atanAngle(float(dx), float(dz))) & kAngleMask);
        fields.deflectTicks = kDeflectTicks;
        soundPlayAtPos(kDeflectSound, shot.pos);
    }
    if (shotFields.target != nullptr) homeOnTarget(shot, slotIdentityWord(shotFields.target), 2, 0);
    --fields.stats.range;
    if (fields.stats.range == 0) return ReflectorEnd::OutOfRange;
    moveShot(shot, shot.velocity.x, 0, shot.velocity.z);
    if (int32_t(actorTestMove(env, shot)) < 0) {
        const uint32_t pushed = actorPushOut(env, shot);
        bounceRotationOffWalls(shot.rotation, pushed);
        const bool flat =
            (pushed & (kWallOnYPlus | kWallOnYMinus)) && !(pushed & kWallOnSides); // a floor / ceiling only
        if (fields.skidTicks == 0) {
            if (flat) fields.skidTicks = kSkidTicks;
            spray(shot);
        } else if (!flat) {
            spray(shot);
        }
    }
    if (fields.skidTicks != 0) --fields.skidTicks;
    // the sprite (an opaque billboard: CLUT 0x7d50, tpage 0x0f) and the glow (exe 0x56edb0)
    const uint32_t level = uint16_t(fields.stats.level);
    dc::Billboard b;
    b.colour = dc::opaqueBillboardColour(0x808080);
    b.xy = drawXY(shot);
    b.z = dc::signedZWord(shot.pos.z.whole);
    b.clut = 0x7d50;
    b.tpage = 0x0f;
    b.uv[0] = dc::texel(0xe0, 0x80);
    b.uv[1] = dc::texel(0xff, 0x80);
    b.uv[2] = dc::texel(0xe0, 0x9f);
    b.uv[3] = dc::texel(0xff, 0x9f);
    b.sizeRotation = dc::sizeRotation(level * 4 + 0x10, 0);
    dc::emit(b, shot.at);
    markFlying(shot);
    dc::emitDisc(shot.at, dc::ringColour(0x808080), drawXY(shot), uint16_t(shot.pos.z.whole) | dc::ringStep(2),
                 (level + 2) << 0x13);
    return ReflectorEnd::Flying;
}

} // namespace

void sparkSprayUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    SparkSprayFields& f = classState<SparkSprayFields>(actor);
    switch (SprayState(actor.state)) {
    case SprayState::Start: sparkSprayStart(actor, f); return;
    case SprayState::Fly: sparkSprayFly(actor, f); return;
    default: notPortedAt(kReflectorArea, "0x566d40 state", actor.state);
    }
}

void reflectorShotUpdate(const PlayerEnv& env, uint32_t address) {
    ShotActor& shot = shotActorAt(address);
    auto&& fields = classState<ReflectorFields>(shot);
    dropDeadTarget(shot);
    ReflectorEnd end;
    switch (ReflectorState(shot.state)) {
    case ReflectorState::Spawn: end = spawn(env, shot, fields); break;
    case ReflectorState::Fly: end = fly(env, shot, fields); break;
    default: notPortedAt(kReflectorArea, "0x56e730 state", shot.state);
    }
    switch (end) {
    case ReflectorEnd::Flying:
        actorCollisionSubmitMover(shot);
        // exe 0x56ecc0: radius (level * 8 + 0x10) / 4, the contact point
        submitShotHit(shot,
                      hitGroup(kHitGroupPlayerAttacks) | kHitContactPoint |
                          hitRadius(uint32_t(uint16_t(fields.stats.level)) * 8 + 0x10),
                      fields.stats.attack | kHitAttackByPlayer);
        return;
    case ReflectorEnd::HitBody:
        spawnShotSpark(shot, fields.stats.level, 0); // exe 0x56ed50
        freeActor(shot);
        return;
    case ReflectorEnd::OutOfRange: freeActor(shot); return;
    }
}

} // namespace od::port
