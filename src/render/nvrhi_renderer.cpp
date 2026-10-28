// NVRHI renderer (D3D12 on Windows, Vulkan elsewhere).
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <cfloat>
#include <map>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <unordered_map>

#ifdef _WIN32
#include "device_d3d12.hpp"
#else
#include "device_vulkan.hpp"
#endif
#include "imgui_nvrhi.hpp"
#include "render/ui_canvas.hpp"
#include "render/ui_pass.hpp"
#include "render_logic.hpp"
#include "renderer.hpp"
#include "upscaler.hpp"

#include "g_blizzard_ps.h"
#include "g_debugview_ms_ps.h"
#include "g_debugview_ps.h"
#include "g_debugview_vs.h"
#include "g_flake_ps.h"
#include "g_flake_vs.h"
#include "g_full_vs.h"
#include "g_height_ps.h"
#include "g_height_vs.h"
#include "g_output_ps.h"
#include "g_output_vs.h"
#include "g_preview_ps.h"
#include "g_preview_vs.h"
#include "g_scene_ps.h"
#include "g_scene_rt_ps.h"
#include "g_scene_vs.h"

using namespace DirectX;

namespace od {

namespace {

// RenderVertex + what the GPU derives: shading normal, the position one frame ago (motion vectors) and, for the quad
// culling rules (RenderVertex::cull 2..5), the winding normals of the own triangle and of the quad's other half
struct GpuVertex {
    RenderVertex v;
    float normal[3];
    float prevPos[3];
    float faceN[3];
    float pairN[3];
    float surface; // bits: kSurfaceModel a model's face (actors, the player, bank models), clipped at the game's near
                   // plane; kSurfaceScenery the scenery (room, segments, doors, static objects), dissolved around a focus;
                   // kSurfaceEffect an effect (particles, billboards, rings): its blended texels fade at the scenery
};
static_assert(sizeof(RenderVertex) == 48 && sizeof(GpuVertex) == 100);
constexpr uint32_t kSurfaceModel = 1, kSurfaceScenery = 2, kSurfaceEffect = 4;
constexpr uint32_t kStaticPoolModels = 0x60; // kKindPoolModel index >> 8 of the static pool's objects (liveScene)

// scene.hlsl cbuffer Frame
struct FrameConstants {
    XMFLOAT4X4 viewProj, viewProjNoJit, prevViewProj;
    float eye[3];
    float cullSign;
    float prevEye[3];
    float fogStart;
    float fwd[3];
    float fogEnd;
    float fogColor[3];
    float reactive;
    float renderSize[2];
    float motionMode;
    float blendMode; // the draw's DynamicObject::blend (0 opaque): fog fades additive / multiplied light out
    float sunDir[3];
    float sunStrength;
    float shadowMode;
    float semiPass; // a semi-transparent range: 1 its opaque texels (no STP bit), 2 its blended ones; 0 any draw else
    float modelNear; // the game's near plane (world units): a model's face is clipped there (scene.hlsl)
    float pad1;
    float fogMode; // 0 off, 1 classic (linear on depth), 2 remaster (exponential height fog), 3 the PS1's depth cue
    float fogDensity;
    float fogHeightFalloff;
    float fogMaxOpacity;
    float sunScatter; // remaster fog brightened toward the sun (0 = sun off)
    float fogSky;     // remaster fog of a sky draw (the ray never ends): 0 not sky, 1 dome, 2 screen-space layer
    float pad2[2];
    float water[4];     // remaster medium look: on, clock (seconds), sky draw, kind (0 water, 1 hot air)
    float waterTint[4]; // the room's water colour
    XMFLOAT4X4 skyRay;  // screen-space sky: NDC -> view direction (inverse of the unjittered rotation-only viewProj)
    float focus[kMaxFocusBodies][4]; // a focus body: its feet (world y), its strength; its depth is focusMask_[k]
    float focusCount;
    float pad3[3];
    float heightBounds[4]; // precipitation height map: min x, min z, 1 / its width, 1 / its depth
    float heightRange[4];  // ... its top y, its height span, its size in texels (x, z)
    float precip[4];       // Precipitation: snow, blizzard, gust, seconds
    float precipWind[4];   // ... wind (world units a second), unit
    float precipView[4];   // ... tan(fovY / 2), the flakes' light
    XMFLOAT4X4 invViewProj; // the blown snow's pass: NDC -> world (jittered)
    float softDepth[4];     // the depth copy's near / far planes (its raw depth -> view depth: the soft effects)
    float precipLook[8];    // Precipitation's look: density, size, brightness, swirl / flutter, reach, streak, chaos
    float precipLayer[4];   // the flake field drawn (drawScene): its cells' scale, flakes a cell, 1 = the far field
};
// scene.hlsl vs_flake's two fields of kFlakeCells (24 x 12 x 24) cells, 6 vertices a flake: the near one (up to
// kFlakesNear a cell) and the far one beyond it (cells kFlakeFarScale times larger, up to kFlakesFar a cell)
constexpr uint32_t kFlakeCellCount = 24 * 12 * 24, kFlakesNear = 32, kFlakesFar = 16;
constexpr float kFlakeFarScale = 3.0f;
constexpr int kHeightTexels = 1024; // the height map's longer side

// Same projection and 24-bit depth as the former D3D11 renderer (ADR 0002): the room data has coplanar faces
// whose depths differ only by rounding (different quad splits). D24 quantisation makes them tie, and LESS keeps
// the first drawn, like the original; reverse-Z on D32F resolves those ties per pixel and shows stripes
// (ST12 sub 1 floor). ADR 0004.
// Vulkan has no D24 on AMD: D32 there, with the scene shader writing its depth at D24's precision (the same ties)
#ifdef _WIN32
constexpr nvrhi::Format kDepthFormat = nvrhi::Format::D24S8;
#else
constexpr nvrhi::Format kDepthFormat = nvrhi::Format::D32;
#endif
constexpr bool kDepthHasStencil = kDepthFormat == nvrhi::Format::D24S8;
constexpr float kFar = 200000.0f;

// jitter in pixels -> NDC offset (row-major: row 2 is multiplied by view z, so the offset scales with w)
XMMATRIX withJitter(XMMATRIX p, float jx, float jy, int rw, int rh) {
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, p);
    m._31 += 2.0f * jx / float(rw);
    m._32 -= 2.0f * jy / float(rh);
    return XMLoadFloat4x4(&m);
}

// smoothRanges: the vertex ranges [first, end) of the objects whose normals are averaged over the faces sharing a
// position inside the same object (actors: the sun's light varies across the low-poly surface instead of
// per face); creases sharper than ~60 degrees and faces wound the other way stay apart. Every other vertex (effects:
// particles / rings / billboards, many of them starting on one point) keeps its flat face normal -- averaged over the
// whole buffer, an effect's hundreds of coincident corners made this quadratic (the ST4B sub 5 clash: 1.5 s a frame)
// and one object took its neighbours' normals.
void toGpu(const std::vector<RenderVertex>& tris, const float* prevPos, std::vector<GpuVertex>& out,
           const std::vector<std::pair<size_t, size_t>>* smoothRanges = nullptr,
           const std::vector<std::pair<size_t, size_t>>* surfaceRanges = nullptr,
           const std::vector<uint32_t>* surfaceBits = nullptr) {
    out.resize(tris.size());
    const size_t faces = tris.size() / 3;
    std::vector<float> fn(faces * 3);
    for (size_t f = 0; f < faces; ++f) faceNormal(tris[f * 3].pos, tris[f * 3 + 1].pos, tris[f * 3 + 2].pos, &fn[f * 3]);
    std::vector<float> nrm(fn.size() * 3); // per vertex
    for (size_t k = 0; k < faces * 3; ++k) std::memcpy(&nrm[k * 3], &fn[k / 3 * 3], 3 * sizeof(float));
    auto key = [](const float* p) {
        auto q = [](float v) { return uint64_t(int64_t(std::lround(v * 4.0f)) & 0x1FFFFF); };
        return q(p[0]) | q(p[1]) << 21 | q(p[2]) << 42;
    };
    if (smoothRanges)
        for (const auto& [first, end] : *smoothRanges) {
            std::unordered_map<uint64_t, std::vector<uint32_t>> shared; // quantised position -> the model's faces
            for (size_t k = first; k < end; ++k) shared[key(tris[k].pos)].push_back(uint32_t(k / 3));
            for (size_t k = first; k < end; ++k) {
                const float* own = &fn[k / 3 * 3];
                float n[3] = {0.0f, 0.0f, 0.0f};
                for (uint32_t f : shared[key(tris[k].pos)]) {
                    const float* o = &fn[size_t(f) * 3];
                    if (o[0] * own[0] + o[1] * own[1] + o[2] * own[2] < 0.5f) continue; // crease / opposite winding
                    for (int c = 0; c < 3; ++c) n[c] += o[c];
                }
                const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                for (int c = 0; c < 3; ++c) nrm[k * 3 + size_t(c)] = len > 1e-6f ? n[c] / len : own[c];
            }
        }
    for (size_t k = 0; k < faces * 3; ++k) {
        const float* own = &fn[k / 3 * 3];
        const float* n = &nrm[k * 3];
        out[k].v = tris[k];
        std::memcpy(out[k].normal, n, 3 * sizeof(float));
        const size_t f = k / 3, c = size_t(std::lround(tris[k].cull));
        const size_t pf = (c == 2 || c == 4) && f + 1 < faces ? f + 1 : (c == 3 || c == 5) && f > 0 ? f - 1 : f;
        std::memcpy(out[k].faceN, own, sizeof out[k].faceN);
        std::memcpy(out[k].pairN, &fn[pf * 3], sizeof out[k].pairN);
        std::memcpy(out[k].prevPos, prevPos ? prevPos + k * 3 : tris[k].pos, sizeof out[k].prevPos);
        out[k].surface = 0.0f;
    }
    if (surfaceRanges)
        for (size_t r = 0; r < surfaceRanges->size(); ++r)
            for (size_t k = (*surfaceRanges)[r].first; k < (*surfaceRanges)[r].second; ++k)
                out[k].surface = float((*surfaceBits)[r]);
}

// ray-tracing attributes per vertex: (u, v, slice, 0) -- what the shadow rays' alpha test reads
void toAttr(const std::vector<GpuVertex>& v, uint32_t count, std::vector<float>& out) {
    out.assign(size_t(std::max<uint32_t>(count, 1)) * 4, 0.0f);
    for (uint32_t i = 0; i < count; ++i) {
        out[i * 4 + 0] = v[i].v.uv[0];
        out[i * 4 + 1] = v[i].v.uv[1];
        out[i * 4 + 2] = v[i].v.slice;
    }
}

// triangle list at the start of vb (position = first 3 floats of GpuVertex), indexed 0..n-1 through ib (NVRHI's
// BLAS build requires an index buffer); not opaque: candidate hits run the alpha test
nvrhi::rt::GeometryDesc triangleGeometry(nvrhi::IBuffer* vb, nvrhi::IBuffer* ib, uint32_t vertexCount) {
    nvrhi::rt::GeometryTriangles t;
    t.setVertexBuffer(vb)
        .setVertexFormat(nvrhi::Format::RGB32_FLOAT)
        .setVertexStride(sizeof(GpuVertex))
        .setVertexCount(vertexCount)
        .setIndexBuffer(ib)
        .setIndexFormat(nvrhi::Format::R32_UINT)
        .setIndexCount(vertexCount);
    return nvrhi::rt::GeometryDesc().setTriangles(t).setFlags(nvrhi::rt::GeometryFlags::None);
}

class NvrhiRenderer final : public Renderer {
    struct PreviewConsts { // preview.hlsl's Params
        XMFLOAT4X4 viewProj;
        float eye[4];
        float light[4];
    };
    struct PreviewSlot { // renderPreview's target
        nvrhi::TextureHandle color, depth, atlas;
        nvrhi::FramebufferHandle fb;
        nvrhi::BindingSetHandle set;
        nvrhi::BufferHandle vb;
        TextureHandle id = 0;
        int w = 0, h = 0, aw = 0, ah = 0;
        bool pending = false;
        PreviewRequest req;
    };

public:
    template <class Window> bool init(Window window, int w, int h, bool debug) {
        if (!dev_.init(window, w, h, debug)) return false;
        if (!ui_.init(dev_.device())) return false;
        nvrhi::IDevice* d = dev_.device();
        cl_ = d->createCommandList();

        nvrhi::BufferDesc cb;
        cb.byteSize = sizeof(FrameConstants);
        cb.isConstantBuffer = true;
        cb.isVolatile = true;
        cb.maxVersions = 64;
        cb.debugName = "frame constants";
        frameCb_ = d->createBuffer(cb);

        pointClamp_ = d->createSampler(nvrhi::SamplerDesc()
                                           .setAllFilters(false)
                                           .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
        // the output pass's distortions (sub-pixel shifts) read bilinearly, or they turn edges into stairs
        linearClamp_ = d->createSampler(nvrhi::SamplerDesc()
                                            .setAllFilters(true)
                                            .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));

        // the ray-query scene shader is SM 6.5
        rt_ = d->queryFeatureSupport(nvrhi::Feature::RayQuery) && dev_.rayQueryShaders();
        SDL_Log("render: ray queries %s", rt_ ? "supported (sun shadows available)" : "unsupported");
        sceneVs_ = d->createShader({nvrhi::ShaderType::Vertex, "", "vs_main"}, g_scene_vs, sizeof(g_scene_vs));
        scenePs_ = rt_ ? d->createShader({nvrhi::ShaderType::Pixel, "", "ps_main"}, g_scene_rt_ps, sizeof(g_scene_rt_ps))
                       : d->createShader({nvrhi::ShaderType::Pixel, "", "ps_main"}, g_scene_ps, sizeof(g_scene_ps));
        outVs_ = d->createShader({nvrhi::ShaderType::Vertex, "", "vs_main"}, g_output_vs, sizeof(g_output_vs));
        outPs_ = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_main"}, g_output_ps, sizeof(g_output_ps));
        heightVs_ = d->createShader({nvrhi::ShaderType::Vertex, "", "vs_height"}, g_height_vs, sizeof(g_height_vs));
        heightPs_ = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_height"}, g_height_ps, sizeof(g_height_ps));
        flakeVs_ = d->createShader({nvrhi::ShaderType::Vertex, "", "vs_flake"}, g_flake_vs, sizeof(g_flake_vs));
        flakePs_ = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_flake"}, g_flake_ps, sizeof(g_flake_ps));
        fullVs_ = d->createShader({nvrhi::ShaderType::Vertex, "", "vs_full"}, g_full_vs, sizeof(g_full_vs));
        blizzardPs_ = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_blizzard"}, g_blizzard_ps, sizeof(g_blizzard_ps));
        {
            nvrhi::BindingLayoutDesc pl; // the flakes, the blown snow: the frame, the height map, the depth copy
            pl.visibility = nvrhi::ShaderType::All;
            pl.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), nvrhi::BindingLayoutItem::Texture_SRV(4),
                           nvrhi::BindingLayoutItem::Texture_SRV(5)};
            precipLayout_ = d->createBindingLayout(pl);
        }

