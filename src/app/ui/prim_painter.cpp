#include "app/ui/prim_painter.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "app/frame_grab.hpp"
#include "app/model_preview.hpp"

namespace od::app {

ui::Color FlatPaint::rgb(uint32_t c, int a) { return ui::rgba(c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff, a); }

void FlatPaint::line(ui::Canvas& cv, float s, ui::Vec2 o, int x0, int y0, int x1, int y1, ui::Color col) {
    auto px = [&](int x, int y, int n) { // n pixels of a horizontal / vertical run
        cv.rectFilled(ui::Vec2(o.x + x * s, o.y + y * s), ui::Vec2(o.x + (x + 1) * s, o.y + (y + n) * s), col);
    };
    if (x0 == x1) {
        px(x0, std::min(y0, y1), std::abs(y1 - y0) + 1);
        return;
    }
    if (y0 == y1) {
        const int a = std::min(x0, x1);
        cv.rectFilled(ui::Vec2(o.x + a * s, o.y + y0 * s), ui::Vec2(o.x + (a + std::abs(x1 - x0) + 1) * s, o.y + (y0 + 1) * s),
                          col);
        return;
    }
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int e = dx + dy;
    for (int guard = 0; guard < 2048; ++guard) {
        px(x0, y0, 1);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * e;
        if (e2 >= dy) {
            e += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            e += dx;
            y0 += sy;
        }
    }
}

void FlatPaint::tri(ui::Canvas& cv, float s, ui::Vec2 o, const od::port::ScreenPrim& p) {
    const int a = p.semi ? 128 : 255;
    auto P = [&](int k) { return ui::Vec2(o.x + p.qx[k] * s, o.y + p.qy[k] * s); };
    cv.triangle(P(0), P(1), P(2), rgb(p.qc[0], a), rgb(p.qc[1], a), rgb(p.qc[2], a));
}

bool FlatPaint::draw(ui::Canvas& cv, float s, ui::Vec2 o, const od::port::ScreenPrim& p) {
    using SP = od::port::ScreenPrim;
    switch (p.kind) {
    case SP::Line: line(cv, s, o, p.qx[0], p.qy[0], p.qx[1], p.qy[1], rgb(p.colour)); return true;
    case SP::Tri: tri(cv, s, o, p); return true;
    case SP::Gouraud: {
        cv.rectFilledMultiColor(ui::Vec2(o.x + p.qx[0] * s, o.y + p.qy[0] * s), ui::Vec2(o.x + p.qx[3] * s, o.y + p.qy[3] * s),
                                    rgb(p.qc[0]), rgb(p.qc[1]), rgb(p.qc[3]), rgb(p.qc[2]));
        return true;
    }
    default: return false;
    }
}

void FlatPaint::band(ui::Canvas& cv, float s, ui::Vec2 o, const std::vector<ui::Vec2>& pts, bool closed,
                     ui::Color col) {
    const size_t n = pts.size();
    if (n < 2) return;
    const size_t segs = closed ? n : n - 1;
    struct Seg {
        ui::Vec2 a, b, u, nrm;
        float d;
    };
    std::vector<Seg> sg;
    for (size_t i = 0; i < segs; ++i) {
        const ui::Vec2 a = pts[i], b = pts[(i + 1) % n];
        const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6f) continue;
        const ui::Vec2 u(dx / len, dy / len);
        sg.push_back({a, b, u, ui::Vec2(-u.y, u.x), 0.5f});
    }
    if (sg.empty()) return;
    // the offset lines' meeting point at the joint of segments p -> q (side +1 / -1)
    auto meet = [](const Seg& p, const Seg& q, float side) {
        const ui::Vec2 pa(p.b.x + p.nrm.x * p.d * side, p.b.y + p.nrm.y * p.d * side);
        const ui::Vec2 qa(q.a.x + q.nrm.x * q.d * side, q.a.y + q.nrm.y * q.d * side);
        const float den = p.u.x * q.u.y - p.u.y * q.u.x;
        if (std::fabs(den) < 1e-6f) return pa; // straight on
        const float t = ((qa.x - pa.x) * q.u.y - (qa.y - pa.y) * q.u.x) / den;
        return ui::Vec2(pa.x + p.u.x * t, pa.y + p.u.y * t);
    };
    auto cap = [](const Seg& g, ui::Vec2 at, float dir, float side) { // an open end: half a pixel past along the axis
        const float e = 0.5f / std::max(std::fabs(g.u.x), std::fabs(g.u.y));
        return ui::Vec2(at.x + g.nrm.x * g.d * side + g.u.x * e * dir, at.y + g.nrm.y * g.d * side + g.u.y * e * dir);
    };
    const size_t m = sg.size();
    auto P = [&](ui::Vec2 v) { return ui::Vec2(o.x + v.x * s, o.y + v.y * s); };
    for (size_t i = 0; i < m; ++i) {
        const Seg& g = sg[i];
        const bool first = !closed && i == 0, last = !closed && i + 1 == m;
        const ui::Vec2 l0 = first ? cap(g, g.a, -1, 1) : meet(sg[(i + m - 1) % m], g, 1);
        const ui::Vec2 r0 = first ? cap(g, g.a, -1, -1) : meet(sg[(i + m - 1) % m], g, -1);
        const ui::Vec2 l1 = last ? cap(g, g.b, 1, 1) : meet(g, sg[(i + 1) % m], 1);
        const ui::Vec2 r1 = last ? cap(g, g.b, 1, -1) : meet(g, sg[(i + 1) % m], -1);
        const ui::Vec2 q[4] = {P(l0), P(l1), P(r1), P(r0)};
        cv.convexFillAA(q, 4, col, 0.0f);
    }
}

