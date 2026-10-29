#include "app/see_through.hpp"

#include <algorithm>
#include <cmath>

#include "render/scene_pieces.hpp"

namespace {

constexpr int kHitsToHide = 2;        // a piece crossed by this many of a body's segments hides that body
constexpr float kWholeExtent = 1.5f;  // a piece whose largest side is under this many body heights fades whole
constexpr float kOpenTime = 0.2f;     // seconds to open
constexpr float kHoldTime = 0.3f;     // seconds an open piece stays open once it hides nothing
constexpr float kCloseTime = 0.3f;    // seconds to close
constexpr float kEndMargin = 0.06f;   // a segment stops this many body heights short of its point (the body's skin)
constexpr float kSideSpread = 0.35f;  // the side points, from the middle, of the body's width
constexpr float kHeights[4] = {0.15f, 0.4f, 0.65f, 0.9f}; // the points' heights, of the body's height
constexpr uint8_t kClearAlpha = 0x40; // a texel's alpha below this is transparent (PS1 colour 0; an STP texel, 0x80,
                                      // is drawn opaque on an opaque face)
constexpr int kClearSamples = 3;      // a face with this many of its 9 samples see-through is see-through

float largestSide(const std::array<float, 6>& box) {
    return std::max({box[3] - box[0], box[4] - box[1], box[5] - box[2]});
}

} // namespace

bool SeeThrough::seeThroughFace(const od::RenderVertex* v) const {
    if (v[0].slice < 0.0f) return false; // untextured: solid
    const int slice = int(std::lround(v[0].slice));
    if (slice >= clearSlices_) return false; // a slice made after the room (effects, the ghosts' looks)
    static constexpr float kWeights[9][3] = {{1 / 3.0f, 1 / 3.0f, 1 / 3.0f}, {0.6f, 0.2f, 0.2f}, {0.2f, 0.6f, 0.2f},
                                             {0.2f, 0.2f, 0.6f}, {0.45f, 0.45f, 0.1f}, {0.1f, 0.45f, 0.45f},
                                             {0.45f, 0.1f, 0.45f}, {0.8f, 0.1f, 0.1f}, {0.1f, 0.1f, 0.8f}};
    int clear = 0;
    for (const auto& w : kWeights) {
        const float u = v[0].uv[0] * w[0] + v[1].uv[0] * w[1] + v[2].uv[0] * w[2];
        const float t = v[0].uv[1] * w[0] + v[1].uv[1] * w[1] + v[2].uv[1] * w[2];
        const int x = std::clamp(int(u * kSliceSize), 0, kSliceSize - 1), y = std::clamp(int(t * kSliceSize), 0, kSliceSize - 1);
        clear += clear_[(size_t(slice) * kSliceSize + size_t(y)) * kSliceSize + size_t(x)];
    }
    return clear >= kClearSamples;
}

void SeeThrough::roomLoaded(std::vector<od::RenderVertex>& tris, size_t opaqueEnd, const std::vector<uint8_t>& slices,
                            int sliceCount) {
    const size_t texels = size_t(kSliceSize) * kSliceSize;
    clearSlices_ = slices.size() == size_t(std::max(sliceCount, 0)) * texels * 4 ? sliceCount : 0;
    clear_.assign(size_t(clearSlices_) * texels, 0);
    for (size_t i = 0; i < clear_.size(); ++i) clear_[i] = slices[i * 4 + 3] < kClearAlpha ? 1 : 0;
    const od::ScenePieces sp = od::scenePieces(tris.data(), tris.size());
    pieces_.clear();
    states_.clear();
    room_.clear();
    for (size_t p = 0; p < sp.box.size(); ++p)
        pieces_[roomKey(uint32_t(p) + 1)] = Piece{uint32_t(p) + 1, largestSide(sp.box[p])};
    for (size_t f = 0; f < sp.of.size(); ++f) {
        // semi-transparent (water, glass) or showing through (a fence): no piece, never in the way
        if (f * 3 + 2 >= opaqueEnd || seeThroughFace(&tris[f * 3])) {
            for (size_t v = 0; v < 3; ++v) tris[f * 3 + v].piece = 0.0f;
            continue;
        }
        const uint32_t number = sp.of[f] + 1;
        for (size_t v = 0; v < 3; ++v) tris[f * 3 + v].piece = float(number);
        Tri t;
        std::copy(tris[f * 3].pos, tris[f * 3].pos + 3, t.a);
        std::copy(tris[f * 3 + 1].pos, tris[f * 3 + 1].pos + 3, t.b);
        std::copy(tris[f * 3 + 2].pos, tris[f * 3 + 2].pos + 3, t.c);
        t.key = roomKey(number);
        room_.push_back(t);
    }
}

