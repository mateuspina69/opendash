#include "app/room_openness.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace od::app {

namespace {

constexpr int kGrid = 128;           // cells on the room's longer side
constexpr int kMaxSamples = 2500;    // floor points a room
constexpr float kCharacter = 250.0f; // a character's height, game units (world = << shift)

struct Tri {
    const od::RenderVertex* v[3];
};

class Rays {
public:
    explicit Rays(const RoomMesh& mesh) : mesh_(mesh) {
        const size_t opaque = mesh.semiStart[3] ? mesh.semiStart[0] : mesh.tris.size();
        for (size_t i = 0; i + 2 < opaque; i += 3) tris_.push_back({{&mesh.tris[i], &mesh.tris[i + 1], &mesh.tris[i + 2]}});
        for (const std::vector<SegQuad>* quads : {&mesh.segQuads, &mesh.lodFloorQuads})
            for (const SegQuad& q : *quads)
                for (size_t i = 0; i + 2 < q.v.size(); i += 3) tris_.push_back({{&q.v[i], &q.v[i + 1], &q.v[i + 2]}});
        lo_[0] = lo_[1] = 1e9f;
        hi_[0] = hi_[1] = -1e9f;
        for (const Tri& t : tris_)
            for (const od::RenderVertex* v : t.v) {
                lo_[0] = std::min(lo_[0], v->pos[0]);
                lo_[1] = std::min(lo_[1], v->pos[2]);
                hi_[0] = std::max(hi_[0], v->pos[0]);
                hi_[1] = std::max(hi_[1], v->pos[2]);
                top_ = std::max(top_, v->pos[1]);
            }
        cell_ = std::max(std::max(hi_[0] - lo_[0], hi_[1] - lo_[1]) / float(kGrid), 1.0f);
        cells_.resize(size_t(kGrid) * kGrid);
        for (size_t i = 0; i < tris_.size(); ++i) {
            const Tri& t = tris_[i];
            const auto a = std::minmax({t.v[0]->pos[0], t.v[1]->pos[0], t.v[2]->pos[0]});
            const auto b = std::minmax({t.v[0]->pos[2], t.v[1]->pos[2], t.v[2]->pos[2]});
            for (int z = cellOf(b.first, 1); z <= cellOf(b.second, 1); ++z)
                for (int x = cellOf(a.first, 0); x <= cellOf(a.second, 0); ++x) at(x, z).push_back(uint32_t(i));
        }
    }

    bool empty() const { return tris_.empty(); }
    float top() const { return top_; }
    float cellSize() const { return cell_; }
    float cellCentre(int c, int axis) const { return lo_[axis] + (float(c) + 0.5f) * cell_; }