        auto attr = [](const char* name, nvrhi::Format f, uint32_t offset) {
            return nvrhi::VertexAttributeDesc().setName(name).setFormat(f).setOffset(offset).setElementStride(
                sizeof(GpuVertex));
        };
        const nvrhi::VertexAttributeDesc attrs[] = {
            attr("POSITION", nvrhi::Format::RGB32_FLOAT, 0), attr("UV", nvrhi::Format::RG32_FLOAT, 12),
            attr("SLICE", nvrhi::Format::R32_FLOAT, 20),     attr("SHADE", nvrhi::Format::R32_FLOAT, 24),
            attr("CULL", nvrhi::Format::R32_FLOAT, 28),      attr("LAYER", nvrhi::Format::R32_FLOAT, 32),
            attr("COLOR", nvrhi::Format::RGB32_FLOAT, 36),   attr("NORMAL", nvrhi::Format::RGB32_FLOAT, 48),
            attr("PREVPOS", nvrhi::Format::RGB32_FLOAT, 60), attr("FACEN", nvrhi::Format::RGB32_FLOAT, 72),
            attr("PAIRN", nvrhi::Format::RGB32_FLOAT, 84),   attr("SURFACE", nvrhi::Format::R32_FLOAT, 96)};
        inputLayout_ = d->createInputLayout(attrs, uint32_t(std::size(attrs)), sceneVs_);

