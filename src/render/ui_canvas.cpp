#include "render/ui_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace od::ui {

void Canvas::begin(float w, float h) {
    w_ = w;
    h_ = h;
    vtx_.clear();
    idx_.clear();
    cmds_.clear();
    clips_.clear();
    clips_.push_back({{0.0f, 0.0f, w, h}});
}

void Canvas::pushClip(Vec2 a, Vec2 b, bool intersect) {
    Clip c{{a.x, a.y, b.x, b.y}};
    if (intersect && !clips_.empty()) {
        const Clip& o = clips_.back();
        c.r[0] = std::max(c.r[0], o.r[0]);
        c.r[1] = std::max(c.r[1], o.r[1]);
        c.r[2] = std::min(c.r[2], o.r[2]);
        c.r[3] = std::min(c.r[3], o.r[3]);
    }
    clips_.push_back(c);
}

void Canvas::popClip() {
    if (clips_.size() > 1) clips_.pop_back();
}

void Canvas::use(TextureId t) {
    const float* r = clips_.back().r;
    if (!cmds_.empty()) {
        Cmd& last = cmds_.back();
        if (last.tex == t && std::memcmp(last.clip, r, sizeof last.clip) == 0) return;
        if (last.count == 0) { // nothing drawn under it: reuse
            last.tex = t;
            std::memcpy(last.clip, r, sizeof last.clip);
            return;
        }
    }
    Cmd c;
    c.tex = t;
    std::memcpy(c.clip, r, sizeof c.clip);
    c.first = uint32_t(idx_.size());
    cmds_.push_back(c);
}

void Canvas::quad(TextureId t, const Vec2* p, const Vec2* uv, const Color* c) {
    use(t);
    const uint32_t b = uint32_t(vtx_.size());
    for (int k = 0; k < 4; ++k) vtx_.push_back({p[k], uv[k], c[k]});
    for (const uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) idx_.push_back(b + i);
    cmds_.back().count += 6;
}

void Canvas::rectFilled(Vec2 a, Vec2 b, Color c) {
    rectFilledMultiColor(a, b, c, c, c, c);
}

void Canvas::rectFilledMultiColor(Vec2 a, Vec2 b, Color tl, Color tr, Color br, Color bl) {
    if (((tl | tr | br | bl) >> 24) == 0) return;
    const Vec2 p[4] = {a, {b.x, a.y}, b, {a.x, b.y}}, uv[4]{};
    const Color c[4] = {tl, tr, br, bl};
    quad(0, p, uv, c);
}

void Canvas::quadFilled(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, Color c) {
    if ((c >> 24) == 0) return;
    const Vec2 p[4] = {p0, p1, p2, p3}, uv[4]{};
    const Color cs[4] = {c, c, c, c};
    quad(0, p, uv, cs);
}

void Canvas::triangle(Vec2 p0, Vec2 p1, Vec2 p2, Color c0, Color c1, Color c2) {
    use(0);
    const uint32_t b = uint32_t(vtx_.size());
    vtx_.push_back({p0, {}, c0});
    vtx_.push_back({p1, {}, c1});
    vtx_.push_back({p2, {}, c2});
    for (uint32_t i = 0; i < 3; ++i) idx_.push_back(b + i);
    cmds_.back().count += 3;
}

void Canvas::line(Vec2 a, Vec2 b, Color c, float thickness) {
    if ((c >> 24) == 0) return;
    float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0f) return;
    dx *= 0.5f * thickness / len;
    dy *= 0.5f * thickness / len;
    quadFilled({a.x + dy, a.y - dx}, {b.x + dy, b.y - dx}, {b.x - dy, b.y + dx}, {a.x - dy, a.y + dx}, c);
}

