// The game's 2D layer (menus, message windows, HUD, title screens), independent of Dear ImGui (which only draws the
// F1 developer panels): a list of coloured / textured triangles in window pixels, cut into draw commands by texture
// and clip rect. The renderer draws it after the scene (Renderer::renderCanvas), under the ImGui panels.
#pragma once

#include <cstdint>
#include <vector>

namespace od::ui {

struct Vec2 {
    float x = 0.0f, y = 0.0f;
    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
};

using Color = uint32_t; // 0xAABBGGRR (red in the low byte)
constexpr Color rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255) { return a << 24 | b << 16 | g << 8 | r; }

struct Vertex { // 20 bytes: position (window pixels), texture coordinates, colour (multiplies the texel)
    Vec2 pos, uv;
    Color col;
};

using TextureId = uint64_t; // the renderer's UI texture (Renderer::createTexture); 0 = untextured (white)

struct Cmd {
    TextureId tex = 0;
    float clip[4]{}; // x0 y0 x1 y1 in window pixels
    uint32_t first = 0, count = 0; // indices
};

class Canvas {
public:
    // a new frame of a w x h window: no geometry, the clip = the window
    void begin(float w, float h);
    float width() const { return w_; }
    float height() const { return h_; }

    // the clip rect of what follows until the pop; intersect = within the current one
    void pushClip(Vec2 a, Vec2 b, bool intersect = false);
    void popClip();

    void rectFilled(Vec2 a, Vec2 b, Color c);
    // corner colours: top-left, top-right, bottom-right, bottom-left
    void rectFilledMultiColor(Vec2 a, Vec2 b, Color tl, Color tr, Color br, Color bl);
    void quadFilled(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, Color c);
    void triangle(Vec2 p0, Vec2 p1, Vec2 p2, Color c0, Color c1, Color c2);
    void line(Vec2 a, Vec2 b, Color c, float thickness);
    // a convex polygon (either winding) with antialiased edges: a fringe `aa` px wide fading out across its outline
    void convexFillAA(const Vec2* pts, int n, Color c, float aa = 1.0f);
    // antialiased shapes: a rect with round corners of radius r, a disc
    void roundRect(Vec2 a, Vec2 b, float r, Color c);
    void disc(Vec2 centre, float r, Color c);
    void image(TextureId t, Vec2 a, Vec2 b, Vec2 uv0, Vec2 uv1, Color c);
    void imageQuad(TextureId t, Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, Vec2 uv0, Vec2 uv1, Vec2 uv2, Vec2 uv3, Color c);

    const std::vector<Vertex>& vertices() const { return vtx_; }
    const std::vector<uint32_t>& indices() const { return idx_; }
    const std::vector<Cmd>& cmds() const { return cmds_; }

private:
    struct Clip {
        float r[4];
    };
    // a command for texture t under the current clip (the last one when they match)
    void use(TextureId t);
    // a quad p0 p1 p2 p3 (in order around it) with its uvs / colours
    void quad(TextureId t, const Vec2* p, const Vec2* uv, const Color* c);

    float w_ = 0.0f, h_ = 0.0f;
    std::vector<Vertex> vtx_;
    std::vector<uint32_t> idx_;
    std::vector<Cmd> cmds_;
    std::vector<Clip> clips_;
};

} // namespace od::ui