        nvrhi::BindingLayoutDesc bl;
        bl.visibility = nvrhi::ShaderType::All;
        bl.bindings = {nvrhi::BindingLayoutItem::VolatileConstantBuffer(0), nvrhi::BindingLayoutItem::Texture_SRV(0),
                       nvrhi::BindingLayoutItem::Sampler(0), nvrhi::BindingLayoutItem::Texture_SRV(5),
                       nvrhi::BindingLayoutItem::Texture_SRV(6), nvrhi::BindingLayoutItem::Texture_SRV(7)};
        if (rt_) {
            bl.bindings.push_back(nvrhi::BindingLayoutItem::RayTracingAccelStruct(1));
            bl.bindings.push_back(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(2));
            bl.bindings.push_back(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3));
            tlas_ = d->createAccelStruct(nvrhi::rt::AccelStructDesc().setTopLevelMaxInstances(2).setDebugName("scene tlas"));
            const std::vector<float> none(4, 0.0f);
            dynAttr_ = attrBuffer(none, "dynamic rt attributes");
            uploadBuffer(dynAttr_, none);
        }
        sceneLayout_ = d->createBindingLayout(bl);
        nvrhi::BindingLayoutDesc ol;
        ol.visibility = nvrhi::ShaderType::Pixel;
        ol.bindings = {nvrhi::BindingLayoutItem::PushConstants(0, sizeof(OutConsts)), nvrhi::BindingLayoutItem::Texture_SRV(0),
                       nvrhi::BindingLayoutItem::Sampler(0), nvrhi::BindingLayoutItem::Sampler(1),
                       nvrhi::BindingLayoutItem::Texture_SRV(1)};
        outLayout_ = d->createBindingLayout(ol);
        nvrhi::BindingLayoutDesc dl;
        dl.visibility = nvrhi::ShaderType::Pixel;
        dl.bindings = {nvrhi::BindingLayoutItem::PushConstants(0, sizeof(int) * 4),
                       nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::Sampler(0)};
        debugLayout_ = d->createBindingLayout(dl);
        debugVs_ = d->createShader({nvrhi::ShaderType::Vertex, "", "vs_main"}, g_debugview_vs, sizeof(g_debugview_vs));
        debugPs_ = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_main"}, g_debugview_ps, sizeof(g_debugview_ps));
        debugMsPs_ = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_main"}, g_debugview_ms_ps,
                                     sizeof(g_debugview_ms_ps));
        previewVs_ = d->createShader({nvrhi::ShaderType::Vertex, "", "vs_main"}, g_preview_vs, sizeof(g_preview_vs));
        previewPs_ = d->createShader({nvrhi::ShaderType::Pixel, "", "ps_main"}, g_preview_ps, sizeof(g_preview_ps));
        {
            auto pa = [](const char* name, nvrhi::Format f, uint32_t offset) {
                return nvrhi::VertexAttributeDesc().setName(name).setFormat(f).setOffset(offset).setElementStride(
                    sizeof(PreviewVertex));
            };
            const nvrhi::VertexAttributeDesc pattrs[] = {pa("POSITION", nvrhi::Format::RGB32_FLOAT, 0),
                                                         pa("UV", nvrhi::Format::RG32_FLOAT, 12),
                                                         pa("COLOR", nvrhi::Format::RGB32_FLOAT, 20)};
            previewInput_ = d->createInputLayout(pattrs, uint32_t(std::size(pattrs)), previewVs_);
            nvrhi::BindingLayoutDesc pl;
            pl.visibility = nvrhi::ShaderType::All;
            pl.bindings = {nvrhi::BindingLayoutItem::PushConstants(0, sizeof(PreviewConsts)),
                           nvrhi::BindingLayoutItem::Texture_SRV(0), nvrhi::BindingLayoutItem::Sampler(0)};
            previewLayout_ = d->createBindingLayout(pl);
        }

        createTargets(dev_.width(), dev_.height());
        buildHeightMap({}); // no room yet: no surfaces
        if (!opaque_ || !sky_pipe_ || !semi_[0] || !semi_[1] || !semi_[2] || !semi_[3] || !flakePipe_ || !blizzardPipe_ ||
            !height_) {
            SDL_Log("render: the scene pipelines failed to create");
            return false;
        }
        setAntiAliasing(AaBackend::None, AaQuality::NativeAA);
        return cl_ && frameCb_ && sceneVs_ && scenePs_ && outVs_ && outPs_ && inputLayout_;
    }

    ~NvrhiRenderer() override {
        if (dev_.device()) dev_.device()->waitForIdle();
    }

    bool upload(const std::vector<RenderVertex>& tris, const std::vector<uint8_t>& slices, int sliceCount,
                const std::array<uint32_t, 4>& semiStart, int sliceSize) override {
        sliceSize_ = sliceSize;
        sliceUpdates_.clear();
        staticGv_.clear();
        uvDirtyLo_ = UINT32_MAX;
        uvDirtyHi_ = 0;
        nvrhi::IDevice* d = dev_.device();
        d->waitForIdle();
        vertexCount_ = uint32_t(tris.size());
        semiStart_ = semiStart;
        if (semiStart_[3] == 0) semiStart_.fill(vertexCount_); // all opaque
        vbuf_ = nullptr;
        pages_ = nullptr;
        sceneSet_ = nullptr;
        staticBlas_ = nullptr;
        staticAttr_ = nullptr;
        if (tris.empty() || sliceCount <= 0) {
            buildHeightMap({}); // the last room's surfaces are gone
            return bool(height_);
        }

        std::vector<GpuVertex> gv;
        const std::vector<std::pair<size_t, size_t>> room{{0, tris.size()}};
        const std::vector<uint32_t> roomBits{kSurfaceScenery};
        toGpu(tris, nullptr, gv, nullptr, &room, &roomBits);
        nvrhi::BufferDesc bd;
        bd.byteSize = gv.size() * sizeof(GpuVertex);
        bd.isVertexBuffer = true;
        bd.isAccelStructBuildInput = rt_;
        bd.initialState = nvrhi::ResourceStates::VertexBuffer;
        bd.keepInitialState = true;
        bd.debugName = "room vertices";
        vbuf_ = d->createBuffer(bd);

        nvrhi::TextureDesc td;
        td.width = td.height = uint32_t(sliceSize);
        td.arraySize = uint32_t(sliceCount);
        td.dimension = nvrhi::TextureDimension::Texture2DArray;
        td.format = nvrhi::Format::RGBA8_UNORM;
        td.initialState = nvrhi::ResourceStates::ShaderResource;
        td.keepInitialState = true;
        td.debugName = "texture pages";
        pages_ = d->createTexture(td);
        if (!vbuf_ || !pages_) return false;

        nvrhi::CommandListHandle cl = d->createCommandList();
        cl->open();
        cl->writeBuffer(vbuf_, gv.data(), bd.byteSize);
        const size_t sliceBytes = size_t(sliceSize) * size_t(sliceSize) * 4;
        for (int i = 0; i < sliceCount; ++i)
            cl->writeTexture(pages_, uint32_t(i), 0, slices.data() + size_t(i) * sliceBytes, size_t(sliceSize) * 4);
        if (rt_) { // shadow casters: the opaque range (semi-transparent water / glass lets the sun through)
            std::vector<float> attr;
            toAttr(gv, semiStart_[0], attr);
            staticAttr_ = attrBuffer(attr, "room rt attributes");
            cl->writeBuffer(staticAttr_, attr.data(), attr.size() * sizeof(float));
            const uint32_t casters = semiStart_[0] / 3 * 3;
            if (casters) {
                const nvrhi::rt::GeometryDesc g = triangleGeometry(vbuf_, sequentialIndices(cl, casters), casters);
                staticBlas_ = d->createAccelStruct(nvrhi::rt::AccelStructDesc()
                                                       .addBottomLevelGeometry(g)
                                                       .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastTrace)
                                                       .setDebugName("room blas"));
                cl->buildBottomLevelAccelStruct(staticBlas_, &g, 1, nvrhi::rt::AccelStructBuildFlags::PreferFastTrace);
            }
        }
        cl->close();
        d->executeCommandList(cl);
        d->waitForIdle(); // the BLAS build scratch memory lives in cl: keep it until the GPU is done
        staticGv_ = std::move(gv); // CPU copy for updateStaticUvs
        makeSceneSet();
        buildHeightMap(tris);
        return bool(sceneSet_) && bool(height_);
    }

    void setSun(const SunLight& sun) override { sun_ = sun; }
    void setPrecipitation(const Precipitation& p) override { precip_ = p; }
    void setFocus(const std::vector<FocusBody>& bodies) override {
        focus_.assign(bodies.begin(), bodies.begin() + std::ptrdiff_t(std::min(bodies.size(), kMaxFocusBodies)));
    }
    bool rayTracingSupported() const override { return rt_; }

    void updateSlice(int slice, const std::vector<uint8_t>& rgba) override {
        if (pages_ && sliceSize_ == 256 && slice >= 0 && uint32_t(slice) < pages_->getDesc().arraySize &&
            rgba.size() == size_t(256) * 256 * 4)
            sliceUpdates_[slice] = rgba;
    }

    bool appendSlices(const std::vector<uint8_t>& rgba, int count) override {
        constexpr size_t kSliceBytes = size_t(256) * 256 * 4;
        if (!pages_ || sliceSize_ != 256 || count <= 0 || rgba.size() != size_t(count) * kSliceBytes) return false;
        nvrhi::IDevice* d = dev_.device();
        d->waitForIdle();
        nvrhi::TextureDesc td = pages_->getDesc();
        const uint32_t old = td.arraySize;
        td.arraySize = old + uint32_t(count);
        nvrhi::TextureHandle grown = d->createTexture(td);
        if (!grown) return false;
        nvrhi::CommandListHandle cl = d->createCommandList();
        cl->open();
        for (uint32_t i = 0; i < old; ++i) // the slices so far (their pending updateSlice writes still apply)
            cl->copyTexture(grown, nvrhi::TextureSlice().setArraySlice(i), pages_, nvrhi::TextureSlice().setArraySlice(i));
        for (int k = 0; k < count; ++k)
            cl->writeTexture(grown, old + uint32_t(k), 0, rgba.data() + size_t(k) * kSliceBytes, size_t(256) * 4);
        cl->close();
        d->executeCommandList(cl);
        d->waitForIdle();
        pages_ = grown;
        makeSceneSet();
        return bool(sceneSet_);
    }

    void updateStaticUvs(const std::vector<StaticUvPatch>& patches) override {
        for (const StaticUvPatch& p : patches) {
            if (p.vertex >= staticGv_.size()) continue;
            staticGv_[p.vertex].v.uv[0] = p.uv[0];
            staticGv_[p.vertex].v.uv[1] = p.uv[1];
            uvDirtyLo_ = std::min(uvDirtyLo_, p.vertex);
            uvDirtyHi_ = std::max(uvDirtyHi_, p.vertex + 1);
        }
    }
    // the dirty vertex range of updateStaticUvs into the room vertex buffer (+ the shadow rays' alpha-test UVs)
    void flushStaticUvs() {
        if (uvDirtyLo_ >= uvDirtyHi_ || !vbuf_) return;
        const uint32_t lo = uvDirtyLo_, hi = std::min<uint32_t>(uvDirtyHi_, uint32_t(staticGv_.size()));
        uvDirtyLo_ = UINT32_MAX;
        uvDirtyHi_ = 0;
        if (lo >= hi) return;
        cl_->writeBuffer(vbuf_, &staticGv_[lo], size_t(hi - lo) * sizeof(GpuVertex), uint64_t(lo) * sizeof(GpuVertex));
        const uint32_t casters = std::min(hi, semiStart_[0]);
        if (staticAttr_ && lo < casters) { // toAttr's layout: float4 (u, v, slice, 0) per opaque vertex
            std::vector<float> attr(size_t(casters - lo) * 4, 0.0f);
            for (uint32_t i = lo; i < casters; ++i) {
                attr[size_t(i - lo) * 4 + 0] = staticGv_[i].v.uv[0];
                attr[size_t(i - lo) * 4 + 1] = staticGv_[i].v.uv[1];
                attr[size_t(i - lo) * 4 + 2] = staticGv_[i].v.slice;
            }
            cl_->writeBuffer(staticAttr_, attr.data(), attr.size() * sizeof(float), uint64_t(lo) * 4 * sizeof(float));
        }
    }

    void setClearColor(float r, float g, float b) override {
        clear_[0] = r;
        clear_[1] = g;
        clear_[2] = b;
    }

    void setFog(const Fog& fog) override { fog_ = fog; }
    void setScreenRipples(const std::vector<ScreenRipple>& ripples) override { ripples_ = ripples; }
    void setScreenWave(float amplitude, float phase, const float tint[3], bool remaster, float seconds, bool heat) override {
        waveHeat_ = heat;
        wave_[0] = amplitude;
        wave_[1] = phase;
        waveRemaster_ = remaster;
        waveSeconds_ = seconds;
        for (int k = 0; k < 3; ++k) waveTint_[k] = tint[k];
    }

    void uploadSky(const std::vector<RenderVertex>& tris, bool screenSpace, bool isSky) override {
        skyScreen_ = screenSpace;
        skyIsSky_ = isSky;
        sky_ = tris; // GPU copy made in draw(): the dome's previous positions need the camera
    }

    void uploadDynamic(const std::vector<DynamicObject>& objects) override {
        // opaque objects first, then the blended ones by PS1 mode; dynStart_[m] = vertices with blend <= m
        std::vector<DynamicObject> sorted = objects;
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const DynamicObject& a, const DynamicObject& b) { return a.blend < b.blend; });
        dyn_.clear();
        dynStart_.fill(0);
        objectRanges_.clear();
        std::vector<std::pair<size_t, size_t>> models; // the objects' vertex ranges, smoothed per object
        // the surface bits by object: the model draws (exe 0x5f4980 / 0x5f3820) and the scenery (the room's segments
        // and doors, the static pool's objects)
        std::vector<std::pair<size_t, size_t>> surfaces;
        std::vector<uint32_t> surfaceBits;
        for (const DynamicObject& o : sorted) {
            const int b = std::min<int>(o.blend, 4);
            for (int m = b; m < 5; ++m) dynStart_[m] += uint32_t(o.tris.size());
            const uint32_t kind = o.id >> 24;
            if (kind != kKindEffect) // effects: particles / rings / billboards keep their face normals
                models.push_back({dyn_.size(), dyn_.size() + o.tris.size()});
            uint32_t bits = 0;
            if (kind == kKindActor || kind == kKindPlayer || kind == kKindPoolModel || kind == kKindWalker)
                bits |= kSurfaceModel;
            if (kind == kKindLive || (kind == kKindPoolModel && ((o.id & 0xffffffu) >> 8) == kStaticPoolModels))
                bits |= kSurfaceScenery;
            if (kind == kKindEffect) bits |= kSurfaceEffect;
            if (bits) {
                surfaces.push_back({dyn_.size(), dyn_.size() + o.tris.size()});
                surfaceBits.push_back(bits);
            }
            if (o.blend == 0) objectRanges_[o.id] = {uint32_t(dyn_.size()), uint32_t(o.tris.size())};
            dyn_.insert(dyn_.end(), o.tris.begin(), o.tris.end());
        }
        history_.apply(sorted, dynPrev_);
        toGpu(dyn_, dynPrev_.data(), dynGpu_, &models, &surfaces, &surfaceBits);
    }

    void resetHistory() override {
        history_.reset();
        historyValid_ = false;
        pendingReset_ = true;
    }

    void resize(int w, int h) override {
        if (w <= 0 || h <= 0) return; // minimised
        if (w == dev_.width() && h == dev_.height()) return;
        dev_.device()->waitForIdle();
        backFbs_.clear(); // framebuffers hold the swapchain images: ResizeBuffers fails while they live
        dev_.resize(w, h);
        broken_.clear(); // run-time failures can be size-specific (DLSS feature creation at 1x1)
        setAntiAliasing(wanted_, quality_); // recreates the upscaler and the targets at the new size
    }

    void setVsync(bool on) override { vsync_ = on; }

    void setAntiAliasing(AaBackend backend, AaQuality quality) override {
        nvrhi::IDevice* d = dev_.device();
        d->waitForIdle();
        wanted_ = backend;
        quality_ = quality;
        up_.reset();
        for (AaBackend b : backendCandidates(backend, broken_)) {
            std::unique_ptr<Upscaler> u = createUpscaler(b);
            if (u && u->init(d, dev_.width(), dev_.height(), quality)) {
                up_ = std::move(u);
                break;
            }
        }
        if (up_->backend() != backend && backend != AaBackend::Auto)
            SDL_Log("aa: %s unavailable, falling back to auto", aaBackendName(backend));
        int rw = dev_.width(), rh = dev_.height();
        up_->renderSize(dev_.width(), dev_.height(), quality, rw, rh);
        createTargets(rw, rh);
        frameIndex_ = 0;
        historyValid_ = false;
        pendingReset_ = true;
        SDL_Log("aa: %s %s render %dx%d -> output %dx%d", aaBackendName(up_->backend()), aaQualityName(quality), rw, rh,
                dev_.width(), dev_.height());
    }

    std::vector<AaBackend> availableBackends() override {
        if (available_.empty()) {
            dev_.device()->waitForIdle();
            for (AaBackend b : {AaBackend::Dlss, AaBackend::Fsr, AaBackend::Xess, AaBackend::Nis, AaBackend::Msaa,
                                AaBackend::Ssaa, AaBackend::None}) {
                std::unique_ptr<Upscaler> u = createUpscaler(b);
                if (u && u->init(dev_.device(), dev_.width(), dev_.height(), AaQuality::NativeAA)) available_.push_back(b);
            }
        }
        return available_;
    }

    AaBackend activeBackend() const override { return up_ ? up_->backend() : AaBackend::None; }

    void imguiInit() override { imgui_.init(&ui_); }
    void imguiNewFrame() override {}
    void imguiShutdown() override {}
    void renderImGui(ImDrawData* data) override {
        if (backFb_) imgui_.render(cl_, backFb_, data);
    }
    void renderCanvas(const ui::Canvas& c) override {
        if (backFb_)
            ui_.draw(cl_, backFb_, UiPass::Game, c.vertices().data(), c.vertices().size(), c.indices().data(),
                     c.indices().size(), c.cmds().data(), c.cmds().size(), {0.0f, 0.0f}, {c.width(), c.height()});
    }

    TextureHandle createTexture(int w, int h, const uint32_t* rgba) override {
        nvrhi::IDevice* d = dev_.device();
        nvrhi::TextureDesc td;
        td.width = uint32_t(w);
        td.height = uint32_t(h);
        td.format = nvrhi::Format::RGBA8_UNORM;
        td.initialState = nvrhi::ResourceStates::ShaderResource;
        td.keepInitialState = true;
        td.debugName = "ui texture";
        nvrhi::TextureHandle tex = d->createTexture(td);
        if (!tex) return 0;
        nvrhi::CommandListHandle cl = d->createCommandList();
        cl->open();
        cl->writeTexture(tex, 0, 0, rgba, size_t(w) * 4);
        cl->close();
        d->executeCommandList(cl);
        return ui_.addTexture(tex);
    }
    void updateTexture(TextureHandle tex, const uint32_t* rgba) override {
        nvrhi::ITexture* t = ui_.texture(tex);
        if (!t) return;
        nvrhi::IDevice* d = dev_.device();
        nvrhi::CommandListHandle cl = d->createCommandList();
        cl->open();
        cl->writeTexture(t, 0, 0, rgba, size_t(t->getDesc().width) * 4);
        cl->close();
        d->executeCommandList(cl);
    }
    void destroyTexture(TextureHandle tex) override {
        dev_.device()->waitForIdle();
        ui_.removeTexture(tex);
    }

    TextureHandle sceneGrab(int x, int y, int w, int h) override {
        if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return 0;
        if (!grabTex_ || grabW_ != w || grabH_ != h) {
            nvrhi::IDevice* d = dev_.device();
            if (grabId_) {
                d->waitForIdle();
                ui_.removeTexture(grabId_);
                grabId_ = 0;
            }
            nvrhi::TextureDesc td;
            td.width = uint32_t(w);
            td.height = uint32_t(h);
            td.format = dev_.backbuffer()->getDesc().format; // the back buffer's (copied, then sampled)
            td.initialState = nvrhi::ResourceStates::ShaderResource;
            td.keepInitialState = true;
            td.debugName = "scene grab";
            grabTex_ = d->createTexture(td);
            if (!grabTex_) return 0;
            grabW_ = w;
            grabH_ = h;
            grabId_ = ui_.addTexture(grabTex_);
        }
        grabX_ = x;
        grabY_ = y;
        grabPending_ = true;
        return grabId_;
    }

    TextureHandle renderPreview(int slot, int w, int h, const PreviewRequest& req) override {
        if (slot < 0 || slot >= int(std::size(preview_)) || w <= 0 || h <= 0 || w > 2048 || h > 2048) return 0;
        if (req.tris.empty() || req.atlasW <= 0 || req.atlasH <= 0 ||
            req.atlas.size() < size_t(req.atlasW) * size_t(req.atlasH) || !previewLayout_)
            return 0;
        PreviewSlot& ps = preview_[slot];
        nvrhi::IDevice* d = dev_.device();
        if (!ps.color || ps.w != w || ps.h != h) {
            d->waitForIdle();
            if (ps.id) ui_.removeTexture(ps.id);
            ps.color = makeTarget(w, h, nvrhi::Format::RGBA8_UNORM, "preview colour");
            ps.depth = makeTarget(w, h, kDepthFormat, "preview depth", true);
            ps.fb = d->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(ps.color).setDepthAttachment(ps.depth));
            ps.id = ui_.addTexture(ps.color);
            ps.w = w;
            ps.h = h;
        }
        if (!ps.atlas || ps.aw != req.atlasW || ps.ah != req.atlasH) {
            nvrhi::TextureDesc td;
            td.width = uint32_t(req.atlasW);
            td.height = uint32_t(req.atlasH);
            td.format = nvrhi::Format::RGBA8_UNORM;
            td.initialState = nvrhi::ResourceStates::ShaderResource;
            td.keepInitialState = true;
            td.debugName = "preview atlas";
            ps.atlas = d->createTexture(td);
            ps.aw = req.atlasW;
            ps.ah = req.atlasH;
            nvrhi::BindingSetDesc bs;
            bs.bindings = {nvrhi::BindingSetItem::PushConstants(0, sizeof(PreviewConsts)),
                           nvrhi::BindingSetItem::Texture_SRV(0, ps.atlas), nvrhi::BindingSetItem::Sampler(0, pointClamp_)};
            ps.set = d->createBindingSet(bs, previewLayout_);
        }
        if (!previewPipe_) {
            nvrhi::GraphicsPipelineDesc pd;
            pd.primType = nvrhi::PrimitiveType::TriangleList;
            pd.inputLayout = previewInput_;
            pd.VS = previewVs_;
            pd.PS = previewPs_;
            pd.bindingLayouts = {previewLayout_};
            pd.renderState.rasterState.setCullNone(); // the models' faces are wound either way
            pd.renderState.depthStencilState.depthTestEnable = true;
            pd.renderState.depthStencilState.depthWriteEnable = true;
            pd.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less;
            previewPipe_ = d->createGraphicsPipeline(pd, ps.fb->getFramebufferInfo());
        }
        ps.req = req;
        ps.pending = true;
        return ps.id;
    }

    void setDebugView(DebugView view) override { view_ = view; }

    bool motionSelfTest(const Camera& a, const Camera& b, float& maxErrPx, float& staticMaxPx) override {
        nvrhi::IDevice* d = dev_.device();
        if (!vbuf_ || !sceneSet_) return false;
        const std::vector<GpuVertex> dynKeep = dynGpu_;
        const std::vector<RenderVertex> skyKeep = sky_;
        dynGpu_.clear(); // static scene only: actors would move between the two frames
        jitter_ = {0.0f, 0.0f};
        sky_.clear();
        auto frame = [&](const Camera& cam, std::vector<float>* motion, std::vector<float>* depth) {
            cl_->open();
            clearTargets();
            drawScene(cam);
            nvrhi::StagingTextureHandle sm, sd;
            if (motion) sm = stage(t_.motion);
            if (depth) sd = stage(copyDepth());
            cl_->close();
            d->executeCommandList(cl_);
            d->waitForIdle();
            if (motion) readFloats(sm, nvrhi::Format::RG16_FLOAT, 2, *motion);
            if (depth) readFloats(sd, nvrhi::Format::R32_FLOAT, 1, *depth);
        };
        std::vector<float> mv, dep, still;
        resetHistory();
        frame(a, nullptr, nullptr);
        frame(b, &mv, &dep);
        frame(b, &still, nullptr);
        dynGpu_ = dynKeep;
        sky_ = skyKeep;
        resetHistory();

        const XMMATRIX vpA = cameraViewProj(a), vpB = cameraViewProj(b);
        XMFLOAT4X4 fa, fb;
        XMStoreFloat4x4(&fa, vpA);
        XMStoreFloat4x4(&fb, vpB);
        const XMMATRIX inv = XMMatrixInverse(nullptr, vpB);
        const int w = t_.renderW, h = t_.renderH;
        maxErrPx = 0.0f;
        staticMaxPx = 0.0f;
        int samples = 0;
        for (int y = 4; y < h; y += 8)
            for (int x = 4; x < w; x += 8) {
                const size_t i = size_t(y) * size_t(w) + size_t(x);
                staticMaxPx = std::max({staticMaxPx, std::fabs(still[i * 2]), std::fabs(still[i * 2 + 1])});
                const float z = dep[i];
                if (z >= 1.0f) continue; // cleared: sky / background
                const float nx = (float(x) + 0.5f) / float(w) * 2.0f - 1.0f;
                const float ny = 1.0f - (float(y) + 0.5f) / float(h) * 2.0f;
                XMFLOAT4 wp;
                XMStoreFloat4(&wp, XMVector4Transform(XMVectorSet(nx, ny, z, 1.0f), inv));
                const float world[3] = {wp.x / wp.w, wp.y / wp.w, wp.z / wp.w};
                float ref[2];
                motionPixels(&fb._11, &fa._11, world, w, h, ref);
                maxErrPx = std::max({maxErrPx, std::fabs(mv[i * 2] - ref[0]), std::fabs(mv[i * 2 + 1] - ref[1])});
                ++samples;
            }
        SDL_Log("motion selftest: %d samples", samples);
        return samples > 0;
    }

    void draw(const Camera& cam, std::vector<uint8_t>* readback, const std::function<void()>& overlay) override {
        if (up_->failed()) { // run-time failure last frame: never use that backend again this session
            SDL_Log("aa: %s failed at run time, switching", aaBackendName(up_->backend()));
            broken_.insert(up_->backend());
            setAntiAliasing(wanted_, quality_);
        }
        nvrhi::IDevice* d = dev_.device();
        nvrhi::ITexture* back = dev_.backbuffer();
        cl_->open();
        for (const auto& [slice, rgba] : sliceUpdates_) // animated CLUTs (updateSlice)
            cl_->writeTexture(pages_, uint32_t(slice), 0, rgba.data(), size_t(256) * 4);
        sliceUpdates_.clear();
        flushStaticUvs(); // the room UV animation (updateStaticUvs)
        for (PreviewSlot& ps : preview_) // renderPreview: before the overlay samples them
            if (ps.pending) drawPreview(ps);
        nearZ_ = cam.nearZ;
        camPitch_ = cam.pitch;
        tanHalfFov_ = std::tan(cam.fovY * 0.5f);
        clearTargets();
        jitter_ = up_->temporal() ? jitterAt(frameIndex_++, up_->phaseCount(t_.renderW, t_.outW)) : Jitter{0.0f, 0.0f};
        if (vbuf_ && sceneSet_) drawScene(cam);
        if (t_.samples > 1) { // aa=msaa: the samples resolved (averaged) into the render-size targets
            cl_->resolveTexture(t_.color, nvrhi::AllSubresources, t_.msColor, nvrhi::AllSubresources);
            cl_->resolveTexture(t_.motion, nvrhi::AllSubresources, t_.msMotion, nvrhi::AllSubresources);
            cl_->resolveTexture(t_.normal, nvrhi::AllSubresources, t_.msNormal, nvrhi::AllSubresources);
            cl_->resolveTexture(t_.reactive, nvrhi::AllSubresources, t_.msReactive, nvrhi::AllSubresources);
        }
        if (vbuf_ && sceneSet_ && precip_.blizzard > 0.0f) drawBlowingSnow();
        if (view_ == DebugView::Final) {
            upscale(cam);
            drawOutput(t_.upscaled, back);
        } else if (view_ == DebugView::Color) {
            drawOutput(t_.color, back);
        } else {
            drawDebug(back);
        }
        if (grabPending_) copyGrab(back); // sceneGrab: the picture without the overlay
        backFb_ = backFramebuffer(back);
        if (overlay) overlay(); // UI at output resolution, after the scene (and later the upscaler)
        backFb_ = nullptr;
        nvrhi::StagingTextureHandle staging;
        if (readback) {
            nvrhi::TextureDesc sd = back->getDesc();
            sd.isRenderTarget = false;
            sd.initialState = nvrhi::ResourceStates::CopyDest;
            sd.keepInitialState = true;
            staging = d->createStagingTexture(sd, nvrhi::CpuAccessMode::Read);
            cl_->copyTexture(staging, nvrhi::TextureSlice(), back, nvrhi::TextureSlice());
        }
        cl_->close();
        d->executeCommandList(cl_);
        if (readback) readBack(staging, *readback);
        dev_.present(vsync_);
    }

