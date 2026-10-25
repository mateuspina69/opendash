// The ice ruin's spinning blade barrier, static class 0x5ac270 of ST2D (see ice_ruin_blade_barrier.hpp), one routine
// per exe routine. Names are hypotheses from the observed behaviour.
#include "content/yosyonke/ice_ruin_blade_barrier.hpp"
#include "content/yosyonke/ice_ruin_blade_barrier_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_collision.hpp"
#include "game/actor_model.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/audio_logic.hpp"
#include "game/field_alias.hpp"
#include "game/hit_list.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/random.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace ice_ruin_blade_barrier_state;

namespace {

enum class BarrierState : uint8_t { Bind = 0, Run = 1, Leave = 2, Free = 3 };
enum class BarrierPart : uint8_t { Hub = 0, Segment = 1, Blocker = 2, Debris = 3 };
enum class SegmentSub : uint8_t { Standing = 0, Hidden = 1, FadeIn = 2 };

// The parts' parameters: a debris piece's index, a segment's place round the hub
struct BarrierParams {
    explicit BarrierParams(const StaticActor& actor) : BarrierParams(const_cast<StaticActor&>(actor)) {}
    explicit BarrierParams(StaticActor& actor) : debrisIndex(actor.params[0]), segmentIndex(actor.params[3]) {}
    FieldAlias<int8_t, uint8_t> debrisIndex;
    FieldAlias<int8_t, uint8_t> segmentIndex;
};

// the parts' models (exe data: [0] the hub, [1] a segment, [2] a damaged segment; the debris models by the table
// below); their collision boxes by part (0x48 bytes apart); the debris pieces' offsets on the hub (s16 x / y)
const DataTable<ScalarTable<uint8_t, 0x30>> kBarrierModels{TableId::IceRuinBladeBarrierModels};
const DataTable<ScalarTable<uint8_t, 8>> kDebrisModelIndex{TableId::IceRuinBladeBarrierDebrisModelIndex};
constexpr uint32_t kBarrierBoxStride = 0x48;
const DataTable<ScalarTable<int16_t, 0x10>> kDebrisOffsets{TableId::IceRuinBladeBarrierDebrisOffsets};
// a segment's two hit spheres: their distances along the blade and their radii
const DataTable<ScalarTable<int16_t, 2>> kSegmentHitDistance{TableId::IceRuinBladeBarrierSegmentHitDistance};
const DataTable<ScalarTable<int16_t, 2>> kSegmentHitRadius{TableId::IceRuinBladeBarrierSegmentHitRadius};

constexpr int16_t kSegmentHitPoints = 0x28, kSegmentDamagedBelow = 0x14;
constexpr int16_t kSegmentHiddenTicks = 0xb4, kSegmentFadeTicks = 0x10;
constexpr uint16_t kSpinStep = 0x18;
constexpr uint32_t kSegmentCount = 4, kDebrisCount = 6;
constexpr uint16_t kSegmentBreakSound = 0x251;
constexpr uint16_t kAlongZ = 0x400; // the yaw bit of a barrier across z (else across x)
constexpr uint32_t kSegmentHurtBy = 0x140000u;
constexpr int16_t kBlockerHeight = 0x5a;
constexpr int16_t kDebrisGravity = 0x40;
const Matrix3 kIdentity{{0x1000, 0, 0, 0, 0x1000, 0, 0, 0, 0x1000}};

// a part's spawn (exe 0x5e12f0): live, the parent's type and sub-type, the part's variant
StaticActor* spawnPart(const StaticActor& parent, BarrierPart part) {
    const uint32_t e = allocActor60(kActorStageClass);
    if (!e) return nullptr;
    StaticActor& child = staticActorAt(e);
    child.flags = kActorLive;
    child.type = parent.type;
    child.subtype = parent.subtype;
    child.variant = uint8_t(part);
    return &child;
}
// ... placed on the parent (position, rotation)
void copyPlace(StaticActor& child, const StaticActor& parent) {
    child.pos = parent.pos;
    child.rotation = parent.rotation;
}

// ---- 0x5ac2c0 (state 0): bind by part ----------------------------------------------------------------------------
void bind(StaticActor& actor, BarrierFields& fields) {
    actor.airborne = 0;
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.collisionBox.pointAt(stateAddress(TableId::IceRuinBladeBarrierBoxes) + uint32_t(actor.variant) * kBarrierBoxStride);
    fields.clearOwnBytes();
    switch (BarrierPart(actor.variant)) { // table 0x5ac588
    case BarrierPart::Hub: { // 0x5ac316
        if (!bindActorModel(actor, kBarrierModels[0])) { // exe 0x5e0bc0
            freeActor(actor);
            return;
        }
        actor.pos.y.whole -= 0x100u;
        actor.rotation.y &= kAlongZ;
        actor.flags |= kActorDrawn;
        if (StaticActor* blocker = spawnPart(actor, BarrierPart::Blocker)) {
            BarrierParams(*blocker).segmentIndex = 0;
            copyPlace(*blocker, actor);
            classState<BarrierFields>(*blocker).hub = &actor;
        }
        for (uint32_t k = 0; k < kSegmentCount; ++k) {
            StaticActor* segment = spawnPart(actor, BarrierPart::Segment);
            if (!segment) continue;
            BarrierParams(*segment).segmentIndex = int8_t(k);
            copyPlace(*segment, actor);
            segment->rotation.z += uint16_t(k << 10);
            classState<BarrierFields>(*segment).hub = &actor;
        }
        actor.enterState(uint8_t(BarrierState::Run));
        return;
    }
    case BarrierPart::Segment: // 0x5ac3fc
        if (!bindActorModel(actor, kBarrierModels[1])) {
            freeActor(actor);
            return;
        }
        actor.enterState(uint8_t(BarrierState::Run));
        actor.flags |= kActorDrawn;
        actor.hitPoints = kSegmentHitPoints;
        actor.maxHitPoints = kSegmentHitPoints;
        return;
    case BarrierPart::Blocker: // 0x5ac43d
        if (!bindActorModel(actor, kBarrierModels[1])) {
            freeActor(actor);
            return;
        }
        actor.enterState(uint8_t(BarrierState::Run));
        actor.setDrawn(false);
        return;
    case BarrierPart::Debris: { // 0x5ac471: a random piece, set off the hub by its offset turned by the roll
        const uint32_t r = crtRand(); // exe 0x611443
        if (!bindActorModel(actor, kBarrierModels[kDebrisModelIndex[r & 7]])) {
            freeActor(actor);
            return;
        }
        actor.flags |= kActorDrawn;
        const Matrix3 m = mulMatrix3x3(rotationZ(int(actor.rotation.z)), kIdentity); // exe 0x40bf10
        const uint32_t row = uint32_t(int32_t(BarrierParams(actor).debrisIndex.get())) * 2;
        const int16_t offset[3] = {kDebrisOffsets[row], kDebrisOffsets[row + 1], 0};
        int16_t out[3];
        transformShortVector(m, offset, out); // exe 0x40bb30
        if ((uint16_t(actor.rotation.y) & kAlongZ) == 0) {
            actor.pos.x.whole += uint16_t(out[0]);
            actor.pos.y.whole += uint16_t(out[1]);
        } else {
            actor.pos.z.whole -= uint16_t(out[0]);
            actor.pos.y.whole += uint16_t(out[1]);
        }
        const uint32_t r2 = crtRand();
        actor.velocity.y = 0;
        actor.rotation.z = int16_t((r2 & 0xff) << 4);
        actor.enterState(uint8_t(BarrierState::Run));
        return;
    }
    default: // 0x5ac572
        actor.flags |= kActorDrawn;
        actor.enterState(uint8_t(BarrierState::Run));
        return;
    }
}

// the integer parts of the actor's position as an s16 point
void wholePoint(const StaticActor& actor, int16_t p[3]) {
    p[0] = actor.pos.x.whole;
    p[1] = actor.pos.y.whole;
    p[2] = actor.pos.z.whole;
}
// ---- 0x5ac920(actor, x, y): the blade's capsule -- both ends at (+-x, +-y, 0) turned by the yaw ---------------------
void bladeCapsule(const StaticActor& actor, BarrierFields& fields, int x, int y) {
    int16_t p1[3], p2[3];
    wholePoint(actor, p1);
    moveAlongYaw16(p1, actor.rotation.y, x, y, 0); // exe 0x5e3750
    wholePoint(actor, p2);
    moveAlongYaw16(p2, actor.rotation.y, -x, -y, 0);
    hitListSubmitCapsule(p1, 8, p2, 8, fields.hit, (uint32_t(actor.slot) << 24) | 0x483000u,
                         0x200008u); // exe 0x5e4190
}
// ---- 0x5ac9d0: a segment's two hit spheres along its roll - a quarter turn -----------------------------------------
void segmentHits(const StaticActor& actor, BarrierFields& fields) {
    const int ang = int((uint32_t(int32_t(actor.rotation.z)) - 0x400u) & 0xfff);
    for (uint32_t i = 0; i < 2; ++i) {
        int16_t p[3];
        wholePoint(actor, p);
        const int32_t d = kSegmentHitDistance[i];
        const int32_t y = (int32_t(fixedSin(ang)) * d) >> 8;
        const int32_t x = (int32_t(fixedCos(ang)) * d) >> 8;
        moveAlongYaw16(p, actor.rotation.y, x, y, 0); // exe 0x5e3750
        const int16_t r = int16_t(kSegmentHitRadius[i] >> 2);
        const uint32_t shape = uint32_t(int32_t(r)) | (uint32_t(actor.slot) << 24) | 0x480000u;
        hitListSubmit(p, fields.hit, shape, 0xa0008u); // exe 0x5e4120
    }
}

// 0x5ac6b3: a segment standing -- its hits taken (the damaged model; broken: hidden, its debris), its hit spheres
void segmentStanding(StaticActor& actor, BarrierFields& fields, StaticActor& hub, uint8_t bit) {
    const uint32_t hit = fields.hit.result;
    if (hit & kSegmentHurtBy) {
        actor.hitPoints -= hit & 0xfff;
        if (actor.step == 0 && actor.hitPoints < kSegmentDamagedBelow) {
            actor.step = 1;
            bindActorModel(actor, kBarrierModels[2]); // exe 0x5e0bc0 (the damaged model)
        }
        if (actor.hitPoints < 0) {
            actor.flags ^= kActorDrawn;
            ++actor.subState;
            actor.step = 0;
            fields.ticks = kSegmentHiddenTicks;
            soundPlayAtPos(kSegmentBreakSound, actor.pos); // exe 0x5d5380
            for (uint32_t k = 0; k < kDebrisCount; ++k) {
                StaticActor* debris = spawnPart(actor, BarrierPart::Debris);
                if (!debris) continue;
                BarrierParams(*debris).debrisIndex = int8_t(k);
                copyPlace(*debris, actor);
            }
        }
    }
    fields.hit.result = 0;
    fields.hit.previousX = actor.prevPos.x.whole;
    fields.hit.previousZ = actor.prevPos.z.whole;
    segmentHits(actor, fields); // 0x5ac9d0
    classState<BarrierFields>(hub).standingSegments |= bit;
}
// 0x5ac613: a segment spins with the hub; standing, hidden for a while, then faded back in
void segment(StaticActor& actor, BarrierFields& fields) {
    const uint8_t bit = uint8_t(1u << (int32_t(BarrierParams(actor).segmentIndex.get()) & 0x1f));
    actor.rotation.z += kSpinStep;
    StaticActor& hub = *fields.hub;
    classState<BarrierFields>(hub).standingSegments &= uint8_t(~bit);
    switch (SegmentSub(actor.subState)) {
    case SegmentSub::Standing: segmentStanding(actor, fields, hub, bit); return;
    case SegmentSub::Hidden: // 0x5ac68e
        --fields.ticks;
        if (fields.ticks != 0) return;
        actor.flags ^= kActorDrawn;
        ++actor.subState;
        actor.tint = 0;
        return;
    case SegmentSub::FadeIn: { // 0x5ac650: back over 16 ticks
        ++fields.ticks;
        const uint32_t t = uint32_t(int32_t(fields.ticks));
        if (t == uint32_t(kSegmentFadeTicks)) {
            actor.hitPoints = actor.maxHitPoints;
            actor.subState = uint8_t(SegmentSub::Standing);
            bindActorModel(actor, kBarrierModels[1]); // exe 0x5e0bc0
        }
        actor.tint = uint16_t((((t << 5) | t) << 5) | t);
        return;
    }
    default: return;
    }
}
// 0x5ac7a7: the blocker follows the player while the hub's quadrant toward them holds a segment
void blocker(StaticActor& actor) {
    const StaticActor& hub = *classState<BarrierFields>(actor).hub;
    const Player& p = player();
    const uint16_t py = uint16_t(uint16_t(p.pos.y.whole) - kBlockerHeight);
    if (uint16_t(uint16_t(hub.pos.y.whole) - py + 0x100u) > 0x200) return;
    uint16_t ax, bx; // the pairs A = (ax, hub y), B = (bx, player y - 0x5a)
    if ((uint16_t(hub.rotation.y) & kAlongZ) == 0) {
        ax = uint16_t(p.pos.x.whole);
        bx = uint16_t(hub.pos.x.whole);
    } else {
        ax = uint16_t(hub.pos.z.whole);
        bx = uint16_t(p.pos.z.whole);
    }
    if (uint16_t(ax - bx + 0x100u) > 0x200) return;
    // exe 0x5e3c50(B, A): exe 0x40b960(A.x - B.x, A.y - B.y) & 0xfff
    const int32_t ang = atanAngle(float(int32_t(int16_t(ax)) - int32_t(int16_t(bx))),
                                  float(int32_t(hub.pos.y.whole) - int32_t(int16_t(py)))) &
                        0xfff;
    const uint32_t q = ((uint32_t(ang) - uint16_t(hub.rotation.z) + 0x200u) >> 10) & 3;
    if ((int32_t(int8_t(classState<BarrierFields>(hub).standingSegments)) & int32_t(1u << q)) == 0) return;
    if ((uint16_t(actor.rotation.y) & kAlongZ) == 0) actor.pos.x.whole = p.pos.x.whole;
    else actor.pos.z.whole = p.pos.z.whole;
    actor.pos.y.whole = p.pos.y.whole;
    actorCollisionSubmitSolid(actor, SolidType::PushOut, 0, 0); // exe 0x414050(a, 0, 0, 0)
}

// ---- 0x5ac5a0 (state 1): run by part (table 0x5ac908) ------------------------------------------------------------
void run(const PlayerEnv& env, StaticActor& actor, BarrierFields& fields) {
    switch (BarrierPart(actor.variant)) {
    case BarrierPart::Hub: { // 0x5ac5bd: the hub spins; capsules on both blade axes
        actor.rotation.z += kSpinStep;
        const int ang = int((uint32_t(int32_t(actor.rotation.z)) + 0x200u) & 0xfff);
        const int32_t c = (int32_t(fixedCos(ang)) * 240) >> 8;
        const int32_t s = (int32_t(fixedSin(ang)) * 240) >> 8;
        bladeCapsule(actor, fields, c, s);
        bladeCapsule(actor, fields, s, -c);
        return;
    }
    case BarrierPart::Segment: segment(actor, fields); return;
    case BarrierPart::Blocker: blocker(actor); return;
    case BarrierPart::Debris: { // 0x5ac8ce: falls until the move test fails
        const int32_t v = int32_t(actor.velocity.y) + kDebrisGravity;
        actor.velocity.y = int16_t(v);
        actor.pos.y += uint32_t(v) << 12;
        if (int32_t(actorTestMove(env, actor)) < 0) { // exe 0x40f570
            actor.setDrawn(false);
            ++actor.state;
        }
        return;
    }
    default: return;
    }
}

void bladeBarrier(const PlayerEnv& env, StaticActor& actor) { // 0x5ac270
    BarrierFields& fields = classState<BarrierFields>(actor);
    actor.savePreviousPose();
    actor.savePreviousContact();
    switch (BarrierState(actor.state)) { // table 0x8cf610
    case BarrierState::Bind: bind(actor, fields); return;
    case BarrierState::Run: run(env, actor, fields); return;
    case BarrierState::Leave: actor.enterState(uint8_t(BarrierState::Free)); return; // 0x5aca90
    case BarrierState::Free: freeActor(actor); return;                              // 0x5afdd0 -> exe 0x5e1540
    default: notPortedAt(kStoryPort, "0x5ac270 state (table 0x8cf610 runs into other data)", actor.state);
    }
}

} // namespace

void bladeBarrierUpdate(const PlayerEnv& env, uint32_t actor) { bladeBarrier(env, staticActorAt(actor)); }

} // namespace od::port
