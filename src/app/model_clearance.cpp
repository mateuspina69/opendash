#include "app/model_clearance.hpp"

#include <algorithm>
#include <cmath>

#include "render/scene_pieces.hpp"

namespace {

constexpr float kSteep = 0.7f;        // a face whose normal rises less than this is a wall
constexpr int kDirections = 8;        // the horizontal segments' directions
constexpr float kHeights[4] = {0.15f, 0.4f, 0.65f, 0.9f}; // the segments' heights, of the model's height
constexpr float kBand = 0.125f;       // a height takes the vertices within this many model heights of it
constexpr float kSameWall = 0.9f;     // pushes whose normals turn less than this apart are one wall's
constexpr float kMaxReach = 1.5f;     // a model reaching further than this many heights from the pivot is not at it
constexpr float kMaxPush = 0.35f;     // the push never exceeds this many model heights (about an arm)

// a wall from a triangle (false: not steep, or no area)
template <typename Wall> bool makeWall(const float* a, const float* b, const float* c, Wall& w) {
    const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
    const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]), flat = std::sqrt(n[0] * n[0] + n[2] * n[2]);
    if (len < 1e-6f || std::abs(n[1]) / len >= kSteep) return false;
    w.normal[0] = n[0] / flat;
    w.normal[1] = n[2] / flat;
    for (int k = 0; k < 3; ++k) {
        w.a[k] = a[k];
        w.b[k] = b[k];
        w.c[k] = c[k];
        w.lo[k] = std::min({a[k], b[k], c[k]});
        w.hi[k] = std::max({a[k], b[k], c[k]});
    }
    return true;
}

} // namespace

void ModelClearance::roomLoaded(const std::vector<od::RenderVertex>& tris, size_t opaqueEnd) {
    room_.clear();
    offset_[0] = offset_[1] = 0.0f;
    havePivot_ = false;
    for (size_t k = 0; k + 2 < std::min(opaqueEnd, tris.size()); k += 3) {
        Wall w;
        if (makeWall(tris[k].pos, tris[k + 1].pos, tris[k + 2].pos, w)) room_.push_back(w);
    }
}