private:
    void drawPreview(PreviewSlot& ps) {
        ps.pending = false;
        const PreviewRequest& r = ps.req;
        const uint64_t bytes = r.tris.size() * sizeof(PreviewVertex);
        if (!ps.vb || ps.vb->getDesc().byteSize < bytes) {
            nvrhi::BufferDesc bd;
            bd.byteSize = bytes + bytes / 2;
            bd.isVertexBuffer = true;
            bd.initialState = nvrhi::ResourceStates::VertexBuffer;
            bd.keepInitialState = true;
            bd.debugName = "preview vertices";
            ps.vb = dev_.device()->createBuffer(bd);
        }
        cl_->writeBuffer(ps.vb, r.tris.data(), bytes);
        cl_->writeTexture(ps.atlas, 0, 0, r.atlas.data(), size_t(r.atlasW) * 4);
        cl_->clearTextureFloat(ps.color, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f));
        cl_->clearDepthStencilTexture(ps.depth, nvrhi::AllSubresources, true, 1.0f, kDepthHasStencil, 0);
        nvrhi::GraphicsState gs;
        gs.pipeline = previewPipe_;
        gs.framebuffer = ps.fb;
        gs.viewport.addViewportAndScissorRect(nvrhi::Viewport(float(ps.w), float(ps.h)));
        gs.bindings = {ps.set};
        gs.vertexBuffers = {{ps.vb, 0, 0}};
        cl_->setGraphicsState(gs);
        PreviewConsts c{};
        std::memcpy(&c.viewProj, r.viewProj, sizeof c.viewProj);
        std::memcpy(c.eye, r.eye, sizeof r.eye);
        std::memcpy(c.light, r.light, sizeof r.light);
        cl_->setPushConstants(&c, sizeof c);
        cl_->draw(nvrhi::DrawArguments().setVertexCount(uint32_t(r.tris.size())));
    }

    // sceneGrab's copy: the requested rect clipped to the back buffer, at the same offset in the grab texture
    void copyGrab(nvrhi::ITexture* back) {
        grabPending_ = false;
        if (!grabTex_) return;
        const auto& bd = back->getDesc();
        const int x0 = std::max(grabX_, 0), y0 = std::max(grabY_, 0);
        const int x1 = std::min(grabX_ + grabW_, int(bd.width)), y1 = std::min(grabY_ + grabH_, int(bd.height));
        if (x1 <= x0 || y1 <= y0) return;
        nvrhi::TextureSlice dst, src;
        dst.setOrigin(uint32_t(x0 - grabX_), uint32_t(y0 - grabY_)).setSize(uint32_t(x1 - x0), uint32_t(y1 - y0), 1);
        src.setOrigin(uint32_t(x0), uint32_t(y0)).setSize(uint32_t(x1 - x0), uint32_t(y1 - y0), 1);
        cl_->copyTexture(grabTex_, dst, back, src);
    }

    struct Targets {
        nvrhi::TextureHandle color, motion, normal, reactive, depth, upscaled;
        // aa=msaa: the scene draws into these (and a multisampled depth), resolved into color..reactive
        nvrhi::TextureHandle msColor, msMotion, msNormal, msReactive;
        int samples = 1;
        nvrhi::FramebufferHandle scene;
        nvrhi::FramebufferHandle post; // color + reactive at render size (after the MSAA resolve): the blown snow
        int renderW = 0, renderH = 0, outW = 0, outH = 0;
    };

    nvrhi::TextureHandle makeTarget(int w, int h, nvrhi::Format f, const char* name, bool depth = false, int samples = 1) {
        nvrhi::TextureDesc td;
        td.sampleCount = uint32_t(samples);
        if (samples > 1) td.dimension = nvrhi::TextureDimension::Texture2DMS;
        td.width = uint32_t(w);
        td.height = uint32_t(h);
        td.format = f;
        td.isRenderTarget = true;
        td.isShaderResource = true;
        td.initialState = depth ? nvrhi::ResourceStates::DepthWrite : nvrhi::ResourceStates::RenderTarget;
        td.keepInitialState = true;
        td.setClearValue(nvrhi::Color(depth ? 1.0f : 0.0f));
        td.debugName = name;
        return dev_.device()->createTexture(td);
    }

    // render-size G-buffer + output-size upscaled target; pipelines are made once (formats never change)
    void createTargets(int rw, int rh) {
        nvrhi::IDevice* d = dev_.device();
        d->waitForIdle();
        const int ow = dev_.width(), oh = dev_.height();
        const int prevSamples = t_.samples, samples = up_ ? up_->sampleCount() : 1;
        t_ = {};
        t_.samples = samples;
        t_.renderW = rw;
        t_.renderH = rh;
        t_.outW = ow;
        t_.outH = oh;
        t_.color = makeTarget(rw, rh, nvrhi::Format::RGBA16_FLOAT, "color");
        t_.motion = makeTarget(rw, rh, nvrhi::Format::RG16_FLOAT, "motion");
        t_.normal = makeTarget(rw, rh, nvrhi::Format::R10G10B10A2_UNORM, "normal");
        t_.reactive = makeTarget(rw, rh, nvrhi::Format::R8_UNORM, "reactive");
        t_.depth = makeTarget(rw, rh, kDepthFormat, "depth", true, samples);
        if (samples > 1) {
            t_.msColor = makeTarget(rw, rh, nvrhi::Format::RGBA16_FLOAT, "ms color", false, samples);
            t_.msMotion = makeTarget(rw, rh, nvrhi::Format::RG16_FLOAT, "ms motion", false, samples);
            t_.msNormal = makeTarget(rw, rh, nvrhi::Format::R10G10B10A2_UNORM, "ms normal", false, samples);
            t_.msReactive = makeTarget(rw, rh, nvrhi::Format::R8_UNORM, "ms reactive", false, samples);
        }
        {
            nvrhi::TextureDesc ud;
            ud.width = uint32_t(ow);
            ud.height = uint32_t(oh);
            ud.format = nvrhi::Format::RGBA16_FLOAT;
            ud.isShaderResource = true;
            ud.isUAV = true; // written by the upscalers' compute passes
            ud.initialState = nvrhi::ResourceStates::ShaderResource;
            ud.keepInitialState = true;
            ud.debugName = "upscaled";
            t_.upscaled = d->createTexture(ud);
        }
        const bool ms = samples > 1;
        t_.scene = d->createFramebuffer(nvrhi::FramebufferDesc()
                                            .addColorAttachment(ms ? t_.msColor : t_.color)
                                            .addColorAttachment(ms ? t_.msMotion : t_.motion)
                                            .addColorAttachment(ms ? t_.msNormal : t_.normal)
                                            .addColorAttachment(ms ? t_.msReactive : t_.reactive)
                                            .setDepthAttachment(t_.depth));
        t_.post = d->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(t_.color).addColorAttachment(t_.reactive));
        blizzardSet_ = nullptr; // it binds the old depth copy
        if (!blizzardPipe_) { // the blown snow over the resolved colour: premultiplied (One / SrcAlpha), reactive = max
            nvrhi::GraphicsPipelineDesc bp;
            bp.primType = nvrhi::PrimitiveType::TriangleList;
            bp.VS = fullVs_;
            bp.PS = blizzardPs_;
            bp.bindingLayouts = {precipLayout_};
            bp.renderState.rasterState.setCullNone();
            bp.renderState.depthStencilState.depthTestEnable = false;
            bp.renderState.depthStencilState.depthWriteEnable = false;
            auto& c = bp.renderState.blendState.targets[0];
            c.blendEnable = true;
            c.srcBlend = nvrhi::BlendFactor::One;
            c.destBlend = nvrhi::BlendFactor::SrcAlpha;
            c.colorWriteMask = nvrhi::ColorMask::Red | nvrhi::ColorMask::Green | nvrhi::ColorMask::Blue;
            auto& r = bp.renderState.blendState.targets[1];
            r.blendEnable = true;
            r.srcBlend = r.destBlend = r.srcBlendAlpha = r.destBlendAlpha = nvrhi::BlendFactor::One;
            r.blendOp = r.blendOpAlpha = nvrhi::BlendOp::Max;
            blizzardPipe_ = d->createGraphicsPipeline(bp, t_.post->getFramebufferInfo());
        }
        if (samples != prevSamples) opaque_ = nullptr; // the scene pipelines carry the sample count
        outSets_.clear();
        debugSets_.clear();
        backFbs_.clear();
        if (!opaque_) createScenePipelines();
    }

    void createScenePipelines() {
        nvrhi::IDevice* d = dev_.device();
        nvrhi::GraphicsPipelineDesc pd;
        pd.primType = nvrhi::PrimitiveType::TriangleList;
        pd.inputLayout = inputLayout_;
        pd.VS = sceneVs_;
        pd.PS = scenePs_;
        pd.bindingLayouts = {sceneLayout_};
        pd.renderState.rasterState.setCullNone(); // PS1 strips have inconsistent winding
        auto& ds = pd.renderState.depthStencilState;
        ds.depthTestEnable = true;
        ds.depthWriteEnable = true;
        ds.depthFunc = nvrhi::ComparisonFunc::Less;
        const nvrhi::FramebufferInfo& fb = t_.scene->getFramebufferInfo();
        opaque_ = d->createGraphicsPipeline(pd, fb);

        nvrhi::GraphicsPipelineDesc sky = pd; // drawn first, behind everything, no depth
        sky.renderState.depthStencilState.depthTestEnable = false;
        sky.renderState.depthStencilState.depthWriteEnable = false;
        sky_pipe_ = d->createGraphicsPipeline(sky, fb);

        // PS1 blend modes on (B)ackground and (F)oreground as the PC exe sets them (0x402280): 0 B/2+F/2, 1 B+F,
        // 2 B*(1-F), 3 B+F/4 -- the fragment's alpha is the exe's vertex alpha (0x402100: 0x80 / 0x40, see
        // scene.hlsl blendMode; not the blend constant: NVRHI sets OMSetBlendFactor only when the value changes
        // since the last state, so the opaque draws' value never reached the semi pipelines and it stayed 1 = B+F);
        // no motion / normal, reactive = max(dst, this draw's value); depth test, no depth write
        for (int mode = 0; mode < 4; ++mode) {
            nvrhi::GraphicsPipelineDesc sp = pd;
            sp.renderState.depthStencilState.depthWriteEnable = false;
            auto& c = sp.renderState.blendState.targets[0];
            c.blendEnable = true;
            using BF = nvrhi::BlendFactor;
            static constexpr BF kSrc[4] = {BF::SrcAlpha, BF::One, BF::Zero, BF::SrcAlpha};
            static constexpr BF kDst[4] = {BF::InvSrcAlpha, BF::One, BF::InvSrcColor, BF::One};
            c.srcBlend = kSrc[mode];
            c.destBlend = kDst[mode];
            c.blendOp = nvrhi::BlendOp::Add;
            c.colorWriteMask = nvrhi::ColorMask::Red | nvrhi::ColorMask::Green | nvrhi::ColorMask::Blue;
            c.srcBlendAlpha = nvrhi::BlendFactor::One;
            c.destBlendAlpha = nvrhi::BlendFactor::Zero;
            sp.renderState.blendState.targets[1].colorWriteMask = nvrhi::ColorMask(0);
            sp.renderState.blendState.targets[2].colorWriteMask = nvrhi::ColorMask(0);
            auto& r = sp.renderState.blendState.targets[3];
            r.blendEnable = true;
            r.srcBlend = r.destBlend = nvrhi::BlendFactor::One;
            r.blendOp = nvrhi::BlendOp::Max;
            r.srcBlendAlpha = r.destBlendAlpha = nvrhi::BlendFactor::One;
            r.blendOpAlpha = nvrhi::BlendOp::Max;
            semi_[mode] = d->createGraphicsPipeline(sp, fb);
        }

        // the precipitation's flakes (no vertex buffer: made from the vertex id): alpha blended over the scene, behind
        // what is nearer (depth test, no depth write), no motion / normal, reactive = max
        nvrhi::GraphicsPipelineDesc fp;
        fp.primType = nvrhi::PrimitiveType::TriangleList;
        fp.VS = flakeVs_;
        fp.PS = flakePs_;
        fp.bindingLayouts = {precipLayout_};
        fp.renderState.rasterState.setCullNone();
        fp.renderState.depthStencilState.depthTestEnable = true;
        fp.renderState.depthStencilState.depthWriteEnable = false;
        fp.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less;
        auto& c = fp.renderState.blendState.targets[0];
        c.blendEnable = true;
        c.srcBlend = nvrhi::BlendFactor::SrcAlpha;
        c.destBlend = nvrhi::BlendFactor::InvSrcAlpha;
        c.colorWriteMask = nvrhi::ColorMask::Red | nvrhi::ColorMask::Green | nvrhi::ColorMask::Blue;
        fp.renderState.blendState.targets[1].colorWriteMask = nvrhi::ColorMask(0);
        fp.renderState.blendState.targets[2].colorWriteMask = nvrhi::ColorMask(0);
        auto& r = fp.renderState.blendState.targets[3];
        r.blendEnable = true;
        r.srcBlend = r.destBlend = r.srcBlendAlpha = r.destBlendAlpha = nvrhi::BlendFactor::One;
        r.blendOp = r.blendOpAlpha = nvrhi::BlendOp::Max;
        flakePipe_ = d->createGraphicsPipeline(fp, fb);
    }

    // The precipitation's height map (renderer.hpp Precipitation): the room's surfaces seen from above (an orthographic
    // depth map, the highest surface per texel), over the room's x / z bounds at kHeightTexels on the longer side.
    // No geometry: one texel without a surface
    void buildHeightMap(const std::vector<RenderVertex>& tris) {
        nvrhi::IDevice* d = dev_.device();
        float lo[3] = {FLT_MAX, FLT_MAX, FLT_MAX}, hi[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
        for (const RenderVertex& v : tris)
            for (int c = 0; c < 3; ++c) {
                lo[c] = std::min(lo[c], v.pos[c]);
                hi[c] = std::max(hi[c], v.pos[c]);
            }
        const bool any = !tris.empty() && hi[0] > lo[0] && hi[2] > lo[2];
        int tw = 1, th = 1;
        if (any) {
            const float texel = std::max(hi[0] - lo[0], hi[2] - lo[2]) / float(kHeightTexels);
            tw = std::clamp(int(std::ceil((hi[0] - lo[0]) / texel)), 1, kHeightTexels);
            th = std::clamp(int(std::ceil((hi[2] - lo[2]) / texel)), 1, kHeightTexels);
            // a margin above / below: a surface exactly at the bottom keeps a depth below 1 (1 = no surface)
            heightBounds_[0] = lo[0];
            heightBounds_[1] = lo[2];
            heightBounds_[2] = 1.0f / (hi[0] - lo[0]);
            heightBounds_[3] = 1.0f / (hi[2] - lo[2]);
            heightRange_[0] = hi[1] + 1.0f;
            heightRange_[1] = hi[1] - lo[1] + 2.0f;
        } else {
            std::fill(std::begin(heightBounds_), std::end(heightBounds_), 0.0f);
            heightRange_[0] = 0.0f;
            heightRange_[1] = 1.0f;
        }
        heightRange_[2] = float(tw);
        heightRange_[3] = float(th);
        flakeSet_ = blizzardSet_ = nullptr; // they bind the old map
        height_ = makeTarget(tw, th, nvrhi::Format::D32, "precipitation height map", true);
        if (!height_) return;
        nvrhi::FramebufferHandle fb = d->createFramebuffer(nvrhi::FramebufferDesc().setDepthAttachment(height_));
        if (!heightPipe_) {
            nvrhi::GraphicsPipelineDesc hp;
            hp.primType = nvrhi::PrimitiveType::TriangleList;
            hp.inputLayout = inputLayout_;
            hp.VS = heightVs_;
            hp.PS = heightPs_;
            hp.bindingLayouts = {sceneLayout_};
            hp.renderState.rasterState.setCullNone();
            hp.renderState.depthStencilState.depthTestEnable = true;
            hp.renderState.depthStencilState.depthWriteEnable = true;
            hp.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less; // the highest surface
            heightPipe_ = d->createGraphicsPipeline(hp, fb->getFramebufferInfo());
        }
        nvrhi::CommandListHandle cl = d->createCommandList();
        cl->open();
        cl->clearDepthStencilTexture(height_, nvrhi::AllSubresources, true, 1.0f, false, 0);
        if (any && vbuf_ && sceneSet_ && heightPipe_) {
            FrameConstants f{};
            std::copy(std::begin(heightBounds_), std::end(heightBounds_), f.heightBounds);
            std::copy(std::begin(heightRange_), std::end(heightRange_), f.heightRange);
            cl->writeBuffer(frameCb_, &f, sizeof f);
            nvrhi::GraphicsState gs;
            gs.pipeline = heightPipe_;
            gs.framebuffer = fb;
            gs.viewport.addViewportAndScissorRect(nvrhi::Viewport(float(tw), float(th)));
            gs.bindings = {sceneSet_};
            gs.vertexBuffers = {nvrhi::VertexBufferBinding().setBuffer(vbuf_).setSlot(0).setOffset(0)};
            cl->setGraphicsState(gs);
            cl->draw(nvrhi::DrawArguments().setVertexCount(vertexCount_));
        }
        cl->close();
        d->executeCommandList(cl);
    }

    bool precipitating() const { return precip_.snow > 0.0f || precip_.blizzard > 0.0f; }

    // the flakes' / the blown snow's bindings: the frame, the height map, the depth copy
    nvrhi::BindingSetHandle precipSet() {
        nvrhi::BindingSetDesc bs;
        bs.bindings = {nvrhi::BindingSetItem::ConstantBuffer(0, frameCb_), nvrhi::BindingSetItem::Texture_SRV(4, height_),
                       nvrhi::BindingSetItem::Texture_SRV(5, depthCopy_)};
        return dev_.device()->createBindingSet(bs, precipLayout_);
    }

    // the flakes' light: the place's (its fog colour, or the background) -- a dark place has dim snow
    float precipLight() const {
        const float* c = fog_.mode != FogMode::Off ? fog_.color : clear_;
        return std::clamp(0.3f + 0.8f * std::max({c[0], c[1], c[2]}), 0.45f, 1.0f);
    }

    // the blown snow over the resolved scene colour (Precipitation::blizzard), before the upscaler
    void drawBlowingSnow() {
        copyDepth();
        if (!blizzardSet_) blizzardSet_ = precipSet();
        writeFrame(lastVpJit_, lastVp_, prevVp_, lastEye_, lastEye_, lastFwd_, 1.0f, true, 0.0f, 0.0f);
        nvrhi::GraphicsState gs;
        gs.pipeline = blizzardPipe_;
        gs.framebuffer = t_.post;
        gs.viewport.addViewportAndScissorRect(nvrhi::Viewport(float(t_.renderW), float(t_.renderH)));
        gs.bindings = {blizzardSet_};
        cl_->setGraphicsState(gs);
        cl_->draw(nvrhi::DrawArguments().setVertexCount(3));
    }

    void upscale(const Camera& cam) {
        const auto now = std::chrono::steady_clock::now();
        const float dtMs = lastFrame_.time_since_epoch().count()
                               ? std::chrono::duration<float, std::milli>(now - lastFrame_).count()
                               : 16.7f;
        lastFrame_ = now;
        UpscalerInputs in{};
        in.color = t_.color;
        in.depth = copyDepth(); // R32F: every SDK reads float depth, D24S8 views are not portable
        in.motion = t_.motion;
        in.reactive = t_.reactive;
        in.output = t_.upscaled;
        in.jitterX = jitter_.x;
        in.jitterY = jitter_.y;
        in.renderW = t_.renderW;
        in.renderH = t_.renderH;
        in.outW = t_.outW;
        in.outH = t_.outH;
        in.frameTimeMs = std::clamp(dtMs, 1.0f, 100.0f);
        in.nearZ = cam.nearZ;
        in.farZ = kFar;
        in.fovY = cam.fovY;
        in.reset = pendingReset_;
        if (pendingReset_) SDL_Log("aa: history reset");
        pendingReset_ = false;
        up_->evaluate(cl_, in);
        cl_->clearState(); // vendor SDKs change heaps / root signatures behind NVRHI
    }

    // scene.hlsl / output.hlsl waterMedium: the far water along a view direction of height dirY
    static void waterMedium(const float tint[3], float dirY, float out[3]) {
        const float m = std::max({tint[0], tint[1], tint[2], 1e-3f});
        float t = std::clamp((dirY + 0.65f) / 1.5f, 0.0f, 1.0f);
        t = t * t * (3.0f - 2.0f * t);
        const float lo[3] = {0.025f, 0.08f, 0.12f}, hi[3] = {0.15f, 0.38f, 0.46f};
        for (int k = 0; k < 3; ++k) out[k] = tint[k] / m * (lo[k] + (hi[k] - lo[k]) * t);
    }
    // scene.hlsl / output.hlsl heatMedium: the hot air far away (lava glow at the horizon, dark smoke above)
    static void heatMedium(const float tint[3], float dirY, float out[3]) {
        const float m = std::max({tint[0], tint[1], tint[2], 1e-3f});
        float t = std::clamp((dirY + 0.3f) / 1.0f, 0.0f, 1.0f);
        t = t * t * (3.0f - 2.0f * t);
        const float lo[3] = {0.34f, 0.13f, 0.05f}, hi[3] = {0.06f, 0.03f, 0.025f};
        for (int k = 0; k < 3; ++k) out[k] = (1.0f + (tint[k] / m - 1.0f) * 0.3f) * (lo[k] + (hi[k] - lo[k]) * t);
    }

    // hot air under an open sky (the desert: sky layers or a bright background colour) keeps the sky; only the lava
    // caves' empty black void is filled with the hot air's smoke / glow
    bool heatOpenSky() const {
        return waveHeat_ && (!sky_.empty() || clear_[0] * 0.3f + clear_[1] * 0.59f + clear_[2] * 0.11f > 0.08f);
    }

    void clearTargets() {
        float clear[3] = {clear_[0], clear_[1], clear_[2]};
        // underwater (remaster): the empty background starts as the far water at the view's height, so the upscaler
        // and the blur never blend a black void into the edges of the room (the output pass paints the gradient)
        if (waveRemaster_ && wave_[0] > 0.0f && !heatOpenSky())
            (waveHeat_ ? heatMedium : waterMedium)(waveTint_, std::sin(camPitch_), clear);
        const bool ms = t_.samples > 1;
        cl_->clearTextureFloat(ms ? t_.msColor : t_.color, nvrhi::AllSubresources,
                               nvrhi::Color(clear[0], clear[1], clear[2], 1.0f));
        cl_->clearTextureFloat(ms ? t_.msMotion : t_.motion, nvrhi::AllSubresources, nvrhi::Color(0.0f));
        cl_->clearTextureFloat(ms ? t_.msNormal : t_.normal, nvrhi::AllSubresources, nvrhi::Color(0.0f));
        cl_->clearTextureFloat(ms ? t_.msReactive : t_.reactive, nvrhi::AllSubresources, nvrhi::Color(0.0f));
        cl_->clearDepthStencilTexture(t_.depth, nvrhi::AllSubresources, true, 1.0f, kDepthHasStencil, 0);
        depthFresh_ = false;
    }

    // fullscreen debugview pass of src (mode, see debugview.hlsl) into fb
    void debugPass(nvrhi::ITexture* src, int mode, nvrhi::IFramebuffer* fb, float w, float h) {
        const nvrhi::FramebufferInfo& info = fb->getFramebufferInfo();
        const bool ms = src->getDesc().sampleCount > 1; // the MSAA depth: debugview.hlsl OD_MSAA
        nvrhi::GraphicsPipelineHandle& pipe =
            debugPipes_[(info.colorFormats.empty() ? 0 : int(info.colorFormats[0])) * 2 + (ms ? 1 : 0)];
        if (!pipe) {
            nvrhi::GraphicsPipelineDesc pd;
            pd.primType = nvrhi::PrimitiveType::TriangleList;
            pd.VS = debugVs_;
            pd.PS = ms ? debugMsPs_ : debugPs_;
            pd.bindingLayouts = {debugLayout_};
            pd.renderState.rasterState.setCullNone();
            pd.renderState.depthStencilState.depthTestEnable = false;
            pd.renderState.depthStencilState.depthWriteEnable = false;
            pipe = dev_.device()->createGraphicsPipeline(pd, info);
        }
        auto it = debugSets_.find(src);
        if (it == debugSets_.end()) {
            nvrhi::BindingSetDesc bs;
            bs.bindings = {nvrhi::BindingSetItem::PushConstants(0, sizeof(int) * 4),
                           nvrhi::BindingSetItem::Texture_SRV(0, src), nvrhi::BindingSetItem::Sampler(0, pointClamp_)};
            it = debugSets_.emplace(src, dev_.device()->createBindingSet(bs, debugLayout_)).first;
        }
        nvrhi::GraphicsState gs;
        gs.pipeline = pipe;
        gs.framebuffer = fb;
        gs.viewport.addViewportAndScissorRect(nvrhi::Viewport(w, h));
        gs.bindings = {it->second};
        cl_->setGraphicsState(gs);
        const int params[4] = {mode, 0, 0, 0};
        cl_->setPushConstants(params, sizeof params);
        cl_->draw(nvrhi::DrawArguments().setVertexCount(3));
    }

    void drawDebug(nvrhi::ITexture* back) {
        nvrhi::ITexture* src = t_.color;
        switch (view_) {
        case DebugView::Motion: src = t_.motion; break;
        case DebugView::Normal: src = t_.normal; break;
        case DebugView::Depth: src = t_.depth; break;
        case DebugView::Reactive: src = t_.reactive; break;
        default: break;
        }
        debugPass(src, int(view_) - 1, backFramebuffer(back), float(dev_.width()), float(dev_.height()));
    }

    // the R32F copy of the depth (render size): the soft effects, the precipitation, the upscalers and the CPU readback
    // read it; made / resized with the binding sets that bind it
    bool depthCopyStale() const {
        return !depthCopy_ || depthCopy_->getDesc().width != uint32_t(t_.renderW) ||
               depthCopy_->getDesc().height != uint32_t(t_.renderH);
    }
    void makeDepthCopy() {
        depthCopy_ = makeTarget(t_.renderW, t_.renderH, nvrhi::Format::R32_FLOAT, "depth copy");
        depthCopyFb_ = dev_.device()->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(depthCopy_));
        for (size_t k = 0; k < kMaxFocusBodies; ++k) {
            focusMask_[k] = makeTarget(t_.renderW, t_.renderH, nvrhi::Format::R32_FLOAT, "focus depth");
            focusMaskFb_[k] = dev_.device()->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(focusMask_[k]));
        }
        outSets_.clear(); // they bind the old copy
        flakeSet_ = blizzardSet_ = nullptr;
        makeSceneSet();
    }
    // D24 depth -> the R32F copy
    nvrhi::ITexture* copyDepth() {
        if (depthCopyStale()) makeDepthCopy();
        debugPass(t_.depth, 5, depthCopyFb_, float(t_.renderW), float(t_.renderH));
        depthFresh_ = true;
        return depthCopy_;
    }

    nvrhi::StagingTextureHandle stage(nvrhi::ITexture* tex) {
        nvrhi::TextureDesc sd; // plain readback copy: no render-target / clear-value flags
        sd.width = tex->getDesc().width;
        sd.height = tex->getDesc().height;
        sd.format = tex->getDesc().format;
        sd.initialState = nvrhi::ResourceStates::CopyDest;
        sd.keepInitialState = true;
        nvrhi::StagingTextureHandle s = dev_.device()->createStagingTexture(sd, nvrhi::CpuAccessMode::Read);
        cl_->copyTexture(s, nvrhi::TextureSlice(), tex, nvrhi::TextureSlice());
        return s;
    }

    // RG16F / R32F staging texture -> floats, `channels` per pixel, rows tightly packed
    void readFloats(nvrhi::IStagingTexture* s, nvrhi::Format f, int channels, std::vector<float>& out) {
        size_t pitch = 0;
        const auto* p = static_cast<const uint8_t*>(
            dev_.device()->mapStagingTexture(s, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &pitch));
        const int w = t_.renderW, h = t_.renderH;
        out.assign(size_t(w) * size_t(h) * size_t(channels), 0.0f);
        if (!p) return;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w * channels; ++x) {
                const uint8_t* row = p + size_t(y) * pitch;
                float v;
                if (f == nvrhi::Format::R32_FLOAT) {
                    std::memcpy(&v, row + size_t(x) * 4, 4);
                } else { // half
                    uint16_t hbits;
                    std::memcpy(&hbits, row + size_t(x) * 2, 2);
                    v = PackedVector::XMConvertHalfToFloat(hbits);
                }
                out[size_t(y) * size_t(w) * size_t(channels) + size_t(x)] = v;
            }
        dev_.device()->unmapStagingTexture(s);
    }

    // the camera's own up vector (d fwd / d pitch): equal to (0, 1, 0) re-orthogonalised for |pitch| < 90 degrees, and
    // still defined when looking straight down (the travel map ST01: pitch -0x400, where (0, 1, 0) is degenerate)
    static XMVECTOR cameraUp(const Camera& cam) {
        const XMVECTOR up = XMVectorSet(-std::sin(cam.yaw) * std::sin(cam.pitch), std::cos(cam.pitch),
                                        -std::cos(cam.yaw) * std::sin(cam.pitch), 0.0f);
        if (cam.roll == 0.0f) return up;
        // rolled about the forward axis: up cos r + right sin r (right = up x fwd, left-handed)
        const XMVECTOR fwd = XMVectorSet(std::sin(cam.yaw) * std::cos(cam.pitch), std::sin(cam.pitch),
                                         std::cos(cam.yaw) * std::cos(cam.pitch), 0.0f);
        const XMVECTOR right = XMVector3Cross(up, fwd);
        return XMVectorAdd(XMVectorScale(up, std::cos(cam.roll)), XMVectorScale(right, std::sin(cam.roll)));
    }
    XMMATRIX cameraViewProj(const Camera& cam) const {
        const XMVECTOR eye = XMVectorSet(cam.eye[0], cam.eye[1], cam.eye[2], 1.0f);
        const XMVECTOR fwd = XMVectorSet(std::sin(cam.yaw) * std::cos(cam.pitch), std::sin(cam.pitch),
                                         std::cos(cam.yaw) * std::cos(cam.pitch), 0.0f);
        return XMMatrixLookToLH(eye, fwd, cameraUp(cam)) *
               XMMatrixPerspectiveFovLH(cam.fovY, float(t_.renderW) / float(t_.renderH), cam.nearZ, kFar);
    }

    nvrhi::IFramebuffer* backFramebuffer(nvrhi::ITexture* back) {
        auto it = backFbs_.find(back);
        if (it != backFbs_.end()) return it->second;
        nvrhi::FramebufferHandle fb = dev_.device()->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(back));
        if (!outPipe_) {
            nvrhi::GraphicsPipelineDesc pd;
            pd.primType = nvrhi::PrimitiveType::TriangleList;
            pd.VS = outVs_;
            pd.PS = outPs_;
            pd.bindingLayouts = {outLayout_};
            pd.renderState.rasterState.setCullNone();
            pd.renderState.depthStencilState.depthTestEnable = false;
            pd.renderState.depthStencilState.depthWriteEnable = false;
            outPipe_ = dev_.device()->createGraphicsPipeline(pd, fb->getFramebufferInfo());
        }
        backFbs_[back] = fb;
        return fb;
    }

    // grow-only vertex buffer rewritten each frame through the command list
    void stream(nvrhi::BufferHandle& buf, const std::vector<GpuVertex>& v, const char* name, bool rtInput = false) {
        if (v.empty()) return;
        const uint64_t bytes = v.size() * sizeof(GpuVertex);
        if (!buf || buf->getDesc().byteSize < bytes) {
            nvrhi::BufferDesc bd;
            bd.byteSize = bytes + bytes / 2;
            bd.isVertexBuffer = true;
            bd.isAccelStructBuildInput = rtInput;
            bd.initialState = nvrhi::ResourceStates::VertexBuffer;
            bd.keepInitialState = true;
            bd.debugName = name;
            buf = dev_.device()->createBuffer(bd);
        }
        cl_->writeBuffer(buf, v.data(), bytes);
    }

    // shared index buffer 0, 1, 2 ... (grow-only, written through cl when it grows)
    nvrhi::IBuffer* sequentialIndices(nvrhi::ICommandList* cl, uint32_t n) {
        if (!indices_ || indices_->getDesc().byteSize < uint64_t(n) * 4) {
            std::vector<uint32_t> seq(std::max<uint32_t>(n + n / 2, 1u << 16));
            for (uint32_t i = 0; i < seq.size(); ++i) seq[i] = i;
            nvrhi::BufferDesc bd;
            bd.byteSize = seq.size() * 4;
            bd.isAccelStructBuildInput = true;
            bd.initialState = nvrhi::ResourceStates::AccelStructBuildInput;
            bd.keepInitialState = true;
            bd.debugName = "rt sequential indices";
            indices_ = dev_.device()->createBuffer(bd);
            cl->writeBuffer(indices_, seq.data(), bd.byteSize);
        }
        return indices_;
    }

    // structured float4 buffer (shadow-ray attributes), sized for `attr`
    nvrhi::BufferHandle attrBuffer(const std::vector<float>& attr, const char* name) {
        nvrhi::BufferDesc bd;
        bd.byteSize = attr.size() * sizeof(float);
        bd.structStride = 16;
        bd.initialState = nvrhi::ResourceStates::ShaderResource;
        bd.keepInitialState = true;
        bd.debugName = name;
        return dev_.device()->createBuffer(bd);
    }
    void uploadBuffer(nvrhi::IBuffer* buf, const std::vector<float>& data) {
        nvrhi::CommandListHandle cl = dev_.device()->createCommandList();
        cl->open();
        cl->writeBuffer(buf, data.data(), data.size() * sizeof(float));
        cl->close();
        dev_.device()->executeCommandList(cl);
    }

    void makeSceneSet() {
        if (!pages_) return;
        if (depthCopyStale()) {
            makeDepthCopy(); // (it makes the set)
            return;
        }
        nvrhi::BindingSetDesc bs;
        bs.bindings = {nvrhi::BindingSetItem::ConstantBuffer(0, frameCb_), nvrhi::BindingSetItem::Texture_SRV(0, pages_),
                       nvrhi::BindingSetItem::Sampler(0, pointClamp_), nvrhi::BindingSetItem::Texture_SRV(5, depthCopy_),
                       nvrhi::BindingSetItem::Texture_SRV(6, focusMask_[0]),
                       nvrhi::BindingSetItem::Texture_SRV(7, focusMask_[1])};
        if (rt_) {
            bs.bindings.push_back(nvrhi::BindingSetItem::RayTracingAccelStruct(1, tlas_));
            bs.bindings.push_back(nvrhi::BindingSetItem::StructuredBuffer_SRV(2, staticAttr_));
            bs.bindings.push_back(nvrhi::BindingSetItem::StructuredBuffer_SRV(3, dynAttr_));
        }
        sceneSet_ = dev_.device()->createBindingSet(bs, sceneLayout_);
    }

    // this frame's acceleration structures: room BLAS (built at upload) + actors' BLAS (rebuilt from the opaque
    // dynamic vertices every frame) -> TLAS
    void buildScene(uint32_t dynOpaque) {
        nvrhi::IDevice* d = dev_.device();
        nvrhi::rt::InstanceDesc inst[2];
        size_t n = 0;
        const auto flags = nvrhi::rt::InstanceFlags::TriangleCullDisable; // shadows from both sides of every face
        if (staticBlas_)
            inst[n++] = nvrhi::rt::InstanceDesc().setBLAS(staticBlas_).setInstanceID(0).setInstanceMask(1).setFlags(flags);
        const uint32_t casters = dynOpaque / 3 * 3;
        if (sun_.enabled && casters && dynBuf_) {
            std::vector<float> attr;
            toAttr(dynGpu_, casters, attr);
            if (dynAttr_->getDesc().byteSize < attr.size() * sizeof(float)) {
                std::vector<float> grown(attr.size() * 3 / 2 + 4, 0.0f);
                dynAttr_ = attrBuffer(grown, "dynamic rt attributes");
                makeSceneSet();
            }
            cl_->writeBuffer(dynAttr_, attr.data(), attr.size() * sizeof(float));
            if (!dynBlas_ || dynBlasBytes_ != dynBuf_->getDesc().byteSize) { // sized for the whole grow-only buffer
                const uint32_t capacity = uint32_t(dynBuf_->getDesc().byteSize / sizeof(GpuVertex)) / 3 * 3;
                const nvrhi::rt::GeometryDesc full = triangleGeometry(dynBuf_, sequentialIndices(cl_, capacity), capacity);
                dynBlas_ = d->createAccelStruct(nvrhi::rt::AccelStructDesc()
                                                    .addBottomLevelGeometry(full)
                                                    .setBuildFlags(nvrhi::rt::AccelStructBuildFlags::PreferFastBuild)
                                                    .setDebugName("actors blas"));
                dynBlasBytes_ = dynBuf_->getDesc().byteSize;
            }
            const nvrhi::rt::GeometryDesc g = triangleGeometry(dynBuf_, sequentialIndices(cl_, casters), casters);
            cl_->buildBottomLevelAccelStruct(dynBlas_, &g, 1, nvrhi::rt::AccelStructBuildFlags::PreferFastBuild);
            inst[n++] = nvrhi::rt::InstanceDesc().setBLAS(dynBlas_).setInstanceID(1).setInstanceMask(1).setFlags(flags);
        }
        cl_->buildTopLevelAccelStruct(tlas_, inst, n, nvrhi::rt::AccelStructBuildFlags::AllowEmptyInstances);
    }

    void writeFrame(const XMMATRIX& vp, const XMMATRIX& vpNoJit, const XMMATRIX& prevVp, const float* eye,
                    const float* prevEye, const float* fwd, float cullSign, bool fog, float reactive,
                    float motionMode, bool shadow = false) {
        FrameConstants f{};
        XMStoreFloat4x4(&f.viewProj, vp);
        XMStoreFloat4x4(&f.viewProjNoJit, vpNoJit);
        XMStoreFloat4x4(&f.prevViewProj, prevVp);
        for (int c = 0; c < 3; ++c) {
            f.eye[c] = eye[c];
            f.prevEye[c] = prevEye[c];
            f.fwd[c] = fwd[c];
            f.fogColor[c] = fog_.color[c];
        }
        f.cullSign = cullSign;
        f.modelNear = nearZ_;
        f.focusCount = float(focusShown_);
        for (uint32_t k = 0; k < focusShown_; ++k) {
            f.focus[k][0] = focusShownBody_[k].feet;
            f.focus[k][1] = focusShownBody_[k].strength;
        }
        f.fogStart = fog_.start;
        f.fogEnd = fog_.end;
        f.fogMode = fog ? float(int(fog_.mode)) : 0.0f;
        // the remaster fog is a medium around the eye: the sky behind it fogs too, as a ray that never ends (else the
        // fogged scenery's silhouette cuts against a clear sky); the classic table fog never touched the sky
        if (frameSky_ > 0.0f && skyIsSky_ && fog_.mode == FogMode::Remaster) {
            f.fogMode = float(int(FogMode::Remaster));
            f.fogSky = frameSky_;
            XMStoreFloat4x4(&f.skyRay, skyRay_);
        }
        f.fogDensity = fog_.density;
        f.fogHeightFalloff = fog_.heightFalloff;
        f.fogMaxOpacity = fog_.maxOpacity;
        f.reactive = reactive;
        f.renderSize[0] = float(t_.renderW);
        f.renderSize[1] = float(t_.renderH);
        f.motionMode = motionMode;
        f.blendMode = frameBlend_;
        f.semiPass = frameSemiPass_;
        const float len = std::sqrt(sun_.dir[0] * sun_.dir[0] + sun_.dir[1] * sun_.dir[1] + sun_.dir[2] * sun_.dir[2]);
        for (int c = 0; c < 3; ++c) f.sunDir[c] = len > 0.0f ? sun_.dir[c] / len : 0.0f;
        f.sunStrength = sun_.strength;
        f.shadowMode = shadow && rt_ && sun_.enabled && len > 0.0f ? 1.0f : 0.0f;
        f.sunScatter = sun_.enabled && len > 0.0f ? 0.35f : 0.0f;
        if (waveRemaster_ && wave_[0] > 0.0f) { // the sky (no fog) is replaced by the water seen far away
            f.water[0] = 1.0f;
            f.water[1] = waveSeconds_;
            f.water[2] = fog ? 0.0f : 1.0f;
            f.water[3] = waveHeat_ ? (heatOpenSky() ? 2.0f : 1.0f) : 0.0f;
            if (heatOpenSky()) { // the haze takes the sky's colour (the background), a little warmer
                for (int c = 0; c < 3; ++c) f.waterTint[c] = clear_[c];
                f.waterTint[3] = 1.0f;
            } else
                for (int c = 0; c < 3; ++c) f.waterTint[c] = waveTint_[c];
        }
        std::copy(std::begin(heightBounds_), std::end(heightBounds_), f.heightBounds);
        std::copy(std::begin(heightRange_), std::end(heightRange_), f.heightRange);
        f.precip[0] = precip_.snow;
        f.precip[1] = precip_.blizzard;
        f.precip[2] = precip_.gust;
        f.precip[3] = precip_.seconds;
        for (int c = 0; c < 3; ++c) f.precipWind[c] = precip_.wind[c];
        f.precipWind[3] = precip_.unit;
        f.precipView[0] = tanHalfFov_;
        f.precipView[1] = precipLight();
        XMStoreFloat4x4(&f.invViewProj, invVp_);
        f.softDepth[0] = nearZ_;
        f.softDepth[1] = kFar;
        const float look[8] = {precip_.density, precip_.size, precip_.brightness, precip_.swirl,
                               precip_.flutter, precip_.reach, precip_.streak, precip_.chaos};
        std::copy(std::begin(look), std::end(look), f.precipLook);
        std::copy(std::begin(flakeLayer_), std::end(flakeLayer_), f.precipLayer);
        cl_->writeBuffer(frameCb_, &f, sizeof f);
    }

    void drawScene(const Camera& cam) {
        const XMVECTOR eye = XMVectorSet(cam.eye[0], cam.eye[1], cam.eye[2], 1.0f);
        const XMVECTOR fwd = XMVectorSet(std::sin(cam.yaw) * std::cos(cam.pitch), std::sin(cam.pitch),
                                         std::cos(cam.yaw) * std::cos(cam.pitch), 0.0f);
        const XMMATRIX view = XMMatrixLookToLH(eye, fwd, cameraUp(cam));
        const XMMATRIX proj = XMMatrixPerspectiveFovLH(cam.fovY, float(t_.renderW) / float(t_.renderH), cam.nearZ, kFar);
        const XMMATRIX vp = view * proj;
        const XMMATRIX vpJit = view * withJitter(proj, jitter_.x, jitter_.y, t_.renderW, t_.renderH);
        // dome: camera rotation only (it follows the eye), current and previous
        const XMMATRIX rotView = XMMatrixLookToLH(XMVectorZero(), fwd, cameraUp(cam));
        if (!historyValid_) {
            prevVp_ = vp;
            prevRotVp_ = rotView * proj;
            for (int c = 0; c < 3; ++c) prevEye_[c] = cam.eye[c];
            historyValid_ = true;
        }
        float fwdv[3];
        for (int c = 0; c < 3; ++c) fwdv[c] = XMVectorGetByIndex(fwd, c);
        // the focus bodies drawn this frame (their objects uploaded, FrameConstants / focusMask_)
        focusShown_ = 0;
        for (const FocusBody& b : focus_) {
            const auto it = objectRanges_.find(b.object);
            if (b.strength <= 0.0f || it == objectRanges_.end() || it->second.second == 0) continue;
            focusShownBody_[focusShown_] = b;
            focusShownRange_[focusShown_++] = it->second;
        }

        // streamed geometry: sky (dome previous positions follow the eye) and dynamic actors
        std::vector<float> skyPrev(sky_.size() * 3);
        for (size_t i = 0; i < sky_.size(); ++i)
            for (int c = 0; c < 3; ++c) skyPrev[i * 3 + c] = sky_[i].pos[c] - cam.eye[c];
        std::vector<GpuVertex> skyGpu;
        toGpu(sky_, skyPrev.data(), skyGpu);
        stream(skyBuf_, skyGpu, "sky vertices");
        stream(dynBuf_, dynGpu_, "dynamic vertices", rt_);
        if (rt_) buildScene(dynGpu_.empty() ? 0 : std::min<uint32_t>(dynStart_[0], uint32_t(dynGpu_.size())));

        if (depthCopyStale()) makeDepthCopy(); // (the set below binds it)
        nvrhi::GraphicsState gs;
        gs.framebuffer = t_.scene;
        gs.viewport.addViewportAndScissorRect(nvrhi::Viewport(float(t_.renderW), float(t_.renderH)));
        gs.bindings = {sceneSet_}; // after buildScene: a grown attribute buffer makes a new set
        auto drawRange = [&](nvrhi::IGraphicsPipeline* p, nvrhi::IBuffer* vb, uint32_t first, uint32_t count) {
            if (!count) return;
            gs.pipeline = p;
            gs.vertexBuffers = {nvrhi::VertexBufferBinding().setBuffer(vb).setSlot(0).setOffset(0)};
            cl_->setGraphicsState(gs);
            cl_->draw(nvrhi::DrawArguments().setVertexCount(count).setStartVertexLocation(first));
        };

        if (!sky_.empty() && !skyScreen_) { // sky dome: world positions around the eye, no room fog
            // previous position = eye-relative (skyPrev), reprojected with the previous rotation-only matrix
            const float zero[3] = {0.0f, 0.0f, 0.0f};
            // reactive 1: scrolling cloud layers move without per-vertex history (motion covers rotation only)
            frameSky_ = 1.0f;
            writeFrame(vpJit, vp, prevRotVp_, cam.eye, zero, fwdv, cam.cullSign, false, 1.0f, 0.0f);
            drawRange(sky_pipe_, skyBuf_, 0, uint32_t(sky_.size()));
        } else if (!sky_.empty()) { // screen-space sky: identity transform, no depth, no room fog, no motion
            const float skyEye[3] = {0.0f, 0.0f, -1.0f}; // behind the screen plane: no layer push
            const float skyFwd[3] = {0.0f, 0.0f, 1.0f};
            skyRay_ = XMMatrixInverse(nullptr, rotView * proj);
            frameSky_ = 2.0f;
            writeFrame(XMMatrixIdentity(), XMMatrixIdentity(), XMMatrixIdentity(), skyEye, skyEye, skyFwd, 1.0f,
                       false, 1.0f, 1.0f);
            drawRange(sky_pipe_, skyBuf_, 0, uint32_t(sky_.size()));
        }
        frameSky_ = 0.0f;

        // the see-through's focus bodies, each drawn alone (its opaque and its STP texels) into the cleared depth and
        // copied to its focusMask_; the depth is cleared again for the scene, which draws them where they are seen
        for (uint32_t k = 0; k < focusShown_; ++k) {
            const uint32_t n = uint32_t(dynGpu_.size()), first = std::min(focusShownRange_[k].first, n);
            const uint32_t count = std::min(focusShownRange_[k].second, n - first);
            for (const float semiPass : {0.0f, 1.0f}) {
                frameSemiPass_ = semiPass;
                writeFrame(vpJit, vp, prevVp_, cam.eye, prevEye_, fwdv, cam.cullSign, true, 0.0f, 0.0f, true);
                drawRange(opaque_, dynBuf_, first, count);
            }
            frameSemiPass_ = 0.0f;
            debugPass(t_.depth, 5, focusMaskFb_[k], float(t_.renderW), float(t_.renderH));
            cl_->clearDepthStencilTexture(t_.depth, nvrhi::AllSubresources, true, 1.0f, kDepthHasStencil, 0);
        }

        // every opaque surface (room, then actors / switchable segments) before any semi-transparent one: the blended
        // faces write no depth, so an opaque draw after them overwrote them wherever it lay behind (the ST4B sub 1
        // key case of static segment 9 vanished over the key and the switchable walls behind it); on the PS1 a
        // semi-transparent face always blends over what is behind it
        writeFrame(vpJit, vp, prevVp_, cam.eye, prevEye_, fwdv, cam.cullSign, true, 0.0f, 0.0f, true);
        drawRange(opaque_, vbuf_, 0, semiStart_[0]);
        if (!dynGpu_.empty())
            drawRange(opaque_, dynBuf_, 0, std::min<uint32_t>(dynStart_[0], uint32_t(dynGpu_.size())));
        // a semi-transparent polygon blends only the texels whose colour has the STP bit (Vram::decode): its other
        // texels are opaque surfaces, drawn here with the rest of the opaque ones (ST10's ice over the buried ship:
        // its snow is opaque, only the clear patches show the pit below)
        frameSemiPass_ = 1.0f;
        writeFrame(vpJit, vp, prevVp_, cam.eye, prevEye_, fwdv, cam.cullSign, true, 0.0f, 0.0f, true);
        drawRange(opaque_, vbuf_, semiStart_[0], semiStart_[3] - semiStart_[0]);
        if (const uint32_t n = uint32_t(dynGpu_.size()); dynStart_[0] < n)
            drawRange(opaque_, dynBuf_, dynStart_[0], n - dynStart_[0]);
        // the opaque surfaces' depth, which the blended effects and the flakes fade against (scene.hlsl softEdge)
        copyDepth();
        frameSemiPass_ = 2.0f;
        static const float kReactive[4] = {0.5f, 0.9f, 0.5f, 0.6f};      // avg, add, B*(1-F), B+F/4
        for (int mode = 0; mode < 3; ++mode) { // the room's semi quads (modes 0..2)
            const uint32_t first = semiStart_[mode], last = semiStart_[mode + 1];
            if (last <= first) continue;
            frameBlend_ = float(mode + 1);
            writeFrame(vpJit, vp, prevVp_, cam.eye, prevEye_, fwdv, cam.cullSign, true, kReactive[mode], 0.0f);
            drawRange(semi_[mode], vbuf_, first, last - first);
        }
        if (!dynGpu_.empty()) {
            for (int mode = 0; mode < 4; ++mode) { // blended effects (DynamicObject::blend = mode + 1)
                const uint32_t first = dynStart_[mode], last = std::min<uint32_t>(dynStart_[mode + 1], uint32_t(dynGpu_.size()));
                if (last <= first) continue;
                frameBlend_ = float(mode + 1);
                    writeFrame(vpJit, vp, prevVp_, cam.eye, prevEye_, fwdv, cam.cullSign, true, kReactive[mode], 0.0f);
                drawRange(semi_[mode], dynBuf_, first, last - first);
            }
        }
        frameBlend_ = 0.0f;
        frameSemiPass_ = 0.0f;

        // the precipitation's flakes, among the scene (the blown snow's medium follows after the resolve, draw())
        invVp_ = XMMatrixInverse(nullptr, vpJit);
        lastVpJit_ = vpJit;
        lastVp_ = vp;
        for (int c = 0; c < 3; ++c) {
            lastEye_[c] = cam.eye[c];
            lastFwd_[c] = fwdv[c];
        }
        if (precipitating()) {
            if (!flakeSet_) flakeSet_ = precipSet();
            nvrhi::GraphicsState fs;
            fs.pipeline = flakePipe_;
            fs.framebuffer = t_.scene;
            fs.viewport.addViewportAndScissorRect(nvrhi::Viewport(float(t_.renderW), float(t_.renderH)));
            fs.bindings = {flakeSet_};
            for (const bool farField : {false, true}) { // the near field, then the far one beyond it
                const float perCell = float(farField ? kFlakesFar : kFlakesNear);
                const float layer[4] = {farField ? kFlakeFarScale : 1.0f, perCell, farField ? 1.0f : 0.0f, 0.0f};
                std::copy(std::begin(layer), std::end(layer), flakeLayer_);
                writeFrame(vpJit, vp, prevVp_, cam.eye, prevEye_, fwdv, cam.cullSign, true, 0.0f, 0.0f);
                cl_->setGraphicsState(fs);
                cl_->draw(nvrhi::DrawArguments().setVertexCount(kFlakeCellCount * uint32_t(perCell) * 6));
            }
        }

        prevVp_ = vp;
        prevRotVp_ = rotView * proj;
        for (int c = 0; c < 3; ++c) prevEye_[c] = cam.eye[c];
    }

    void drawOutput(nvrhi::ITexture* src, nvrhi::ITexture* back) {
        nvrhi::ITexture* depth = depthFresh_ ? depthCopy_.Get() : copyDepth(); // the underwater distance blur
        nvrhi::IFramebuffer* fb = backFramebuffer(back);
        auto it = outSets_.find(src);
        if (it == outSets_.end()) {
            nvrhi::BindingSetDesc bs;
            bs.bindings = {nvrhi::BindingSetItem::PushConstants(0, sizeof(OutConsts)), nvrhi::BindingSetItem::Texture_SRV(0, src),
                           nvrhi::BindingSetItem::Sampler(0, pointClamp_), nvrhi::BindingSetItem::Sampler(1, linearClamp_),
                           nvrhi::BindingSetItem::Texture_SRV(1, depth)};
            it = outSets_.emplace(src, dev_.device()->createBindingSet(bs, outLayout_)).first;
        }
        nvrhi::GraphicsState gs;
        gs.pipeline = outPipe_;
        gs.framebuffer = fb;
        gs.viewport.addViewportAndScissorRect(nvrhi::Viewport(float(dev_.width()), float(dev_.height())));
        gs.bindings = {it->second};
        cl_->setGraphicsState(gs);
        OutConsts c{}; // the screen ripples in output pixels (PS1 320 x 240 fitted to the height, centred)
        const float w = float(dev_.width()), h = float(dev_.height()), s = h / 240.0f, ox = (w - 320.0f * s) * 0.5f;
        c.size[0] = w;
        c.size[1] = h;
        c.wave[0] = wave_[0] * s; // amplitude in output pixels
        c.wave[1] = wave_[1];     // phase in PS1 rows
        c.wave[2] = s;            // output pixels per PS1 row
        c.wave[3] = waveRemaster_ ? (waveHeat_ ? (heatOpenSky() ? 3.0f : 2.0f) : 1.0f) : 0.0f;
        if (waveRemaster_) { // the remaster look has no row phase / scale: the camera's pitch and tan(fov / 2) instead
            c.wave[1] = camPitch_;
            c.wave[2] = tanHalfFov_;
        }
        c.seconds = waveSeconds_;
        for (int k = 0; k < 3; ++k) c.tint[k] = wave_[0] > 0.0f ? waveTint_[k] : 1.0f;
        c.tint[3] = nearZ_; // the depth copy's near plane (far = kFar)
        for (const ScreenRipple& r : ripples_) {
            if (c.count >= kMaxScreenRipples) break;
            float* q = c.ring[c.count];
            q[0] = ox + r.cx * s;
            q[1] = r.cy * s;
            q[2] = r.r0 * s;
            q[3] = r.r1 * s;
            c.shift[c.count] = r.shift * s;
            ++c.count;
        }
        cl_->setPushConstants(&c, sizeof(c));
        cl_->draw(nvrhi::DrawArguments().setVertexCount(3));
    }

    // RGBA8 rows, tightly packed, top row first (what --shot saves)
    void readBack(nvrhi::IStagingTexture* staging, std::vector<uint8_t>& out) {
        dev_.device()->waitForIdle();
        size_t pitch = 0;
        const auto* src = static_cast<const uint8_t*>(
            dev_.device()->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &pitch));
        if (!src) return;
        const size_t w = size_t(dev_.width()), h = size_t(dev_.height());
        out.resize(w * h * 4);
        for (size_t y = 0; y < h; ++y) std::memcpy(out.data() + y * w * 4, src + y * pitch, w * 4);
        dev_.device()->unmapStagingTexture(staging);
        if (staging->getDesc().format == nvrhi::Format::BGRA8_UNORM) // a Vulkan swapchain's: out is RGBA
            for (size_t i = 0; i < out.size(); i += 4) std::swap(out[i], out[i + 2]);
    }

