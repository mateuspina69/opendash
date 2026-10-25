// The Flutter's crash landing (ST3A, script 6): the ships in the sky (lowered into place, bobbing, firing pairs of
// enemy shots while the script's step is young), the cloud rings and layers (their angles turned and their scroll
// counters stepped; the clouds themselves are render output), the dropper that leaves falling pieces when the script
// asks, the static that rides its parent's bone, a flash, and the falling pieces.
#include "content/intro/crash_landing.hpp"
#include "content/intro/crash_landing_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_animation.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/camera_state.hpp"
#include "game/enemy_shot.hpp"
#include "game/field_alias.hpp"
#include "game/fixed_point.hpp"
#include "game/geometry_scratch.hpp"
#include "game/matrix.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/stage_state.hpp"

namespace od::port {

using namespace crash_landing_state;

namespace {

inline constexpr const char* kCrashArea = "crash landing";

// ---- the sky ship ---------------------------------------------------------------------------------------------------
// the sky ship's states
enum class SkyShipState : uint8_t { Place = 0, Lower = 1, Hover = 2 };
const DataTable<CollisionBoxRecord> skyShipBox{TableId::CrashLandingSkyShipBox};
// each kind's pitch, the height it starts above its own and the ticks it is lowered in
struct SkyShipKind {
    int16_t pitch;
    uint16_t lift;
    int16_t ticks;
};
constexpr SkyShipKind kSkyShipKinds[3] = {{0x38, 500, 0xc}, {0x70, 600, 0xf}, {0x58, 0x1c2, 10}};
constexpr int32_t kBobHeight = 0x200;
constexpr int32_t kFireUntil = 0xe6; // the script step's tick after which the ships stop firing
constexpr uint8_t kShipShotVariant = 4;
constexpr int16_t kShipShotTicks = 0x40;
constexpr uint16_t kShipShotAttack = 1;
constexpr int kGunSide = 0x400, kGunDown = 0xe80, kGunAhead = -0x1380;

// the ship's spawn: its model, its height raised by its kind's lift, its pitch, its lowering's ticks
void skyShipPlace(StaticActor& actor, SkyShipFields& ship) {
    if (!bindActorModel(actor, 0)) {
        freeActor(actor);
        return;
    }
    actor.collisionBox.pointAt(skyShipBox.stateAddress());
    actor.airborne = 0;
    ship.clear(); // exe 0x40dc40(+0xbc, +0xc8)
    const int32_t y = actor.pos.y;
    actor.rotation.z = 0;
    actor.velocity.z = 0;
    actor.velocity.y = 0;
    actor.velocity.x = 0;
    actor.acceleration.z = 0;
    actor.acceleration.y = 0;
    actor.acceleration.x = 0;
    actor.enterState(uint8_t(SkyShipState::Lower));
    ship.groundY = y;
    ship.raisedY = y;
    ship.tick = 0;
    const SkyShipKind& k = kSkyShipKinds[ship.kind == 0 ? 0 : ship.kind == 1 ? 1 : 2];
    actor.rotation.x = k.pitch;
    ship.raisedY.whole = wrap16(ship.raisedY.whole + k.lift);
    ship.lowerTicks = k.ticks;
    actor.pos.y = int32_t(ship.raisedY);
}

void skyShipLower(StaticActor& actor, SkyShipFields& ship) { // a quadratic ease onto its height
    const int16_t c = ship.tick;
    if (c == 0) {
        actor.pos.y = int32_t(ship.raisedY);
        ++ship.tick;
        return;
    }
    const int32_t n = ship.lowerTicks;
    if (int32_t(c) == n - 1) {
        actor.pos.y = ship.groundY;
        ship.tick = 0;
        actor.enterState(uint8_t(SkyShipState::Hover));
        return;
    }
    const int32_t i = ((int32_t(c) - n) * 0x100) / n;
    actor.pos.y = int32_t(((int32_t(ship.raisedY) - ship.groundY) >> 8) * ((i * i) >> 8) + ship.groundY);
    ++ship.tick;
}

void skyShipHover(StaticActor& actor, SkyShipFields& ship) { // bobs; fires two shots every 4 ticks (exe 0x595720)
    actor.pos.y = int32_t(fixedSin(int((uint8_t(ship.tick) & 0x3f) * 0x40)) * kBobHeight + ship.groundY);
    if (scriptState().stepTick < kFireUntil && (uint8_t(ship.tick) & 3) == 0) {
        for (int i = 0; i < 2; ++i) {
            const uint32_t s = allocActor40(kActorStageClass); // exe 0x5e1280
            if (s == 0) continue;
            int32_t v[3];
            rotateVector(actor.rotation, i == 0 ? kGunSide : -kGunSide, kGunDown, kGunAhead, v);
            ProjectileActor& shot = projectileActorAt(s);
            shot.flags = kActorLive;
            shot.type = kEnemyShotType;
            shot.variant = kShipShotVariant;
            shot.pos.x = int32_t(uint32_t(actor.pos.x) + uint32_t(v[0]));
            shot.pos.y = int32_t(uint32_t(actor.pos.y) + uint32_t(v[1]));
            shot.pos.z = int32_t(uint32_t(actor.pos.z) + uint32_t(v[2]));
            shot.rotation = actor.rotation;
            auto&& params = EnemyShotFields(projectileActorAt(s));
            params.ticks = kShipShotTicks;
            params.attack = kShipShotAttack;
        }
    }
    ++ship.tick;
}

// ---- the cloud rings -----------------------------------------------------------------------------------------------
constexpr uint8_t kCloudTwinVariant = 1;
// the puffs' frame rebuilt every tick (the exe's stores: the identity matrix, no translation, in the geometry
// scratch's transform at +0x40)
void identityFrame() { geometryScratch().transform(0x40).setIdentityWords(); }
// the first n angles turned back by k + step each (wrapped below a turn)
void turnAngles(CloudRingFields& ring, int n, int step) {
    for (int k = 0; k < n; ++k) {
        const int v = int(wrap16(ring.angles[size_t(k)] - k - step));
        ring.angles[size_t(k)] = int16_t(v % kFullTurn);
    }
}
// a counter following the camera's yaw change (forward: last tick's yaw - this tick's, else the other way), times mul
// plus add, back to 0 past lim
void followCameraYaw(CloudRingFields& ring, size_t i, int mul, int add, int16_t lim, bool forward) {
    const CameraState& cam = cameraState();
    const int d = forward ? int(uint16_t(uint16_t(cam.prevViewAngles.y) - uint16_t(cam.viewAngles.y)))
                          : int(cam.viewAngles.y) - int(cam.prevViewAngles.y);
    ring.angles[i] = int16_t(uint16_t(uint16_t(ring.angles[i]) + uint16_t(d * mul + add)));
    if (ring.angles[i] > lim) ring.angles[i] = 0;
}

// ---- the dropper / the rider / the flash / the falling piece --------------------------------------------------------
// The dropper's trigger (set by the script: 1 = a piece to the right, else to the left)
struct DropperFields {
    explicit DropperFields(const StaticActor& actor) : DropperFields(const_cast<StaticActor&>(actor)) {}
    explicit DropperFields(StaticActor& actor) : drop(actor.params[1]) {}
    uint8_t& drop;
};
enum class DropperSub : uint8_t { Fall = 0, Wait = 1 };
const DataTable<CollisionBoxRecord> dropperBox{TableId::CrashLandingDropperBox};
constexpr int16_t kDropperScale = 0x440;
constexpr int16_t kDropperGravity = 8, kDropperFloor = 0x10;
constexpr uint8_t kFallingPieceType = 0x21;
constexpr int16_t kPieceSide = 0x40, kPieceUp = 0x80, kPieceBack = 0x80;

// The rider's parent (its slot id, the spawn's parameter) and the pose record it copies from the parent's first bone
struct RiderFields {
    explicit RiderFields(const ActorHeader& actor) : RiderFields(const_cast<ActorHeader&>(actor)) {}
    explicit RiderFields(ActorHeader& actor) : parent(actor.params[0]) {}
    FieldAlias<int8_t, uint8_t> parent;
};
const DataTable<CollisionBoxRecord> riderBox{TableId::CrashLandingRiderBox};

constexpr uint16_t kFlashSound = 0x8c;

constexpr int16_t kPieceDiveSpeed = -0xc00, kPieceDiveAccel = -0x10, kPieceFallSpeed = -0x800;
constexpr int16_t kPieceScale = 0x180;

} // namespace

void skyShipUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    SkyShipFields& ship = classState<SkyShipFields>(actor);
    actor.savePreviousPose();
    switch (SkyShipState(actor.state)) {
    case SkyShipState::Place: skyShipPlace(actor, ship); return;
    case SkyShipState::Lower: skyShipLower(actor, ship); return;
    case SkyShipState::Hover: skyShipHover(actor, ship); return;
    default: return;
    }
}

void cloudRingUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    auto&& ring = classState<CloudRingFields>(actor);
    if (actor.state == uint8_t(CloudRingState::Start)) {
        for (size_t k = 0; k < 8; ++k) ring.angles[k] = int16_t(k * 0x200);
        if (actor.variant == 0) {                                    // its late twin
            if (const uint32_t t = allocActor60(kActorStageClass)) { // exe 0x5e12f0
                StaticActor& twin = staticActorAt(t);
                twin.flags = kActorLive;
                twin.type = actor.type;
                twin.loopFlags |= kActorLateLoop;
                twin.variant = kCloudTwinVariant;
                twin.pos = actor.pos;
                classState<CloudRingFields>(staticActorAt(t)).copyParamsFrom(ring);
            }
        }
        actor.state = uint8_t(CloudRingState::Turn);
    } else if (actor.state != uint8_t(CloudRingState::Turn)) {
        notPortedAt(kCrashArea, "0x598b00 state", actor.state);
    }
    switch (cloudRoutines->idAt(actor.variant)) {
    case RoutineId::CrashCloudPuffs: // the puffs' rings (render output: culled against the camera, projected, textured quads)
        identityFrame();
        turnAngles(ring, 9, 6);
        break;
    case RoutineId::CrashCloudPuffsSmall:
        identityFrame();
        turnAngles(ring, 4, 8);
        break;
    case RoutineId::CrashCloudScroll: // three scroll counters (the sky tiles and sprite bands: render output)
        for (size_t k = 0; k < 3; ++k) {
            const int16_t c = ring.angles[k];
            ring.angles[k] = int16_t(c + 1);
            if (c >= (k == 2 ? 0x200 : 0x400)) ring.angles[k] = 0;
        }
        break;
    case RoutineId::CrashCloudQuad: break; // one semi-transparent quad (render output)
    case RoutineId::CrashCloudSprites:     // a ring of sprites (render output)
        identityFrame();
        turnAngles(ring, 4, 6);
        break;
    case RoutineId::CrashCloudYawFollow:
        followCameraYaw(ring, 1, 8, 1, 0xfff, true);
        followCameraYaw(ring, 2, 2, 1, 0x3ff, false);
        break;
    case RoutineId::CrashCloudYawFollowOne: followCameraYaw(ring, 1, 8, 1, 0xfff, true); break;
    default: notPortedAt(kCrashArea, "0x598b00 variant", actor.variant);
    }
}

