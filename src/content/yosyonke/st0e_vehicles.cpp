// ST0E: what its classes share (see st0e_vehicles.hpp).
#include "content/yosyonke/st0e_vehicles.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/explosions.hpp"
#include "game/global_class_types.hpp"
#include "game/player/player_state.hpp"
#include "game/random.hpp"
#include "game/port_gap.hpp"
#include "game/room_scripts.hpp"
#include "game/stage_state.hpp"
#include "game/vectors.hpp"

namespace od::port {

namespace {
int16_t t16(int64_t v) { return int16_t(uint16_t(uint64_t(v))); }
constexpr size_t kBossRecord = 0, kDefendedRecord = 1;
} // namespace

AnimatedActor& bossVehicle() {
    const uint32_t b = roomScriptState().actors[kBossRecord];
    if (!b) notPorted(kSt0ePort, "ST0E: the boss vehicle 0xa714e4 is null (the exe reads its fields)");
    return animatedActorAt(b);
}

StaticActor& defendedVehicle() {
    const uint32_t d = roomScriptState().actors[kDefendedRecord];
    if (!d) notPorted(kSt0ePort, "ST0E: the defended vehicle 0xa714e8 is null (the exe reads its fields)");
    return staticActorAt(d);
}

uint32_t defendedVehicleAddress() { return roomScriptState().actors[kDefendedRecord]; }


BonePoint bonePointAt(const AnimatedActor& actor, uint32_t bone, int x, int y, int z) {
    const int16_t in[3] = {t16(x), t16(y), t16(z)};
    BonePoint o{};
    bonePointOffset(actor, bone, in, o.v);
    return o;
}

void fixedOfPoint(const BonePoint& p, int32_t out[3]) {
    for (int k = 0; k < 3; ++k) out[k] = int32_t(uint32_t(int32_t(p.v[k])) << 16);
}

bool facesAwayFrom(const Actor& actor, const int32_t point[3], int r) {
    const int ang = angleOf(actor.pos.x, actor.pos.z, point[0], point[2]);
    return int((((uint32_t(uint16_t(ang - actor.rotation.y))) - uint32_t(kHalfTurn)) + uint32_t(r)) & kAngleMask) <=
           int(t16(r)) * 2;
}

bool facesAwayFrom(const Actor& actor, const Fixed16Vec3& point, int r) {
    const int32_t p[3] = {point.x, point.y, point.z};
    return facesAwayFrom(actor, p, r);
}

void vehicleVoice(uint16_t id) { soundPlayPanned(id, 0, 0); }

void flashBone(AnimatedActor& actor, uint32_t bone) {
    actor.tint = 0;
    BoneBlock* b = boneBlockOf(actor, int(bone >> 2));
    if (!b) notPortedAt(kSt0ePort, "exe 0x5e1810 returned null, bone", bone);
    b->colour(bone & 3) = kTintFlash;
}

int angleFromPlayer(const BonePoint& p) {
    return angleOf(player().pos.x, player().pos.z, int32_t(uint32_t(int32_t(p.v[0])) << 16),
                   int32_t(uint32_t(int32_t(p.v[2])) << 16));
}

void aimStep(const Actor& actor, int16_t& aim, int angle) {
    const int16_t d = t16(angleDiff(uint32_t(int(actor.rotation.y) + aim), uint32_t(angle)));
    if (uint32_t(int(d) + 8) >= 0x11) aim = int16_t(uint16_t(uint32_t(aim + (d >= 9 ? 8 : -8))));
    else aim = int16_t(uint16_t(uint32_t(aim + d)));
}

int sparkJitter(uint32_t r, bool wide) {
    return wide ? (int32_t((r & 0x1ff) * 0x180) - 0x18000) >> 8 : (int32_t((r & 0xff) * 0x180) - 0xc000) >> 8;
}

void spawnBoneSpark(AnimatedActor& actor, uint32_t bone, int x, int y, int z, bool wide) {
    const uint32_t at = allocActorA0(0); // exe 0x5e13b0
    if (!at) return;
    EffectActor& e = effectActorAt(at);
    e.flags = kActorLive;
    e.type = typeByte(GlobalEffectType::HitSpark);
    e.subtype = 0;
    e.variant = 0;
    e.runFlags |= kActorWarpKept;
    const BonePoint p = bonePointAt(actor, bone, x, y, z);
    e.pos.x = t16(sparkJitter(rngStep(), wide) + p.v[0]);
    e.pos.y = t16(sparkJitter(rngStep(), wide) + p.v[1]);
    e.pos.z = t16(sparkJitter(rngStep(), wide) + p.v[2]);
    e.params[0] = uint8_t((rngStep() & 0x7f) + 0x40);
}

StaticActor* spawnFireEmitter(Actor& owner, uint8_t variant, uint8_t mode, uint32_t x, uint32_t y, uint32_t z,
                              uint16_t lifetime) {
    const uint32_t at = allocActor60(kActorStageClass); // exe 0x5e12f0
    if (!at) return nullptr;
    StaticActor& e = staticActorAt(at);
    FireEmitterParams f(e);
    e.flags = kActorLive;
    e.type = kFireEmitterType;
    e.subtype = 0;
    e.variant = variant;
    e.spawnByte = mode;
    e.pos.x = int32_t(x);
    e.pos.y = int32_t(y);
    e.pos.z = int32_t(z);
    f.paramA = 0;
    f.paramB = 0;
    f.lifetime = lifetime;
    f.owner = owner.at;
    return &e;
}

void spawnBoneBlast(AnimatedActor& actor, uint32_t bone, int x, int y, int z, uint16_t size) {
    const uint32_t at = allocActor60(0); // exe 0x5e1300
    if (!at) return;
    StaticActor& e = staticActorAt(at);
    e.flags = kActorLive;
    e.loopFlags |= kActorSweepGroup2;
    e.type = typeByte(GlobalStaticType::Blast);
    e.subtype = 0;
    e.variant = 0;
    const BonePoint p = bonePointAt(actor, bone, x, y, z);
    e.pos.x.whole = p.v[0];
    e.pos.y.whole = p.v[1];
    e.pos.z.whole = p.v[2];
    BlastParams b(e);
    b.size = size;
    b.attack = 0;
}

ControllerActor& spawnHitWatcher(Actor& parent) {
    const uint32_t at = allocActorE0(); // exe 0x5e1440 (the exe writes through a null result)
    if (!at) notPorted(kSt0ePort, "ST0E boss: exe 0x5e1440 returned null (watcher)");
    ControllerActor& e = controllerActorAt(at);
    e.flags = kActorLive;
    e.type = 0;
    e.loopFlags |= kActorLateLoop;
    e.runFlags |= kActorWarpKept;
    e.setParamsWord(parent.at);
    return e;
}

} // namespace od::port