#ifdef _WIN32
    DeviceD3D12 dev_;
#else
    DeviceVulkan dev_;
#endif
    UiPass ui_;         // the 2D pass: the game canvas + the UI textures
    ImGuiNvrhi imgui_;  // the F1 panels, on ui_
    nvrhi::IFramebuffer* backFb_ = nullptr; // set while the overlay callback runs
    nvrhi::TextureHandle grabTex_; // sceneGrab
    uint64_t grabId_ = 0;
    int grabX_ = 0, grabY_ = 0, grabW_ = 0, grabH_ = 0;
    bool grabPending_ = false;
    bool vsync_ = true;
    nvrhi::CommandListHandle cl_;
    nvrhi::BufferHandle frameCb_, vbuf_, skyBuf_, dynBuf_;
    nvrhi::SamplerHandle pointClamp_, linearClamp_;
    nvrhi::ShaderHandle sceneVs_, scenePs_, outVs_, outPs_;
    nvrhi::InputLayoutHandle inputLayout_;
    struct OutConsts { // output.hlsl's push constants (128 bytes, HLSL cbuffer packing)
        float ring[kMaxScreenRipples][4]; // centre x / y, inner / outer radius (output pixels)
        float shift[4];                   // the picture's shift inside ring k (output pixels)
        int count;
        float size[2];
        float seconds; // the remaster underwater look's clock
        float wave[4]; // the flooded rooms' screen wave: amplitude (output pixels), phase (PS1 rows), pixels per row,
                       // style (0 PS1, 1 remaster)
        float tint[4]; // its colour / 0x80
    };
    float wave_[2] = {0.0f, 0.0f}, waveTint_[3] = {1.0f, 1.0f, 1.0f}, waveSeconds_ = 0.0f;
    bool waveRemaster_ = false, waveHeat_ = false;
    bool depthFresh_ = false; // depthCopy_ holds this frame's depth
    float nearZ_ = 8.0f, camPitch_ = 0.0f, tanHalfFov_ = 0.5f;
    std::vector<ScreenRipple> ripples_;
    nvrhi::BindingLayoutHandle sceneLayout_, outLayout_;
    nvrhi::BindingSetHandle sceneSet_;
    nvrhi::GraphicsPipelineHandle opaque_, sky_pipe_, semi_[4], outPipe_;
    nvrhi::ShaderHandle debugVs_, debugPs_, debugMsPs_;
    nvrhi::ShaderHandle previewVs_, previewPs_; // renderPreview
    nvrhi::InputLayoutHandle previewInput_;
    nvrhi::BindingLayoutHandle previewLayout_;
    nvrhi::GraphicsPipelineHandle previewPipe_;
    PreviewSlot preview_[4];
    nvrhi::BindingLayoutHandle debugLayout_;
    std::unordered_map<int, nvrhi::GraphicsPipelineHandle> debugPipes_; // per target format, x2 + multisampled source
    std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> debugSets_;
    nvrhi::TextureHandle depthCopy_;
    nvrhi::FramebufferHandle depthCopyFb_;
    DebugView view_ = DebugView::Final;
    nvrhi::TextureHandle pages_;
    int sliceSize_ = 256;
    std::map<int, std::vector<uint8_t>> sliceUpdates_;
    std::vector<GpuVertex> staticGv_;                 // the uploaded room vertices (updateStaticUvs patches them)
    uint32_t uvDirtyLo_ = UINT32_MAX, uvDirtyHi_ = 0; // their range not yet written to vbuf_
    Targets t_;
    std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> outSets_;
    std::unordered_map<nvrhi::ITexture*, nvrhi::FramebufferHandle> backFbs_;

    std::array<uint32_t, 4> semiStart_{};
    uint32_t vertexCount_ = 0;
    std::vector<RenderVertex> sky_, dyn_;
    std::vector<GpuVertex> dynGpu_;
    std::array<uint32_t, 5> dynStart_{}; // dynamic vertices with blend <= m ([0] = end of the opaque objects)
    float frameSemiPass_ = 0.0f;         // FrameConstants::semiPass of the draws writeFrame is preparing
    float frameBlend_ = 0.0f;            // the blend of the draws writeFrame is preparing (FrameConstants::blendMode)
    std::vector<float> dynPrev_;
    MotionHistory history_;
    bool skyScreen_ = true;
    bool skyIsSky_ = true;   // the sky draw is the sky (not ST01's travel map): the remaster fog covers it
    float frameSky_ = 0.0f;  // FrameConstants::fogSky of the draws writeFrame is preparing
    bool historyValid_ = false;
    XMMATRIX prevVp_ = XMMatrixIdentity(), prevRotVp_ = XMMatrixIdentity(), skyRay_ = XMMatrixIdentity();
    float prevEye_[3] = {0.0f, 0.0f, 0.0f};
    Jitter jitter_{0.0f, 0.0f};
    std::unique_ptr<Upscaler> up_;
    AaBackend wanted_ = AaBackend::None;
    AaQuality quality_ = AaQuality::NativeAA;
    std::vector<AaBackend> available_;
    std::set<AaBackend> broken_; // backends that failed after init (feature creation / dispatch)
    uint64_t frameIndex_ = 0;
    bool pendingReset_ = true;
    std::chrono::steady_clock::time_point lastFrame_{};
    float clear_[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    Fog fog_;
    // sun shadows (ray queries)
    bool rt_ = false;
    SunLight sun_;
    std::vector<FocusBody> focus_;
    FocusBody focusShownBody_[kMaxFocusBodies]{}; // drawScene: the focus bodies drawn this frame
    std::pair<uint32_t, uint32_t> focusShownRange_[kMaxFocusBodies]{}; // ... their opaque vertices in dynBuf_
    uint32_t focusShown_ = 0;
    // the opaque objects' vertex ranges in dyn_ (first, count) by DynamicObject id: a focus body's draw
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> objectRanges_;
    // a focus body's depth (R32F, render size, 1 = none): drawn alone before the scene (scene.hlsl see-through)
    nvrhi::TextureHandle focusMask_[kMaxFocusBodies];
    nvrhi::FramebufferHandle focusMaskFb_[kMaxFocusBodies];
    // precipitation (setPrecipitation): the room's height map from above, the flakes, the blown snow
    Precipitation precip_;
    nvrhi::ShaderHandle heightVs_, heightPs_, flakeVs_, flakePs_, fullVs_, blizzardPs_;
    nvrhi::BindingLayoutHandle precipLayout_;
    nvrhi::BindingSetHandle flakeSet_, blizzardSet_; // precipSet()
    nvrhi::GraphicsPipelineHandle heightPipe_, flakePipe_, blizzardPipe_;
    nvrhi::TextureHandle height_;
    float heightBounds_[4]{}, heightRange_[4]{0.0f, 1.0f, 1.0f, 1.0f};
    float flakeLayer_[4]{1.0f, 1.0f, 0.0f, 0.0f}; // FrameConstants::precipLayer of the flake draw being prepared
    // the last drawScene's camera (the blown snow's pass after the resolve), the inverse of its jittered viewProj
    XMMATRIX lastVpJit_ = XMMatrixIdentity(), lastVp_ = XMMatrixIdentity(), invVp_ = XMMatrixIdentity();
    float lastEye_[3]{}, lastFwd_[3]{0.0f, 0.0f, 1.0f};
    nvrhi::rt::AccelStructHandle tlas_, staticBlas_, dynBlas_;
    uint64_t dynBlasBytes_ = 0; // size of the dynBuf_ the actors' BLAS was made for
    nvrhi::BufferHandle staticAttr_, dynAttr_, indices_;
};

} // namespace

std::unique_ptr<Renderer> createRenderer(SDL_Window* window, int w, int h) {
    auto r = std::make_unique<NvrhiRenderer>();
#ifdef NDEBUG
    const bool debug = false;
#else
    const bool debug = true;
#endif
#ifdef _WIN32
    const auto hwnd = static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!r->init(hwnd, w, h, debug)) return nullptr;
#else
    if (!r->init(window, w, h, debug)) return nullptr;
#endif
    return r;
}

} // namespace od