    // the lowest level surface over a cell's centre
    bool floorAt(int x, int z, float& y) const {
        const float px = cellCentre(x, 0), pz = cellCentre(z, 1);
        bool found = false;
        for (const uint32_t i : at(x, z)) {
            const Tri& t = tris_[i];
            const float* a = t.v[0]->pos;
            const float* b = t.v[1]->pos;
            const float* c = t.v[2]->pos;
            const float e0[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e1[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            const float n[3] = {e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2], e0[0] * e1[1] - e0[1] * e1[0]};
            const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (len < 1e-3f || std::fabs(n[1]) < 0.7f * len) continue;
            const float d = e0[0] * e1[2] - e0[2] * e1[0];
            if (std::fabs(d) < 1e-6f) continue;
            const float u = ((px - a[0]) * e1[2] - (pz - a[2]) * e1[0]) / d;
            const float v = ((pz - a[2]) * e0[0] - (px - a[0]) * e0[2]) / d;
            if (u < 0.0f || v < 0.0f || u + v > 1.0f) continue;
            const float h = a[1] + u * e0[1] + v * e1[1];
            if (!found || h < y) y = h;
            found = true;
        }
        return found;
    }

    // something opaque along the ray, inside the room's bounds
    bool hits(const float from[3], const float dir[3]) const {
        const float flat = std::sqrt(dir[0] * dir[0] + dir[2] * dir[2]);
        int lastX = -1, lastZ = -1;
        // marched in half cells: a cell's triangles are tested whole, so nothing thin is stepped over
        const float step = flat > 1e-4f ? cell_ * 0.5f / flat : 0.0f;
        for (float s = 0.0f;; s += step) {
            const float px = from[0] + dir[0] * s, py = from[1] + dir[1] * s, pz = from[2] + dir[2] * s;
            if (px < lo_[0] || px > hi_[0] || pz < lo_[1] || pz > hi_[1] || (dir[1] > 0.0f && py > top_)) return false;
            const int x = cellOf(px, 0), z = cellOf(pz, 1);
            if (x != lastX || z != lastZ) {
                lastX = x;
                lastZ = z;
                for (const uint32_t i : at(x, z))
                    if (blocks(tris_[i], from, dir)) return true;
            }
            if (step == 0.0f) return false; // straight up or down: the one cell
        }
    }

private:
    int cellOf(float p, int axis) const { return std::clamp(int((p - lo_[axis]) / cell_), 0, kGrid - 1); }
    std::vector<uint32_t>& at(int x, int z) { return cells_[size_t(z) * kGrid + size_t(x)]; }
    const std::vector<uint32_t>& at(int x, int z) const { return cells_[size_t(z) * kGrid + size_t(x)]; }

    bool blocks(const Tri& t, const float o[3], const float d[3]) const {
        const float* a = t.v[0]->pos;
        const float e0[3] = {t.v[1]->pos[0] - a[0], t.v[1]->pos[1] - a[1], t.v[1]->pos[2] - a[2]};
        const float e1[3] = {t.v[2]->pos[0] - a[0], t.v[2]->pos[1] - a[1], t.v[2]->pos[2] - a[2]};
        const float p[3] = {d[1] * e1[2] - d[2] * e1[1], d[2] * e1[0] - d[0] * e1[2], d[0] * e1[1] - d[1] * e1[0]};
        const float det = e0[0] * p[0] + e0[1] * p[1] + e0[2] * p[2];
        if (std::fabs(det) < 1e-6f) return false;
        const float s[3] = {o[0] - a[0], o[1] - a[1], o[2] - a[2]};
        const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) / det;
        if (u < 0.0f || u > 1.0f) return false;
        const float q[3] = {s[1] * e0[2] - s[2] * e0[1], s[2] * e0[0] - s[0] * e0[2], s[0] * e0[1] - s[1] * e0[0]};
        const float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) / det;
        if (v < 0.0f || u + v > 1.0f) return false;
        if ((e1[0] * q[0] + e1[1] * q[1] + e1[2] * q[2]) / det < 0.5f) return false;
        const int slice = int(t.v[0]->slice);
        constexpr size_t kPageBytes = 256 * 256 * 4;
        if (slice < 0 || (size_t(slice) + 1) * kPageBytes > mesh_.slices.size()) return true; // untextured
        const float w = 1.0f - u - v;
        const int tx = std::clamp(int((t.v[0]->uv[0] * w + t.v[1]->uv[0] * u + t.v[2]->uv[0] * v) * 256.0f), 0, 255);
        const int ty = std::clamp(int((t.v[0]->uv[1] * w + t.v[1]->uv[1] * u + t.v[2]->uv[1] * v) * 256.0f), 0, 255);
        return mesh_.slices[size_t(slice) * kPageBytes + (size_t(ty) * 256 + size_t(tx)) * 4 + 3] >= 64; // as blocksLight
    }

    const RoomMesh& mesh_;
    std::vector<Tri> tris_;
    std::vector<std::vector<uint32_t>> cells_;
    float lo_[2], hi_[2], top_ = -1e9f, cell_ = 1.0f;
};

} // namespace

bool roomHasNoSky(const RoomMesh& mesh) {
    if (!mesh.bgCleared || mesh.stage == 0x57) return false;
    return std::none_of(mesh.sky.begin(), mesh.sky.end(), [](const SkyLayer& l) { return (l.flags & 2) != 0; });
}

