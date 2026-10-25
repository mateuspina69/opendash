#include "app/ui/hd_art.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#pragma warning(push, 0)
#include <stb_image_write.h>
#pragma warning(pop)
#include <cmath>
#include <cstddef>
#include <functional>

namespace od::app {

using SP = od::port::ScreenPrim;

// dev (the vector UI's reference): OD_DUMP_ART=<dir> writes each new picture at 1x as <dir>/art_<x>_<y>_<w>x<h>.png
void HdArt::dumpPicture(const std::vector<uint32_t>& px, int w, int h, int x, int y) {
    static const char* dir = std::getenv("OD_DUMP_ART");
    if (!dir) return;
    const std::string name = std::string(dir) + "/art_" + std::to_string(x) + "_" + std::to_string(y) + "_" +
                             std::to_string(w) + "x" + std::to_string(h) + ".png";
    if (!dumped_.insert(name).second) return;
    stbi_write_png(name.c_str(), w, h, 4, px.data(), w * 4);
}

void HdArt::reset() {
    pages_.clear();
    shapes_.clear();
}

const std::vector<uint8_t>& HdArt::page(const od::Vram& vram, uint16_t tpage, uint16_t clut) {
    const uint32_t key = uint32_t(tpage & 0x19f) << 16 | clut;
    auto it = pages_.find(key);
    if (it == pages_.end()) it = pages_.emplace(key, vram.decode(uint16_t(tpage & 0x19f), clut)).first;
    return it->second;
}

bool HdArt::compose(const od::Vram& vram, const SP* prims, size_t n, int& bx, int& by, int& w, int& h,
                    std::vector<uint32_t>& px) {
    int x0 = 1 << 20, y0 = 1 << 20, x1 = -(1 << 20), y1 = -(1 << 20);
    auto grow = [&](int ax, int ay, int bx_, int by_) {
        x0 = std::min(x0, ax);
        y0 = std::min(y0, ay);
        x1 = std::max(x1, bx_);
        y1 = std::max(y1, by_);
    };
    for (size_t i = 0; i < n; ++i) {
        const SP& p = prims[i];
        if (p.semi) return false; // blended pieces: not flat colours
        if (p.kind == SP::Sprite) {
            if (p.half || p.grab || p.model >= 0) return false;
            grow(p.x, p.y, p.x + p.w, p.y + p.h);
        } else if (p.kind == SP::Flat) {
            grow(p.x, p.y, p.x + p.w, p.y + p.h);
        } else if (p.kind == SP::Quad) { // the stretched pieces of a 9-slice: axis-aligned rects
            if (!p.textured || p.qx[0] != p.qx[2] || p.qx[1] != p.qx[3] || p.qy[0] != p.qy[1] || p.qy[2] != p.qy[3])
                return false;
            grow(std::min(p.qx[0], p.qx[1]), std::min(p.qy[0], p.qy[2]), std::max(p.qx[0], p.qx[1]),
                 std::max(p.qy[0], p.qy[2]));
        } else {
            return false;
        }
    }
    w = x1 - x0;
    h = y1 - y0;
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) return false;
    bx = x0;
    by = y0;
    px.assign(size_t(w) * size_t(h), 0u);
    auto modulate = [](uint32_t texel, const SP& p) {
        if (p.raw || p.colour == 0x808080) return texel;
        uint32_t r = texel & 0xff000000u;
        for (int k = 0; k < 24; k += 8)
            r |= std::min<uint32_t>(((texel >> k) & 0xff) * (((p.colour >> k) & 0xff) * 2) / 255, 255) << k;
        return r;
    };
    auto put = [&](int x, int y, uint32_t c) {
        if ((c >> 24) == 0) return; // PS1 colour 0: transparent
        const int X = x - bx, Y = y - by;
        if (X >= 0 && Y >= 0 && X < w && Y < h) px[size_t(Y) * size_t(w) + size_t(X)] = c;
    };
    auto texel = [](const std::vector<uint8_t>& pg, int u, int v) {
        const size_t k = (size_t(v & 255) * 256 + size_t(u & 255)) * 4;
        return uint32_t(pg[k]) | uint32_t(pg[k + 1]) << 8 | uint32_t(pg[k + 2]) << 16 | uint32_t(pg[k + 3]) << 24;
    };
    for (size_t i = 0; i < n; ++i) {
        const SP& p = prims[i];
        if (p.kind == SP::Flat) {
            for (int y = 0; y < p.h; ++y)
                for (int x = 0; x < p.w; ++x) put(p.x + x, p.y + y, 0xff000000u | (p.colour & 0xffffff));
        } else if (p.kind == SP::Sprite) {
            const auto& pg = page(vram, p.tpage, p.clut);
            for (int y = 0; y < p.h; ++y)
                for (int x = 0; x < p.w; ++x) put(p.x + x, p.y + y, modulate(texel(pg, p.u + x, p.v + y), p));
        } else { // Quad: nearest texel of the corners' range (as the GPU interpolates u0 .. u1 over the rect)
            const auto& pg = page(vram, p.tpage, p.clut);
            const int qx0 = std::min(p.qx[0], p.qx[1]), qx1 = std::max(p.qx[0], p.qx[1]);
            const int qy0 = std::min(p.qy[0], p.qy[2]), qy1 = std::max(p.qy[0], p.qy[2]);
            const int u0 = p.qu[0], u1 = p.qu[1], v0 = p.qv[0], v1 = p.qv[2];
            for (int y = qy0; y < qy1; ++y)
                for (int x = qx0; x < qx1; ++x) {
                    const int u = u0 + int(std::floor((float(x - qx0) + 0.5f) * float(u1 - u0) / float(qx1 - qx0)));
                    const int v = v0 + int(std::floor((float(y - qy0) + 0.5f) * float(v1 - v0) / float(qy1 - qy0)));
                    put(x, y, modulate(texel(pg, u, v), p));
                }
        }
    }
    return true;
}

