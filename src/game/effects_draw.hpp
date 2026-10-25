// The effects as the game draws them (presentation only: reads the game state, never writes it): the primitives the
// app turns into triangles facing its camera, the texture pages and gradient rows they sample, and the draws of the
// game's effect classes (the content's classes draw through the same helpers from their areas, registered in
// game/content_registry effectDraws).
//
// dash2.exe draws its effects in two ways:
//  * draw commands (the cursor 0x6d0050, game/draw_commands) consumed by exe 0x5f0580 in the draw pass: the billboard
//    record 0x2c (a camera-facing textured quad: centre, half-diagonal size, rotation, uv rect, tpage / clut, colour,
//    semi bit), the ring records 0xf0 / 0xf4 (a camera-facing gouraud ring / disc: 16 G4 segments between two radii);
//  * primitives linked straight into the ordering table: the burst's 16 spokes (exe 0x55d4e0), the hurt sparks' gouraud
//    lines (exe 0x566290) and the pickup list 0x6d006c drawn by exe 0x5f1500 (gouraud crystals + sprites).
// Each draw below reproduces one game draw function from the actor's post-tick state. Geometry is returned in game units
// around a centre: camera-plane offsets (x right, y down, as the PS1 view space) for billboards / rings / spokes, world
// offsets (game axes, Y down) for the pickup models and the line ends.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <vector>