// +8 of the dropper and the bone rider: bound, running
enum class StaticBindState : uint8_t { Bind = 0, Run = 1 };

void piecesDropperUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    auto&& dropper = DropperFields(actor);
    if (StaticBindState(actor.state) == StaticBindState::Bind) {
        if (!bindActorModel(actor, 0)) {
            freeActor(actor);
            return;
        }
        actor.collisionBox.pointAt(dropperBox.stateAddress());
        actor.airborne = 0;
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        actor.velocity.z = 0;
        actor.velocity.y = 0;
        actor.velocity.x = 0;
        actor.scale.z = kDropperScale;
        actor.scale.y = kDropperScale;
        actor.scale.x = kDropperScale;
        actor.enterState(uint8_t(StaticBindState::Run));
    }
    if (actor.subState == uint8_t(DropperSub::Fall)) {
        actor.velocity.y = wrap16(uint16_t(actor.velocity.y) - kDropperGravity);
        actor.pos.y.whole = wrap16(uint16_t(actor.pos.y.whole) + uint16_t(actor.velocity.y));
        if (actor.pos.y.whole <= kDropperFloor) {
            actor.pos.y.whole = kDropperFloor;
            actor.subState = uint8_t(DropperSub::Wait);
        }
    } else if (actor.subState == uint8_t(DropperSub::Wait) && dropper.drop != 0) { // exe 0x5b3a50
        if (const uint32_t d = allocActor40(kActorStageClass)) {                   // exe 0x5e1280
            ProjectileActor& piece = projectileActorAt(d);
            piece.flags = kActorLive | kActorDrawn;
            piece.type = kFallingPieceType;
            piece.subtype = 0;
            piece.variant = 0;
            for (size_t k = 0; k < piece.params.size(); ++k) piece.params[k] = 0;
            piece.pos = actor.pos;
            piece.pos.x.whole = wrap16(uint16_t(piece.pos.x.whole) + (dropper.drop == 1 ? kPieceSide : -kPieceSide));
            piece.pos.y.whole = wrap16(piece.pos.y.whole - kPieceUp);
            piece.pos.z.whole = wrap16(piece.pos.z.whole - kPieceBack);
            const int16_t yaw = actor.rotation.y;
            piece.rotation.z = 0;
            piece.rotation.x = 0;
            piece.rotation.y = int16_t((uint32_t(yaw - kHalfTurn)) & kAngleMask);
        }
        dropper.drop = 0;
    }
}

void boneRiderUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& actor = staticActorAt(address);
    actor.savePreviousPose();
    if (StaticBindState(actor.state) == StaticBindState::Bind) {
        if (!bindActorModel(actor, 0)) {
            freeActor(actor);
            return;
        }
        actor.collisionBox.pointAt(riderBox.stateAddress());
        actor.airborne = 0;
        actor.rotation.z = 0;
        actor.rotation.x = 0;
        actor.velocity.z = 0;
        actor.velocity.y = 0;
        actor.velocity.x = 0;
        actor.enterState(uint8_t(StaticBindState::Run));
    }
    const uint32_t parent = actorBySlot(uint32_t(int32_t(RiderFields(actor).parent)));
    if (!(actorHeaderAt(parent).flags & kActorLive)) {
        freeActor(actor);
        return;
    }
    const BoneBlock* bones = boneBlockOf(animatedActorAt(parent), 0); // exe 0x5e1780
    if (!bones) notPorted(kCrashArea, "exe 0x5e1780 returned null (the parent has no bone block)");
    actor.copyRootWordsFrom(*bones, 0); // the record's copy in the rider: its root matrix and the bytes after it
    const uint8_t sh = stage().coordinateShift;
    const int32_t scale = int32_t(0x1000u << ((3u - sh) & 0x1f));
    Matrix3 m = actor.rootMatrix;
    for (int i = 0; i < 9; ++i) m.e[i] = int16_t((int32_t(m.e[i]) * scale) >> 12);
    actor.rootMatrix = m;
    const auto& point = bones->points[0];
    actor.pos.x.whole = int16_t(point.x >> (sh & 0x1f));
    actor.pos.y.whole = int16_t(point.y >> (sh & 0x1f));
    actor.pos.z.whole = int16_t(point.z >> (sh & 0x1f));
}

