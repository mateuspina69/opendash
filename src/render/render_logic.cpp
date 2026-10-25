#include "render_logic.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace od {

void faceNormal(const float a[3], const float b[3], const float c[3], float out[3]) {
    const float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    // u x v: room floors come out +Y (checked in the F1 normal view, ST09)
    const float n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
    const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (len < 1e-12f) {
        out[0] = 0.0f;
        out[1] = 1.0f;
        out[2] = 0.0f;
        return;
    }
    for (int i = 0; i < 3; ++i) out[i] = n[i] / len;
}

float halton(uint32_t index, uint32_t base) {
    float f = 1.0f, r = 0.0f;
    for (uint32_t i = index; i > 0; i /= base) {
        f /= float(base);
        r += f * float(i % base);
    }
    return r;
}

Jitter jitterAt(uint64_t frame, uint32_t phaseCount) {
    const uint32_t i = uint32_t(frame % std::max(1u, phaseCount)) + 1;
    return {halton(i, 2) - 0.5f, halton(i, 3) - 0.5f};
}

float renderScale(AaQuality q) {
    switch (q) {
    case AaQuality::Quality: return 1.0f / 1.5f;
    case AaQuality::Balanced: return 1.0f / 1.7f;
    case AaQuality::Performance: return 0.5f;
    default: return 1.0f;
    }
}

void renderSize(int outW, int outH, AaQuality q, int& w, int& h) {
    const float s = renderScale(q);
    w = std::max(1, int(std::lround(float(outW) * s)));
    h = std::max(1, int(std::lround(float(outH) * s)));
}

uint32_t dlssPhaseCount(int renderW, int outW) {
    const float r = float(outW) / float(std::max(1, renderW));
    return uint32_t(std::ceil(8.0f * r * r));
}

// Effects are particle lists (smoke / trails / rings) that grow, shrink and recycle FIFO: the puff at index i now is
// the puff at i + k a tick ago. The shift k (in prims of `unit` vertices, |k| <= 8) whose pairs of prim centres are
// closest; 0 when nothing fits. Vertex positions as xyz triples in p / c.
int effectShift(const float* p, size_t np, const float* c, size_t nc, size_t unit) {
    auto centre = [unit](const float* v, size_t i, float out[3]) {
        out[0] = out[1] = out[2] = 0.0f;
        for (size_t q = 0; q < unit; ++q)
            for (int a = 0; a < 3; ++a) out[a] += v[(i * unit + q) * 3 + size_t(a)];
        for (int a = 0; a < 3; ++a) out[a] /= float(unit);
    };
    const long pn = long(np / unit), cn = long(nc / unit);
    int best = 0;
    float bestCost = 3.4e38f;
    for (int k = -8; k <= 8; ++k) {
        const long i0 = std::max(0L, long(-k)), i1 = std::min(cn, pn - k);
        if (i1 - i0 < 1) continue;
        const long n = std::min(24L, i1 - i0);
        float cost = 0.0f;
        for (long s = 0; s < n; ++s) {
            const long i = i0 + (i1 - i0 - 1) * s / std::max(1L, n - 1);
            float a[3], b[3];
            centre(c, size_t(i), a);
            centre(p, size_t(i + k), b);
            for (int q = 0; q < 3; ++q) cost += (a[q] - b[q]) * (a[q] - b[q]);
        }
        cost = cost / float(n) * (1.0f + 0.02f * float(std::abs(k))); // ties: the smaller shift
        if (cost < bestCost) {
            bestCost = cost;
            best = k;
        }
    }
    return best;
}
size_t effectUnit(size_t a, size_t b) { return (a % 6 == 0 && b % 6 == 0) ? 6 : 3; }

std::vector<DynamicObject> interpolateObjects(const std::vector<DynamicObject>& prev,
                                              const std::vector<DynamicObject>& cur, float alpha) {
    std::unordered_map<uint32_t, const DynamicObject*> byId;
    for (const DynamicObject& o : prev) byId[o.id] = &o;
    std::vector<DynamicObject> out = cur;
    for (DynamicObject& o : out) {
        const auto it = byId.find(o.id);
        if (it == byId.end() || it->second->layout != o.layout) continue;
        const auto& p = it->second->tris;
        if ((o.id >> 24) == kKindEffect) { // each puff with the same puff a tick ago (effectShift)
            const size_t unit = effectUnit(p.size(), o.tris.size());
            std::vector<float> pp(p.size() * 3), cp(o.tris.size() * 3);
            for (size_t i = 0; i < p.size(); ++i)
                for (int c = 0; c < 3; ++c) pp[i * 3 + size_t(c)] = p[i].pos[c];
            for (size_t i = 0; i < o.tris.size(); ++i)
                for (int c = 0; c < 3; ++c) cp[i * 3 + size_t(c)] = o.tris[i].pos[c];
            const long k = effectShift(pp.data(), p.size(), cp.data(), o.tris.size(), unit) * long(unit);
            for (long i = 0; i < long(o.tris.size()); ++i) {
                const long j = i + k;
                if (j < 0 || j >= long(p.size())) continue; // a new puff: as is
                for (int c = 0; c < 3; ++c)
                    o.tris[size_t(i)].pos[c] = p[size_t(j)].pos[c] + (o.tris[size_t(i)].pos[c] - p[size_t(j)].pos[c]) * alpha;
            }
            continue;
        }
        if (p.size() != o.tris.size()) continue;
        for (size_t i = 0; i < o.tris.size(); ++i)
            for (int c = 0; c < 3; ++c) o.tris[i].pos[c] = p[i].pos[c] + (o.tris[i].pos[c] - p[i].pos[c]) * alpha;
    }
    return out;
}

