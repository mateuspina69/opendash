// A pair of flame / spark tongues (pool 0xa0, exe 0x42dde0; hypothesis names): two flat triangles (OT F3) thrown outward
// from its point (at random yaws around its spawn's yaw, the speed from its variant's row [0x88c980 + variant * 8]),
// their tilt angles spinning by the row's steps; their height rises by the growth (+0x40 a tick) until it passes 0x400
// (kind 0) or 0x40 (kind 1), then freed. States [0x88ca34]: 0 start 0x42de00, 1 fly 0x42df40, 2 freed 0x4358c0, 3 / 4
// the kinds' growth 0x42e030 / 0x42e060.
#include "content/saul_kada/flame_tongues.hpp"
#include "content/saul_kada/flame_tongues_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_movement.hpp"
#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"
#include "game/angles.hpp"
#include "game/draw_commands.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/random.hpp"
#include "game/stage_state.hpp"
#include "game/story.hpp"

namespace od::port {

using namespace flame_tongues_state;

namespace {

enum class TongueState : uint8_t { Start = 0, Fly = 1, Free = 2, GrowFar = 3, GrowNear = 4 };
enum class TongueKind : uint8_t { Far = 0, Near = 1 };
// A variant's row (exe 0x88c980, 8 bytes)
struct TongueRow : DataRecord {
    uint8_t kind = 0;
    uint8_t shapeSet = 0;
    uint16_t growth = 0;
    int16_t reach = 0;
    uint8_t tiltBStep = 0;
    uint8_t tiltAStep = 0;
    static TongueRow load(const TableReader& r) {
        TongueRow rec;
        rec.kind = r.u8(0);
        rec.shapeSet = r.u8(1);
        rec.growth = r.u16(2);
        rec.reach = r.s16(4);
        rec.tiltBStep = r.u8(6);
        rec.tiltAStep = r.u8(7);
        return rec;
    }
};
const DataTable<RecordTable<TongueRow, 0x10, 8>> tongueRows{TableId::FlameTonguesTongueRows};
// a shape's corner (4 bytes): its radius and its two angles
struct TongueCorner {
    int8_t radius = 0;
    uint8_t angleA = 0, angleB = 0;
};
constexpr uint32_t kTongueShapeSize = 12; // 3 corners
using TongueShape = std::array<TongueCorner, 3>;
// the shape tables (exe table 0x88ca28: a pointer per set to its shapes)
struct TongueShapeSets : DataRecord {
    static constexpr size_t kSets = 0x10, kMaxShapes = 0x40;
    static constexpr uint32_t kBytes = kSets * 4;
    std::array<TableReader, kSets> sets;
    TongueShape shape(size_t set, size_t k) const {
        if (set >= kSets) tableIndexError(set, kSets);
        const TableReader r = sets[set].sub(k * kTongueShapeSize, kTongueShapeSize);
        TongueShape s{};
        for (size_t c = 0; c < 3; ++c) s[c] = {r.s8(c * 4), r.u8(c * 4 + 1), r.u8(c * 4 + 2)};
        return s;
    }
    static TongueShapeSets load(const TableReader& r) {
        TongueShapeSets t;
        for (size_t k = 0; k < kSets; ++k) t.sets[k] = r.follow(k * 4, kMaxShapes * kTongueShapeSize);
        return t;
    }
};
const DataTable<TongueShapeSets> shapeSets{TableId::FlameTongueShapeSets};
constexpr int16_t kGrowthStep = 0x40;
constexpr int16_t kFarLimit = 0x400, kNearLimit = 0x40;
constexpr int32_t kNearestDepth = 0xc0;

// exe 0x42e030 / 0x42e060: the height rises by the growth, which rises; past the limit freed
void grow(EffectActor& actor, TongueFields& tongues, int limit) {
    const uint16_t growth = uint16_t(tongues.growth);
    tongues.height += growth;
    tongues.growth = int16_t(growth + kGrowthStep);
    if (tongues.growth > limit) actor.enterState(uint8_t(TongueState::Free));
}
// exe 0x42e090(pos, angles, colour, shape): one tongue triangle (OT POLY_F3 at AVSZ3: dc::otTriG3 with the colour on
// the three corners, the corners as camera-input points) at the raw s16 point; skipped when its view z is beyond the
// draw depth range * 4 or nearer than 0xc0. Corner k: A = (a0 + tiltA) & 0x3f, B = (b0 + tiltB) & 0x3f: pos +
// (cos B * cos A * r >> 24, sin A * r >> 12, sin B * r >> 12) (16-bit adds)
void tongueTriangle(const EffectActor& actor, const TongueFields& tongues, int16_t x, int16_t y, int16_t z,
                    const TongueShape& shape) {
    const int16_t pos[3] = {x, y, z};
    const CameraPoint view = cameraPoint(pos); // exe 0x40c3c0 / 0x40c3e0 / 0x40c6a0 / 0x40c800
    if (view.view[2] > int32_t(uint32_t(stage().depthRange) << 2) || view.view[2] < kNearestDepth) return;
    const uint32_t colour = tongues.colour;
    const int tiltB = tongues.tiltB, tiltA = tongues.tiltA;
    int16_t corners[3][3];
    for (uint32_t k = 0; k < 3; ++k) {
        const TongueCorner& corner = shape[k];
        const int32_t r = corner.radius;
        const uint32_t a = uint32_t(int(corner.angleA) + tiltA) & 0x3f;
        const uint32_t b = uint32_t(int(corner.angleB) + tiltB) & 0x3f;
        const int32_t sinA = fixedSin64(a), cosA = fixedCos64(a), sinB = fixedSin64(b), cosB = fixedCos64(b);
        corners[k][0] =
            int16_t(uint16_t(x + int16_t(uint16_t(uint32_t(int32_t(uint32_t(cosB) * uint32_t(cosA) * uint32_t(r)) >> 24)))));
        corners[k][1] = int16_t(uint16_t(y + int16_t(uint16_t(uint32_t((sinA * r) >> 12)))));
        corners[k][2] = int16_t(uint16_t(z + int16_t(uint16_t(uint32_t((sinB * r) >> 12)))));
    }
    uint32_t code = (colour & 0xffffffu) | 0x20000000u;
    if (!(colour & 0x80000000u)) code |= 0x2000000u;
    const uint32_t colours[3] = {code, colour & 0xffffffu, colour & 0xffffffu};
    static const float kNoOffsets[3][2] = {};
    dc::otTriG3(actor.at, colours, uint8_t((colour >> 24) & 3), corners, kNoOffsets);
}

// exe 0x42de00: the two tongues' velocities at random yaws around the spawn's, their shapes; the height << 4
void start(EffectActor& actor, TongueFields& tongues, const TongueRow& row) {
    actor.enterState(uint8_t(TongueState::Fly));
    actor.subState = row.kind;
    const int yaw0 = tongues.spawnYaw;
    const uint16_t x0 = uint16_t(tongues.spawnX), z0 = uint16_t(tongues.spawnZ);
    for (size_t k = 0; k < 2; ++k) {
        tongues.x[k] = int16_t(x0);
        tongues.z[k] = int16_t(z0);
        rngStep();
        const uint32_t r = rngStep();
        const int length = int(r & 0x7f) + row.reach;
        int32_t v[3];
        rotateByYaw(int((r & 0x3f) << 4) + yaw0, 0, 0, -length, v); // exe 0x5e38d0
        tongues.velocityX[k] = int16_t(v[0] >> 16);
        tongues.velocityZ[k] = int16_t(v[2] >> 16);
        tongues.shapes[k] = uint8_t(rngStep() & 3);
    }
    tongues.height = int16_t(uint16_t(tongues.spawnY) << 4);
    tongues.growth = int16_t(row.growth);
    tongues.tiltB = 0;
    tongues.tiltA = 0;
}
// exe 0x42df40
void fly(EffectActor& actor, TongueFields& tongues, const TongueRow& row) {
    tongues.x[0] += uint16_t(tongues.velocityX[0]);
    tongues.z[0] += uint16_t(tongues.velocityZ[0]);
    tongues.x[1] += uint16_t(tongues.velocityX[1]);
    tongues.z[1] += uint16_t(tongues.velocityZ[1]);
    tongues.tiltB = int8_t((row.tiltBStep + uint8_t(tongues.tiltB)) & 0x3f);
    tongues.tiltA = int8_t((row.tiltAStep + uint8_t(tongues.tiltA)) & 0x3f);
    if (TongueKind(actor.subState) == TongueKind::Far) grow(actor, tongues, kFarLimit);
    else if (TongueKind(actor.subState) == TongueKind::Near) grow(actor, tongues, kNearLimit);
    else notPortedAt(kStoryPort, "0x42df40 kind (table 0x88ca40)", actor.subState);
    // the two triangles (exe 0x42e090: the shapes of the row's set, one per tongue)
    const int16_t y = int16_t(tongues.height >> 4);
    tongueTriangle(actor, tongues, tongues.x[0], y, tongues.z[0], shapeSets->shape(row.shapeSet, tongues.shapes[0]));
    tongueTriangle(actor, tongues, tongues.x[1], y, tongues.z[1], shapeSets->shape(row.shapeSet, tongues.shapes[1]));
}

void flameTongues(EffectActor& actor) {
    TongueFields& tongues = classState<TongueFields>(actor);
    const TongueRow row = tongueRows[actor.variant];
    switch (TongueState(actor.state)) {
    case TongueState::Start: start(actor, tongues, row); return;
    case TongueState::Fly: fly(actor, tongues, row); return;
    case TongueState::Free: freeActor(actor); return; // 0x4358c0 -> exe 0x5e1580
    case TongueState::GrowFar: grow(actor, tongues, kFarLimit); return;
    case TongueState::GrowNear: grow(actor, tongues, kNearLimit); return;
    default: notPortedAt(kStoryPort, "0x42dde0 state (table 0x88ca34)", actor.state);
    }
}

} // namespace

void tonguesUpdate(const PlayerEnv&, uint32_t actor) { flameTongues(effectActorAt(actor)); }

} // namespace od::port