// the flash's step: its ticks left (kFlashOver: freed)
constexpr uint8_t kFlashOver = 0;

void crashFlashUpdate(const PlayerEnv&, uint32_t address) {
    EffectActor& flash = effectActorAt(address);
    auto&& fields = classState<CrashFlashFields>(flash);
    switch (CrashFlashState(flash.state)) {
    case CrashFlashState::Start:
        fields.shadeA = 0x7f;
        fields.shadeB = 0x7f;
        fields.shadeC = 0x7f;
        flash.runFlags |= kActorWarpKept;
        if (flash.variant == 0) {
            for (uint32_t i = 0; i < fields.rays.size(); ++i) {
                rngStep();
                const uint32_t r = rngStep();
                fields.rays[i] = uint8_t((r & 0x3f) | ((r << 6) & 0xff));
            }
            soundPlayAtPosS16(kFlashSound, flash.pos);
            flash.step = kFlashTicks;
            flash.state = uint8_t(CrashFlashState::Rays);
            return;
        }
        flash.step = kFlashTicks;
        flash.state = uint8_t(CrashFlashState::Glow);
        return;
    case CrashFlashState::Rays:
    case CrashFlashState::Glow:
        if (flash.step != kFlashOver) {
            --flash.step;
            return;
        }
        freeActor(flash); // exe 0x5e1580
        return;
    default: notPortedAt(kCrashArea, "0x431120 state", flash.state);
    }
}

void fallingPieceUpdate(const PlayerEnv&, uint32_t address) {
    ProjectileActor& piece = projectileActorAt(address);
    auto&& fields = classState<FallingPieceFields>(piece);
    piece.savePreviousPose();
    piece.wasAirborne = piece.airborne;
    const RoutineId routine = pieceRoutines->idAt(piece.variant);
    if (routine != RoutineId::CrashPieceDives && routine != RoutineId::CrashPieceFalls) notPortedAt(kCrashArea, "0x5cbf00 variant", piece.variant);
    if (piece.state == uint8_t(FallingPieceState::Spawn)) {
        piece.runFlags |= kActorMeshModel;
        if (!bindActorModel(piece, 0)) {
            freeActor(piece);
            return;
        }
        piece.enterState(uint8_t(FallingPieceState::Fall));
        piece.clearClassBlock(); // exe 0x40dc40 over +0xbc..+0xdb
        if (routine == RoutineId::CrashPieceDives) {
            piece.velocity.z = 0;
            piece.velocity.y = 0;
            piece.velocity.x = 0;
            piece.acceleration.z = 0;
            piece.acceleration.y = 0;
            piece.acceleration.x = 0;
        } else {
            piece.velocity.z = kPieceFallSpeed;
            piece.scale.z = kPieceScale;
            piece.scale.y = kPieceScale;
            piece.scale.x = kPieceScale;
        }
    } else if (routine == RoutineId::CrashPieceDives && piece.state == uint8_t(FallingPieceState::Fall)) {
        piece.enterState(uint8_t(FallingPieceState::Dive));
        piece.velocity.z = kPieceDiveSpeed;
        piece.acceleration.z = kPieceDiveAccel;
    }
    if (routine == RoutineId::CrashPieceDives) {
        piece.velocity.x = wrap16(piece.velocity.x + piece.acceleration.x);
        piece.velocity.z = wrap16(piece.velocity.z + piece.acceleration.z);
        piece.velocity.y = wrap16(piece.velocity.y + piece.acceleration.y);
    } else {
        piece.velocity.y = wrap16(piece.velocity.y + piece.acceleration.y);
        piece.velocity.z = wrap16(piece.velocity.z + piece.acceleration.z);
        piece.velocity.x = wrap16(piece.velocity.x + piece.acceleration.x);
    }
    integrateVelocity(piece, piece.velocity.x, piece.velocity.y, piece.velocity.z);
    ++fields.ticks;
}

} // namespace od::port