std::vector<ui::Vec2> FlatPaint::roundCuts(const std::vector<ui::Vec2>& pts) {
    const size_t n = pts.size();
    std::vector<ui::Vec2> out;
    for (size_t i = 0; i < n; ++i) {
        const ui::Vec2 a = pts[i], b = pts[(i + 1) % n], p = pts[(i + n - 1) % n], q = pts[(i + 2) % n];
        const float dx = b.x - a.x, dy = b.y - a.y;
        // a frame's corner cut: 45 degrees between a horizontal and a vertical edge (else as drawn: a triangle's
        // slant stays straight)
        const bool inV = std::fabs(a.x - p.x) < 1e-3f, inH = std::fabs(a.y - p.y) < 1e-3f;
        const bool outV = std::fabs(q.x - b.x) < 1e-3f, outH = std::fabs(q.y - b.y) < 1e-3f;
        const bool cut = n >= 8 && std::fabs(dx) > 0.5f && std::fabs(std::fabs(dx) - std::fabs(dy)) < 1e-3f &&
                         ((inV && outH) || (inH && outV));
        out.push_back(a);
        if (!cut) continue;
        // the edge into a is vertical: the centre is level with a, under / over b (else the other way round)
        const bool vertIn = std::fabs(a.x - p.x) < 1e-3f;
        const ui::Vec2 c = vertIn ? ui::Vec2(b.x, a.y) : ui::Vec2(a.x, b.y);
        const float r = std::fabs(dx);
        const float a0 = std::atan2(a.y - c.y, a.x - c.x), a1 = std::atan2(b.y - c.y, b.x - c.x);
        float da = a1 - a0;
        while (da > 3.14159265f) da -= 6.28318531f;
        while (da < -3.14159265f) da += 6.28318531f;
        const int k = 12;
        for (int j = 1; j < k; ++j) {
            const float t = a0 + da * float(j) / float(k);
            out.emplace_back(c.x + r * std::cos(t), c.y + r * std::sin(t));
        }
    }
    return out;
}

void FlatPaint::drawAll(ui::Canvas& cv, float s, ui::Vec2 o, const std::vector<od::port::ScreenPrim>& prims, bool vector) {
    for (size_t i = 0; i < prims.size();) i = drawRun(cv, s, o, prims, i, vector);
}

