// GPU-free render logic: normals, jitter, render sizes, per-object interpolation / motion history, AA names.
#include "check.hpp"
#include "render/render_logic.hpp"

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

int main() {
    face_normal();
    jitter_sequence();
    render_sizes();
    interpolate_basic();
    interpolate_spawn_same_total();
    interpolate_vertex_count_change();
    interpolate_layout_change();
    history_new_id_zero_motion();
    motion_static_camera_zero();
    motion_pan_pixels();
    aa_names();
    backend_order();
    return testResult("test_render_logic");
}