void ModelClearance::tick(std::vector<od::DynamicObject>& objects, uint32_t id, const float pivot[3], bool active) {
    auto model = std::find_if(objects.begin(), objects.end(), [&](const od::DynamicObject& o) { return o.id == id; });
    if (!active || model == objects.end() || model->tris.empty()) {
        offset_[0] = offset_[1] = 0.0f;
        havePivot_ = false;
        return;
    }
    // the model's height and how far it reaches from its pivot (the walls near enough to matter)
    float lo = model->tris[0].pos[1], hi = lo, reachAll = 0.0f;
    for (const od::RenderVertex& v : model->tris) {
        lo = std::min(lo, v.pos[1]);
        hi = std::max(hi, v.pos[1]);
        reachAll = std::max(reachAll, std::hypot(v.pos[0] - pivot[0], v.pos[2] - pivot[2]));
    }
    const float height = hi - lo;
    // a model away from the game's position (the tick a room is entered: the pivot is still the last room's) is drawn
    // as built
    if (height <= 0.0f || reachAll <= 0.0f || reachAll > kMaxReach * height) {
        offset_[0] = offset_[1] = 0.0f;
        havePivot_ = false;
        return;
    }
    std::vector<const Wall*> near;
    std::vector<Wall> dynamic;
    for (const od::DynamicObject& o : objects) {
        if (!od::isSceneryObject(o)) continue;
        for (size_t k = 0; k + 2 < o.tris.size(); k += 3) {
            Wall w;
            if (makeWall(o.tris[k].pos, o.tris[k + 1].pos, o.tris[k + 2].pos, w)) dynamic.push_back(w);
        }
    }
    for (const std::vector<Wall>* list : {&room_, &dynamic})
        for (const Wall& w : *list)
            if (w.hi[0] >= pivot[0] - reachAll && w.lo[0] <= pivot[0] + reachAll && w.hi[2] >= pivot[2] - reachAll &&
                w.lo[2] <= pivot[2] + reachAll && w.hi[1] >= lo && w.lo[1] <= hi)
                near.push_back(&w);
    // per height and direction: the model's reach, the nearest wall the segment out to it crosses, what lies beyond
    struct Push {
        float normal[2], depth;
    };
    std::vector<Push> pushes;
    for (const float f : kHeights) {
        const float y = lo + height * f;
        float reach[kDirections] = {};
        for (const od::RenderVertex& v : model->tris) {
            if (std::abs(v.pos[1] - y) > height * kBand) continue;
            for (int d = 0; d < kDirections; ++d) {
                const float angle = 6.2831853f * float(d) / float(kDirections);
                const float along = (v.pos[0] - pivot[0]) * std::cos(angle) + (v.pos[2] - pivot[2]) * std::sin(angle);
                reach[d] = std::max(reach[d], along);
            }
        }
        for (int d = 0; d < kDirections; ++d) {
            if (reach[d] <= 0.0f) continue;
            const float angle = 6.2831853f * float(d) / float(kDirections);
            const float dir[2] = {std::cos(angle), std::sin(angle)};
            const float o[3] = {pivot[0], y, pivot[2]}, seg[3] = {dir[0] * reach[d], 0.0f, dir[1] * reach[d]};
            float first = 1.0f;
            const Wall* hit = nullptr;
            for (const Wall* w : near) {
                const float t = od::segmentHitsTriangle(o, seg, w->a, w->b, w->c);
                if (t > 0.0f && t < first) {
                    first = t;
                    hit = w;
                }
            }
            if (!hit) continue;
            // the wall's normal toward the pivot; what lies beyond the wall, measured along that normal
            float n[2] = {hit->normal[0], hit->normal[1]};
            if (n[0] * dir[0] + n[1] * dir[1] > 0.0f) n[0] = -n[0], n[1] = -n[1];
            const float depth = reach[d] * (1.0f - first) * -(n[0] * dir[0] + n[1] * dir[1]);
            bool merged = false;
            for (Push& p : pushes)
                if (p.normal[0] * n[0] + p.normal[1] * n[1] > kSameWall) {
                    p.depth = std::max(p.depth, depth);
                    merged = true;
                    break;
                }
            if (!merged) pushes.push_back({{n[0], n[1]}, depth});
        }
    }
    float target[2] = {0.0f, 0.0f};
    for (const Push& p : pushes) {
        target[0] += p.normal[0] * p.depth;
        target[1] += p.normal[1] * p.depth;
    }
    const float targetLen = std::hypot(target[0], target[1]);
    if (targetLen > kMaxPush * height) { // walls on more sides than one pushing it past an arm: as far as an arm
        target[0] *= kMaxPush * height / targetLen;
        target[1] *= kMaxPush * height / targetLen;
    }
    // a deeper push at once (the model never shows inside the wall); a shallower one only as the model travels, by at
    // most the distance it moved this tick: a pose that reaches less (stopping a run against the wall) never slides
    // the drawing over the ground, and walking away fades the push within the walk
    const float moved = havePivot_ ? std::hypot(pivot[0] - lastPivot_[0], pivot[2] - lastPivot_[1]) : 0.0f;
    lastPivot_[0] = pivot[0];
    lastPivot_[1] = pivot[2];
    havePivot_ = true;
    if (std::hypot(target[0], target[1]) >= std::hypot(offset_[0], offset_[1])) {
        offset_[0] = target[0];
        offset_[1] = target[1];
    } else {
        const float gap[2] = {target[0] - offset_[0], target[1] - offset_[1]}, len = std::hypot(gap[0], gap[1]);
        const float step = len > moved ? moved / len : 1.0f;
        offset_[0] += gap[0] * step;
        offset_[1] += gap[1] * step;
    }
    for (od::RenderVertex& v : model->tris) {
        v.pos[0] += offset_[0];
        v.pos[2] += offset_[1];
    }
}
