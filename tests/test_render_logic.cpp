// GPU-free render logic: normals, jitter, render sizes, per-object interpolation / motion history, AA names, the
// see-through's scenery pieces.
#include "check.hpp"
#include "render/render_logic.hpp"
#include "render/scene_pieces.hpp"

#include <cmath>
#include <string_view>

using namespace od;

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

static RenderVertex vtx(float x, float y, float z) {
    RenderVertex v{};
    v.pos[0] = x;
    v.pos[1] = y;
    v.pos[2] = z;
    return v;
}

static void face_normal() {
    const float a[3] = {0, 0, 0}, b[3] = {1, 0, 0}, c[3] = {0, 0, 1};
    float n[3];
    faceNormal(a, b, c, n);
    CHECK(near(n[0], 0) && near(std::fabs(n[1]), 1) && near(n[2], 0));
    const float d[3] = {2, 0, 0};
    faceNormal(a, b, d, n); // degenerate (collinear): up, never NaN
    CHECK(near(n[0], 0) && near(n[1], 1) && near(n[2], 0));
}

static void jitter_sequence() {
    CHECK(near(halton(1, 2), 0.5f) && near(halton(2, 2), 0.25f) && near(halton(3, 2), 0.75f));
    CHECK(near(halton(1, 3), 1.0f / 3.0f) && near(halton(2, 3), 2.0f / 3.0f));
    const Jitter j0 = jitterAt(0, 8), j8 = jitterAt(8, 8);
    CHECK(near(j0.x, j8.x) && near(j0.y, j8.y)); // periodic
    for (uint64_t f = 0; f < 64; ++f) {
        const Jitter j = jitterAt(f, 32);
        CHECK(j.x >= -0.5f && j.x < 0.5f && j.y >= -0.5f && j.y < 0.5f);
    }
}

static void render_sizes() {
    int w, h;
    renderSize(1920, 1080, AaQuality::NativeAA, w, h);
    CHECK(w == 1920 && h == 1080);
    renderSize(1920, 1080, AaQuality::Quality, w, h);
    CHECK(w == 1280 && h == 720);
    renderSize(1920, 1080, AaQuality::Performance, w, h);
    CHECK(w == 960 && h == 540);
    renderSize(1, 1, AaQuality::Performance, w, h);
    CHECK(w == 1 && h == 1); // never zero
    CHECK(dlssPhaseCount(1920, 1920) == 8 && dlssPhaseCount(960, 1920) == 32);
}

static DynamicObject obj(uint32_t id, std::initializer_list<float> xs) {
    DynamicObject o{id, {}};
    for (float x : xs) o.tris.push_back(vtx(x, 0, 0));
    return o;
}

static void interpolate_basic() {
    const auto r = interpolateObjects({obj(1, {0, 10})}, {obj(1, {10, 20})}, 0.5f);
    CHECK(r.size() == 1 && near(r[0].tris[0].pos[0], 5) && near(r[0].tris[1].pos[0], 15));
}

static void interpolate_spawn_same_total() {
    // A (2 verts) despawns and C (2 verts) spawns: total count unchanged, B must still blend
    const auto r = interpolateObjects({obj(1, {0, 0}), obj(2, {0})}, {obj(2, {10}), obj(3, {7, 7})}, 0.5f);
    CHECK(r.size() == 2);
    CHECK(r[0].id == 2 && near(r[0].tris[0].pos[0], 5)); // blended
    CHECK(r[1].id == 3 && near(r[1].tris[0].pos[0], 7)); // new: current position
}

static void interpolate_vertex_count_change() {
    const auto r = interpolateObjects({obj(1, {0})}, {obj(1, {4, 4})}, 0.5f);
    CHECK(near(r[0].tris[0].pos[0], 4)); // shape changed: no blend
}

static void interpolate_layout_change() {
    // same id and vertex count, other parts drawn (a cutscene head swap, part flags 0x80): no blend, no motion
    DynamicObject a = obj(1, {0, 0}), b = obj(1, {10, 10});
    a.layout = 1;
    b.layout = 2;
    const auto r = interpolateObjects({a}, {b}, 0.5f);
    CHECK(near(r[0].tris[0].pos[0], 10));
    MotionHistory h;
    std::vector<float> prev;
    h.apply({a}, prev);
    h.apply({b}, prev);
    CHECK(prev.size() == 6 && near(prev[0], 10)); // zero motion across the swap
    b.layout = 1;
    CHECK(near(interpolateObjects({a}, {b}, 0.5f)[0].tris[0].pos[0], 5)); // same layout: blended
}