void MotionHistory::reset() { last_.clear(); }

void MotionHistory::apply(const std::vector<DynamicObject>& objs, std::vector<float>& prevPos) {
    prevPos.clear();
    std::unordered_map<uint32_t, Entry> next;
    for (const DynamicObject& o : objs) {
        std::vector<float> curPos(o.tris.size() * 3);
        for (size_t i = 0; i < o.tris.size(); ++i)
            for (int c = 0; c < 3; ++c) curPos[i * 3 + c] = o.tris[i].pos[c];
        const auto it = last_.find(o.id);
        const bool have = it != last_.end() && it->second.pos.size() == curPos.size() && it->second.layout == o.layout;
        const auto& src = have ? it->second.pos : curPos;
        if (it != last_.end() && it->second.layout == o.layout && (o.id >> 24) == kKindEffect) {
            // an effect: each puff's motion from the same puff last frame (effectShift), a new one none
            const auto& lp = it->second.pos;
            const size_t nv = curPos.size() / 3, pv = lp.size() / 3, unit = effectUnit(pv, nv);
            const long k = effectShift(lp.data(), pv, curPos.data(), nv, unit) * long(unit);
            for (long i = 0; i < long(nv); ++i) {
                const long j = i + k;
                const float* src = (j >= 0 && j < long(pv)) ? &lp[size_t(j) * 3] : &curPos[size_t(i) * 3];
                prevPos.insert(prevPos.end(), src, src + 3);
            }
            next[o.id] = Entry{o.layout, std::move(curPos)};
            continue;
        }
        prevPos.insert(prevPos.end(), src.begin(), src.end());
        next[o.id] = Entry{o.layout, std::move(curPos)};
    }
    last_ = std::move(next); // objects not drawn this frame are forgotten
}

void motionPixels(const float cur[16], const float prv[16], const float p[3], int width, int height, float out[2]) {
    auto project = [&](const float* m, float& sx, float& sy) {
        const float x = p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12];
        const float y = p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13];
        const float w = p[0] * m[3] + p[1] * m[7] + p[2] * m[11] + m[15];
        sx = (x / w * 0.5f + 0.5f) * float(width);
        sy = (0.5f - y / w * 0.5f) * float(height);
    };
    float cx, cy, px, py;
    project(cur, cx, cy);
    project(prv, px, py);
    out[0] = px - cx;
    out[1] = py - cy;
}

namespace {
constexpr std::pair<AaBackend, const char*> kBackends[] = {
    {AaBackend::Auto, "auto"}, {AaBackend::None, "none"}, {AaBackend::Dlss, "dlss"},
    {AaBackend::Fsr, "fsr"},   {AaBackend::Xess, "xess"}, {AaBackend::Nis, "nis"}};
constexpr std::pair<AaQuality, const char*> kQualities[] = {{AaQuality::NativeAA, "native"},
                                                            {AaQuality::Quality, "quality"},
                                                            {AaQuality::Balanced, "balanced"},
                                                            {AaQuality::Performance, "performance"}};
} // namespace

std::vector<AaBackend> backendCandidates(AaBackend wanted, const std::set<AaBackend>& broken) {
    std::vector<AaBackend> out;
    auto add = [&](AaBackend b) {
        if ((b == AaBackend::None || !broken.count(b)) && std::find(out.begin(), out.end(), b) == out.end())
            out.push_back(b);
    };
    if (wanted != AaBackend::Auto) add(wanted);
    if (wanted != AaBackend::None)
        for (AaBackend b : {AaBackend::Dlss, AaBackend::Fsr, AaBackend::None}) add(b);
    return out;
}

bool parseAaBackend(std::string_view s, AaBackend& out) {
    for (const auto& [b, n] : kBackends)
        if (s == n) {
            out = b;
            return true;
        }
    return false;
}

bool parseAaQuality(std::string_view s, AaQuality& out) {
    for (const auto& [q, n] : kQualities)
        if (s == n) {
            out = q;
            return true;
        }
    return false;
}

const char* aaBackendName(AaBackend b) {
    for (const auto& [k, n] : kBackends)
        if (k == b) return n;
    return "?";
}

const char* aaQualityName(AaQuality q) {
    for (const auto& [k, n] : kQualities)
        if (k == q) return n;
    return "?";
}

} // namespace od
