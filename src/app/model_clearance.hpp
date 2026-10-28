#pragma once
// The model clearance (OpenDash): the game collides with the room's collision grid, thinner than the walls it draws,
// and the player's box is smaller than its model (arms, feet), so the drawn model sinks into walls, door frames and
// furniture. Only the drawing is moved: each tick, at four heights of the model, horizontal segments from its pivot
// reach as far as its vertices do in eight directions; a drawn wall one of them crosses (the room's opaque surfaces,
// the dynamic scenery) pushes the model out along the wall's normal by what lies beyond it. The game's position, its
// collision and its hits are untouched.

#include "render/renderer.hpp"

#include <vector>

class ModelClearance {
public:
    // A room was loaded: keeps its opaque walls (tris[0 .. opaqueEnd), the steep ones)
    void roomLoaded(const std::vector<od::RenderVertex>& tris, size_t opaqueEnd);
    // A logic tick: moves the drawn object `id` (a model at `pivot`, the room's units) out of the walls it sinks into;
    // inactive (a cutscene: the game frames the scene): drawn where the game puts it
    void tick(std::vector<od::DynamicObject>& objects, uint32_t id, const float pivot[3], bool active);

private:
    struct Wall {
        float a[3], b[3], c[3];
        float normal[2]; // horizontal, unit
        float lo[3], hi[3];
    };
    std::vector<Wall> room_;
    float offset_[2]{};    // the drawn model's push this tick (x, z)
    float lastPivot_[2]{}; // the pivot at the last tick (x, z): how far the model travelled
    bool havePivot_ = false;
};