size_t FlatPaint::drawRun(ui::Canvas& cv, float s, ui::Vec2 o, const std::vector<od::port::ScreenPrim>& prims, size_t i,
                          bool vector) {
    using SP = od::port::ScreenPrim;
    {
        const SP& p = prims[i];
        size_t fan = i; // a fill fan: >= 3 tris round one centre
        while (vector && fan < prims.size() && prims[fan].kind == SP::Tri && prims[fan].qx[2] == p.qx[2] &&
               prims[fan].qy[2] == p.qy[2])
            ++fan;
        if (p.kind == SP::Tri && fan - i >= 3) { // its rim (the tris' first corners) rounded at the cuts
            const size_t j = fan;
            std::vector<ui::Vec2> rim;
            for (size_t k = i; k < j; ++k) {
                const ui::Vec2 v(prims[k].qx[0], prims[k].qy[0]);
                if (rim.empty() || rim.back().x != v.x || rim.back().y != v.y) rim.push_back(v);
            }
            if (rim.size() > 1 && rim.front().x == rim.back().x && rim.front().y == rim.back().y) rim.pop_back();
            const int passes = std::max<int>(1, int(j - i) / std::max<int>(1, int(rim.size())));
            const std::vector<ui::Vec2> rr = roundCuts(rim);
            const int a = p.semi ? 128 : 255;
            const ui::Vec2 cc(o.x + p.qx[2] * s, o.y + p.qy[2] * s);
            for (int pass = 0; pass < passes; ++pass)
                for (size_t k = 0; k < rr.size(); ++k) {
                    const ui::Vec2 v0 = rr[k], v1 = rr[(k + 1) % rr.size()];
                    cv.triangle(ui::Vec2(o.x + v0.x * s, o.y + v0.y * s), ui::Vec2(o.x + v1.x * s, o.y + v1.y * s), cc,
                                rgb(p.qc[0], a), rgb(p.qc[1], a), rgb(p.qc[2], a));
                }
            return j;
        }
        if (!vector || p.kind != SP::Line) {
            draw(cv, s, o, p);
            return i + 1;
        }
        // the run of lines joined end to start in one colour
        std::vector<ui::Vec2> pts{ui::Vec2(p.qx[0] + 0.5f, p.qy[0] + 0.5f), ui::Vec2(p.qx[1] + 0.5f, p.qy[1] + 0.5f)};
        size_t j = i + 1;
        while (j < prims.size() && prims[j].kind == SP::Line && prims[j].colour == p.colour &&
               prims[j].qx[0] == prims[j - 1].qx[1] && prims[j].qy[0] == prims[j - 1].qy[1]) {
            pts.emplace_back(prims[j].qx[1] + 0.5f, prims[j].qy[1] + 0.5f);
            ++j;
        }
        const bool closed = pts.size() > 3 && pts.front().x == pts.back().x && pts.front().y == pts.back().y;
        if (closed) {
            pts.pop_back();
            pts = roundCuts(pts);
        }
        band(cv, s, o, pts, closed, rgb(p.colour));
        return j;
    }
}

bool PrimPainter::overlap(const std::array<int, 4>& r, int x, int y, int w, int h) {
    return r[0] < x + w && x < r[0] + r[2] && r[1] < y + h && y < r[1] + r[3];
}

void PrimPainter::invalidate(std::vector<std::array<int, 4>>& dirty) {
    if (!dirty.empty()) art.reset();
    for (const auto& r : dirty)
        for (auto it = tex.begin(); it != tex.end();) {
            const uint16_t tp = uint16_t(it->first >> 16), cl = uint16_t(it->first);
            const int mode = (tp >> 7) & 3;
            const int px = (tp & 0xf) * 64, py = ((tp >> 4) & 1) * 256, pw = mode == 0 ? 64 : mode == 1 ? 128 : 256;
            const int cx = (cl & 0x3f) * 16, cy = (cl >> 6) & 0x1ff, cw = mode == 0 ? 16 : 256;
            if (overlap(r, px, py, pw, 256) || (mode < 2 && overlap(r, cx, cy, cw, 1))) {
                stale.push_back(it->second);
                it = tex.erase(it);
            } else {
                ++it;
            }
        }
    dirty.clear();
}

