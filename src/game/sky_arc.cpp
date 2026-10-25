// The sky arc (see sky_arc.hpp). Its states (exe table 0x889f80: the named routines of engine/routines.csv): the start
// (exe 0x417510: the sky point, the late loop) and the draw (exe 0x417540). The draw is presentation only: the exe's GPU
// packets become ordering-table side-channel quads (dc::otQuadG4) whose corners are the arc's screen pixels mapped into
// the camera plane at the depth of the ordering-table slot the exe links them into, with the projection's focal and
// centre (the screen-space discs' mapping of game/draw_command_decode). Not replicated: the GTE register and float
// history the exe leaves (render-only state) and the packets' links.
#include "game/actor_state.hpp"
#include "game/sky_arc.hpp"
#include "game/sky_arc_state.hpp"

#include "engine/data_source.hpp"
#include "game/actor_pools.hpp"
#include "game/angles.hpp"
#include "game/camera_state.hpp"
#include "game/draw_commands.hpp"
#include "game/fixed_point.hpp"
#include "game/matrix.hpp"
#include "game/player/player_state.hpp"
#include "game/port_gap.hpp"
#include "game/projection_state.hpp"

namespace od::port {

using namespace sky_arc_state;

namespace {

inline constexpr const char* kSkyArcArea = "sky arc";

constexpr int32_t kSkyPoint[3] = {0, 90000, 300000};

// +8: the states (exe table 0x889f80: a routine per state, the named routines of engine/routines.csv)
const DataTable<RoutineTable<2>> skyArcStates{TableId::SkyArcStates};
enum class SkyArcState : uint8_t { Start = 0, Draw = 1 };

// The arc's exe tables: the band widths of the two passes (s16, added to the radius band by band, the last first), the
// colour record of each segment's ends (25 bytes: segment n's are entries n and n + 1) and the two passes' colour words
// (4 / 6 words per record, read backwards from a record's second word: the first records' bands reach the words before
// the table, as the exe reads them)
constexpr size_t kInnerBands = 3, kOuterBands = 5, kSegmentEnds = 0x19;
constexpr int32_t kInnerRecordWords = 4, kOuterRecordWords = 6;
constexpr int32_t kColourRecords = 7; // the colour records the segments' ends name (0 .. 6)
// A pass's colour words (exe data: 0x889fc0 inner, 0x88a038 outer), with the words before the table the first records'
// bands read (the exe's unchecked read): the table's source location starts `Before` words before the table
template <int32_t Before, int32_t RecordWords> struct SkyArcColours : DataRecord {
    static constexpr int32_t kWords = Before + kColourRecords * RecordWords;
    static constexpr uint32_t kBytes = uint32_t(kWords * 4);
    std::array<uint32_t, size_t(kWords)> words{};
    // colour word k of the table (k < 0: a word before it)
    uint32_t operator[](int32_t k) const {
        if (k < -Before || k >= kWords - Before) tableIndexError(size_t(k + Before), size_t(kWords));
        return words[size_t(k + Before)];
    }
    static SkyArcColours load(const TableReader& r) {
        SkyArcColours t;
        for (size_t i = 0; i < t.words.size(); ++i) t.words[i] = r.u32(i * 4);
        return t;
    }
};
using InnerColours = SkyArcColours<2, kInnerRecordWords>;
using OuterColours = SkyArcColours<5, kOuterRecordWords>;
const DataTable<ScalarTable<int16_t, kInnerBands>> innerBands{TableId::SkyArcInnerBands};
const DataTable<ScalarTable<int16_t, kOuterBands>> outerBands{TableId::SkyArcOuterBands};
const DataTable<ScalarTable<uint8_t, kSegmentEnds>> segmentColours{TableId::SkyArcSegmentColours};
const DataTable<InnerColours> innerColours{TableId::SkyArcInnerColours};
const DataTable<OuterColours> outerColours{TableId::SkyArcOuterColours};

// the arc's geometry: its own focal (0x1800000 / z), the screen centre (0xa0, 0x78 less the player's height >> 5), the
// radius 200000 at that focal, the visible box of the centre, the 24 segments of the 64-step circle from index 0x14 and
// the screen test of a segment kept by the first pass
constexpr int32_t kArcFocal = 0x1800000;
constexpr int32_t kCentreX = 0xa0, kCentreY = 0x78;
constexpr int kPlayerHeightShift = 5;
constexpr int32_t kArcRadius = 200000;
constexpr uint16_t kVisibleX = 0x260, kVisibleWidth = 0x600, kVisibleY = 0x88, kVisibleHeight = 0x400;
constexpr int32_t kLastSegment = 0x17;
constexpr uint32_t kFirstIndex = 0x14;
constexpr int16_t kKeptTop = -8, kKeptBottom = 0x100, kKeptLeft = -0x10, kKeptRight = 0x150;
// the passes' packets: a semi-transparent POLY_G4 after a draw mode of blend 2 (subtractive) / 1 (additive)
constexpr uint8_t kSubtractiveBlend = 2;
constexpr uint32_t kSemiG4Code = dc::kPolyG4Semi << 24;

// A screen point (exe pixels) into the camera plane at view depth kArcDepth (the depth of the ordering-table slot the
// exe links the packets into), as the screen-space discs are mapped: anchor = R^T (view - T), offsets in camera-input
// units along the camera rows 0 / 1.
constexpr int32_t kArcDepth = 0x88;
struct ScreenPlane {
    int16_t anchor[3];
    int32_t ax, ay;
    float scale; // camera-input units per pixel at kArcDepth
    bool ok;
};
// the camera plane of the screen point (ax, ay) at kArcDepth
ScreenPlane screenPlane(int32_t ax, int32_t ay) {
    ScreenPlane p{};
    const ProjectionState& proj = projection();
    const int32_t h = proj.focal, cx = proj.centreX, cy = proj.centreY;
    if (h == 0) return p;
    const ViewTransform& view = cameraTransform().view;
    const Matrix3 m = view.matrix.get();
    const double v[3] = {double(ax - cx) * kArcDepth / h, double(ay - cy) * kArcDepth / h, double(kArcDepth)};
    double d[3];
    for (size_t k = 0; k < 3; ++k) d[k] = v[k] - double(view.translation[k].get());
    for (int i = 0; i < 3; ++i) {
        double s = 0;
        for (int j = 0; j < 3; ++j) s += double(m.e[j * 3 + i]) * d[j];
        s /= 4096.0;
        if (s < -32768.0) s = -32768.0;
        if (s > 32767.0) s = 32767.0;
        p.anchor[i] = int16_t(s);
    }
    p.ax = ax;
    p.ay = ay;
    p.scale = float(kArcDepth) / float(h);
    p.ok = true;
    return p;
}
// a POLY_G4 packet (w1 code / colour 0, w2 xy0, w3 colour 1, w4 xy1, w5 colour 2, w6 xy2, w7 colour 3, w8 xy3)
void screenG4(uint32_t owner, const ScreenPlane& sp, const uint32_t w[9], uint8_t abr) {
    if (!sp.ok) return;
    const uint32_t c[4] = {w[1], w[3], w[5], w[7]};
    int16_t v[4][3];
    float off[4][2];
    const uint32_t xy[4] = {w[2], w[4], w[6], w[8]};
    for (int i = 0; i < 4; ++i) {
        for (int k = 0; k < 3; ++k) v[i][k] = sp.anchor[k];
        off[i][0] = float(int32_t(int16_t(uint16_t(xy[i]))) - sp.ax) * sp.scale;
        off[i][1] = float(int32_t(int16_t(uint16_t(xy[i] >> 16))) - sp.ay) * sp.scale;
    }
    dc::otQuadG4(owner, c, abr, v, off);
}

// exe 0x417510: the sky point, run by the late loop, then the draw state
void skyArcStart(ControllerActor& actor) {
    SkyArcFields& f = classState<SkyArcFields>(actor);
    actor.state = uint8_t(SkyArcState::Draw);
    actor.loopFlags |= kActorLateLoop;
    for (size_t k = 0; k < 3; ++k) f.point[k] = kSkyPoint[k];
}

// one pass of the arc: for each segment n (23 .. 0) the packets of its bands, the radius growing band by band (the
// band table read from its last entry); `keep` tests a segment (and the first pass marks it in the mask), the colour
// words of the segment's two end records read backwards one word per band
template <size_t Bands, class Colours, class Keep>
void arcPass(uint32_t owner, const ScreenPlane& sp, int32_t sx, int32_t sy, int32_t r0,
             const ScalarTable<int16_t, Bands>& bands, const Colours& colours, int32_t recordWords, uint8_t abr,
             Keep keep) {
    const ScalarTable<uint8_t, kSegmentEnds>& ends = *segmentColours;
    auto pack = [&](int16_t c, int16_t s, int32_t r) {
        return (uint32_t((mul32(c, r) >> 12) + sx) & 0xffffu) | (uint32_t((mul32(s, r) >> 12) + sy) << 16);
    };
    int16_t c0 = sin64(kFirstIndex), s0 = cos64(kFirstIndex);
    for (int32_t n = kLastSegment, idx = int32_t(kFirstIndex);; --n) {
        ++idx;
        const int16_t c1 = sin64(uint32_t(idx)), s1 = cos64(uint32_t(idx));
        uint32_t in0 = pack(c0, s0, r0), in1 = pack(c1, s1, r0);
        if (keep(n, in0, in1)) {
            int32_t ka = int32_t(ends[size_t(n) + 1]) * recordWords + 1;
            int32_t kb = int32_t(ends[size_t(n)]) * recordWords + 1;
            int32_t r = r0;
            for (size_t band = Bands; band-- > 0;) {
                r += bands[band];
                const uint32_t out0 = pack(c0, s0, r), out1 = pack(c1, s1, r);
                const uint32_t w[9] = {0,
                                       colours[ka] | kSemiG4Code,
                                       in0,
                                       colours[kb],
                                       in1,
                                       colours[ka - 1],
                                       out0,
                                       colours[kb - 1],
                                       out1};
                screenG4(owner, sp, w, abr);
                in0 = out0;
                in1 = out1;
                --ka;
                --kb;
            }
        }
        c0 = c1;
        s0 = s1;
        if (n == 0) break;
    }
}

// exe 0x417540: the arc drawn around the projected sky point
void skyArcDraw(ControllerActor& actor) {
    SkyArcFields& f = classState<SkyArcFields>(actor);
    const int32_t p[3] = {f.point[0], f.point[1], f.point[2]};
    int32_t o[3];
    const ViewTransform& view = cameraTransform().view;
    transformVectorFloat(view.matrix.get(), p, o); // exe 0x40bbb0 on the camera matrix
    const int32_t x = int32_t(uint32_t(o[0]) + uint32_t(view.translation[0].get()));
    const int32_t y = int32_t(uint32_t(o[1]) + uint32_t(view.translation[1].get()));
    const int32_t z = int32_t(uint32_t(o[2]) + uint32_t(view.translation[2].get()));
    if (z < 1) return;
    const int32_t k = kArcFocal / z;
    const int32_t sx = (mul32(k, x) >> 16) + kCentreX;
    const int32_t sy = (mul32(k, y) >> 16) - (int32_t(player().pos.y.whole) >> kPlayerHeightShift) + kCentreY;
    if (uint16_t(sx + kVisibleX) > kVisibleWidth) return;
    if (uint16_t(sy + kVisibleY) > kVisibleHeight) return;
    const int32_t r0 = mul32(k, kArcRadius) >> 16;
    const ScreenPlane sp = screenPlane(sx, sy);
    // pass 1 (draw mode 0xe1000240: subtractive): the segments on screen set their mask bit
    uint32_t mask = 0;
    arcPass(actor.at, sp, sx, sy, r0, *innerBands, *innerColours, kInnerRecordWords, kSubtractiveBlend,
            [&](int32_t n, uint32_t in0, uint32_t in1) {
                const int16_t y0 = int16_t(in0 >> 16), x0 = int16_t(uint16_t(in0));
                if (y0 < kKeptTop || y0 > kKeptBottom || x0 < kKeptLeft) return false;
                if (int16_t(uint16_t(in1)) > kKeptRight) return false;
                mask |= 1u << (uint32_t(n) & 0x1f);
                return true;
            });
    // pass 2 (draw mode 0xe1000220: additive) over the kept segments
    arcPass(actor.at, sp, sx, sy, r0, *outerBands, *outerColours, kOuterRecordWords, dc::kAdditiveBlend,
            [&](int32_t n, uint32_t, uint32_t) { return ((mask >> (uint32_t(n) & 0x1f)) & 1u) != 0; });
}

} // namespace

// exe 0x4174f0: the state's routine (exe table 0x889f80)
void skyArcUpdate(const PlayerEnv&, uint32_t address) {
    ControllerActor& actor = controllerActorAt(address);
    if (actor.state >= skyArcStates.size()) notPortedAt(kSkyArcArea, "state", actor.state);
    switch (skyArcStates[actor.state].id()) {
    case RoutineId::SkyArcStart: skyArcStart(actor); return;
    case RoutineId::SkyArcDraw: skyArcDraw(actor); return;
    default: notPortedAt(kSkyArcArea, "state", actor.state);
    }
}

} // namespace od::port
