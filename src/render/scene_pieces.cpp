#include "render/scene_pieces.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <unordered_map>

namespace od {

namespace {

constexpr float kPieceCrease = 0.82f; // cos 35 degrees: faces turning more than this meet at a crease (a new piece)
constexpr float kWeld = 8.0f;         // positions closer than 1 / kWeld world units are the same vertex

uint32_t root(std::vector<uint32_t>& parent, uint32_t i) {
    while (parent[i] != i) i = parent[i] = parent[parent[i]];
    return i;
}

} // namespace

ScenePieces scenePieces(const RenderVertex* tris, size_t count) {
    const size_t faces = count / 3;
    ScenePieces out;
    out.of.assign(faces, 0);
    if (!faces) return out;
    // the welded vertices, the faces' unit normals (zero: no area)
    std::map<std::array<long long, 3>, uint32_t> weld;
    std::vector<uint32_t> vert(faces * 3);
    for (size_t k = 0; k < faces * 3; ++k) {
        std::array<long long, 3> q;
        for (int c = 0; c < 3; ++c) q[size_t(c)] = std::llround(tris[k].pos[c] * kWeld);
        vert[k] = weld.emplace(q, uint32_t(weld.size())).first->second;
    }
    std::vector<std::array<float, 3>> normal(faces);
    for (size_t f = 0; f < faces; ++f) {
        const float* a = tris[f * 3].pos;
        const float* b = tris[f * 3 + 1].pos;
        const float* c = tris[f * 3 + 2].pos;
        const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (int i = 0; i < 3; ++i) normal[f][size_t(i)] = len > 1e-6f ? n[i] / len : 0.0f;
    }
    // faces sharing an edge join when they turn less than the crease (either winding: double-sided PS1 faces)
    std::unordered_map<uint64_t, std::vector<uint32_t>> edges;
    for (size_t f = 0; f < faces; ++f)
        for (int e = 0; e < 3; ++e) {
            const uint32_t v0 = vert[f * 3 + size_t(e)], v1 = vert[f * 3 + size_t((e + 1) % 3)];
            if (v0 == v1) continue;
            edges[uint64_t(std::min(v0, v1)) << 32 | std::max(v0, v1)].push_back(uint32_t(f));
        }
    std::vector<uint32_t> parent(faces);
    std::iota(parent.begin(), parent.end(), 0u);
    for (const auto& [edge, list] : edges)
        for (size_t i = 0; i < list.size(); ++i)
            for (size_t j = i + 1; j < list.size(); ++j) {
                const auto& a = normal[list[i]];
                const auto& b = normal[list[j]];
                const float turn = std::abs(a[0] * b[0] + a[1] * b[1] + a[2] * b[2]);
                if (turn < kPieceCrease) continue;
                const uint32_t ra = root(parent, list[i]), rb = root(parent, list[j]);
                if (ra != rb) parent[std::max(ra, rb)] = std::min(ra, rb);
            }
    // the pieces numbered in first-face order, with their bounds
    std::unordered_map<uint32_t, uint32_t> number;
    for (size_t f = 0; f < faces; ++f) {
        const uint32_t r = root(parent, uint32_t(f));
        auto [it, fresh] = number.emplace(r, uint32_t(number.size()));
        out.of[f] = it->second;
        if (fresh) {
            const float* p = tris[f * 3].pos;
            out.box.push_back({p[0], p[1], p[2], p[0], p[1], p[2]});
        }
        std::array<float, 6>& box = out.box[it->second];
        for (int v = 0; v < 3; ++v)
            for (int c = 0; c < 3; ++c) {
                box[size_t(c)] = std::min(box[size_t(c)], tris[f * 3 + size_t(v)].pos[c]);
                box[size_t(c) + 3] = std::max(box[size_t(c) + 3], tris[f * 3 + size_t(v)].pos[c]);
            }
    }
    return out;
}

float segmentHitsTriangle(const float* o, const float* d, const float* a, const float* b, const float* c) {
    const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const float p[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
    const float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (std::abs(det) < 1e-6f) return -1.0f;
    const float inv = 1.0f / det, s[3] = {o[0] - a[0], o[1] - a[1], o[2] - a[2]};
    const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
    if (u < 0.0f || u > 1.0f) return -1.0f;
    const float q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
    const float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
    if (v < 0.0f || u + v > 1.0f) return -1.0f;
    const float t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
    return t > 0.0f && t < 1.0f ? t : -1.0f;
}

} // namespace od