namespace {

struct Run {
    uint32_t c;
    int n;
    bool operator==(const Run& o) const { return c == o.c && n == o.n; }
};
struct Profile { // a side's colour runs from the outside in, after `offset` transparent pixels
    int offset = 0;
    std::vector<Run> runs;
    bool operator==(const Profile& o) const { return offset == o.offset && runs == o.runs; }
};

// the convex outline (clockwise, y down) of rect l t r b with per-corner shapes (TL TR BR BL: size > 0 = a 45-degree
// cut, or an arc when round)
std::vector<ui::Vec2> outline(float l, float t, float r, float b, const float size[4], const bool round[4]) {
    std::vector<ui::Vec2> p;
    const float cx[4] = {l, r, r, l}, cy[4] = {t, t, b, b};
    const float sx[4] = {1, -1, -1, 1}, sy[4] = {1, 1, -1, -1}; // toward the inside
    const float maxc = std::max(0.0f, std::min(r - l, b - t) * 0.5f);
    for (int k = 0; k < 4; ++k) {
        const float c = std::min(size[k], maxc);
        if (c <= 0.01f) {
            p.push_back({cx[k], cy[k]});
            continue;
        }
        // TL: from its left side to its top side; TR: top -> right; BR: right -> bottom; BL: bottom -> left
        const ui::Vec2 a = (k % 2 == 0) ? ui::Vec2(cx[k], cy[k] + sy[k] * c) : ui::Vec2(cx[k] + sx[k] * c, cy[k]);
        const ui::Vec2 e = (k % 2 == 0) ? ui::Vec2(cx[k] + sx[k] * c, cy[k]) : ui::Vec2(cx[k], cy[k] + sy[k] * c);
        if (!round[k]) {
            p.push_back(a);
            p.push_back(e);
            continue;
        }
        const ui::Vec2 o(cx[k] + sx[k] * c, cy[k] + sy[k] * c);
        const float a0 = std::atan2(a.y - o.y, a.x - o.x);
        float a1 = std::atan2(e.y - o.y, e.x - o.x);
        while (a1 < a0) a1 += 6.2831853f;
        const int seg = std::clamp(int(c * 2.0f), 3, 12);
        for (int i = 0; i <= seg; ++i) {
            const float t = a0 + (a1 - a0) * float(i) / float(seg);
            p.push_back({o.x + std::cos(t) * c, o.y + std::sin(t) * c});
        }
    }
    return p;
}

bool inside(const std::vector<ui::Vec2>& p, float x, float y) { // clockwise (y down) convex polygon
    for (size_t i = 0; i < p.size(); ++i) {
        const ui::Vec2 a = p[i], b = p[(i + 1) % p.size()];
        if ((b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x) < 0.0f) return false;
    }
    return true;
}

} // namespace