void Canvas::convexFillAA(const Vec2* pts, int n, Color c, float aa) {
    if (n < 3 || (c >> 24) == 0) return;
    float area = 0.0f;
    for (int i = 0; i < n; ++i) {
        const Vec2 a = pts[i], b = pts[(i + 1) % n];
        area += a.x * b.y - b.x * a.y;
    }
    const float sign = area < 0.0f ? -1.0f : 1.0f; // outward normal = sign * (dy, -dx) of each edge
    use(0);
    const uint32_t base = uint32_t(vtx_.size());
    const Color clear = c & 0x00ffffffu;
    for (int i = 0; i < n; ++i) { // per vertex: the inner point (half the fringe in) and the outer one (half out)
        const Vec2 p = pts[i], a = pts[(i + n - 1) % n], b = pts[(i + 1) % n];
        auto normal = [&](Vec2 u, Vec2 v) {
            float nx = (v.y - u.y) * sign, ny = -(v.x - u.x) * sign;
            const float l = std::sqrt(nx * nx + ny * ny);
            return l > 0.0f ? Vec2(nx / l, ny / l) : Vec2(0.0f, 0.0f);
        };
        const Vec2 n0 = normal(a, p), n1 = normal(p, b);
        float mx = n0.x + n1.x, my = n0.y + n1.y;
        const float d2 = mx * mx + my * my;
        if (d2 > 1e-6f) { // the miter, capped (sharp corners)
            const float k = std::min(1.0f / (d2 * 0.25f), 4.0f) * 0.5f;
            mx *= k;
            my *= k;
        }
        const float h = aa * 0.5f;
        vtx_.push_back({{p.x - mx * h, p.y - my * h}, {}, c});
        vtx_.push_back({{p.x + mx * h, p.y + my * h}, {}, clear});
    }
    for (int i = 2; i < n; ++i) // the inside: a fan of the inner points
        for (const uint32_t k : {0u, uint32_t(i - 1), uint32_t(i)}) idx_.push_back(base + k * 2);
    for (int i = 0; i < n; ++i) { // the fringe: a quad per edge
        const uint32_t j = uint32_t((i + 1) % n), ii = uint32_t(i);
        for (const uint32_t k : {ii * 2, j * 2, j * 2 + 1, ii * 2, j * 2 + 1, ii * 2 + 1}) idx_.push_back(base + k);
    }
    cmds_.back().count += uint32_t((n - 2) * 3 + n * 6);
}

void Canvas::roundRect(Vec2 a, Vec2 b, float r, Color c) {
    r = std::max(0.0f, std::min({r, (b.x - a.x) * 0.5f, (b.y - a.y) * 0.5f}));
    std::vector<Vec2> p;
    const float cx[4] = {b.x - r, b.x - r, a.x + r, a.x + r}, cy[4] = {a.y + r, b.y - r, b.y - r, a.y + r};
    const int seg = r < 0.5f ? 0 : std::clamp(int(r), 3, 10);
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i <= seg; ++i) {
            const float t = 6.2831853f * (float(k) * 0.25f - 0.25f + (seg ? 0.25f * float(i) / float(seg) : 0.125f));
            p.push_back({cx[k] + std::cos(t) * r, cy[k] + std::sin(t) * r});
        }
    convexFillAA(p.data(), int(p.size()), c);
}

void Canvas::disc(Vec2 centre, float r, Color c) {
    const int n = std::clamp(int(r * 1.5f), 12, 64);
    std::vector<Vec2> p;
    for (int i = 0; i < n; ++i) {
        const float t = 6.2831853f * float(i) / float(n);
        p.push_back({centre.x + std::cos(t) * r, centre.y + std::sin(t) * r});
    }
    convexFillAA(p.data(), n, c);
}

void Canvas::image(TextureId t, Vec2 a, Vec2 b, Vec2 uv0, Vec2 uv1, Color c) {
    if ((c >> 24) == 0) return;
    const Vec2 p[4] = {a, {b.x, a.y}, b, {a.x, b.y}}, uv[4] = {uv0, {uv1.x, uv0.y}, uv1, {uv0.x, uv1.y}};
    const Color cs[4] = {c, c, c, c};
    quad(t, p, uv, cs);
}

void Canvas::imageQuad(TextureId t, Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, Vec2 uv0, Vec2 uv1, Vec2 uv2, Vec2 uv3,
                       Color c) {
    if ((c >> 24) == 0) return;
    const Vec2 p[4] = {p0, p1, p2, p3}, uv[4] = {uv0, uv1, uv2, uv3};
    const Color cs[4] = {c, c, c, c};
    quad(t, p, uv, cs);
}

} // namespace od::ui