od::TextureHandle PrimPainter::page(od::Renderer& r, const od::Vram& vram, uint16_t tpage, uint16_t clut) {
    const uint32_t key = uint32_t(tpage & 0x19f) << 16 | clut;
    if (const auto it = tex.find(key); it != tex.end()) return it->second;
    const auto rgba = vram.decode(uint16_t(tpage & 0x19f), clut);
    std::vector<uint32_t> px(1024 * 1024);
    for (size_t y = 0; y < 1024; ++y) {
        const uint8_t* src = &rgba[(y / 4) * 256 * 4];
        uint32_t* dst = &px[y * 1024];
        for (size_t x = 0; x < 1024; ++x) std::memcpy(&dst[x], src + (x / 4) * 4, 4);
    }
    const od::TextureHandle t = r.createTexture(1024, 1024, px.data());
    tex[key] = t;
    return t;
}

void PrimPainter::draw(od::Renderer& r, MenuVram& mv, ui::Canvas& cv, const std::vector<od::port::ScreenPrim>& prims, float s,
              ui::Vec2 o, const std::function<void(const od::port::ScreenPrim&)>& text, float sw, float sh,
              float ext) {
    for (od::TextureHandle t : stale) r.destroyTexture(t);
    stale.clear();
    invalidate(mv.dirty);
    using SP = od::port::ScreenPrim;
    auto P = [&](float x, float y) { return ui::Vec2(o.x + x * s, o.y + y * s); };
    auto col = [](const SP& p, bool textured) {
        const uint32_t c = p.colour;
        auto ch = [&](uint32_t v) { return textured ? (p.raw ? 255u : std::min<uint32_t>(v * 2, 255)) : v; };
        return ui::rgba(ch(c & 0xff), ch((c >> 8) & 0xff), ch((c >> 16) & 0xff), p.semi ? 128 : 255);
    };
    // the frame's clip; a drawing area (Clip) may open up to `ext` px past its sides (widescreen, game/menu_draw)
    const ui::Vec2 c0 = P(-ext, 0), c1 = P(sw + ext, sh);
    cv.pushClip(P(0, 0), P(sw, sh), true);
    for (size_t i = 0; i < prims.size(); ++i) {
        const SP& p = prims[i];
        if (p.hitOnly) continue; // a mouse target only (game/menu_draw otHitRow)
        if (p.label && vector && vector->label(r, cv, p, s, o)) continue;
        if (p.art && art.enabled) { // a container picture: its primitives up to the next id, as vector shapes
            size_t j = i + 1;
            while (j < prims.size() && prims[j].art == p.art) ++j;
            float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f; // the group's bounds
            for (size_t k = i; k < j; ++k) {
                const SP& q = prims[k];
                if (q.kind == SP::Quad) {
                    for (int c = 0; c < 4; ++c) {
                        bx0 = std::min(bx0, float(q.qx[c]));
                        by0 = std::min(by0, float(q.qy[c]));
                        bx1 = std::max(bx1, float(q.qx[c]));
                        by1 = std::max(by1, float(q.qy[c]));
                    }
                } else {
                    bx0 = std::min(bx0, float(q.x));
                    by0 = std::min(by0, float(q.y));
                    bx1 = std::max(bx1, float(q.x + q.w));
                    by1 = std::max(by1, float(q.y + q.h));
                }
            }
            const bool full = p.vec == od::port::kVecStatusPanel; // a whole-screen picture: drawn at the origin
            if ((p.vec && vector &&
                 vector->draw(r, cv, p.vec, full ? 0.0f : bx0, full ? 0.0f : by0, bx1 - bx0, by1 - by0, s, o,
                              p.colour & 0xff)) ||
                art.draw(cv, mv.vram, &prims[i], j - i, s, o)) {
                i = j - 1;
                continue;
            }
        }
        switch (p.kind) {
        case SP::Sprite: {
            if (p.w <= 0 || p.h <= 0) break;
            if (p.model >= 0) { // OpenDash: a GPU model preview (app/model_preview.hpp)
                od::app::paintModelPreview(r, mv.vram, cv, p, s, o);
                break;
            }
            if (p.grab) { // a frame-buffer copy (app/frame_grab.hpp): this frame's 3D picture, not VRAM
                od::app::paintFrameGrab(r, cv, p, s, o);
                break;
            }
            const od::TextureHandle t = page(r, mv.vram, p.tpage, p.clut);
            if (!t) break;
            const float u0 = (p.u + 0.01f) / 256.0f, v0 = (p.v + 0.01f) / 256.0f;
            const int tw = p.half ? p.w * 2 : p.w, th = p.half ? p.h * 2 : p.h; // PC 0x8c: 2w x 2h texels
            const float u1 = (p.u + tw - 0.01f) / 256.0f, v1 = (p.v + th - 0.01f) / 256.0f;
            // corners on whole window pixels: tiles meet without seams at a fractional scale (e.g. 16:10)
            auto R = [](ui::Vec2 v) { return ui::Vec2(std::floor(v.x + 0.5f), std::floor(v.y + 0.5f)); };
            cv.image(t, R(P(p.x, p.y)), R(P(float(p.x + p.w), float(p.y + p.h))), ui::Vec2(u0, v0),
                         ui::Vec2(u1, v1), col(p, true));
            break;
        }
        case SP::Flat:
            cv.rectFilled(P(p.x, p.y), P(float(p.x + p.w), float(p.y + p.h)), col(p, false));
            break;
        case SP::Gouraud: {
            auto g = [](uint32_t c) { return ui::rgba(c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff, 255); };
            cv.rectFilledMultiColor(P(p.qx[0], p.qy[0]), P(p.qx[3], p.qy[3]), g(p.qc[0]), g(p.qc[1]), g(p.qc[3]),
                                        g(p.qc[2]));
            break;
        }
        case SP::Quad: {
            const ui::Vec2 a = P(p.qx[0], p.qy[0]), b = P(p.qx[1], p.qy[1]), c = P(p.qx[3], p.qy[3]), d = P(p.qx[2], p.qy[2]);
            if (!p.textured) {
                cv.quadFilled(a, b, c, d, col(p, false));
                break;
            }
            const od::TextureHandle t = page(r, mv.vram, p.tpage, p.clut);
            if (!t) break;
            auto uv = [&](int k) { return ui::Vec2((p.qu[k] + 0.5f) / 256.0f, (p.qv[k] + 0.5f) / 256.0f); };
            cv.imageQuad(t, a, b, c, d, uv(0), uv(1), uv(3), uv(2), col(p, true));
            break;
        }
        case SP::Clip: {
            cv.popClip();
            const ui::Vec2 a = P(p.x, p.y), b = P(float(p.x + p.w), float(p.y + p.h));
            cv.pushClip(ui::Vec2(std::max(a.x, c0.x), std::max(a.y, c0.y)), ui::Vec2(std::min(b.x, c1.x), std::min(b.y, c1.y)),
                             false);
            break;
        }
        case SP::Line:
        case SP::Tri: // the vector UI: a frame's rings / fill fan at once (FlatPaint::drawRun)
            if (vector && vector->enabled) i = FlatPaint::drawRun(cv, s, o, prims, i, true) - 1;
            else FlatPaint::draw(cv, s, o, p);
            break;
        case SP::Glyph:
        case SP::Text:
            if (text) text(p);
            break;
        }
    }
    cv.popClip();
}

} // namespace od::app