void HdArt::fit(const std::vector<uint32_t>& px, int w, int h, Shapes& out) {
    auto at = [&](int x, int y) -> uint32_t {
        return x < 0 || y < 0 || x >= w || y >= h ? 0u : px[size_t(y) * size_t(w) + size_t(x)];
    };
    // the panels: 4-connected opaque regions
    std::vector<int> label(px.size(), -1);
    struct Comp {
        int x0, y0, x1, y1, count = 0;
    };
    std::vector<Comp> comps;
    std::vector<int> stack;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (!at(x, y) || label[size_t(y) * size_t(w) + size_t(x)] >= 0) continue;
            const int id = int(comps.size());
            comps.push_back({x, y, x, y});
            Comp& c = comps.back();
            stack.assign(1, y * w + x);
            label[size_t(y) * size_t(w) + size_t(x)] = id;
            while (!stack.empty()) {
                const int k = stack.back();
                stack.pop_back();
                const int kx = k % w, ky = k / w;
                ++c.count;
                c.x0 = std::min(c.x0, kx);
                c.y0 = std::min(c.y0, ky);
                c.x1 = std::max(c.x1, kx);
                c.y1 = std::max(c.y1, ky);
                const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (const auto& d : nb) {
                    const int nx = kx + d[0], ny = ky + d[1];
                    if (!at(nx, ny) || label[size_t(ny) * size_t(w) + size_t(nx)] >= 0) continue;
                    label[size_t(ny) * size_t(w) + size_t(nx)] = id;
                    stack.push_back(ny * w + nx);
                }
            }
        }

    // the pieces: a region's parts joined by thin necks (a tab between two boxes) are cut apart at the rows / columns
    // much narrower than the part's widest one (those rows / columns stay pixels)
    struct Piece {
        int id, x0, y0, x1, y1;
    };
    std::vector<Piece> pieces;
    auto opaqueIn = [&](const Piece& p, int x, int y) {
        return x >= p.x0 && x <= p.x1 && y >= p.y0 && y <= p.y1 && label[size_t(y) * size_t(w) + size_t(x)] == p.id;
    };
    std::function<void(Piece, int)> split = [&](Piece p, int depth) {
        for (int axis = 0; axis < 2; ++axis) { // 0: cut between rows, 1: between columns
            const int n = axis == 0 ? p.y1 - p.y0 + 1 : p.x1 - p.x0 + 1, m = axis == 0 ? p.x1 - p.x0 + 1 : p.y1 - p.y0 + 1;
            std::vector<int> width(size_t(n), 0);
            int widest = 0;
            for (int i = 0; i < n; ++i) {
                for (int j = 0; j < m; ++j)
                    if (axis == 0 ? opaqueIn(p, p.x0 + j, p.y0 + i) : opaqueIn(p, p.x0 + i, p.y0 + j)) ++width[size_t(i)];
                widest = std::max(widest, width[size_t(i)]);
            }
            std::vector<std::pair<int, int>> spans; // the runs of rows / columns at least half the widest
            for (int i = 0; i < n;) {
                if (width[size_t(i)] * 2 < widest) {
                    ++i;
                    continue;
                }
                int k = i;
                while (k < n && width[size_t(k)] * 2 >= widest) ++k;
                spans.push_back({i, k - 1});
                i = k;
            }
            if (spans.size() == 1 && spans[0].first == 0 && spans[0].second == n - 1) continue; // nothing to cut
            if (depth > 4) break;
            for (const auto& [a, b] : spans) {
                Piece q = p;
                if (axis == 0) q.y0 = p.y0 + a, q.y1 = p.y0 + b;
                else q.x0 = p.x0 + a, q.x1 = p.x0 + b;
                split(q, depth + 1);
            }
            return;
        }
        pieces.push_back(p);
    };
    for (size_t id = 0; id < comps.size(); ++id) {
        const Comp& c = comps[id];
        split({int(id), c.x0, c.y0, c.x1, c.y1}, 0);
    }

    for (const Piece& pc : pieces) {
        // the piece's own bounds (its opaque pixels)
        Comp c{pc.x1, pc.y1, pc.x0, pc.y0};
        for (int y = pc.y0; y <= pc.y1; ++y)
            for (int x = pc.x0; x <= pc.x1; ++x)
                if (opaqueIn(pc, x, y)) {
                    ++c.count;
                    c.x0 = std::min(c.x0, x);
                    c.y0 = std::min(c.y0, y);
                    c.x1 = std::max(c.x1, x);
                    c.y1 = std::max(c.y1, y);
                }
        const int cw = c.x1 - c.x0 + 1, ch = c.y1 - c.y0 + 1;
        // icons (a clock, a coin: small) and sparse regions keep their pixels
        if (c.count == 0 || cw < 24 || ch < 16 || c.count * 2 < cw * ch) continue;
        auto colour = [&](int x, int y) { return opaqueIn(pc, x, y) ? at(x, y) : 0u; };
        const size_t firstShape = out.shapes.size();
        // the inside colour: the most common one of the middle
        std::map<uint32_t, int> hist;
        for (int y = c.y0 + ch * 3 / 10; y <= c.y1 - ch * 3 / 10; ++y)
            for (int x = c.x0 + cw * 3 / 10; x <= c.x1 - cw * 3 / 10; ++x)
                if (const uint32_t v = colour(x, y)) ++hist[v];
        uint32_t fill = 0;
        int best = 0;
        for (const auto& [v, n] : hist)
            if (n > best) best = n, fill = v;
        if (!fill) continue;
        // the four sides' profiles (the most common over the side's middle half): 0 left, 1 top, 2 right, 3 bottom
        Profile side[4];
        for (int sd = 0; sd < 4; ++sd) {
            const bool alongRows = sd == 0 || sd == 2;
            const int len = alongRows ? ch : cw, depth = alongRows ? cw : ch;
            std::vector<std::pair<Profile, int>> seen;
            for (int i = len / 4; i < len - len / 4; ++i) {
                auto pix = [&](int k) {
                    if (sd == 0) return colour(c.x0 + k, c.y0 + i);
                    if (sd == 2) return colour(c.x1 - k, c.y0 + i);
                    if (sd == 1) return colour(c.x0 + i, c.y0 + k);
                    return colour(c.x0 + i, c.y1 - k);
                };
                Profile pr;
                int d = 0;
                while (d < depth / 2 && pix(d) == 0) ++d;
                pr.offset = d;
                while (d < depth / 2 && pr.runs.size() < 8) {
                    const uint32_t v = pix(d);
                    if (v == fill || v == 0) break;
                    int n = 0;
                    while (d < depth / 2 && pix(d) == v) ++d, ++n;
                    pr.runs.push_back({v, n});
                }
                auto it = std::find_if(seen.begin(), seen.end(), [&](const auto& e) { return e.first == pr; });
                if (it == seen.end()) seen.push_back({pr, 1});
                else ++it->second;
            }
            int bestN = 0;
            for (const auto& [pr, n] : seen)
                if (n > bestN) bestN = n, side[sd] = pr;
        }
        // the bands the four sides share (the same colours in the same order)
        size_t bands = side[0].runs.size();
        for (int sd = 1; sd < 4; ++sd) bands = std::min(bands, side[sd].runs.size());
        for (size_t k = 0; k < bands; ++k)
            for (int sd = 1; sd < 4; ++sd)
                if (side[sd].runs[k].c != side[0].runs[k].c) bands = std::min(bands, k);
        // each shape (the shared bands, then the inside) is the region of its colour and the ones inside it; its
        // corners are measured on the pixels: from the corner of its rect (inset by the widths before it), the rows
        // until its region reaches the side and the first row's inset -- a straight 45-degree run is a cut, anything
        // else a round
        std::vector<uint32_t> cols;
        for (size_t k = 0; k < bands; ++k) cols.push_back(side[0].runs[k].c);
        cols.push_back(fill);
        auto measure = [&](float l, float t, float r, float b, size_t k, float size[4], bool round[4]) {
            auto mine = [&](int x, int y) {
                const uint32_t v = colour(x, y);
                if (!v) return false;
                if (k == 0) return true; // the outer shape: every opaque pixel
                for (size_t j = k; j < cols.size(); ++j)
                    if (cols[j] == v) return true;
                return false;
            };
            const int L = int(l), T = int(t), R = int(r) - 1, B = int(b) - 1;
            const int lim = std::max(0, std::min(R - L + 1, B - T + 1) / 2);
            for (int q = 0; q < 4; ++q) { // TL TR BR BL
                const bool right = q == 1 || q == 2, bottom = q >= 2;
                auto edgeAt = [&](int dy) {
                    const int y = bottom ? B - dy : T + dy;
                    for (int dx = 0; dx < lim; ++dx)
                        if (mine(right ? R - dx : L + dx, y)) return dx;
                    return lim;
                };
                std::vector<int> e;
                while (int(e.size()) < lim) {
                    e.push_back(edgeAt(int(e.size())));
                    if (e.back() == 0) break;
                }
                const int rows = int(e.size()) - 1;
                if (rows <= 0) {
                    size[q] = 0.0f;
                    round[q] = false;
                    continue;
                }
                bool chamfer = std::abs(e[0] - rows) <= 1;
                for (int i = 0; i < rows && chamfer; ++i)
                    chamfer = std::abs(float(e[size_t(i)]) - float(e[0]) * float(rows - i) / float(rows)) <= 1.0f;
                size[q] = float(e[0] + rows) * 0.5f;
                round[q] = !chamfer;
            }
        };
        // the highlight / shade line of a 45-degree corner: the step pixels along a cut (inside the outer shape's,
        // outside an inner shape's: between it and the band around it) in another colour -> a strip 1 px wide
        auto accents = [&](const float g[4], const float size[4], const bool round[4], bool inward, uint32_t a0,
                           uint32_t a1) {
            for (int q = 0; q < 4; ++q) {
                const float cs = std::min(size[q], std::min(g[2] - g[0], g[3] - g[1]) * 0.5f);
                if (round[q] || cs < 2.0f) continue;
                const float cx = (q == 1 || q == 2) ? g[2] : g[0], cy = q >= 2 ? g[3] : g[1];
                const float sx = (q == 1 || q == 2) ? -1.0f : 1.0f, sy = q >= 2 ? -1.0f : 1.0f;
                const float dir = inward ? 0.7071f : -0.7071f;
                const ui::Vec2 a(cx, cy + sy * cs), e(cx + sx * cs, cy), nrm(sx * dir, sy * dir);
                std::map<uint32_t, int> hist;
                int samples = 0;
                for (int i = 0; i <= int(cs); ++i) { // the step pixels along the cut (centres 0.707 px from it)
                    const float f = float(i) / cs;
                    const float px_ = a.x + (e.x - a.x) * f + nrm.x * 0.7071f;
                    const float py_ = a.y + (e.y - a.y) * f + nrm.y * 0.7071f;
                    const uint32_t v = colour(int(std::floor(px_)), int(std::floor(py_)));
                    ++samples;
                    if (v && v != a0 && v != a1) ++hist[v];
                }
                uint32_t accent = 0;
                int best = 0;
                for (const auto& [v, cnt] : hist)
                    if (cnt > best) best = cnt, accent = v;
                if (!accent || best * 5 < samples * 3) continue;
                Shape st;
                st.pts = {a, e, ui::Vec2(e.x + nrm.x, e.y + nrm.y), ui::Vec2(a.x + nrm.x, a.y + nrm.y)};
                st.c = accent;
                out.shapes.push_back(std::move(st));
            }
        };
        float ins[4] = {float(side[0].offset), float(side[1].offset), float(side[2].offset), float(side[3].offset)};
        for (size_t k = 0; k < cols.size(); ++k) {
            const float g[4] = {float(c.x0) + ins[0], float(c.y0) + ins[1], float(c.x1 + 1) - ins[2],
                                float(c.y1 + 1) - ins[3]};
            if (g[2] - g[0] < 1.0f || g[3] - g[1] < 1.0f) break;
            float sz[4];
            bool rd[4];
            measure(g[0], g[1], g[2], g[3], k, sz, rd);
            if (k > 0) accents(g, sz, rd, false, cols[k - 1], cols[k]); // the line between this shape and its band
            Shape sh;
            sh.pts = outline(g[0], g[1], g[2], g[3], sz, rd);
            sh.c = cols[k];
            out.shapes.push_back(std::move(sh));
            if (k == 0) {
                // a fit that paints where the picture is clear (not one panel: an odd outline) is dropped
                int clear = 0;
                for (int y = c.y0; y <= c.y1; ++y)
                    for (int x = c.x0; x <= c.x1; ++x)
                        if (!colour(x, y) && inside(out.shapes.back().pts, float(x) + 0.5f, float(y) + 0.5f)) ++clear;
                if (clear * 33 > c.count) {
                    out.shapes.resize(firstShape);
                    break;
                }
                accents(g, sz, rd, true, cols[0], cols.size() > 1 ? cols[1] : cols[0]); // the outer highlight
            }
            if (k < bands)
                for (int sd = 0; sd < 4; ++sd) ins[sd] += float(side[sd].runs[k].n);
        }
    }
    // the shapes' colour at a point: the innermost shape holding it (they nest in drawing order)
    auto modelAt = [&](float fx, float fy) -> uint32_t {
        for (size_t k = out.shapes.size(); k-- > 0;)
            if (inside(out.shapes[k].pts, fx, fy)) return out.shapes[k].c;
        return 0u;
    };
    std::vector<uint32_t> model(px.size(), 0u);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) model[size_t(y) * size_t(w) + size_t(x)] = modelAt(float(x) + 0.5f, float(y) + 0.5f);
    // the pixels the shapes do not explain (their colour is not the shapes' within 0.75 px): crisp runs
    static const float kOff[8][2] = {{-0.75f, 0}, {0.75f, 0}, {0, -0.75f}, {0, 0.75f},
                                     {-0.75f, -0.75f}, {0.75f, -0.75f}, {-0.75f, 0.75f}, {0.75f, 0.75f}};
    auto unexplained = [&](int x, int y) {
        const uint32_t v = at(x, y);
        if (v == 0 || v == model[size_t(y) * size_t(w) + size_t(x)]) return false;
        for (const auto& o : kOff)
            if (modelAt(float(x) + 0.5f + o[0], float(y) + 0.5f + o[1]) == v) return false;
        return true;
    };
    std::vector<Rect> runs;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w;) {
            if (!unexplained(x, y)) {
                ++x;
                continue;
            }
            const uint32_t v = at(x, y);
            int n = 1;
            while (x + n < w && at(x + n, y) == v && unexplained(x + n, y)) ++n;
            runs.push_back({x, y, n, 1, ui::Color(v)});
            x += n;
        }
    // runs repeated on the next rows: one rect
    for (const Rect& r : runs) {
        bool merged = false;
        for (auto it = out.pixels.rbegin(); it != out.pixels.rend() && it->y + it->h >= r.y; ++it)
            if (it->x == r.x && it->w == r.w && it->c == r.c && it->y + it->h == r.y) {
                ++it->h;
                merged = true;
                break;
            }
        if (!merged) out.pixels.push_back(r);
    }
}

