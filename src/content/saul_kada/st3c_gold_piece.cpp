// The gold-statue boss's gold pieces (ST3C static type 0x53, exe 0x5b8440; hypothesis names). Per sub-type state
// tables [0x8d0b68 + sub * 4]; the exe keeps the running piece's block (+0xbc) in the global 0xa5a92c.
//  sub 0, the thrown gold (the boss throws it): 0 start 0x5b84a0 (its model, its box, random spins), 1 flight 0x5b8540
//  (spinning, its gravity, its forward speed along its yaw; a wall turns it off the wall with a hit spark, the floor
//  ends it; a hit of the player's / a body's ends it), its hit entry 0x5b8770 every tick; 2 end 0x5b8700 (a hit spark,
//  freed).
//  sub 1, the electric ring (a thrown piece of content/saul_kada/st3c_boss_shot moves it, copies its matrix and sets
//  its size): 0 0x5b8690 hidden, a random phase; 1 0x5b86e0 the phase turned 0x123, drawn 0x5b87f0; 2 0x5b8700.
//  The draw 0x5b87f0: 16 POLY_G4 (a disc of its size in its matrix, segments 0x100 apart, colours by k & 7) and 4 random
//  4-point gouraud polylines (lightning, 2 RNG steps each) from its centre on the screen, under one DR_MODE (abr 1),
//  at its centre's OTZ -- all skipped (with the RNG steps) when the centre is clipped or its OTZ is outside the draw's
//  depth range. The exe's GTE register loads (exe 0x40c3c0 / 0x40c3e0) are presentation and not replicated.
#include "content/saul_kada/st3c_gold_piece.hpp"
#include "content/saul_kada/st3c_gold_piece_state.hpp"

#include "game/actor_model.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/collision.hpp"
#include "game/draw_commands.hpp"
#include "game/field_alias.hpp"
#include "game/global_class_types.hpp"
#include "game/hit_effects.hpp"
#include "game/hit_list.hpp"
#include "game/matrix.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/projection_state.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"

using namespace od::port::st3c_gold_piece_state;