namespace od::fx {

struct Vertex {
    float off[3] = {0, 0, 0}; // offset from Prim::centre (see Prim::screen)
    float u = 0, v = 0;       // texels in the 256x256 page (gradient page: u 0.5..255.5 along the row, v = row + 0.5)
    float shade = 1.0f;       // texel multiplier (PS1 0x80 = 1 for textured, colour / 255 for the gradient rows)
    float color[3] = {1.0f, 1.0f, 1.0f}; // per-channel multiplier (R, G, B): PS1 colour / 128 textured, / 255 untextured
    float woff[3] = {0, 0, 0}; // Prim::screen only: a world offset (game axes) added before the camera-plane offset
                               // (screen-sized shapes stretched between two world points, e.g. the Blade Arm's beam)
};

constexpr int kUntextured = -2;

struct Prim {
    float centre[3] = {0, 0, 0}; // game units, game axes (Y down)
    bool screen = true;          // off = (x right, y down) in the camera plane; false: world offset (game axes)
    bool line = false;           // v[0] / v[1] are the ends of a 1-pixel line (the app widens it)
    int n = 4;                   // 3 or 4 vertices, PS1 order (triangles 0 1 2 and 1 3 2)
    Vertex v[4];
    int texture = -1;            // index into the TextureCache; -1 = the gradient page (rows of gradients());
                                 // kUntextured = a plain gouraud polygon (Vertex::color, PS1 colour / 255)
    uint8_t blend = 0;           // 0 opaque, 1 + PS1 semi mode (1 avg, 2 add, 3 B * (1 - F), 4 B + F/4): od::DynamicObject::blend
    uint32_t object = 0;         // groups the prims of one actor (stable across ticks)
    bool front = false;          // OpenDash: pushed toward the eye in the shader so the target's model never
                                 // covers it (the lock-on reticle, app/reticle_overlay)
};

// A texture page the effects sample: decoded from VRAM (tpage / clut) and modulated by the PS1 texture colour
// (tint 0xBBGGRR, 0x808080 = neutral: texel * tint / 128).
struct Texture {
    uint16_t tpage = 0, clut = 0;
    uint32_t tint = 0x808080;
};
// The effect textures met so far, (tpage, clut, tint) -> a stable index. The draws register what they sample (index());
// the app decodes one slice per entry from the current stage's VRAM when it builds a room, and rebuilds the room's slices
// when a draw met a new key (takeDirty()): prims whose index has no slice yet are not drawn until then. Any stage can use
// any (tpage, clut): the texture is whatever that VRAM area holds in the loaded stage.
class TextureCache {
public:
    int index(uint16_t tpage, uint16_t clut, uint32_t tint = 0x808080); // registers a new key (marks dirty)
    int find(uint16_t tpage, uint16_t clut, uint32_t tint = 0x808080) const; // -1: not registered
    const std::vector<Texture>& entries() const { return list_; }
    bool takeDirty() {
        const bool d = dirty_;
        dirty_ = false;
        return d;
    }
    void clear() {
        list_.clear();
        dirty_ = false;
    }

private:
    std::vector<Texture> list_;
    bool dirty_ = false;
};
// The textures known to be sampled by the draws (registered up front so a room built with them needs no rebuild when
// the first effect appears; the content's lists after the game's, game/content_registry effectTextures); the tables
// come from the bound data source.
std::vector<Texture> textures();
// Gradient rows of the untextured (gouraud) primitives: row r goes from colour [0] (u = 0) to [1] (u = 1), 0xBBGGRR
// (the content's rows after the game's, game/content_registry effectGradientRows).
std::vector<std::array<uint32_t, 2>> gradients();

// Presentation state kept across ticks (the frame counter 0x6d0006 the colour cycles read is not maintained by the
// logic port; the explosion's flash counters are aged by its own draw function, so the drawn value is recovered
// from the previous tick).
struct State {
    uint32_t tick = 0;
    std::map<uint32_t, std::array<int8_t, 4>> flashes; // explosion actor -> last post-tick flash counters
};

// Every live effect of the ported classes: the buster shots and the extinguisher's stream (pool 0x10), sparks / bursts /
// the Blade Arm's trail / the Shining Laser's beam (pool 0xa0), the death explosion, the hurt sparks and the pickups
// (pool 0x60), and the content classes' draws (game/content_registry effectDraws). Call once per logic tick, after the
// tick. `tex`: the texture cache (Prim::texture = its index).
void collect(State& st, TextureCache& tex, std::vector<Prim>& out);

// ---- the drawing helpers of the class draws -------------------------------------------------------------------------
struct DrawContext {
    TextureCache& tex;
    std::vector<Prim>& out;
};
constexpr uint32_t kNeutralTint = 0x808080;
// a PS1 semi-transparency mode (a texture page's / a draw mode's abr bits, & 3) as od::DynamicObject::blend 1..4 (1 B/2 +
// F/2, 2 B + F, 3 B - F, 4 B + F/4); a texture page's: blendOfAbr(tpage >> 5)
uint8_t blendOfAbr(uint32_t abr);
// the game's 64-step cosine (0xa72500, the screen x component) / sine (0xa6c660, the y component): the boot loop exe
// 0x54a683 copies every 64th {s16 sin, s16 cos} of the exe table 0x847ad8 -- read from the exe table (identical, and
// present before the boot step ran); a std:: fallback without exe data
float tableCos(uint32_t i);
float tableSin(uint32_t i);
// exe 0x5f0580's billboard record 0x2c: corners C - (a, b), C + (-b, a), C + (b, -a), C + (a, b) with a = cos[i] * size,
// b = sin[i] * size, i = (rotation + 0x600) >> 6 (screen y down); uv (u0, v0) .. (u1, v1); nothing for texture < 0
void billboard(DrawContext& c, uint32_t object, const float centre[3], float size, uint32_t rot, int texture,
               uint8_t blend, int u0, int v0, int u1, int v1, float shade = 1.0f);
// a vertex of the gradient row `row` at (x, y), t along the row (0 the first colour, 1 the second)
Vertex grad(float x, float y, int row, float t, float shade);
// exe 0x5f0580's ring record 0xf0: 16 G4 segments of the 64-step circle between radius rIn (the row's colour, u 0) and
// rOut (black, u 1); additive (draw mode 0xe1000220)
void ring(DrawContext& c, uint32_t object, const float centre[3], float rIn, float rOut, int row, float shade);
// the gradient row of a content colour pair (game/content_registry effectGradientRows); -1 when not registered
int contentGradientRow(uint32_t inner, uint32_t outer);
// a texture key added once to an up-front list
void addTexture(std::vector<Texture>& t, uint16_t tpage, uint16_t clut, uint32_t tint = kNeutralTint);

// A content class's draw (game/content_registry effectDraws): `actor` its slot's address, `slot` its index in its pool
using ClassDraw = void (*)(DrawContext& c, const State& st, uint32_t actor, uint32_t slot);

} // namespace od::fx