bool HdArt::draw(ui::Canvas& cv, const od::Vram& vram, const SP* prims, size_t n, float s, ui::Vec2 o) {
    int bx = 0, by = 0, w = 0, h = 0;
    std::vector<uint32_t> px;
    if (!compose(vram, prims, n, bx, by, w, h, px)) return false;
    dumpPicture(px, w, h, bx, by);
    uint64_t key = 1469598103934665603ull; // FNV-1a of the picture
    auto mix = [&](uint64_t v) {
        key ^= v;
        key *= 1099511628211ull;
    };
    mix(uint64_t(uint32_t(bx)) << 32 | uint32_t(by));
    mix(uint64_t(uint32_t(w)) << 32 | uint32_t(h));
    for (const uint32_t v : px) mix(v);
    auto it = shapes_.find(key);
    if (it == shapes_.end()) {
        if (shapes_.size() > 256) shapes_.clear();
        Shapes sh;
        fit(px, w, h, sh);
        sh.x = bx;
        sh.y = by;
        it = shapes_.emplace(key, std::move(sh)).first;
    }
    const Shapes& sh = it->second;
    auto P = [&](float x, float y) { return ui::Vec2(o.x + (float(sh.x) + x) * s, o.y + (float(sh.y) + y) * s); };
    std::vector<ui::Vec2> pts;
    for (const Shape& q : sh.shapes) {
        pts.clear();
        for (const ui::Vec2& p : q.pts) pts.push_back(P(p.x, p.y));
        cv.convexFillAA(pts.data(), int(pts.size()), q.c);
    }
    for (const Rect& r : sh.pixels) cv.rectFilled(P(float(r.x), float(r.y)), P(float(r.x + r.w), float(r.y + r.h)), r.c);
    return true;
}

} // namespace od::app