RoomOpenness measureRoomOpenness(const RoomMesh& mesh) {
    RoomOpenness out;
    const Rays rays(mesh);
    if (rays.empty()) return out;
    const float world = float(1 << mesh.shift), character = kCharacter * world;
    struct Point {
        float x, y, z;
    };
    std::vector<Point> floor;
    for (int z = 0; z < kGrid; ++z)
        for (int x = 0; x < kGrid; ++x) {
            float y;
            if (rays.floorAt(x, z, y)) floor.push_back({rays.cellCentre(x, 0), y, rays.cellCentre(z, 1)});
        }
    if (floor.empty()) return out;
    const size_t stride = std::max<size_t>(1, floor.size() / kMaxSamples);
    constexpr float kPi = 3.14159265f, kSunUp = 50.0f * kPi / 180.0f;
    uint64_t covered = 0, sunOut = 0, wallHits = 0, sunRays = 0, wallRays = 0;
    std::vector<float> heights;
    for (size_t i = 0; i < floor.size(); i += stride) {
        const Point& p = floor[i];
        heights.push_back(p.y);
        const float lifted[3] = {p.x, p.y + 8.0f * world, p.z};
        const float up[3] = {0.0f, 1.0f, 0.0f};
        covered += rays.hits(lifted, up) ? 1 : 0;
        const float chest[3] = {p.x, p.y + character * 0.6f, p.z};
        for (int k = 0; k < 8; ++k) {
            const float az = float(k) * kPi / 4.0f;
            const float sun[3] = {std::cos(kSunUp) * std::sin(az), std::sin(kSunUp), std::cos(kSunUp) * std::cos(az)};
            sunOut += rays.hits(lifted, sun) ? 0 : 1;
            ++sunRays;
            const float level[3] = {std::sin(az), 0.0f, std::cos(az)};
            wallHits += rays.hits(chest, level) ? 1 : 0;
            ++wallRays;
        }
    }
    out.samples = int(heights.size());
    out.covered = float(covered) / float(heights.size());
    out.sunReach = float(sunOut) / float(sunRays);
    out.walled = float(wallHits) / float(wallRays);
    std::nth_element(heights.begin(), heights.begin() + std::ptrdiff_t(heights.size() / 2), heights.end());
    out.floorY = heights[heights.size() / 2];
    out.gap = (rays.top() - out.floorY) / character;
    constexpr size_t kPageBytes = 256 * 256 * 4;
    double areaSum = 0.0, lumSum = 0.0, clipSum = 0.0, shadeSum = 0.0;
    const size_t opaque = mesh.semiStart[3] ? mesh.semiStart[0] : mesh.tris.size();
    for (size_t i = 0; i + 2 < opaque; i += 3) {
        const od::RenderVertex* v = &mesh.tris[i];
        const float e0[3] = {v[1].pos[0] - v[0].pos[0], v[1].pos[1] - v[0].pos[1], v[1].pos[2] - v[0].pos[2]};
        const float e1[3] = {v[2].pos[0] - v[0].pos[0], v[2].pos[1] - v[0].pos[1], v[2].pos[2] - v[0].pos[2]};
        const float cr[3] = {e0[1] * e1[2] - e0[2] * e1[1], e0[2] * e1[0] - e0[0] * e1[2], e0[0] * e1[1] - e0[1] * e1[0]};
        const double area = std::sqrt(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]);
        if (area <= 0.0) continue;
        float texel[3] = {1.0f, 1.0f, 1.0f};
        const int slice = int(v[0].slice);
        if (slice >= 0 && (size_t(slice) + 1) * kPageBytes <= mesh.slices.size()) {
            const int tx = std::clamp(int((v[0].uv[0] + v[1].uv[0] + v[2].uv[0]) / 3.0f * 256.0f), 0, 255);
            const int ty = std::clamp(int((v[0].uv[1] + v[1].uv[1] + v[2].uv[1]) / 3.0f * 256.0f), 0, 255);
            const uint8_t* t = mesh.slices.data() + size_t(slice) * kPageBytes + (size_t(ty) * 256 + size_t(tx)) * 4;
            if (t[3] < 64) continue;
            for (int c = 0; c < 3; ++c) texel[c] = float(t[c]) / 255.0f;
        }
        float lum = 0.0f, shade = 0.0f;
        bool clip = false;
        for (int c = 0; c < 3; ++c) {
            const float vertex = (v[0].shade * v[0].color[c] + v[1].shade * v[1].color[c] + v[2].shade * v[2].color[c]) / 3.0f;
            const float value = texel[c] * vertex;
            clip = clip || value > 1.0f;
            lum += std::min(value, 1.0f) / 3.0f;
            shade += vertex / 3.0f;
        }
        areaSum += area;
        lumSum += area * lum;
        clipSum += clip ? area : 0.0;
        shadeSum += area * shade;
    }
    if (areaSum > 0.0) {
        out.luminance = float(lumSum / areaSum);
        out.clipped = float(clipSum / areaSum);
        out.vertexShade = float(shadeSum / areaSum);
    }
    return out;
}

} // namespace od::app