// an effect's list of the given quads (quad q at x = q * 100 + move)
static DynamicObject quadRow(std::initializer_list<int> quads, float move) {
    DynamicObject o{objectId(kKindEffect, 1), {}};
    for (int q : quads)
        for (int v = 0; v < 6; ++v) o.tris.push_back(vtx(float(q) * 100.0f + move, 0, 0));
    return o;
}

static void interpolate_effect_quad_culled_mid_list() {
    // quad 2 of 12 left the view: the quads after it keep their own places (no shift of the whole list fits both ends)
    const DynamicObject prev = quadRow({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, 0.0f);
    const DynamicObject cur = quadRow({0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11}, 1.0f);
    const auto r = interpolateObjects({prev}, {cur}, 0.5f);
    size_t v = 0;
    for (int q : {0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11})
        for (int k = 0; k < 6; ++k) CHECK(near(r[0].tris[v++].pos[0], float(q) * 100.0f + 0.5f));
    MotionHistory h;
    std::vector<float> from;
    h.apply({prev}, from);
    h.apply({cur}, from);
    v = 0;
    for (int q : {0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11})
        for (int k = 0; k < 6; ++k, ++v) CHECK(near(from[v * 3], float(q) * 100.0f));
}

static void interpolate_effect_quad_entering_mid_list() {
    // quad 2 came into view: it has no place a tick ago (shown as is), its neighbours keep theirs
    const DynamicObject prev = quadRow({0, 1, 3, 4}, 0.0f), cur = quadRow({0, 1, 2, 3, 4}, 1.0f);
    const auto r = interpolateObjects({prev}, {cur}, 0.5f);
    CHECK(near(r[0].tris[6].pos[0], 100.5f));
    CHECK(near(r[0].tris[12].pos[0], 201.0f)); // new: current position
    CHECK(near(r[0].tris[18].pos[0], 300.5f));
    CHECK(near(r[0].tris[24].pos[0], 400.5f));
}

static void interpolate_effect_list_replaced() {
    // a camera cut: the list is another part of the wall, but for one quad near one of the old: nothing blends
    const DynamicObject prev = quadRow({0, 1, 2}, 0.0f), cur = quadRow({2, 20, 21, 22}, 1.0f);
    const auto r = interpolateObjects({prev}, {cur}, 0.5f);
    CHECK(near(r[0].tris[0].pos[0], 201.0f));
    CHECK(near(r[0].tris[6].pos[0], 2001.0f));
}

static void interpolate_effect_ring_moving_as_one() {
    // the buster shot (a glow ring of 16 quads) moves further in a tick than its quads lie apart: no quad's nearest quad a
    // tick ago is its own, but the whole list moved by one step and each quad stays in its place in the ring
    const auto ring = [](float x, float z) {
        DynamicObject o{objectId(kKindEffect, 1), {}};
        for (int q = 0; q < 16; ++q) {
            const float a = float(q) * 0.3926991f, r = 40.0f;
            for (int v = 0; v < 6; ++v)
                o.tris.push_back(vtx(x + r * std::cos(a) + float(v), 0.0f, z + r * std::sin(a)));
        }
        return o;
    };
    const float step[][2] = {{15.0f, 0.0f}, {30.0f, 10.0f}, {60.0f, -20.0f}, {0.0f, 120.0f}};
    for (const auto& s : step) {
        const DynamicObject prev = ring(0.0f, 0.0f), cur = ring(s[0], s[1]), half = ring(s[0] * 0.5f, s[1] * 0.5f);
        const auto r = interpolateObjects({prev}, {cur}, 0.5f);
        for (size_t v = 0; v < half.tris.size(); ++v) {
            CHECK(near(r[0].tris[v].pos[0], half.tris[v].pos[0]));
            CHECK(near(r[0].tris[v].pos[2], half.tris[v].pos[2]));
        }
        MotionHistory h;
        std::vector<float> from;
        h.apply({prev}, from);
        h.apply({cur}, from);
        for (size_t v = 0; v < prev.tris.size(); ++v) CHECK(near(from[v * 3], prev.tris[v].pos[0]));
    }
}

static void history_new_id_zero_motion() {
    MotionHistory h;
    std::vector<float> prev;
    h.apply({obj(1, {3})}, prev);
    CHECK(prev.size() == 3 && near(prev[0], 3)); // first sight: prev == cur
    h.apply({obj(1, {5}), obj(2, {9})}, prev);
    CHECK(prev.size() == 6 && near(prev[0], 3) && near(prev[3], 9));
    h.reset();
    h.apply({obj(1, {6})}, prev);
    CHECK(near(prev[0], 6)); // reset: no history
}

static void motion_static_camera_zero() {
    const float I[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float p[3] = {0.25f, -0.5f, 0.5f};
    float mv[2];
    motionPixels(I, I, p, 100, 100, mv);
    CHECK(near(mv[0], 0) && near(mv[1], 0));
}

static void motion_pan_pixels() {
    // prev camera shifted so the point was 0.2 NDC further right/up: +10 px x; NDC y up = screen y down -> -10 px y
    const float cur[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float prv[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0.2f, 0.2f, 0, 1};
    const float p[3] = {0, 0, 0.5f};
    float mv[2];
    motionPixels(cur, prv, p, 100, 100, mv);
    CHECK(near(mv[0], 10.0f) && near(mv[1], -10.0f));
}

static void aa_names() {
    AaBackend b;
    AaQuality q;
    CHECK(parseAaBackend("dlss", b) && b == AaBackend::Dlss);
    CHECK(parseAaBackend("auto", b) && b == AaBackend::Auto);
    CHECK(!parseAaBackend("taa", b));
    CHECK(parseAaQuality("native", q) && q == AaQuality::NativeAA);
    CHECK(!parseAaQuality("ultra", q));
    CHECK(std::string_view(aaBackendName(AaBackend::Xess)) == "xess");
    CHECK(parseAaBackend("ssaa", b) && b == AaBackend::Ssaa);
    CHECK(parseAaBackend("msaa", b) && b == AaBackend::Msaa);
}

static void backend_order() {
    using B = AaBackend;
    CHECK((backendCandidates(B::Auto, {}) == std::vector<B>{B::Dlss, B::Fsr, B::None}));
    CHECK((backendCandidates(B::Xess, {}) == std::vector<B>{B::Xess, B::Dlss, B::Fsr, B::None}));
    // a backend that failed at run time (e.g. DLSS feature creation) is skipped from then on
    CHECK((backendCandidates(B::Dlss, {B::Dlss}) == std::vector<B>{B::Fsr, B::None}));
    CHECK((backendCandidates(B::Auto, {B::Dlss, B::Fsr}) == std::vector<B>{B::None}));
    CHECK((backendCandidates(B::None, {B::Dlss}) == std::vector<B>{B::None}));
}

// a wall of two triangles is one piece; the floor meeting it at a right angle is another; a lone triangle a third
static void scene_pieces() {
    const std::vector<RenderVertex> tris = {
        vtx(0, 0, 0), vtx(4, 0, 0), vtx(4, 4, 0), vtx(0, 0, 0), vtx(4, 4, 0), vtx(0, 4, 0), // wall (z = 0)
        vtx(0, 0, 0), vtx(0, 0, 4), vtx(4, 0, 0),                                         // floor, on the wall's edge
        vtx(9, 0, 9), vtx(10, 0, 9), vtx(9, 1, 9)};                                       // apart
    const ScenePieces sp = scenePieces(tris.data(), tris.size());
    CHECK(sp.of.size() == 4 && sp.box.size() == 3);
    CHECK(sp.of[0] == sp.of[1] && sp.of[2] != sp.of[0] && sp.of[3] != sp.of[0] && sp.of[3] != sp.of[2]);
    CHECK(near(sp.box[sp.of[0]][3], 4) && near(sp.box[sp.of[0]][4], 4) && near(sp.box[sp.of[0]][5], 0));
    // a segment through the wall meets it half way; one that stops short, or misses, does not
    const float a[3] = {0, 0, 0}, b[3] = {4, 0, 0}, c[3] = {0, 4, 0};
    const float o[3] = {1, 1, -2}, through[3] = {0, 0, 4}, shortOf[3] = {0, 0, 1.5f};
    const float miss[3] = {9, 0, 4};
    CHECK(near(segmentHitsTriangle(o, through, a, b, c), 0.5f));
    CHECK(segmentHitsTriangle(o, shortOf, a, b, c) < 0.0f);
    CHECK(segmentHitsTriangle(o, miss, a, b, c) < 0.0f);
}

int main() {
    face_normal();
    jitter_sequence();
    render_sizes();
    interpolate_basic();
    interpolate_spawn_same_total();
    interpolate_vertex_count_change();
    interpolate_layout_change();
    interpolate_effect_quad_culled_mid_list();
    interpolate_effect_quad_entering_mid_list();
    interpolate_effect_list_replaced();
    interpolate_effect_ring_moving_as_one();
    history_new_id_zero_motion();
    motion_static_camera_zero();
    motion_pan_pixels();
    aa_names();
    backend_order();
    scene_pieces();
    return testResult("test_render_logic");
}