namespace od::port {

namespace {

enum class PieceState : uint8_t { Start = 0, Run = 1, End = 2 };
// The piece's own bytes: its hit box; the ring's phase (the velocity's spare lane)
struct PieceFields {
    explicit PieceFields(const StaticActor& actor) : PieceFields(const_cast<StaticActor&>(actor)) {}
    explicit PieceFields(StaticActor& actor) : hitBox(actor.hitBox), ringPhase(actor.velocity.spare) {}
    HitBoxHead& hitBox;
    FieldAlias<uint16_t, int16_t> ringPhase;
};
// the exe's global holding the running piece's block
constexpr uint8_t kSparkVariant = 3;
constexpr uint32_t kGoldEnds = kHitAttackBump | kHitAttackByPlayer;
constexpr uint8_t kRingAbr = (0x3e >> 5) & 3; // the DR_MODE tpage 0x3e's blend: additive

// a hit spark (the global hit spark, variant 3) at its integer position
void spark(const StaticActor& actor) {
    const uint32_t slot = allocActorA0(0); // exe 0x5e13b0
    if (!slot) return;
    EffectActor& effect = effectActorAt(slot);
    effect.flags = kActorLive;
    effect.loopFlags |= kActorSweepGroup1 | kActorLateLoop;
    effect.type = typeByte(GlobalEffectType::HitSpark);
    effect.variant = kSparkVariant;
    effect.pos.x = actor.pos.x.whole;
    effect.pos.y = actor.pos.y.whole;
    effect.pos.z = actor.pos.z.whole;
    classState<HitSparkFields>(effectActorAt(slot)).size = uint16_t((rngStep() & 0x1f) + 0x20);
}

// ---- sub 0: the thrown gold --------------------------------------------------------------------------------------
// exe 0x5b8770: its hit entry (through the projection scratch's point)
void goldHit(const StaticActor& actor, PieceFields& piece) {
    piece.hitBox.previousX = actor.prevPos.x.whole;
    piece.hitBox.previousZ = actor.prevPos.z.whole;
    piece.hitBox.result = 0;
    LaneVec4& point = projectionScratch().point;
    point.x = actor.pos.x.whole;
    point.y = actor.pos.y.whole;
    point.z = actor.pos.z.whole;
    const int16_t at[3] = {point.x, point.y, point.z};
    hitListSubmit(at, piece.hitBox,
                  hitOwner(actor.slot) | hitGroup(kHitGroupHeavyThrown) | kHitNoLockOn | hitRadius(0x20),
                  uint32_t(ThrownGoldParams(actor).attack) | kHitImmunityMask | kHitAttackHarmless | kHitAttackStrong);
}
// exe 0x5b84a0: its model (by its variant), airborne, its box, random spins
void goldStart(StaticActor& actor) {
    if (!bindActorModel(actor, actor.variant)) { // exe 0x5e0bc0
        freeActor(actor);                        // exe 0x5e1540
        return;
    }
    actor.airborne = 1;
    actor.flags |= kActorDrawn;
    actor.collisionBox.pointAt(TableId::St3cGoldPieceGoldBox);
    actor.onPlatform = 0;
    actor.platform = nullptr;
    actor.actorHit = 0;
    actor.interactionBox = nullptr;
    actor.interaction = 0;
    actor.spin.x = int16_t(rngStep() & 0xfff);
    actor.spin.y = int16_t((rngLastValue() >> 0xc) & 0xfff);
    actor.clearClassBlock(); // exe 0x40dc40
    actor.enterState(uint8_t(PieceState::Run));
}
// exe 0x5b8540: the flight
void goldFlight(const PlayerEnv& env, StaticActor& actor, PieceFields& piece) {
    if (piece.hitBox.result & kGoldEnds) {
        actor.enterState(uint8_t(PieceState::End));
        actor.setDrawn(false);
        return;
    }
    actor.rotation.x = int16_t(uint16_t(uint16_t(actor.rotation.x) + uint16_t(actor.spin.x)));
    actor.rotation.z = int16_t(uint16_t(uint16_t(actor.rotation.z) + uint16_t(actor.spin.y)));
    actor.velocity.y = int16_t(uint16_t(uint16_t(actor.velocity.y) + uint16_t(actor.acceleration.y)));
    int16_t dx, dz;
    angleVector(actor.rotation.y, int(int16_t(uint16_t(-int(uint16_t(actor.velocity.z))))), dx, dz); // exe 0x5e40c0
    LaneVec4& point = projectionScratch().point;
    point.x = dx;
    point.y = 0;
    point.z = dz;
    projectionScratch().pointYaw = 0;
    actor.pos.x = int32_t(uint32_t(actor.pos.x) + (uint32_t(int32_t(point.x)) << 12));
    actor.pos.y = int32_t(uint32_t(actor.pos.y) + (uint32_t(int32_t(actor.velocity.y)) << 12));
    actor.pos.z = int32_t(uint32_t(actor.pos.z) + (uint32_t(int32_t(point.z)) << 12));
    if (int32_t(actorTestMove(env, actor)) < 0) {        // exe 0x40f570
        const uint32_t result = actorPushOut(env, actor); // exe 0x410960
        if (result & kWallOnYPlus) {
            actor.enterState(uint8_t(PieceState::End));
            actor.setDrawn(false);
            return;
        }
        // off the wall (its local pitch uninitialised in the exe: 0 here; only its yaw is read back)
        int16_t pitch = 0, yaw = actor.rotation.y;
        reflectRotationOffWalls(pitch, yaw, result); // exe 0x410740
        actor.rotation.y = yaw;
        spark(actor);
    }
    goldHit(actor, piece);
}
// exe 0x5b8700: a hit spark, freed
void end(StaticActor& actor) {
    spark(actor);
    freeActor(actor); // exe 0x5e1540
}

// ---- sub 1: the electric ring ------------------------------------------------------------------------------------
// exe 0x5b87f0: the ring and the lightning
void drawRing(const StaticActor& actor, const PieceFields& piece) {
    ProjectionScratch& scratch = projectionScratch();
    scratch.rotatedPoint.x = actor.pos.x.whole;
    scratch.rotatedPoint.y = actor.pos.y.whole;
    scratch.rotatedPoint.z = actor.pos.z.whole;
    const int32_t size = ElectricRingParams(actor).size;
    int16_t ring[17][3];
    for (uint32_t i = 0; i < 8; ++i) {
        const int k = int(((i << 8) + piece.ringPhase) & 0xfff);
        const int16_t z = int16_t(uint16_t(uint32_t(int32_t(uint32_t(int32_t(fixedCos(k))) * uint32_t(size)) >> 12)));
        const int16_t x = int16_t(uint16_t(uint32_t(int32_t(uint32_t(int32_t(fixedSin(k))) * uint32_t(size)) >> 12)));
        scratch.point.z = z;
        scratch.point.x = x;
        ring[i][0] = x;
        ring[i][1] = 0;
        ring[i][2] = z;
        ring[8 + i][0] = int16_t(uint16_t(-int(x)));
        ring[8 + i][1] = 0;
        ring[8 + i][2] = int16_t(uint16_t(-int(z)));
    }
    for (int c = 0; c < 3; ++c) ring[16][c] = ring[0][c];
    const ViewTransform& view = cameraTransform().view;
    const Matrix3 root = actor.rootMatrix;
    scratch.transform.matrix = mulMatrix3x3(view.matrix.get(), root); // exe 0x40bc90
    const int16_t centre[3] = {scratch.rotatedPoint.x, scratch.rotatedPoint.y, scratch.rotatedPoint.z};
    int32_t turned[3];
    transformVector(view.matrix.get(), centre, turned); // exe 0x40bab0
    for (size_t k = 0; k < 3; ++k) scratch.transformed[k] = turned[k];
    for (size_t k = 0; k < 3; ++k) scratch.transform.translation[k] = int32_t(uint32_t(scratch.transformed[k]) + uint32_t(view.translation[k]));
    // exe 0x40c3c0 / 0x40c3e0 (the GTE load), the point (0, 0, 0), RTPS exe 0x40c860: the centre = its position
    scratch.point.x = 0;
    scratch.point.y = 0;
    scratch.point.z = 0;
    const int16_t pos[3] = {scratch.rotatedPoint.x, scratch.rotatedPoint.y, scratch.rotatedPoint.z};
    const CameraPoint cc = cameraPoint(pos);
    if (cc.clipped) return; // exe 0x40cae0 < 0
    const int32_t otz = cameraPointDepth(cc) >> 2; // exe 0x40d010
    if (otz > int32_t(uint16_t(stage().depthRange)) || otz < int32_t(uint16_t(stage().depthOffset))) return;
    // the ring's points in its frame (the exe: the GTE matrix camera * root, the translation camera * pos + T)
    int16_t w[17][3];
    for (uint32_t i = 0; i < 17; ++i)
        for (uint32_t r = 0; r < 3; ++r)
            w[i][r] = int16_t(uint16_t(int(pos[r]) + (int32_t(uint32_t(int32_t(root.e[r * 3 + 0])) * uint32_t(int32_t(ring[i][0])) +
                                                              uint32_t(int32_t(root.e[r * 3 + 1])) * uint32_t(int32_t(ring[i][1])) +
                                                              uint32_t(int32_t(root.e[r * 3 + 2])) * uint32_t(int32_t(ring[i][2]))) >>
                                                      12)));
    // 16 POLY_G4 0x3a {ring k, ring k + 1, centre, centre}, colours {A, B, A, B} by k & 7
    static constexpr float kNoOffsets[4][2] = {};
    for (uint32_t k = 0; k < 16; ++k) {
        uint32_t ca, cb;
        switch (k & 7) {
        case 0: ca = 0x802020; cb = 0x808080; break;
        case 1: ca = 0x808080; cb = 0x808080; break;
        case 2: ca = 0x808080; cb = 0x802020; break;
        default: ca = 0x802020; cb = 0x802020; break;
        }
        const uint32_t c[4] = {ca | 0x3a000000u, cb, ca, cb};
        const int16_t v[4][3] = {{w[k][0], w[k][1], w[k][2]},
                                 {w[k + 1][0], w[k + 1][1], w[k + 1][2]},
                                 {pos[0], pos[1], pos[2]},
                                 {pos[0], pos[1], pos[2]}};
        dc::otQuadG4(actor.at, c, kRingAbr, v, kNoOffsets);
    }
    // 4 lightning polylines (code 0x5e) from the centre's screen point, in steps of exe 0x5f2f20(pos, size) / 2 pixels
    int16_t depth = 0;
    const int32_t length = projectedWidth(pos, int16_t(size), &depth) >> 1; // exe 0x5f2f20
    const float units = pixelUnitsAt(cc);
    const auto step = [&](int16_t p[2], const int16_t q[2], uint32_t angle) {
        const int a = int(angle & 0xfff);
        p[0] = int16_t(uint16_t((int32_t(uint32_t(int32_t(fixedCos(a))) * uint32_t(length)) >> 12) + q[0]));
        p[1] = int16_t(uint16_t((int32_t(uint32_t(int32_t(fixedSin(a))) * uint32_t(length)) >> 12) + q[1]));
    };
    static constexpr uint32_t kBoltColours[4] = {0xc09090, 0x806060, 0x403030, 0};
    for (int j = 0; j < 4; ++j) {
        int16_t p[4][2];
        const int16_t origin[2] = {cc.sx, cc.sy};
        uint32_t angle = rngStep() & 0xfff;
        step(p[0], origin, angle);
        angle = angle + ((rngLastValue() >> 0xc) & 0x7ff) - 0x400;
        step(p[1], p[0], angle);
        angle = angle + (rngStep() & 0x7ff) - 0x400;
        step(p[2], p[1], angle);
        angle = angle + ((rngLastValue() >> 0xb) & 0x7ff) - 0x400;
        step(p[3], p[2], angle);
        for (int s = 0; s < 3; ++s) {
            const uint32_t c[2] = {kBoltColours[s] | 0x52000000u, kBoltColours[s + 1]};
            const int16_t v[2][3] = {{pos[0], pos[1], pos[2]}, {pos[0], pos[1], pos[2]}};
            const float off[2][2] = {
                {float(int16_t(uint16_t(p[s][0] - cc.sx))) * units, float(int16_t(uint16_t(p[s][1] - cc.sy))) * units},
                {float(int16_t(uint16_t(p[s + 1][0] - cc.sx))) * units,
                 float(int16_t(uint16_t(p[s + 1][1] - cc.sy))) * units}};
            dc::otLineG2(actor.at, c, kRingAbr, v, off); // DR_MODE exe 0x40b6d0(p, 1, 1, 0x3e)
        }
    }
}
// exe 0x5b86e0
void ringRun(StaticActor& actor, PieceFields& piece) {
    piece.ringPhase += 0x123u;
    drawRing(actor, piece);
}
// exe 0x5b8690: hidden, a random phase
void ringStart(StaticActor& actor, PieceFields& piece) {
    actor.enterState(uint8_t(PieceState::Run));
    actor.setDrawn(false);
    piece.ringPhase = uint16_t(rngStep() & 0xfff);
    ringRun(actor, piece);
}

void goldPiece(const PlayerEnv& env, StaticActor& actor) { // exe 0x5b8440
    PieceFields piece(actor);
    actor.savePreviousPose();
    actor.wasAirborne = actor.airborne;
    st3cGoldPieceGlobals().pieceBlock = slotFieldWord(actor, kStaticClassBlockOffset); // its hit box's exe address
    const uint32_t sub = actor.subtype, state = actor.state;
    if (sub == kThrownGoldSubtype) {
        switch (PieceState(state)) {
        case PieceState::Start: goldStart(actor); return;
        case PieceState::Run: goldFlight(env, actor, piece); return;
        case PieceState::End: end(actor); return;
        default: break;
        }
    } else if (sub == kElectricRingSubtype) {
        switch (PieceState(state)) {
        case PieceState::Start: ringStart(actor, piece); return;
        case PieceState::Run: ringRun(actor, piece); return;
        case PieceState::End: end(actor); return;
        default: break;
        }
    }
    notPortedAt(kStoryPort, "0x5b8440 sub << 8 | state (table 0x8d0b68)", sub << 8 | state);
}

} // namespace

void goldPieceUpdate(const PlayerEnv& env, uint32_t actor) { goldPiece(env, staticActorAt(actor)); }

} // namespace od::port
