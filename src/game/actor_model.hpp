// The binding of an actor to its model: the stage's model bank holds one entry per (slot class, stage-class flag,
// type, variant); binding copies the model's tables into the actor -- the mesh pools' model, collision mesh, colour
// tables, culling sizes and scale; the animated pool's model, frame-data / animation tables, skeleton, part table,
// bone blocks, scale and texture cells.
#pragma once

#include <cstdint>

#include "game/actor_state.hpp"

namespace od::port {

// bindActorModel's variant that matches any entry (the exe's null variant pointer)
constexpr int kAnyModelVariant = -1;

// exe 0x5e0be0(actor, &variant) (the wrappers exe 0x5e0bc0 = a variant byte's address, 0x5e0ba0 = variant 0): the
// bank entry of the actor's slot class, stage-class flag (~+3 & 8) and type, of `variant` (kAnyModelVariant: the
// first of any) bound to the actor. false when the bank has no such model (an animated actor's bone blocks are then
// cleared).
bool bindActorModel(Actor& actor, int variant);

// the animated pool's unit scale (0x200 = 1.0 << the stage shift)
constexpr int16_t kModelScaleOne = 0x200;
// exe 0x4e5cd0: the animated actor's model scale back to 1.0 on the three axes
void resetModelScale(AnimatedActor& actor);

// exe 0x5e32c0(word, n): a texture-page word (an animated actor's texturePage / secondTexturePage) pointed at cell n of
// its page grid (the cell's width / height in bits 16..22 / 23..29; the cells row by row, four pages at most)
void setTexturePageCell(uint32_t& word, uint32_t n);
// exe 0x5e3360(word, u, v, page): a texture-page word's texel origin u / v (bytes) and its page set, its cell size bits
// (0x3fff0000) kept
void setTexturePageTexel(uint32_t& word, uint32_t u, uint32_t v, uint32_t page);

} // namespace od::port
