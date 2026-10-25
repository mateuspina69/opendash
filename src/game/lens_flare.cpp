// The light effects class 0x4392a0 (see lens_flare.hpp).
//
// The class calls [[0x88d5a4 + kind * 4] + state * 4]: the kind (+5) picks a 3-entry row of 0x88d550 (start, run, free =
// 0x4358c0 -> exe 0x5e1580) and the state (+8) indexes it; the rows are contiguous, so the flat index kind * 3 + state
// is what the dispatch takes (as the exe). Every start clears the tail +0x18..+0x27 (exe 0x40dc40), hides the effect
// from the actor draw, enters the run state and runs it; each kind keeps the address of its tail in a global of its own
// (LensFlareTails).
//
// Drawing: draw-command records exactly as the exe writes them (the screen ring records write word 0, the sprite's
// billboard does not). A ray's packets go through the ordering-table side channel in the pixel-offset form: the corners
// = the projected point (camera-input, << the stage shift) plus the pixel offsets brought back to the point's depth
// ((width << shift) / the projected width units per pixel). The geometry unit's scratch the exe writes for the dropping
// ring and the discs (game/projection_state ProjectionScratch) is kept written; a ray's (exe 0x5e40c0's 8-byte result,
// its high word an uninitialised stack word) and the ordering-table packets are render-only and kept local.
#include "game/loaded_data.hpp"
#include "game/actor_state.hpp"
#include "game/field_alias.hpp"
#include "game/lens_flare.hpp"
#include "game/lens_flare_state.hpp"

#include "game/actor_animation.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/draw_commands.hpp"
#include "game/fixed_point.hpp"
#include "game/game_state.hpp"
#include "game/matrix.hpp"
#include "game/port_gap.hpp"
#include "game/projection.hpp"
#include "game/projection_state.hpp"
#include "game/random.hpp"
#include "game/script_state.hpp"
#include "game/stage_state.hpp"

using namespace od::port::lens_flare_state;

