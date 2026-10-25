// GPU-free render math and bookkeeping shared by the renderers (unit tested in tests/test_render_logic.cpp).
#pragma once

#include <cstdint>
#include <set>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "renderer.hpp"

namespace od {

// unit normal of triangle abc; degenerate triangles give (0, 1, 0)
void faceNormal(const float a[3], const float b[3], const float c[3], float out[3]);

float halton(uint32_t index, uint32_t base); // index >= 1, result in [0, 1)
struct Jitter {
    float x, y; // pixels, in [-0.5, 0.5)
};
Jitter jitterAt(uint64_t frame, uint32_t phaseCount);

float renderScale(AaQuality q); // render / output size: 1, 1/1.5, 1/1.7, 1/2
void renderSize(int outW, int outH, AaQuality q, int& w, int& h);
uint32_t dlssPhaseCount(int renderW, int outW); // ceil(8 * (out / render)^2)

// per id: blend prev -> cur positions when the id existed with the same vertex count and layout, else cur as is
std::vector<DynamicObject> interpolateObjects(const std::vector<DynamicObject>& prev,
                                              const std::vector<DynamicObject>& cur, float alpha);

// effects (particle lists that recycle FIFO): the prim shift k pairing the current prim i with the previous i + k
int effectShift(const float* prevXyz, size_t prevVerts, const float* curXyz, size_t curVerts, size_t unit);
size_t effectUnit(size_t prevVerts, size_t curVerts); // 6 (quads) when both counts allow, else 3

class MotionHistory {
public:
    void reset();
    // prevPos receives 3 floats per vertex of objs (concatenated in order) = the position the object had at the
    // last call when its id was present with the same vertex count and layout, else the current position (zero motion)
    void apply(const std::vector<DynamicObject>& objs, std::vector<float>& prevPos);

private:
    struct Entry {
        uint32_t layout = 0;
        std::vector<float> pos; // xyz per vertex
    };
    std::unordered_map<uint32_t, Entry> last_; // id -> the object at the last apply
};

// screen motion of world point p in pixels (prev - cur); row-major matrices, mul(float4(p, 1), M), y down
void motionPixels(const float curViewProj[16], const float prevViewProj[16], const float world[3], int width,
                  int height, float out[2]);

// backends to try, in order: the wanted one (unless Auto), then the Auto chain DLSS, FSR, none; backends in
// `broken` (failed at run time) are skipped, None never is
std::vector<AaBackend> backendCandidates(AaBackend wanted, const std::set<AaBackend>& broken);

bool parseAaBackend(std::string_view s, AaBackend& out);
bool parseAaQuality(std::string_view s, AaQuality& out);
const char* aaBackendName(AaBackend b);
const char* aaQualityName(AaQuality q);

} // namespace od
