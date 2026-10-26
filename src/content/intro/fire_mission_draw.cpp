// The fire mission's classes as the game draws them (presentation only: reads their state, never writes it): a fire's
// sprite, its flame tongues and the mission's effects' keyframed sprites, each a billboard record 0x2c of the draw
// commands (exe 0x5f0580) reproduced from the post-tick state with game/effects_draw's helpers.
#include "content/intro/fire_mission.hpp"

#include "game/actor_pools.hpp"
#include "game/actor_state.hpp"

namespace od::port {

namespace {

// the prims' object ids: a base by class and the slot's index
constexpr uint32_t kFireObjects = 0x38000u, kFlameObjects = 0x3c000u, kFireEffectObjects = 0x28000u;
constexpr uint32_t kSemiBit = 25; // a frame's colour word: its sprite is semi-transparent

// a colour word's grey (the average of its channels) as a texel scale (neutral = 0x80)
float greyOf(uint32_t word) {
    return float((word & 0xff) + ((word >> 8) & 0xff) + ((word >> 16) & 0xff)) / 3.0f / 128.0f;
}

// the keyframed sprites of the effect's kinds: their key scripts and styles
struct KindSprites {
    KeyScript (*keys)(size_t) = nullptr; // nullptr: not a sprite
    const DataTable<SpriteStyles>* styles = nullptr;
};
// the key scripts and the styles of an effect kind (FireEffectKind; none for the kinds without a sprite)
KindSprites spritesOf(uint8_t kind) {
    switch (FireEffectKind(kind)) {
    case FireEffectKind::SmokePuff: return {[](size_t v) { return smokeKeys[v]; }, &smokeStyles};
    case FireEffectKind::BlastEmber: return {[](size_t v) { return blastEmberKeys[v]; }, &blastEmberStyles};
    case FireEffectKind::Debris: return {[](size_t v) { return debrisKeys[v]; }, &debrisStyles};
    case FireEffectKind::Ember: return {[](size_t v) { return emberKeys[v]; }, &emberStyles};
    default: return {};
    }
}

} // namespace

// exe 0x5aeb90 (a fire, ST1E static type 0x36): one billboard raised by its strength -- the size's frame (the draw
// reads the frame before the update counts it down: a frame just entered, its ticks reloaded, was drawn as the previous
// one), centre y - (strength >> 5), size (strength >> 4) - (strength >> 6), the size's texture page and CLUT
void fireDraw(fx::DrawContext& c, const fx::State&, uint32_t actor, uint32_t slot) {
    const StaticActor& a = staticActorAt(actor);
    if (FireState(a.state) != FireState::Burn) return;
    const BurningFields& fire = classState<BurningFields>(staticActorAt(actor));
    const uint32_t v = a.variant;
    const int32_t str = fire.strength;
    uint32_t f = fire.frame;
    if (fire.frameTicks == fireFrames[v * kFireFramesPerSize + f].ticks) {
        if (f > 0) {
            --f;
        } else {
            uint32_t last = 0;
            while (last < kFireFramesPerSize - 1 && fireFrames[v * kFireFramesPerSize + last].next != -1) ++last;
            f = last;
        }
    }
    const FireFrame frame = fireFrames[v * kFireFramesPerSize + f];
    const FireSize size = fireSizes[v];
    const uint16_t tpage = size.tpage, clut = size.clut;
    const float ctr[3] = {float(a.pos.x.whole), float(int(a.pos.y.whole) - int16_t(str >> 5)), float(a.pos.z.whole)};
    const int u0 = frame.u0, v0 = frame.v0;
    fx::billboard(c, kFireObjects | slot << 5, ctr, float((str >> 4) - (str >> 6)), 0, c.tex.index(tpage, clut),
                  fx::blendOfAbr(tpage >> 5), u0, v0, u0 + frame.width, v0 + frame.height);
}

// exe 0x5b11a0 per tongue (the flames of a fire, ST1E static type 0x3e): a billboard at (x + dx * 2, y + rise, z + dz *
// 2) of the tongue's height, rotation spin << 4, its frame's uv and colour (the update advanced the frame after the
// draw read it: the frame before), the variant's texture page / CLUT / semi bit
void flameDraw(fx::DrawContext& c, const fx::State&, uint32_t actor, uint32_t slot) {
    const StaticActor& a = staticActorAt(actor);
    const FlameState state = FlameState(a.state);
    if (state != FlameState::Emit && state != FlameState::Die) return;
    const FlameFields& flame = classState<FlameFields>(staticActorAt(actor));
    const uint32_t v = a.variant;
    const SpriteStyle style = tongueStyles[v];
    const uint16_t tpage = style.tpage;
    const int tex = c.tex.index(tpage, style.clut);
    for (uint32_t k = 0; k < kTongues; ++k) {
        const FlameTongue t = flame.tongues[k];
        const uint8_t fr = t.frame;
        if (fr == kFreeTongue || fr == 0) continue;
        const TongueFrame frame = tongueFrames[v * kTongueFramesPerVariant + fr - 1u];
        const uint32_t word = frame.colour;
        const bool semi = ((word >> kSemiBit) & 1) || (style.semi & 1);
        const float ctr[3] = {float(int(a.pos.x.whole) + int(t.dx) * 2), float(int(a.pos.y.whole) + int(t.rise)),
                              float(int(a.pos.z.whole) + int(t.dz) * 2)};
        fx::billboard(c, kFlameObjects | slot << 5 | k, ctr, float(t.height), uint32_t(t.spin) << 4, tex,
                      semi ? fx::blendOfAbr(tpage >> 5) : 0, frame.u0, frame.v0, frame.u1, frame.v1, greyOf(word));
    }
}

// exe 0x437d50 -> 0x437de0 (the mission's effects, ST1E effect type 0x12): the kinds with a keyframed sprite (the smoke
// puffs of a fire put out, the wall blast's embers and debris, a fire's embers) -- a billboard of the key's uv and
// colour (a key just entered was drawn as the previous one), the effect's size, rotation spin << 4, the variant's style
void fireEffectDraw(fx::DrawContext& c, const fx::State&, uint32_t actor, uint32_t slot) {
    const EffectActor& e = effectActorAt(actor);
    const KindSprites sprites = spritesOf(e.subtype);
    if (!sprites.keys || FireEffectState(e.state) == FireEffectState::Start) return;
    const FireEffectFields& fx = classState<FireEffectFields>(effectActorAt(actor));
    const KeyScript keys = sprites.keys(e.variant);
    uint32_t f = fx.key;
    if (f > 0 && fx.keyTicks == keys[f].ticks) --f; // just advanced: the draw read the previous key
    const SpriteKey key = keys[f];
    const SpriteStyle style = (*sprites.styles)[e.variant];
    const uint16_t tpage = style.tpage;
    const uint32_t word = key.colour;
    const bool semi = ((word >> kSemiBit) & 1) || (style.semi & 1);
    const float ctr[3] = {float(fx.pos.x), float(fx.pos.y), float(fx.pos.z)};
    fx::billboard(c, kFireEffectObjects | slot << 4, ctr, float(uint16_t(fx.size)), uint32_t(fx.spin) << 4,
                  c.tex.index(tpage, style.clut), semi ? fx::blendOfAbr(tpage >> 5) : 0, key.u0, key.v0, key.u1, key.v1,
                  greyOf(word));
}

void fireMissionTextures(std::vector<fx::Texture>& out) {
    for (uint32_t v = 0; v < 4; ++v) { // the fires, the flame tongues, the smoke puffs (3 variants)
        fx::addTexture(out, fireSizes[v].tpage, fireSizes[v].clut);
        fx::addTexture(out, tongueStyles[v].tpage, tongueStyles[v].clut);
        if (v < 3) fx::addTexture(out, smokeStyles[v].tpage, smokeStyles[v].clut);
    }
    for (const DataTable<SpriteStyles>* styles : {&blastEmberStyles, &debrisStyles, &emberStyles}) // their first variant
        fx::addTexture(out, (*styles)[0].tpage, (*styles)[0].clut);
}

} // namespace od::port