namespace od::port {

using namespace lens_flare_state;

namespace {

inline constexpr const char* kLensFlareArea = "lens flare";

constexpr uint8_t kLensFlareType = 0x19; // the class's stage table type in ST1C / ST54 (per class: game/global_class_types.hpp)
// +5: the kind (a row of the state table)
enum class LensFlareKind : uint8_t {
    Sun = 0,           // exe 0x4392c0 / 0x4392f0
    DroppingRing = 1,  // exe 0x439500 / 0x439580
    PulsingDisc = 2,   // exe 0x439750 / 0x439780
    ShrinkingDisc = 3, // exe 0x4398b0 / 0x439900 (spawned by exe 0x4e4170 / 0x51c393)
    Ray = 4,           // exe 0x439b60 / 0x439c20
    Tint = 5,          // exe 0x43a050 / 0x43a080 (spawned by the room script 0x51d0d0)
    Sprite = 6,        // exe 0x43a150 / 0x43a190
};
constexpr uint32_t kLastKind = uint32_t(LensFlareKind::Sprite);
// +8: the row's states
enum class LensFlareState : uint8_t { Start = 0, Run = 1, Free = 2 }; // Free: exe 0x4358c0 (exe 0x5e1580)
constexpr uint32_t kStatesPerKind = 3;
constexpr uint32_t kLastEntry = 20; // the table's last entry (kind 6, state 2)
// the flat index the dispatch takes
constexpr uint32_t entryOf(LensFlareKind k, LensFlareState s) { return uint32_t(k) * kStatesPerKind + uint32_t(s); }

// the tails' globals (one view)
constexpr uint32_t kTailOffset = 0x18;

// the common start (exe 0x40dc40(a + 0x18, a + 0x24): the tail's first six shorts cleared; the effect hidden from the
// actor draw)
void startCommon(EffectActor& e) {
    e.clearClassShorts(6);
    e.flags &= uint8_t(~kActorDrawn);
}
uint32_t stageShift() { return stage().coordinateShift & 0x1f; } // the 16-bit shl / sar count (cl masked to 5 bits)
uint16_t shl16(uint32_t v, uint32_t s) { return uint16_t(v << s); }
// the screen ring record's colour of the discs: ((((e | 0xfffff400) << 8) | e) << 8) | lo (the exe's shifts leave
// the screen ring opcode in the top byte)
uint32_t discColour(uint32_t e, uint32_t lo) { return ((((e | 0xfffff400u) << 8) | e) << 8) | lo; }
// exe 0x5f2dd0's result when the point is clipped (both coordinates 0x8000)
constexpr uint32_t kClippedScreenPoint = 0x80008000u;
// the discs' depth word: z 3, no step
constexpr uint32_t kDiscDepth = 3;

// ---- the sun lens flare --------------------------------------------------------------------------------------------
// The sun's fields: hidden (the header's step counter), the sun's yaw and pitch (its spawner's), the element set (the
// variant)
struct SunFields {
    explicit SunFields(const EffectActor& actor) : SunFields(const_cast<EffectActor&>(actor)) {}
    explicit SunFields(EffectActor& actor) : hidden(actor.stepCounter), yaw(actor.params, 0), pitch(actor.params, 2) {}
    uint8_t& hidden;
    BytesField<uint16_t, 4> yaw;
    BytesField<uint16_t, 4> pitch;
};
// One element of a flare set (exe data 0x88d660 + set << 7: 16 of 8 bytes): its place along the line (4096 = the
// screen centre), its colour row, its radii word (the record's word 5: the outer radius in the high half)
struct FlareElement : DataRecord {
    static constexpr uint32_t kBytes = 8;
    uint16_t along = 0;     // +0
    uint16_t colourRow = 0; // +2
    uint32_t radii = 0;     // +4
    uint16_t size = 0;      // +6 (the radii's high half)
    template <class R> static FlareElement load(const R& r) { return {{}, r.u16(0), r.u16(2), r.u32(4), r.u16(6)}; }
};
constexpr uint32_t kFlareElements = 0x10;
constexpr uint32_t kFlareSetStride = 0x80;
// a colour row (exe data 0x88d5c0 + (set * 10 + row) * 8): the inner and outer colour words (opcode / blend bytes on top)
struct FlareColours : DataRecord {
    static constexpr uint32_t kBytes = 0x8;
    uint32_t inner = 0; // +0x0
    uint32_t outer = 0; // +0x4
    template <class R> static FlareColours load(const R& r) {
        FlareColours v;
        v.inner = r.template read<uint32_t>(0x0);
        v.outer = r.template read<uint32_t>(0x4);
        return v;
    }
};
constexpr uint32_t kRowsPerSet = 10;
constexpr int kSunWindow = 0x400; // the sun within this of the camera's yaw / pitch
constexpr int kScreenCentreX = 0xa0, kScreenCentreY = 0x78, kScreenWidth = 0x140, kScreenHeight = 0xf0;

// exe 0x439310: h = the camera's yaw - the sun's, v = -(the camera's pitch - the sun's) (signed differences); nothing
// unless both are within kSunWindow. Then the set's 16 elements: screen point (h - (h * along >> 12) + 0xa0, v - (v *
// along >> 12) + 0x78) (the line from the sun through the screen centre), skipped when off the screen by more than its
// size; each kept element one RNG step n = r & 0x1f and a screen ring record: the arc's angle by the frame, the row's
// colours (the grey << (frame & 1), + n), z 3, the element's radii.
void sunFlare(EffectActor& e, const SunFields& f) {
    const uint32_t set = e.variant;
    const uint16_t h = uint16_t(angleDiff(uint32_t(int32_t(cameraState().viewAngles.y)), f.yaw));
    const uint16_t v = uint16_t(-angleDiff(uint32_t(int32_t(cameraState().viewAngles.x)), f.pitch));
    if (uint16_t(h + kSunWindow) > uint16_t(2 * kSunWindow)) return;
    if (uint16_t(v + kSunWindow) > uint16_t(2 * kSunWindow)) return;
    const int32_t hs = int16_t(h), vs = int16_t(v);
    const uint32_t elements = stateAddress(TableId::LensFlareSets) + (set << 7);
    for (uint32_t i = 0; i < kFlareElements; ++i) {
        const FlareElement el = dataRecord<FlareElement>(elements + i * FlareElement::kBytes, FlareElement::kBytes);
        const int32_t k = int32_t(el.along);
        const FlareColours cr = dataRecord<FlareColours>(stateAddress(TableId::LensFlareColourRows) + (uint32_t(el.colourRow) + set * kRowsPerSet) * 8, FlareColours::kBytes);
        const uint32_t size = el.size;
        const uint16_t sx = uint16_t(uint32_t(hs) - uint32_t((hs * k) >> 12) + uint32_t(kScreenCentreX));
        if (uint16_t(sx + size) > uint16_t(size * 2 + kScreenWidth)) continue;
        const uint16_t sy = uint16_t(uint32_t(vs) - uint32_t((vs * k) >> 12) + uint32_t(kScreenCentreY));
        if (uint16_t(sy + size) > uint16_t(size * 2 + kScreenHeight)) continue;
        const uint32_t n = rngStep() & 0x1f;
        const uint32_t frame = gameState().frame;
        const auto colour = [&](uint32_t t) {
            const uint32_t d = ((t & 0xff) << (frame & 1)) + n;
            return (((d << 8) | d) << 8) | (t & 0xff000000u) | d;
        };
        dc::Ring disc;
        disc.arc = dc::kRingFullArc | dc::ringAngle((frame & 0x3f) << 2);
        disc.inner = colour(cr.inner);
        disc.xy = uint32_t(sx) | uint32_t(sy) << 16;
        disc.outer = colour(cr.outer);
        disc.zStep = kDiscDepth;
        disc.radii = el.radii;
        dc::emit(disc, e.at);
    }
}
void sunRun(EffectActor& e) { // exe 0x4392f0
    const SunFields f(e);
    tails().sun = slotIdentityWord(e) + kTailOffset;
    if (f.hidden == 0) sunFlare(e, f); // exe 0x439310
}
void sunStart(EffectActor& e) { // exe 0x4392c0
    tails().sun = slotIdentityWord(e) + kTailOffset;
    startCommon(e);
    e.state = uint8_t(LensFlareState::Run);
    sunRun(e);
}

constexpr uint16_t kRingStep = 0x344;   // the distance's drop a tick (and the stop's base)
constexpr int32_t kRingScale = 0x100;   // the frame's columns scaled by this (/ 4096)

// exe 0x439580: while the distance > the stop: kRingStep a tick, the ticks counted; the centre = the effect + Ry Rx
// Rz(tilt x << 4, tilt y << 4, 0) scaled kRingScale / 4096 * (0, 0, -distance), << the stage shift; one ring record
// (grey (sin[(ticks & 0xf) * 0x100] >> 7) + 0x40, radius the half radius << 1). Else freed next tick.
void droppingRingRun(EffectActor& e) {
    DroppingRingFields& f = classState<DroppingRingFields>(e);
    tails().droppingRing = slotIdentityWord(e) + kTailOffset;
    const uint32_t cur = f.distance;
    if (int32_t(cur) <= int32_t(f.stopAt)) {
        e.enterState(uint8_t(LensFlareState::Free));
        return;
    }
    f.distance = uint16_t(cur - kRingStep);
    ++f.ticks;
    ProjectionScratch& s = projectionScratch();
    s.point.x = int16_t(uint16_t(uint32_t(int32_t(f.tiltX)) << 4));
    s.point.y = int16_t(uint16_t(uint32_t(int32_t(f.tiltY)) << 4));
    s.point.z = 0;
    s.transform.setRotationYXZ(s.point); // exe 0x40bfd0
    s.transformed[0] = kRingScale;
    s.transformed[1] = kRingScale;
    s.transformed[2] = kRingScale;
    const int32_t scale[3] = {s.transformed[0], s.transformed[1], s.transformed[2]};
    s.transform.matrix = scaleColumns(s.transform.matrix, scale); // exe 0x40c0e0
    s.point.x = 0;
    s.point.y = 0;
    s.point.z = int16_t(uint16_t(0u - uint32_t(f.distance)));
    int16_t r[3];
    const int16_t in[3] = {s.point.x, s.point.y, s.point.z};
    transformShortVector(s.transform.matrix, in, r); // exe 0x40bb30
    s.rotatedPoint.x = r[0];
    s.rotatedPoint.y = r[1];
    s.rotatedPoint.z = r[2];
    const uint32_t sh = stageShift();
    s.rotatedPoint.x += uint32_t(uint16_t(e.pos.x));
    s.rotatedPoint.y += uint32_t(uint16_t(e.pos.y));
    s.rotatedPoint.z += uint32_t(uint16_t(e.pos.z));
    s.rotatedPoint.x = int16_t(shl16(uint16_t(s.rotatedPoint.x), sh));
    s.rotatedPoint.y = int16_t(shl16(uint16_t(s.rotatedPoint.y), sh));
    s.rotatedPoint.z = int16_t(shl16(uint16_t(s.rotatedPoint.z), sh));
    const uint32_t g = ((uint32_t(uint16_t(fixedSin(int((f.ticks & 0xfu) << 8)))) >> 7) + 0x40) & 0xff;
    dc::Ring ring;
    ring.inner = ((((((g | 0xfffff000u) << 8) | g) << 7) | (g & 0x7f)) << 1); // the grey, the ring opcode on top
    ring.xy = dc::packXY(s.rotatedPoint.x, s.rotatedPoint.y);
    ring.zStep = dc::zWord(s.rotatedPoint.z) | dc::ringStep(2);
    ring.radii = uint32_t(uint16_t(int16_t(f.halfRadius))) << 17;
    dc::emit(ring, e.at);
}
// exe 0x439500: the distance ((r & 0x1f) + 0x300) << 4, the stop ((r >> 5) & 0x1f) * 16 + kRingStep (one RNG step)
void droppingRingStart(EffectActor& e) {
    DroppingRingFields& f = classState<DroppingRingFields>(e);
    tails().droppingRing = slotIdentityWord(e) + kTailOffset;
    startCommon(e);
    const uint32_t r = rngStep();
    f.distance = uint16_t(((r & 0x1f) + 0x300) << 4);
    e.state = uint8_t(LensFlareState::Run);
    f.stopAt = int16_t(uint16_t(((r >> 5) & 0x1f) * 16 + kRingStep));
    droppingRingRun(e);
}

constexpr uint16_t kDiscGrowth = 4, kDiscMaxRadius = 0x80;
constexpr uint8_t kDiscEndsAtStep = 2; // the script's step sub-state that ends it

// exe 0x439780: while the script's step sub-state < kDiscEndsAtStep: the radius grows by kDiscGrowth up to
// kDiscMaxRadius, the ticks counted; the point (the effect, + the anchor's integer parts) projected; unless clipped one
// screen ring record (grey (sin[(ticks & 7) * 0x200] >> 7) - 0x80). Else freed next tick.
void pulsingDiscRun(EffectActor& e) {
    PulsingDiscFields& f = classState<PulsingDiscFields>(e);
    tails().pulsingDisc = slotIdentityWord(e) + kTailOffset;
    if (scriptState().stepSub >= kDiscEndsAtStep) {
        e.enterState(uint8_t(LensFlareState::Free));
        return;
    }
    const uint16_t r = f.radius;
    f.radius = r < kDiscMaxRadius ? uint16_t(r + kDiscGrowth) : kDiscMaxRadius;
    f.ticks = uint16_t(f.ticks + 1);
    ProjectionScratch& s = projectionScratch();
    int16_t pos[3];
    const int16_t own[3] = {e.pos.x, e.pos.y, e.pos.z};
    for (uint32_t k = 0; k < 3; ++k) {
        if (const uint32_t anchor = e.paramsWord()) {
            const Fixed16Vec3& a = actorPositionAt(anchor);
            const int16_t whole = k == 0 ? a.x.whole : k == 1 ? a.y.whole : a.z.whole;
            pos[k] = int16_t(uint16_t(uint16_t(whole) + uint16_t(own[k])));
        } else {
            pos[k] = own[k];
        }
    }
    s.point.x = pos[0];
    s.point.y = pos[1];
    s.point.z = pos[2];
    int16_t sxy[2];
    projectOnScreen(pos, sxy); // exe 0x5f2dd0
    const uint32_t xy = dc::packXY(sxy[0], sxy[1]);
    if (xy == kClippedScreenPoint) return;
    const uint32_t g = ((uint32_t(uint16_t(fixedSin(int((uint32_t(uint8_t(f.ticks)) & 7u) << 9)))) >> 7) - 0x80) & 0xff;
    dc::Ring disc;
    disc.inner = discColour(g, g + 0x20);
    disc.xy = xy;
    disc.zStep = kDiscDepth | dc::ringStep(2);
    disc.radii = dc::ringRadii(0, f.radius);
    dc::emit(disc, e.at);
}
void pulsingDiscStart(EffectActor& e) { // exe 0x439750
    tails().pulsingDisc = slotIdentityWord(e) + kTailOffset;
    startCommon(e);
    e.state = uint8_t(LensFlareState::Run);
    pulsingDiscRun(e);
}

// the shrinking disc's variant bits
constexpr uint8_t kDiscShrinks = 0x01;   // it shrinks (rays only while wider than kRayWidthMin), else it holds
constexpr uint8_t kDiscSlowRays = 0x80;  // half as many rays (and its rays turn the other half of the circle)
constexpr uint16_t kDiscStartWidth = 0x80, kDiscShrink = 2, kDiscEndWidth = 2, kRayWidthMin = 0x40;
constexpr uint8_t kRayKind = uint8_t(LensFlareKind::Ray);
constexpr uint16_t kRayTicks = 0x20;
constexpr uint32_t kRayWidthSpread = 0x1f, kRayWidthBase = 0x20;
constexpr uint32_t kRayBaseAngle = 0x800; // the slow rays' angle: within half a turn from here

// the point of a shrinking disc / a ray (the scratch point written as the exe leaves it)
void pointOf(const EffectActor& e, const PointedFields& f, int16_t pos[3]) {
    const uint32_t sh = stageShift();
    ProjectionScratch& s = projectionScratch();
    const int16_t own[3] = {e.pos.x, e.pos.y, e.pos.z};
    for (uint32_t k = 0; k < 3; ++k) {
        if (const uint32_t point = e.paramsWord()) {
            const ShortVec4& p = bonePointAtAddress(point);
            const int16_t c = k == 0 ? p.x : k == 1 ? p.y : p.z;
            pos[k] = int16_t(uint16_t(uint32_t(int32_t(c) >> sh) + uint16_t(own[k])));
        } else {
            pos[k] = own[k];
        }
        if (k == 0) s.point.x = pos[k];
        else if (k == 1) s.point.y = pos[k];
        else s.point.z = pos[k];
    }
}
// exe 0x439ab0: a ray (late loop) at the same point, its width = the disc's + (r & 0x1f) + 0x20 and its angle ((r >> 5)
// & 0x3ff) + 0x800 (slow rays) or (r >> 5) & 0xfff (one RNG step)
void spawnRay(const EffectActor& disc, const ShrinkingDiscFields& f) {
    const uint32_t address = allocActorA0(kActorStageClass); // exe 0x5e13a0
    if (address == 0) return;
    EffectActor& ray = effectActorAt(address);
    RayFields& r = classState<RayFields>(effectActorAt(address));
    const uint8_t loopFlags = ray.loopFlags;
    ray.subtype = kRayKind;
    ray.flags = kActorLive;
    ray.type = kLensFlareType;
    ray.loopFlags = uint8_t(loopFlags | kActorLateLoop);
    ray.setParamsWord(disc.paramsWord()); // the point it sits on (+0xc), the disc's
    ray.pos.x = disc.pos.x;
    ray.pos.y = disc.pos.y;
    ray.pos.z = disc.pos.z;
    const uint32_t rnd = rngStep();
    r.spawnWidth = uint16_t(uint32_t(f.width) + (rnd & kRayWidthSpread) + kRayWidthBase);
    if (disc.variant & kDiscSlowRays) r.spawnAngle = uint16_t(((rnd >> 5) & 0x3ff) + kRayBaseAngle);
    else r.spawnAngle = uint16_t((rnd >> 5) & kAngleMask);
}
// exe 0x439900: shrinking (kDiscShrinks): the width shrinks by kDiscShrink (ended at kDiscEndWidth or less), a ray every
// 16 (slow) / 8 frames while wider than kRayWidthMin; holding: a ray every 8 (slow) / 4 frames. The ticks counted. The
// point projected; with a projected width (exe 0x5f2f20) and on screen one screen ring record (grey (sin[(ticks &
// 0x1f) * 0x80] >> 8) - 0x60, radius the projected width). Ends (freed next tick) when the script's step sub-state
// leaves the one it started at (tested only after a drawn record).
void shrinkingDiscRun(EffectActor& e) {
    ShrinkingDiscFields& f = classState<ShrinkingDiscFields>(e);
    tails().shrinkingDisc = slotIdentityWord(e) + kTailOffset;
    const uint8_t variant = e.variant;
    const uint8_t frame = uint8_t(gameState().frame);
    bool spawn;
    if (variant & kDiscShrinks) {
        const uint16_t w = f.width;
        if (w <= kDiscEndWidth) {
            e.enterState(uint8_t(LensFlareState::Free));
            return;
        }
        f.width = uint16_t(w - kDiscShrink);
        if (f.width <= kRayWidthMin) spawn = false;
        else spawn = (e.variant & kDiscSlowRays) ? (frame & 0xf) == 0 : (frame & 7) == 0;
    } else {
        spawn = (variant & kDiscSlowRays) ? (frame & 7) == 0 : (frame & 3) == 0;
    }
    if (spawn) spawnRay(e, f); // exe 0x439ab0
    f.ticks = uint16_t(f.ticks + 1);
    int16_t pos[3];
    pointOf(e, f, pos);
    int16_t sxy[2];
    projectOnScreen(pos, sxy); // exe 0x5f2dd0
    int16_t depth = 0;
    const int w = projectedWidth(pos, int16_t(f.width), &depth); // exe 0x5f2f20
    if (w == 0) return;
    const uint32_t xy = dc::packXY(sxy[0], sxy[1]);
    if (xy == kClippedScreenPoint) return;
    const uint32_t g = ((uint32_t(uint16_t(fixedSin(int((uint32_t(uint8_t(f.ticks)) & 0x1fu) << 7)))) >> 8) - 0x60) & 0xff;
    dc::Ring disc;
    disc.inner = discColour(g, g);
    disc.xy = xy;
    disc.zStep = kDiscDepth | dc::ringStep(2);
    disc.radii = dc::ringRadii(0, uint32_t(w));
    dc::emit(disc, e.at);
    if (f.startStep != scriptState().stepSub) e.enterState(uint8_t(LensFlareState::Free));
}
// exe 0x4398b0: the width kDiscStartWidth, the script's step sub-state remembered
void shrinkingDiscStart(EffectActor& e) {
    ShrinkingDiscFields& f = classState<ShrinkingDiscFields>(e);
    tails().shrinkingDisc = slotIdentityWord(e) + kTailOffset;
    startCommon(e);
    f.width = kDiscStartWidth;
    f.startStep = scriptState().stepSub;
    e.state = uint8_t(LensFlareState::Run);
    shrinkingDiscRun(e);
}

// exe 0x439c20: kRayTicks ticks, the angle turned by its speed; the point projected with its projected width w (exe
// 0x5f2f20); unless w == 0 or clipped: an additive draw mode (exe 0x40b6d0(p, 1, 1, 0x3e)) and two POLY_G4 0x3a {S +
// R(w/4) bright, S + R'(w/4) bright, S + R(w) black, S + R'(w) black} and {S + R(w/8) black, S + R'(w/8) black, S +
// R(w/4) bright, S + R'(w/4) bright}, R(r) = exe 0x5e40c0(angle, r), R' the same at the angle + 0x40, bright = k *
// 0x010101, k = v - (v >> 2), v = (sin[(ticks & 0x3f) * 0x40] >> 5) & 0xff, linked at the OT [0xa67840] + 0x7c. Then it
// ends when the script's step sub-state leaves the one it started at.
void rayRun(EffectActor& e) {
    RayFields& f = classState<RayFields>(e);
    tails().ray = slotIdentityWord(e) + kTailOffset;
    f.ticks = uint16_t(f.ticks + 1);
    if (f.ticks >= kRayTicks) {
        e.enterState(uint8_t(LensFlareState::Free));
        return;
    }
    f.angle = uint16_t(f.angle + f.turnSpeed);
    int16_t pos[3];
    pointOf(e, f, pos);
    int16_t sxy[2];
    projectOnScreen(pos, sxy); // exe 0x5f2dd0
    int16_t depth = 0;
    const int16_t width = int16_t(f.width);
    const int w = projectedWidth(pos, width, &depth); // exe 0x5f2f20
    if (w == 0) return;
    if (dc::packXY(sxy[0], sxy[1]) == kClippedScreenPoint) return;
    const uint32_t v = (uint32_t(uint16_t(fixedSin64(uint32_t(uint8_t(f.ticks)) & 0x3fu))) >> 5) & 0xff;
    const uint32_t k = v - (v >> 2);
    const uint32_t grey = k << 16 | k << 8 | k;
    const uint32_t angle = f.angle;
    // exe 0x5e40c0(angle, s16 r): the pixel offset (sin, cos) * r >> 12 at the angle + 0x800
    struct Off {
        int16_t x, y;
    };
    const auto ray = [](uint32_t ang, uint32_t r) {
        Off o{};
        angleVector(int(ang), int(int16_t(uint16_t(r))), o.x, o.y);
        return o;
    };
    const uint32_t uw = uint32_t(w);
    const Off a1 = ray(angle, uw >> 2), b1 = ray(angle + 0x40, uw >> 2);
    const Off a2 = ray(angle, uw), b2 = ray(angle + 0x40, uw);
    const Off a0 = ray(angle, uw >> 3), b0 = ray(angle + 0x40, uw >> 3);
    const Off a1b = ray(angle, uw >> 2), b1b = ray(angle + 0x40, uw >> 2);
    // presentation: the corners as the point (camera-input) + the pixel offsets at its depth
    const uint32_t sh = stageShift();
    int16_t pt[3];
    for (int i = 0; i < 3; ++i) pt[i] = int16_t(uint16_t(uint32_t(uint16_t(pos[i])) << sh));
    const float scale = float(int32_t(width) << sh) / float(w);
    int16_t vv[4][3];
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 3; ++i) vv[j][i] = pt[i];
    const auto offs = [&](const Off (&o)[4], float out[4][2]) {
        for (int j = 0; j < 4; ++j) {
            out[j][0] = float(o[j].x) * scale;
            out[j][1] = float(o[j].y) * scale;
        }
    };
    const uint32_t bright = dc::packetColour(dc::kPolyG4Semi, grey), black = dc::packetColour(dc::kPolyG4Semi, 0);
    {
        const Off o[4] = {a1, b1, a2, b2};
        float off[4][2];
        offs(o, off);
        const uint32_t c[4] = {bright, grey, 0, 0};
        dc::otQuadG4(e.at, c, dc::kAdditiveBlend, vv, off); // tpage 0x3e: abr 1
    }
    {
        const Off o[4] = {a0, b0, a1b, b1b};
        float off[4][2];
        offs(o, off);
        const uint32_t c[4] = {black, 0, grey, grey};
        dc::otQuadG4(e.at, c, dc::kAdditiveBlend, vv, off);
    }
    if (f.startStep != scriptState().stepSub) e.enterState(uint8_t(LensFlareState::Free));
}
// exe 0x439b60: its width (the spawner's), the script's step sub-state remembered, its turn speed (((r >> 1) & 3) + 6,
// or -6 - ((r >> 1) & 3), by r & 1), its angle (the spawner's), the unread byte ((r >> 3) & 0x3f) + 0x40 (one RNG step)
void rayStart(EffectActor& e) {
    RayFields& f = classState<RayFields>(e);
    tails().ray = slotIdentityWord(e) + kTailOffset;
    startCommon(e);
    f.width = f.spawnWidth;
    f.startStep = scriptState().stepSub;
    const uint32_t r = rngStep();
    if (r & 1) f.turnSpeed = uint16_t(((r >> 1) & 3) + 6);
    else f.turnSpeed = uint16_t(0xfffau - ((r >> 1) & 3));
    f.angle = f.spawnAngle;
    f.unread22 = uint8_t((((r >> 3) & 0x3f) + 0x40) & 0xff);
    e.state = uint8_t(LensFlareState::Run);
    rayRun(e);
}

// ---- the full-screen tint ------------------------------------------------------------------------------------------
// The tint's fields: its colour level (and the held colour's r, g, b), its fade step a tick (the room script 0x51d0d0
// writes +0xc..+0xf = r, g, b, blend)
struct TintFields {
    explicit TintFields(const EffectActor& actor) : TintFields(const_cast<EffectActor&>(actor)) {}
    explicit TintFields(EffectActor& actor) : level(actor.params[0]), fade(actor.params[1]) {}
    uint8_t& level;
    uint8_t& fade;
};
constexpr uint8_t kTintHeld = 0; // the variant that holds its colour (else it fades)

// exe 0x43a080: held, or fading by its step a tick (ended, freed next tick, once the level is at most the step). Its
// packets -- a draw mode exe 0x40b6d0(p, 1, 1, (s8 blend << 5) | 0x1e) and a semi-transparent TILE of the colour over
// the 320 x 240 screen at the OT [0xa67840] + 0x74 -- are NOT DRAWN by the port (no full-screen primitive with the PS1
// blends).
void tintRun(EffectActor& e) {
    TintFields f(e);
    tails().tint = slotIdentityWord(e) + kTailOffset;
    if (e.variant == kTintHeld) return;
    const uint8_t d = f.fade;
    if (d == 0) return;
    const uint8_t c = f.level;
    if (c <= d) {
        e.enterState(uint8_t(LensFlareState::Free));
        return;
    }
    f.level = uint8_t(c - d);
}
void tintStart(EffectActor& e) { // exe 0x43a050
    tails().tint = slotIdentityWord(e) + kTailOffset;
    startCommon(e);
    e.state = uint8_t(LensFlareState::Run);
    tintRun(e);
}

// ---- the 5-frame sprite --------------------------------------------------------------------------------------------
// The sprite's fields: its frame and the frame's ticks, its size (the spawner's)
struct SpriteFields {
    explicit SpriteFields(const EffectActor& actor) : SpriteFields(const_cast<EffectActor&>(actor)) {}
    explicit SpriteFields(EffectActor& actor) : frame(actor.params[0]), frameTicks(actor.params[1]), size(actor.posSpare) {}
    uint8_t& frame;
    uint8_t& frameTicks;
    int16_t& size;
};
constexpr uint8_t kSpriteFrames = 5;
constexpr uint8_t kFirstFrameTicks = 4, kFrameTicks = 2;
// its billboard: the frame's 32-texel cell of the strip v 0xe0..0xff, CLUT 0x3c48, tpage 0x28, grey
constexpr uint16_t kSpriteClut = 0x3c48, kSpriteTpage = 0x28;
constexpr uint32_t kSpriteRgb = 0x808080;

// exe 0x43a190: one billboard a tick at the effect (<< the stage shift, also left in the scratch point): u = frame << 5
// .. (frame + 1) << 5, v 0xe0 .. 0xff; the frame's ticks: kFirstFrameTicks then kFrameTicks; after the last frame
// freed next tick
void spriteRun(EffectActor& e) {
    SpriteFields f(e);
    const uint8_t c0 = uint8_t(f.frame << 5), c1 = uint8_t((f.frame + 1u) << 5);
    const uint32_t sh = stageShift();
    const uint16_t x = shl16(uint16_t(e.pos.x), sh), y = shl16(uint16_t(e.pos.y), sh), z = shl16(uint16_t(e.pos.z), sh);
    ProjectionScratch& s = projectionScratch();
    s.point.x = int16_t(x);
    s.point.y = int16_t(y);
    s.point.z = int16_t(z);
    dc::Billboard b;
    b.colour = dc::billboardColour(kSpriteRgb);
    b.xy = dc::packXY(int16_t(x), int16_t(y));
    b.z = dc::signedZWord(int16_t(z));
    b.clut = kSpriteClut;
    b.tpage = kSpriteTpage;
    b.uv[0] = dc::texel(c0, 0xe0);
    b.uv[1] = dc::texel(c1, 0xe0);
    b.uv[2] = dc::texel(c0, 0xff);
    b.uv[3] = dc::texel(c1, 0xff);
    b.sizeRotation = uint32_t(int32_t(f.size)); // the size word sign-extended (the exe's movsx: a negative size turns it)
    dc::emit(b, e.at);
    const uint8_t n = uint8_t(f.frameTicks - 1u);
    f.frameTicks = n;
    if (n != 0) return;
    const uint8_t next = uint8_t(f.frame + 1u);
    f.frameTicks = kFrameTicks;
    f.frame = next;
    if (next == kSpriteFrames) e.enterState(uint8_t(LensFlareState::Free));
}
void spriteStart(EffectActor& e) { // exe 0x43a150
    SpriteFields f(e);
    tails().sprite = slotIdentityWord(e) + kTailOffset;
    startCommon(e);
    e.state = uint8_t(LensFlareState::Run);
    f.frameTicks = kFirstFrameTicks;
    spriteRun(e);
}

// exe 0x4392a0: [[0x88d5a4 + kind * 4] + state * 4] = the 0x88d550 entry kind * 3 + state (the rows contiguous)
void dispatch(EffectActor& e) {
    const uint32_t kind = e.subtype;
    if (kind > kLastKind) notPortedAt(kLensFlareArea, "0x4392a0 kind (table 0x88d5a4)", kind);
    const uint32_t entry = kind * kStatesPerKind + e.state;
    if (entry > kLastEntry) notPortedAt(kLensFlareArea, "0x4392a0 state (table 0x88d550)", entry);
    if (entry % kStatesPerKind == uint32_t(LensFlareState::Free)) { // exe 0x4358c0
        freeActor(e);
        return;
    }
    using K = LensFlareKind;
    using S = LensFlareState;
    switch (entry) {
    case entryOf(K::Sun, S::Start): sunStart(e); return;
    case entryOf(K::Sun, S::Run): sunRun(e); return;
    case entryOf(K::DroppingRing, S::Start): droppingRingStart(e); return;
    case entryOf(K::DroppingRing, S::Run): droppingRingRun(e); return;
    case entryOf(K::PulsingDisc, S::Start): pulsingDiscStart(e); return;
    case entryOf(K::PulsingDisc, S::Run): pulsingDiscRun(e); return;
    case entryOf(K::ShrinkingDisc, S::Start): shrinkingDiscStart(e); return;
    case entryOf(K::ShrinkingDisc, S::Run): shrinkingDiscRun(e); return;
    case entryOf(K::Ray, S::Start): rayStart(e); return;
    case entryOf(K::Ray, S::Run): rayRun(e); return;
    case entryOf(K::Tint, S::Start): tintStart(e); return;
    case entryOf(K::Tint, S::Run): tintRun(e); return;
    case entryOf(K::Sprite, S::Start): spriteStart(e); return;
    default: spriteRun(e); return; // entryOf(K::Sprite, S::Run)
    }
}

} // namespace

void lensFlareUpdate(const PlayerEnv&, uint32_t address) { dispatch(effectActorAt(address)); }

} // namespace od::port
