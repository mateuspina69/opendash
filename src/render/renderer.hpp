// Minimal 3D renderer interface (NVRHI / D3D12 implementation, see docs/adr/0004-renderer-nvrhi.md).
#pragma once

#include <cstdint>
#include <array>
#include <functional>
#include <memory>
#include <vector>

struct ImDrawData;
struct SDL_Window;

namespace od::ui {
class Canvas;
}

namespace od {

using TextureHandle = uint64_t; // UI texture (ui::Canvas, render/ui_canvas.hpp); also usable as an ImTextureID

inline constexpr float kLayerStep = 3.0f; // scene.hlsl: a vertex is pushed layer * 3.0 world units toward the eye

struct RenderVertex {
    float pos[3];  // world units, Y-up
    float uv[2];   // 0..1 within the 256x256 slice
    float slice;   // texture array slice
    float shade;   // 1.0 = neutral (PS1 gouraud 0x80)
    float cull;    // 0 double-sided; 1 single-sided triangle (back face discarded); quad halves (the next / previous
                   // triangle is the other half): 2 / 3 drawn when either half faces the camera (actor models,
                   // FUN_005f4980), 4 / 5 the whole quad by the first half's facing (Megaman, 0x5f3820)
    float layer;   // coplanar priority (PS1 OT order): pushed layer * kLayerStep toward the camera
    float color[3]{1.0f, 1.0f, 1.0f}; // vertex colour (gouraud), multiplied in; with slice < 0 the polygon is
                                      // untextured (PS1 shaded polygons, e.g. the sky gradient 0x5f03a0)
    float piece = 0.0f; // the see-through: the scenery piece it belongs to (app/see_through, SeeThroughPiece); 0 none
};

// Geometry of one actor rebuilt every tick. The id is stable for the actor's life in the room, so the tick
// interpolation and the motion history can follow it even when other actors spawn or despawn.
// blend: 0 opaque; 1 + PS1 semi-transparency mode (tpage ABR) with the PC exe's D3D blend states (0x402100 /
// 0x402280): 1 avg B/2+F/2 (alpha 0x80, SRCALPHA / INVSRCALPHA), 2 add B+F (ONE / ONE), 3 B*(1-F) (ZERO /
// INVSRCCOLOR), 4 B+F/4 (alpha 0x40, SRCALPHA / ONE); blended objects are drawn after the opaque ones with the
// semi pipelines (depth test, no depth write).
struct DynamicObject {
    uint32_t id;
    std::vector<RenderVertex> tris;
    uint8_t blend = 0;
    // topology signature (e.g. which model parts were drawn): frames are blended / given motion vectors only
    // between two ticks of the same id, vertex count and layout. 0 = no signature (the vertex count decides)
    uint32_t layout = 0;
};
enum ObjectKind : uint32_t { kKindRoomAnim = 1, kKindLoop = 2, kKindWalker = 3, kKindPlayer = 4, kKindLive = 5,
                             kKindActor = 6 /* live actor of the actor system, index = pool slot */,
                             kKindEffect = 7 /* combat effects (game/effects_draw), index = pool | slot | part */,
                             kKindPoolModel = 8 /* bank-model actors of the static / shot pools (liveScene), index =
                                                   pool << 8 | slot */ };
constexpr uint32_t objectId(uint32_t kind, uint32_t index) { return kind << 24 | (index & 0xFFFFFFu); }

// new texture coordinates of one vertex of the static room geometry (the room UV animation, app/room_uv_anim)
struct StaticUvPatch {
    uint32_t vertex; // index into the vertices of the last upload()
    float uv[2];
};

// Anti-aliasing / upscaling
enum class AaBackend { Auto, None, Dlss, Fsr, Xess, Nis, Ssaa, Msaa };
enum class AaQuality { NativeAA, Quality, Balanced, Performance };

// what the output pass shows (F1 / --view): the final image or one scene target
enum class DebugView { Final, Color, Motion, Normal, Depth, Reactive };

// Experimental ray-traced sun shadows (inline ray queries over the room + actor geometry). The original has no
// lights (shading is painted into the textures), so shadow only darkens: strength 0..1 = how much a pixel the sun
// cannot reach (blocked, or facing away) loses. dir points from the scene toward the sun (world, Y-up).
struct SunLight {
    bool enabled = false;
    float dir[3]{0.0f, 1.0f, 0.0f};
    float strength = 0.45f;
};

// The see-through (OpenDash, Baldur's Gate style; app/see_through): a scenery piece that stands between the camera and
// a character in focus opens, in an ordered dither, a soft round hole around that character on screen (only its
// fragments nearer than the character), or fades whole when it is smaller than the hole
struct SeeThroughBody {
    float lo[3]{}, hi[3]{}; // the character's box, world units of the drawn scene
};
struct SeeThroughPiece {
    uint32_t piece = 0;  // RenderVertex::piece
    float open = 0.0f;   // 0..1
    bool whole = false;  // the whole piece fades (smaller than the hole)
    uint8_t bodies = 0;  // the bodies it hides (bit k: SeeThroughBody k): its hole goes around them
};
constexpr size_t kMaxSeeThroughBodies = 2, kMaxSeeThroughPieces = 16;

// Fog of the world geometry. Classic: the original's D3D linear table fog on camera depth (start..end, fully fogged
// at end). Remaster: exponential fog on the view distance from `start` on (density per world unit), thinner above the
// eye and thicker below (heightFalloff per world unit, the analytic integral along the ray), never more opaque than
// maxOpacity (far geometry stays a silhouette), brightened toward the sun when the sun is on.
// DepthCue: the PS1's (GTE DPCS): the colour toward the colour times the far colour (128 = 1.0) by view depth / end,
// from the eye, past 1 too (the colour clamps at 0); not on effects (an added / taken light)
enum class FogMode { Off, Classic, Remaster, DepthCue };
struct Fog {
    FogMode mode = FogMode::Off;
    float color[3]{0.0f, 0.0f, 0.0f};
    float start = 0.0f, end = 0.0f;
    float density = 0.0f, heightFalloff = 0.0f, maxOpacity = 1.0f;
};

// OpenDash's remaster precipitation: the game's snow / blizzard drawn in the world instead of its screen-space quads.
// Flakes of a field fixed in the world fall and drift at `wind` and land on the first surface below them (a height map
// of the room seen from above, made at upload(): nothing falls under a roof); with `blizzard`, the snow the wind blows
// is also a medium along the view (sheets drifting with the wind, none where the height map shelters). 0 / 0 = none
// OpenDash remaster: the light of the particles. Every additive effect (a kKindEffect DynamicObject of blend 2 or 4: the
// shots, the explosions, the sparks, the fire) is a point light at its centre, of its own colour (its texels' mean
// times its vertex colour); the opaque surfaces near it gain that light times their texture colour. Nothing darkens:
// the original's light stays painted in the textures
struct ParticleLighting {
    bool enabled = false;
    float strength = 1.0f; // the light's gain
    float radius = 400.0f; // world units a light reaches at least (a larger effect reaches further)
};
constexpr size_t kMaxParticleLights = 32; // the brightest / largest ones a frame

struct Precipitation {
    float snow = 0.0f;     // 0..1: the falling flakes' density
    float blizzard = 0.0f; // 0..1: the blown snow's density (more, faster, streaked flakes and the drifting medium)
    float gust = 0.0f;     // 0..1: how much the blown snow's density swings between gusts
    float wind[3]{};       // the field's velocity, world units a second (Y-up)
    float seconds = 0.0f;  // the game's clock (stops with the game)
    float unit = 1.0f;     // world units a game unit (the room's 1 << shift): the flakes' size and spacing
    // the flakes' look, tuned in F1 (1 = OpenDash's defaults): how many fall, their size and brightness, the wind's
    // eddies, each flake's flutter, how far they reach from the eye, the blown snow's streaks, the motion's chaos (each
    // flake's own wander off the common course and its quick jitter)
    float density = 1.0f, size = 1.0f, brightness = 1.0f, swirl = 1.0f, flutter = 1.0f, reach = 1.0f, streak = 1.0f,
          chaos = 1.0f;
};

struct Camera {
    float eye[3];
    float yaw;     // radians, 0 = looking down +Z
    float pitch;   // radians
    float roll = 0.0f; // radians about the view direction (+ = the image turns clockwise); the jet skate camera's lean
    float fovY = 1.0f;
    float cullSign = 1.0f; // +1 verified (ST08 houses closed); -1 = show back faces (debug)
    // near plane (world units). Play mode: the game's, focal / 5 (Maybe_ClipAndProjectStrip 0x40d100 clips there,
    // Maybe_CountNearClippedVertices 0x40d020 drops a polygon whose vertices all lie before it)
    float nearZ = 8.0f;
};

// a screen ripple of the exe's class 0x438750 variant 0 (the ST18 machine's pulse, script 0x3b): an annulus of the
// frame buffer redrawn shifted (PS1 screen units, 320 x 240 centred in the window like the HUD)
struct ScreenRipple {
    float cx = 0, cy = 0, r0 = 0, r1 = 0, shift = 0;
};

// A model preview (OpenDash's UI model pictures, e.g. the special weapons' turning model in the menus): triangles
// rendered with depth, light and supersampling into their own target, shown as an ImGui texture
struct PreviewVertex {
    float pos[3];   // any units; viewProj maps them
    float uv[2];    // 0..1 in the atlas
    float color[3]; // multiplied in (1.0 = neutral)
};
struct PreviewRequest {
    std::vector<PreviewVertex> tris;
    std::vector<uint32_t> atlas; // atlasW x atlasH RGBA8; alpha 0 = transparent texel (discarded)
    int atlasW = 0, atlasH = 0;
    float viewProj[16]; // row-major, row vectors (v * M)
    float eye[3];       // for the two-sided lighting
    float light[3];     // direction toward the light
};

class Renderer {
public:
    virtual ~Renderer() = default;
    // this frame's screen ripples (at most kMaxScreenRipples are drawn); the output pass shifts the picture in them
    static constexpr int kMaxScreenRipples = 4;
    virtual void setScreenRipples(const std::vector<ScreenRipple>& ripples) { (void)ripples; }
    // the flooded rooms' screen wave (pool a0 class 0x431e90 variants 0 / 1 / 5; empty ret on PC, drawn on PS1 / PSP by
    // 0x179d94 of the PSP BOOT.BIN): screen row Y shows row Y + (sin(((Y - phase) & 127) >> 1 of 64) * 4096 >> shift),
    // tinted by colour / 0x80. amplitude = 4096 >> shift rows (PS1 screen), phase in rows (0..128), off: amplitude 0.
    // remaster: OpenDash's look instead, animated by `seconds`: underwater (absorption, caustics, refraction) or, with
    // `heat` (the same class in the lava rooms, red tint), hot air (rising shimmer, warm haze); the void is filled
    // with the medium either way. The amplitude / tint still come from the game's effect
    virtual void setScreenWave(float amplitude, float phase, const float tint[3], bool remaster = false, float seconds = 0.0f,
                               bool heat = false) {
        (void)amplitude, (void)phase, (void)tint, (void)remaster, (void)seconds, (void)heat;
    }
    // slices: count * 256*256 RGBA8
    // semiStart[m] (m = 0 avg, 1 add, 2 sub): first vertex of each PS1 semi-transparent range; the
    // ranges follow the opaque vertices in that order (semiStart[3] = end). Empty = all opaque.
    // sliceSize: texture page edge in texels (256 = original PS1 pages; larger for replacement textures)
    virtual bool upload(const std::vector<RenderVertex>& tris, const std::vector<uint8_t>& slices,
                        int sliceCount, const std::array<uint32_t, 4>& semiStart = {}, int sliceSize = 256) = 0;
    // background: clear colour (0..1) and sky triangles drawn first without depth. screenSpace:
    // pos = NDC x, y (PS1-style 2D layers); otherwise world positions around the eye (sky dome)
    // replaces one 256 x 256 RGBA8 slice of the uploaded room textures (the animated CLUTs re-decoded), next frame
    virtual void updateSlice(int slice, const std::vector<uint8_t>& rgba) { (void)slice, (void)rgba; }
    // appends `count` 256 x 256 RGBA8 slices after the uploaded ones (the slices so far and the room geometry kept,
    // no history reset): their indices are the old slice count onward. false = not done (no room uploaded, pages of
    // another size, a failed allocation)
    virtual bool appendSlices(const std::vector<uint8_t>& rgba, int count) { (void)rgba, (void)count; return false; }
    // rewrites the UVs of vertices of the uploaded static geometry (positions unchanged: no BLAS rebuild), next frame
    virtual void updateStaticUvs(const std::vector<StaticUvPatch>& patches) { (void)patches; }
    virtual void setClearColor(float r, float g, float b) = 0;
    // world-geometry fog (Fog below)
    virtual void setFog(const Fog& fog) = 0;
    // this frame's precipitation (Precipitation above)
    virtual void setPrecipitation(const Precipitation& p) = 0;
    // the light of the particles (ParticleLighting above)
    virtual void setParticleLighting(const ParticleLighting& l) { (void)l; }
    // sky: a dome around the eye or screen-space layers; isSky false = a 2D layer that is not the sky (ST01's travel
    // map), which the remaster fog leaves alone
    virtual void uploadSky(const std::vector<RenderVertex>& tris, bool screenSpace, bool isSky) = 0;
    // geometry rebuilt every frame (animated actors, one object per actor), drawn after the static geometry
    virtual void uploadDynamic(const std::vector<DynamicObject>& objects) = 0;
    // discontinuity (room change, teleport, stage switch): forget the motion history of every object
    virtual void resetHistory() = 0;
    virtual void resize(int w, int h) = 0;
    // overlay (optional): drawn after the scene on the back buffer (e.g. Dear ImGui);
    // readback (optional): receives the frame as width*height RGBA8 before it is presented
    virtual void draw(const Camera& cam, std::vector<uint8_t>* readback = nullptr,
                      const std::function<void()>& overlay = {}) = 0;
    // Dear ImGui renderer backend (the platform backend stays SDL3): init after ImGui::CreateContext, new frame
    // before ImGui::NewFrame, render inside draw()'s overlay callback
    virtual void imguiInit() = 0;
    virtual void imguiNewFrame() = 0;
    virtual void imguiShutdown() = 0;
    virtual void renderImGui(ImDrawData* data) = 0;
    // the game's 2D layer (menus, message windows, HUD), inside draw()'s overlay callback before the ImGui panels
    virtual void renderCanvas(const ui::Canvas& canvas) = 0;
    // RGBA8 texture for UI drawing (ui::Canvas images, ImGui AddImage); 0 = failure
    virtual TextureHandle createTexture(int w, int h, const uint32_t* rgba) = 0;
    virtual void destroyTexture(TextureHandle tex) = 0;
    // new contents for a whole createTexture texture (same size), before this frame's draw()
    virtual void updateTexture(TextureHandle tex, const uint32_t* rgba) = 0;
    // A copy of this frame's picture (the back buffer after the scene / output pass, before the overlay) in the
    // output-pixel rect (x, y, w, h), usable as an ImGui texture in this frame's overlay -- the PS1's MoveImage of a
    // frame-buffer rect into VRAM drawn back as a sprite (e.g. the development room's weapon picture, 0x545ca0).
    // Call it while building the frame's UI (before draw()); the handle stays the same while w x h does not change.
    // 0 = not supported. Parts of the rect outside the back buffer keep old contents.
    virtual TextureHandle sceneGrab(int x, int y, int w, int h) { (void)x, (void)y, (void)w, (void)h; return 0; }
    // Renders a model preview into the target of `slot` (w x h pixels, transparent background) during the next
    // draw(), before the overlay; the handle is an ImGui texture for that frame's UI (stable while w x h stays).
    // 0 = not supported
    virtual TextureHandle renderPreview(int slot, int w, int h, const PreviewRequest& req) {
        (void)slot, (void)w, (void)h, (void)req;
        return 0;
    }
    virtual void setDebugView(DebugView view) = 0;
    // sun shadows; ignored when the GPU has no ray queries (DXR 1.1)
    virtual void setSun(const SunLight& sun) = 0;
    // the see-through this frame: the characters in focus (at most kMaxSeeThroughBodies) and the open pieces (at most
    // kMaxSeeThroughPieces, the most open first); debug tints every piece and the open ones
    virtual void setSeeThrough(const std::vector<SeeThroughBody>& bodies, const std::vector<SeeThroughPiece>& pieces,
                               bool debug) = 0;
    virtual bool rayTracingSupported() const = 0;
    // anti-aliasing backend + quality; an unavailable backend falls back to Auto (DLSS, else FSR, else none)
    virtual void setAntiAliasing(AaBackend backend, AaQuality quality) = 0;
    virtual void setVsync(bool on) = 0; // off: MAILBOX / IMMEDIATE (Vulkan), tearing allowed (D3D12)
    virtual std::vector<AaBackend> availableBackends() = 0; // probed once (compiled in + DLL + GPU support)
    virtual AaBackend activeBackend() const = 0;
    // renders the static scene from a, then b (previous = a), and compares the motion vectors with the
    // reprojection of the depth buffer; then b twice (no motion). false = not supported by this renderer
    virtual bool motionSelfTest(const Camera& a, const Camera& b, float& maxErrPx, float& staticMaxPx) = 0;
};

std::unique_ptr<Renderer> createRenderer(SDL_Window* window, int w, int h); // Vulkan: created with SDL_WINDOW_VULKAN

} // namespace od
