// The extinguisher (weapon 15, pool 0x10 type 14, exe 0x5705a0): a stream of water from the hand while the special
// button is held and the energy lasts. The shot spawns a partner slot of its class (its parameter 1) that holds the
// stream's lagging aim and its hit boxes; the stream's points bend from the nozzle (each point along the aim the point
// before it had a tick earlier), stop at a wall or a solid body (a spark there) and are hit entries carrying the water
// flag. Three mist statics (static global type 0x16, exe 0x56a690) spray particles around the nozzle while it runs.
// Under water the shot only releases bubbles (the stage's static type 0x48) while the energy lasts.
//
// The stream's quads and the mist's sprites are draw output the port does not write.
#include "game/actor_state.hpp"
#include "game/shots/extinguisher_state.hpp"
#include "game/shots/extinguisher.hpp"

#include "engine/data_source.hpp"
#include "game/global_class_types.hpp"
#include "game/shots/player_shots.hpp"

#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/fixed_point.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/player/player_motion.hpp"
#include "game/player/player_state.hpp"
#include "game/player/player_weapons.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/shots/shot_common.hpp"
#include "game/stage_state.hpp"

namespace od::port {

using namespace extinguisher_state;

namespace {

inline constexpr const char* kExtinguisherArea = "extinguisher";

// The partner's fields: the nozzle (the hand >> the stage shift, 4 lanes), the pitch / yaw each point had last tick,
// the points' hit boxes


constexpr uint8_t kBubblesType = 0x48; // a stage table type (per class: game/global_class_types.hpp)
constexpr uint8_t kStreamSparkVariant = 9;
const DataTable<CollisionBoxRecord> extinguisherBox{TableId::ExtinguisherBox};
constexpr uint16_t kSpraySound = 0x10d, kStopSound = 0x10e;
constexpr uint16_t kSoundTicks = 0x14;  // the spray's sound repeats
constexpr int kNozzleReach = -0x120;    // point 0: this far along the aim from the nozzle
constexpr int16_t kStreamDrop = -0x40;  // the stream's drop (>> 4 a point), growing by 4 a point
constexpr int16_t kStreamStep = -0x120; // the step between points, growing by 0x10 a point
constexpr int kMists = 3;

// the hand >> the stage shift into the partner's nozzle
void nozzleToPartner(StreamPartnerFields& partner) {
    const auto& hand = player().bonePoints[kLeftHandBone];
    const int sh = stage().coordinateShift & 0x1f;
    partner.nozzle[0] = int16_t(hand.x >> sh);
    partner.nozzle[1] = int16_t(hand.y >> sh);
    partner.nozzle[2] = int16_t(hand.z >> sh);
}

// the special button held with the arm up: the gauge pays the shot; 1 = it still holds one, 0 = out (disarmed), -1 =
// not held
int sprayEnergy() {
    const Player& p = player();
    if (!p.holds(PlayerButton::Special) || !(p.armFlags & kArmSpecial)) return -1;
    paySpecialEnergy(player());
    return p.shotCost <= p.gauge ? 1 : 0;
}

// exe 0x570a10(shot, stats, nozzle): the stream's points, from the nozzle, its spark where it stops and its hit entries
void stream(const PlayerEnv& env, ShotActor& shot, ExtinguisherFields& fields, StreamPartnerFields& partner) {
    const Player& p = player();
    if (fields.pointCount < kStreamPoints) ++fields.pointCount;
    const auto& hand = p.bonePoints[kLeftHandBone];
    const int16_t nozzle[3] = {hand.x, hand.y, hand.z};
    const uint32_t mask = collisionLayerMask(shot.slot);
    if (fields.pointCount == 0) return;
    int16_t pos[4] = {0, 0, 0, 0};
    int16_t rot[3] = {0, 0, 0}; // the roll stays 0
    for (uint32_t i = 0;;) {
        auto&& point = fields.points[i];
        if (i == 0) {
            for (uint32_t k = 0; k < 4; ++k) pos[k] = partner.nozzle[k];
            rot[0] = p.rotation.x;
            rot[1] = p.rotation.y;
            shot.velocity.y = kStreamDrop;
            shot.velocity.z = kStreamStep;
            moveAlongRotation16(pos, rot, 0, 0, kNozzleReach);
            pos[1] = wrap16(pos[1] + (shot.velocity.y >> 4));
            for (uint32_t k = 0; k < 4; ++k) point[k] = pos[k];
            partner.pitches[0] = rot[0];
            partner.yaws[0] = rot[1];
        } else {
            const int16_t lastPitch = partner.pitches[i], lastYaw = partner.yaws[i];
            shot.velocity.z = wrap16(shot.velocity.z - int(i << 4));
            shot.velocity.y = wrap16(shot.velocity.y + int(i * 4));
            moveAlongRotation16(pos, rot, 0, 0, shot.velocity.z);
            pos[1] = wrap16(pos[1] + (shot.velocity.y >> 4));
            for (uint32_t k = 0; k < 4; ++k) point[k] = pos[k];
            partner.pitches[i] = rot[0];
            partner.yaws[i] = rot[1];
            rot[0] = lastPitch;
            rot[1] = lastYaw;
            const uint32_t hit = partner.hitBoxes[i].result & (kHitAttackSolid | kHitAttackDeflects);
            const int16_t at[3] = {point[0], point[1], point[2]};
            if (hit != 0 || int32_t(testMoveBlocked(env, at, nozzle, shot.collisionBox, mask)) < 0) {
                if (const uint32_t e = allocActorA0(0)) { // the spark where it stops
                    EffectActor& spark = effectActorAt(e);
                    spark.flags = kActorLive | kActorDrawn;
                    spark.type = typeByte(GlobalEffectType::HitSpark);
                    spark.variant = kStreamSparkVariant;
                    int16_t at2[3];
                    for (uint32_t k = 0; k < 3; ++k) {
                        const uint32_t r = rngStep();
                        const uint32_t d = (uint32_t(int32_t(r & 0x3f) - 0x20) * (i + 1)) >> 3;
                        at2[k] = int16_t(uint16_t(d + uint32_t(uint16_t(point[k]))));
                    }
                    spark.pos.x = at2[0];
                    spark.pos.y = at2[1];
                    spark.pos.z = at2[2];
                    classState<HitSparkFields>(spark).size = uint16_t((i * 3 + 6) * 2);
                }
                fields.pointCount = uint16_t(hit ? i : i - 1);
                for (uint32_t k = i; k < kStreamPoints; ++k) partner.hitBoxes[k].result = 0;
                return;
            }
        }
        if (i >= fields.tail) { // from the tail on: a hit entry (radius (i * 3 + 6) * 2 / 4)
            auto&& box = partner.hitBoxes[i];
            hitListSubmit(pos, box, hitOwner(shot.slot) | hitGroup(kHitGroupPlayerAttacks) | hitRadius((i * 3 + 6) * 2),
                          fields.stats.attack | kHitAttackWater);
            box.result = 0;
            box.previousX = hand.x;
            box.previousZ = hand.z;
        }
        if (++i >= fields.pointCount) return;
    }
}

void spawn(const PlayerEnv& env, ShotActor& shot, ExtinguisherFields& fields) { // exe 0x5705d0
    Player& p = player();
    shot.flags &= uint8_t(~kActorDrawn);
    shot.runFlags |= kActorWarpKept;
    const uint32_t s = allocActor10();
    if (s == 0) return;
    ShotActor& partnerSlot = shotActorAt(s);
    partnerSlot.flags = kActorLive;
    partnerSlot.type = typeByte(GlobalShotType::Extinguisher);
    partnerSlot.loopFlags |= kActorSweepGroup1;
    partnerSlot.params[0] = 1;
    fields.partner = &partnerSlot;
    StreamPartnerFields& partner = classState<StreamPartnerFields>(partnerSlot);
    for (uint32_t k = 0; k < kStreamPoints; ++k) partner.hitBoxes[k].result = 0;
    shot.pos = {}; // the position's bytes and the roll's cleared as the exe's spawn does (byte stores)
    shot.rotation.x = p.rotation.x;
    shot.rotation.y = p.rotation.y;
    shot.rotation.z = 0;
    fillShotStats(fields.stats);
    fields.statsPad = 0;
    shot.collisionBox.pointAt(extinguisherBox.stateAddress());
    fields.pointCount = 0;
    fields.tail = 0;
    fields.soundTicks = kSoundTicks;
    p.specialFiring = 1;
    if (p.inWater != 0) { // under water: bubbles
        shot.state = uint8_t(shot.state + 3);
        return;
    }
    for (int k = 0; k < kMists; ++k) { // exe 0x5e1300
        const uint32_t m = allocActor60(0);
        if (m == 0) continue;
        StaticActor& mist = staticActorAt(m);
        mist.flags = kActorLive;
        mist.type = typeByte(GlobalStaticType::ExtinguisherMist);
        mist.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
        mist.velocity = shot.velocity;
    }
    nozzleToPartner(partner);
    stream(env, shot, fields, partner);
    soundPlayAtPlayer(kSpraySound);
    ++shot.state;
}

void spray(const PlayerEnv& env, ShotActor& shot, ExtinguisherFields& fields) { // exe 0x570770
    shot.pos.x = 0;
    shot.pos.y = 0;
    shot.pos.z = 0;
    StreamPartnerFields& partner = classState<StreamPartnerFields>(*fields.partner);
    if (sprayEnergy() == 1) {
        nozzleToPartner(partner);
        player().specialFiring = 1;
        stream(env, shot, fields, partner);
        --fields.soundTicks;
        if (fields.soundTicks != 0) return;
        soundPlayAtPlayer(kSpraySound);
        fields.soundTicks = kSoundTicks;
        return;
    }
    stream(env, shot, fields, partner);
    soundPlayAtPlayer(kStopSound);
    ++shot.state;
}

void runOut(const PlayerEnv& env, ShotActor& shot, ExtinguisherFields& fields) { // exe 0x570870
    const uint16_t tail = fields.tail;
    fields.tail = uint16_t(tail + 1);
    ShotActor& s = *fields.partner;
    if (fields.pointCount <= tail) {
        freeActor(shot);
        freeActor(s);
        return;
    }
    StreamPartnerFields& partner = classState<StreamPartnerFields>(s);
    if (int32_t(shot.pos.x) != 0 || int32_t(shot.pos.z) != 0) { // the stream carried by the player's move
        partner.nozzle[0] = wrap16(partner.nozzle[0] + shot.pos.x.whole);
        partner.nozzle[1] = wrap16(partner.nozzle[1] + shot.pos.y.whole);
        partner.nozzle[2] = wrap16(partner.nozzle[2] + shot.pos.z.whole);
        shot.pos.x = 0;
        shot.pos.y = 0;
        shot.pos.z = 0;
    }
    stream(env, shot, fields, partner);
}

// The bubbles' spawn parameters (the stage class's: its size, its rise, its life)
struct BubbleParams {
    explicit BubbleParams(ActorHeader& actor) : size(actor.params[0]), life(actor.params[1]), rise(actor.params[2]) {}
    uint8_t& size; // +0xc
    uint8_t& life; // +0xd
    uint8_t& rise; // +0xe
};
constexpr int16_t kBubbleSpeed = 0x200;

void bubbles(ShotActor& shot, ExtinguisherFields& fields) { // exe 0x5708f0
    Player& p = player();
    if (sprayEnergy() == 1) {
        p.specialFiring = 1;
        const uint32_t m = allocActor60(kActorStageClass); // exe 0x5e12f0
        if (m == 0) return;
        const int sh = (16 - int(stage().coordinateShift)) & 0x1f;
        const auto& hand = p.bonePoints[kLeftHandBone];
        StaticActor& bubble = staticActorAt(m);
        bubble.flags = kActorLive;
        bubble.type = kBubblesType;
        bubble.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
        bubble.variant = 0;
        bubble.pos.x = int32_t(uint32_t(int32_t(hand.x)) << sh);
        bubble.pos.y = int32_t(uint32_t(int32_t(hand.y)) << sh);
        bubble.pos.z = int32_t(uint32_t(int32_t(hand.z)) << sh);
        bubble.rotation = p.rotation;
        bubble.velocity.z = kBubbleSpeed;
        BubbleParams params(actorHeaderAt(m));
        params.size = 7;
        const uint32_t r = rngStep();
        params.rise = 8;
        params.life = uint8_t((r & 7) + 0x14);
        return;
    }
    freeActor(shot);
    freeActor(*fields.partner);
}

// ---- the mist ----------------------------------------------------------------------------------------------------
// The mist's block (+0x28): its particle count (up to 10) and the ones still alive, each particle's s16 point and yaw,
// and its speed (x / y / z) and ticks
// [0x8c488c]
enum class MistState : uint8_t { Start = 0, Emit = 1, Settle = 2 }; // exe 0x56a6b0 / 0x56a700 / 0x56a730
constexpr int16_t kMistGravity = 0x40;
constexpr int16_t kMistLife = 10;

// exe 0x56a760(mist, block, stop): each particle follows the mist's move, falls along its yaw, and (while emitting) is
// reborn at the nozzle when its ticks run out, spread around the player's aim and carried by the player's (or its
// platform's) last move
void mistParticles(StaticActor& mist, MistFields& fields, bool stop) {
    const Player& p = player();
    if (!stop) {
        if (fields.count < kMistParticles) fields.count = int16_t(fields.count + 1);
        fields.alive = uint16_t(fields.count);
    }
    for (int k = 0; k < fields.count; ++k) {
        auto&& particle = fields.particles[size_t(k)];
        auto&& motion = fields.motions[size_t(k)];
        if (int32_t(mist.pos.x) != 0 || int32_t(mist.pos.z) != 0) {
            particle.point.x = wrap16(particle.point.x + mist.pos.x.whole);
            particle.point.y = wrap16(particle.point.y + mist.pos.y.whole);
            particle.point.z = wrap16(particle.point.z + mist.pos.z.whole);
        }
        const int16_t t = wrap16(motion.ticks - 1);
        motion.ticks = t;
        if (t > 0) {
            motion.vy = wrap16(motion.vy + kMistGravity);
            moveAlongYaw16(particle.point, particle.yaw, motion.vx, motion.vy, motion.vz);
            continue;
        }
        if (stop) {
            if (t == 0) fields.alive = uint16_t(fields.alive - 1);
            continue;
        }
        const int d = int((rngStep() >> 16) & 0x1ff) - 0x100;
        int32_t aim[3];
        playerAimVector(0, -0x100, -0x500, aim); // exe 0x5e3e50
        const auto& hand = p.bonePoints[kLeftHandBone];
        particle.point.x = hand.x;
        particle.point.y = hand.y;
        particle.point.z = hand.z;
        const uint16_t yaw = uint16_t(uint16_t(p.rotation.y) + d);
        particle.yaw = int16_t(yaw);
        const Actor& carrier =
            p.onPlatform ? static_cast<const Actor&>(*p.platform) : static_cast<const Actor&>(p);
        const int32_t dz = int32_t(uint32_t(carrier.pos.z) - uint32_t(carrier.prevPos.z)) >> 12;
        const int32_t dx = int32_t(uint32_t(carrier.pos.x) - uint32_t(carrier.prevPos.x)) >> 12;
        const int sn = fixedSin(yaw), cs = fixedCos(yaw);
        const int32_t vx = aim[0] + ((((cs * dx) >> 12) - ((sn * dz) >> 12)) / 2);
        const int32_t vz = aim[2] + ((((sn * dx) >> 12) - ((cs * dz) >> 12)) / 2);
        motion.vx = wrap16(vx);
        const int half = d < 0 ? -(d >> 1) : (d >> 1);
        const uint32_t r = rngStep();
        motion.vy = wrap16(int32_t((r >> 8) & 0xff) + half + aim[1] - 0x80);
        motion.vz = wrap16(vz);
        motion.ticks = kMistLife;
        moveAlongYaw16(particle.point, particle.yaw, motion.vx, motion.vy, motion.vz);
    }
    mist.pos.x = 0;
    mist.pos.y = 0;
    mist.pos.z = 0;
}

} // namespace

void extinguisherShotUpdate(const PlayerEnv& env, uint32_t address) {
    ShotActor& shot = shotActorAt(address);
    if (ExtinguisherState(shot.state) == ExtinguisherState::Spawn && isPartnerSlot(shot)) { // the partner itself
        shot.flags &= uint8_t(~kActorDrawn);
        shot.runFlags |= kActorWarpKept;
        return;
    }
    ExtinguisherFields& fields = classState<ExtinguisherFields>(shot);
    switch (ExtinguisherState(shot.state)) {
    case ExtinguisherState::Spawn: spawn(env, shot, fields); return;
    case ExtinguisherState::Spray: spray(env, shot, fields); return;
    case ExtinguisherState::RunOut: runOut(env, shot, fields); return;
    case ExtinguisherState::Bubbles: bubbles(shot, fields); return;
    default: notPortedAt(kExtinguisherArea, "0x5705a0 state", shot.state);
    }
}

void extinguisherMistUpdate(const PlayerEnv&, uint32_t address) {
    StaticActor& mist = staticActorAt(address);
    MistFields& fields = classState<MistFields>(mist);
    switch (MistState(mist.state)) {
    case MistState::Start:
        mist.runFlags |= kActorWarpKept;
        mist.pos.x = 0;
        mist.pos.y = 0;
        mist.pos.z = 0;
        for (uint32_t k = 0; k < kMistParticles; ++k) fields.motions[k].ticks = 1;
        fields.count = 0;
        fields.alive = 0;
        mistParticles(mist, fields, false);
        ++mist.state;
        return;
    case MistState::Emit: // while the stream sprays
        if (player().specialFiring != 0) {
            mistParticles(mist, fields, false);
            return;
        }
        ++mist.state;
        return;
    case MistState::Settle: // the last particles fall, then freed
        if (int16_t(fields.alive) != 0) {
            mistParticles(mist, fields, true);
            return;
        }
        freeActor(mist);
        return;
    default: notPortedAt(kExtinguisherArea, "0x56a690 state", mist.state);
    }
}

} // namespace od::port
