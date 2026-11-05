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

// effects (particle lists that recycle FIFO, quads culled mid list): per current vertex, the previous vertex of the same
// prim (6 vertices when both counts allow, else 3), -1 for a new prim
std::vector<long> effectPairs(const float* prevXyz, size_t prevVerts, const float* curXyz, size_t curVerts);

// A point light of the additive effects (ParticleLighting)
struct ParticleLight {
    float pos[3];
    float radius;
    float color[3];
    float clearing; // how near the light a character casts no shadow of it: the effect's own size, at least 0.1 radius
};
// the objects with a glow as lights: each at its bounds' centre, reaching `radius` world units, further for a larger
// effect (up to 4 times). A light nearer than half a radius to an earlier one joins it, as does any light past maxLights
// (to the nearest): a burst of sparks is one bright light and no effect's light is dropped
std::vector<ParticleLight> gatherParticleLights(const std::vector<DynamicObject>& objects, float radius, size_t maxLights);

// Phong tessellation (Boubekeur & Alexa 2008) of the triangle a, b, c with unit normals na, nb, nc at the point of
// barycentric weights (u, v, w) of a, b, c: the point projected on the three vertices' tangent planes, blended by those
// weights, `amount` of the way from the flat triangle (0 flat, 1 whole). The vertices stay where they are, and a point on
// an edge depends only on that edge's two vertices
void phongPoint(const float a[3], const float b[3], const float c[3], const float na[3], const float nb[3],
                const float nc[3], float u, float v, float w, float amount, float out[3]);

// How a model subdivides, decided once from one pose and kept while it animates: decided again each frame, a face near the
// limit of the smoothing or a vertex rounded to a neighbouring position switched its edges between round and straight
struct SubdivisionPlan {
    std::vector<uint32_t> smoothStart; // per vertex + 1: where its faces start in smoothFaces
    std::vector<uint32_t> smoothFaces; // the faces whose normals a vertex averages: its smoothing group (under ~60 degrees across each edge)
    std::vector<uint8_t> roundEdges;   // per face: bit 0 edge ab, 1 bc, 2 ca round. The rest (crease, open) stay straight
};
// false, and an empty plan, for a pose with parts fallen on one point (fewer edges than faces), which tells nothing
bool buildSubdivisionPlan(const RenderVertex* tris, size_t count, SubdivisionPlan& plan);
// vertex k's normal over this frame's face normals (3 floats per face)
void subdivisionNormal(const SubdivisionPlan& plan, size_t k, const float* faceNormals, float out[3]);
// How the small triangles of a face cull (RenderVertex::cull). On a curved face they turn away from the flat face near the
// silhouette, so the quad rules, which read a whole half's facing, would drop or keep them by the wrong side: each one goes
// by its own. `opposite`: the quad's two halves are wound opposite ways. A half that follows the other half (5) then
// reverses, to face where that half faces; a pair drawn when either half faces the camera (2, 3) always has one that does
struct SubdividedCull {
    int mode = 0;
    bool reverse = false;
};
SubdividedCull subdividedCull(int cull, bool opposite);

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
        bool anchored = false;  // DynamicObject::anchored, anchor
        float anchor[3]{0.0f, 0.0f, 0.0f};
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