void SeeThrough::tick(std::vector<od::DynamicObject>& objects, const float eye[3],
                      const std::vector<od::SeeThroughBody>& bodies, uint32_t held) {
    // this tick's dynamic pieces, numbered from kDynamicPieces
    for (auto it = pieces_.begin(); it != pieces_.end();) it = it->first >> 63 ? pieces_.erase(it) : std::next(it);
    std::vector<Tri> dynamic;
    uint32_t next = kDynamicPieces;
    for (od::DynamicObject& o : objects) {
        if (!od::isSceneryObject(o) || (held != 0 && o.id == held)) continue;
        const od::ScenePieces sp = od::scenePieces(o.tris.data(), o.tris.size());
        std::vector<uint32_t> number(sp.box.size());
        for (size_t p = 0; p < sp.box.size(); ++p) {
            number[p] = next++;
            pieces_[dynamicKey(o.id, uint32_t(p))] = Piece{number[p], largestSide(sp.box[p])};
        }
        for (size_t f = 0; f < sp.of.size(); ++f) {
            if (seeThroughFace(&o.tris[f * 3])) { // a fence, a grate: no piece, never in the way
                for (size_t v = 0; v < 3; ++v) o.tris[f * 3 + v].piece = 0.0f;
                continue;
            }
            for (size_t v = 0; v < 3; ++v) o.tris[f * 3 + v].piece = float(number[sp.of[f]]);
            Tri t;
            std::copy(o.tris[f * 3].pos, o.tris[f * 3].pos + 3, t.a);
            std::copy(o.tris[f * 3 + 1].pos, o.tris[f * 3 + 1].pos + 3, t.b);
            std::copy(o.tris[f * 3 + 2].pos, o.tris[f * 3 + 2].pos + 3, t.c);
            t.key = dynamicKey(o.id, sp.of[f]);
            dynamic.push_back(t);
        }
    }
    for (auto it = states_.begin(); it != states_.end();) {
        const auto piece = pieces_.find(it->first);
        if (piece == pieces_.end()) { // its object is gone
            it = states_.erase(it);
            continue;
        }
        it->second.number = piece->second.number;
        it->second.bodies = 0;
        ++it;
    }
    // each body's points: four heights, the middle and either side across the view
    std::unordered_map<uint64_t, int> hits;
    std::vector<uint64_t> crossed;
    for (size_t k = 0; k < std::min(bodies.size(), od::kMaxSeeThroughBodies); ++k) {
        const od::SeeThroughBody& b = bodies[k];
        const float height = b.hi[1] - b.lo[1];
        if (height <= 0.0f) continue;
        const float mid[2] = {(b.lo[0] + b.hi[0]) * 0.5f, (b.lo[2] + b.hi[2]) * 0.5f};
        float side[2] = {mid[1] - eye[2], eye[0] - mid[0]}; // across the view, horizontal
        const float sideLen = std::sqrt(side[0] * side[0] + side[1] * side[1]);
        const float spread = kSideSpread * std::max(b.hi[0] - b.lo[0], b.hi[2] - b.lo[2]);
        for (float& s : side) s = sideLen > 1e-3f ? s / sideLen * spread : 0.0f;
        hits.clear();
        for (const float h : kHeights)
            for (int across = -1; across <= 1; ++across) {
                const float point[3] = {mid[0] + side[0] * float(across), b.lo[1] + height * h,
                                        mid[1] + side[1] * float(across)};
                const float d[3] = {point[0] - eye[0], point[1] - eye[1], point[2] - eye[2]};
                const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                if (length <= height * kEndMargin) continue;
                const float stop = 1.0f - height * kEndMargin / length;
                crossed.clear();
                for (const std::vector<Tri>* list : {&room_, &dynamic})
                    for (const Tri& t : *list) {
                        const float at = od::segmentHitsTriangle(eye, d, t.a, t.b, t.c);
                        if (at > 0.0f && at < stop) crossed.push_back(t.key);
                    }
                std::sort(crossed.begin(), crossed.end());
                crossed.erase(std::unique(crossed.begin(), crossed.end()), crossed.end());
                for (uint64_t key : crossed) ++hits[key];
            }
        for (const auto& [key, count] : hits) {
            if (count < kHitsToHide) continue;
            const Piece& piece = pieces_[key];
            State& s = states_[key];
            s.number = piece.number;
            s.bodies |= uint8_t(1u << k);
            s.whole = piece.extent < kWholeExtent * height;
        }
    }
}

void SeeThrough::frame(float dt, std::vector<od::SeeThroughPiece>& out) {
    out.clear();
    for (auto it = states_.begin(); it != states_.end();) {
        State& s = it->second;
        if (s.bodies) {
            s.open = std::min(1.0f, s.open + dt / kOpenTime);
            s.hold = kHoldTime;
            s.shown = s.bodies;
        } else if (s.hold > 0.0f) {
            s.hold -= dt;
        } else {
            s.open -= dt / kCloseTime;
        }
        if (s.open <= 0.0f && !s.bodies) {
            it = states_.erase(it);
            continue;
        }
        const float eased = s.open * s.open * (3.0f - 2.0f * s.open);
        out.push_back(od::SeeThroughPiece{s.number, eased, s.whole, s.shown});
        ++it;
    }
    std::sort(out.begin(), out.end(),
              [](const od::SeeThroughPiece& a, const od::SeeThroughPiece& b) { return a.open > b.open; });
    if (out.size() > od::kMaxSeeThroughPieces) out.resize(od::kMaxSeeThroughPieces);
}
