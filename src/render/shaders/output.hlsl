// Fullscreen copy of an RGBA16F target to the swapchain (output pass), with two frame-buffer effects of the game:
// - the screen ripples (class 0x438750 variant 0: an annulus of the frame buffer redrawn shifted by a few pixels; here
//   the sample is moved toward the centre by the ring's shift);
// - the flooded rooms' screen wave (class 0x431e90 variants 0 / 1 / 5, PS1 / PSP; the PC exe left it out): every PS1
//   screen row Y redrawn from row Y + (sin(((Y - phase) & 127) >> 1 of 64 steps) * amplitude), tinted;
//   or, with OpenDash's remaster underwater look (wave.w = 1), only a gentle continuous refraction (scene.hlsl does
//   the rest in the world).
Texture2D<float4> src : register(t0);
SamplerState pointClamp : register(s0);
Texture2D<float> depthTex : register(t1);    // this frame's raw depth (R32F copy, render size; near = tint.w)
SamplerState linearClamp : register(s1); // the distortions: sub-pixel shifts read bilinearly (no stair-stepped edges)

struct OutConsts {
    float4 ring[4];   // centre x / y, inner / outer radius (output pixels)
    float4 shift;     // the picture's shift inside ring k (output pixels)
    int count;
    float2 size;      // output size in pixels
    float seconds;    // the remaster underwater look's clock
    float4 wave;      // amplitude (output pixels; 0 = off), phase (PS1 rows), output pixels per PS1 row, style
    float4 tint;      // the wave's colour / 0x80; w = the depth copy's near plane
};
#ifdef __spirv__ // a push-constant block in Vulkan (the binding layout's PushConstants)
[[vk::push_constant]]
#endif
ConstantBuffer<OutConsts> c : register(b0);

struct VSOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut vs_main(uint id : SV_VertexID) {
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

// remaster: only a gentle, continuous refraction of the whole picture (the colour, absorption and caustics are drawn
// in the scene pass, in the world)
float2 remasterWarp(float2 uv, float t) {
    const float aspect = c.size.x / c.size.y;
    const float amp = c.wave.x / c.size.y * 0.35;
    float2 off;
    off.x = (sin(uv.y * 7.0 + t * 1.1) * 0.6 + sin(uv.y * 17.0 - t * 1.7 + uv.x * 3.0) * 0.4) / aspect;
    off.y = sin(uv.x * aspect * 5.0 + t * 0.9) * 0.6 + sin(uv.x * aspect * 13.0 + t * 1.4 + uv.y * 2.0) * 0.4;
    return clamp(uv + off * amp, 0.0, 1.0);
}

// the view distance at uv (the scene's LH perspective, far plane 200000) and how much water lies in front of it
float viewDist(float2 uv) {
    const float z = depthTex.SampleLevel(pointClamp, uv, 0);
    const float n = c.tint.w, f = 200000.0;
    return f * n / (f - z * (f - n));
}
float waterBlur(float2 uv) { return smoothstep(500.0, 5000.0, viewDist(uv)); }

// distance blur: a 16-tap disc growing with the distance; a tap only counts when it is itself far, so a sharp near
// object never bleeds into the blurred background around it
float3 volumeBlur(float2 uv) {
    const float b = waterBlur(uv);
    const float3 centre = src.SampleLevel(linearClamp, uv, 0).rgb;
    if (b < 0.01) return centre;
    const float radius = b * 7.0 * c.size.y / 1080.0;
    float3 acc = centre;
    float wsum = 1.0;
    for (int k = 0; k < 16; ++k) {
        const float r = sqrt((k + 0.5) / 16.0) * radius;
        const float a = k * 2.39996;
        const float2 t = clamp(uv + float2(cos(a), sin(a)) * r / c.size, 0.0, 1.0);
        const float w = saturate(waterBlur(t) * 1.5);
        acc += src.SampleLevel(linearClamp, t, 0).rgb * w;
        wsum += w;
    }
    return acc / wsum;
}

// the water seen at infinite distance along a view direction of height dirY: brighter toward the surface above,
// darker down (the scene pass absorbs toward it; the output pass paints it where nothing was drawn)
float3 waterMedium(float3 hue, float dirY) {
    return hue * lerp(float3(0.025, 0.08, 0.12), float3(0.15, 0.38, 0.46), smoothstep(-0.65, 0.85, dirY));
}

// the hot air seen far away along a view direction of height dirY: the glow of the lava at the horizon, dark smoke
// above
float3 heatMedium(float3 hue, float dirY) {
    return lerp(1.0, hue, 0.3) * lerp(float3(0.34, 0.13, 0.05), float3(0.06, 0.03, 0.025), smoothstep(-0.3, 0.7, dirY));
}

float2 hash22(float2 p) {
    float3 q = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yzx + 33.33);
    return frac((q.xx + q.yz) * q.zy);
}
float gnoise(float2 p) { // gradient noise, -1..1
    const float2 i = floor(p), f = frac(p);
    const float2 u = f * f * (3.0 - 2.0 * f);
    const float a = dot(hash22(i) * 2.0 - 1.0, f);
    const float b = dot(hash22(i + float2(1, 0)) * 2.0 - 1.0, f - float2(1, 0));
    const float e = dot(hash22(i + float2(0, 1)) * 2.0 - 1.0, f - float2(0, 1));
    const float d = dot(hash22(i + float2(1, 1)) * 2.0 - 1.0, f - float2(1, 1));
    return lerp(lerp(a, b, u.x), lerp(e, d, u.x), u.y) * 1.4;
}

// remaster hot air: a fine shimmer rising through the picture, stronger on what is far (more hot air in between)
float2 heatWarp(float2 uv, float t) {
    const float aspect = c.size.x / c.size.y;
    const float far = smoothstep(250.0, 4500.0, viewDist(uv));
    const float amp = c.wave.x / c.size.y * (0.25 + 0.75 * far) * 0.9;
    const float2 p = float2(uv.x * aspect * 38.0, uv.y * 26.0 + t * 2.2);
    const float2 q = float2(uv.x * aspect * 71.0 + 13.0, uv.y * 49.0 + t * 3.1);
    const float2 off = float2(gnoise(p) * 0.6 + gnoise(q) * 0.4, gnoise(p + 41.0) * 0.6 + gnoise(q - 17.0) * 0.4);
    return clamp(uv + float2(off.x / aspect, off.y) * amp, 0.0, 1.0);
}

// remaster: the empty background (nothing drawn: depth 1, the PS1 rooms' black / coloured void) becomes the water seen
// far away along that pixel's view direction (pitch wave.y, tan(fov / 2) wave.z), the edge softened by the filter
float3 waterBackdrop(float3 col, float2 uv) {
    // share of empty render pixels around uv (centre + 4 neighbours), for a soft edge
    float2 dim;
    depthTex.GetDimensions(dim.x, dim.y);
    const float2 px = 1.0 / dim;
    float empty = 0.0;
    empty += depthTex.SampleLevel(pointClamp, uv, 0) >= 1.0 ? 2.0 : 0.0;
    empty += depthTex.SampleLevel(pointClamp, uv + float2(px.x, 0), 0) >= 1.0 ? 1.0 : 0.0;
    empty += depthTex.SampleLevel(pointClamp, uv - float2(px.x, 0), 0) >= 1.0 ? 1.0 : 0.0;
    empty += depthTex.SampleLevel(pointClamp, uv + float2(0, px.y), 0) >= 1.0 ? 1.0 : 0.0;
    empty += depthTex.SampleLevel(pointClamp, uv - float2(0, px.y), 0) >= 1.0 ? 1.0 : 0.0;
    const float m = smoothstep(0.0, 1.0, empty / 6.0);
    if (m <= 0.0) return col;
    const float3 hue = c.tint.rgb / max(max(c.tint.r, c.tint.g), max(c.tint.b, 1e-3));
    const float dirY = sin(c.wave.y + atan((0.5 - uv.y) * 2.0 * c.wave.z));
    const bool heat = c.wave.w > 1.5;
    // the void was cleared to the medium at the view's height (clearTargets): only the gradient's difference is
    // added, so what was blended over the void (bubbles, effects without depth) stays on top of it
    const float3 cleared = heat ? heatMedium(hue, sin(c.wave.y)) : waterMedium(hue, sin(c.wave.y));
    const float3 medium = heat ? heatMedium(hue, dirY) : waterMedium(hue, dirY);
    return max(col + (medium - cleared) * m, 0.0);
}

float4 ps_main(VSOut i) : SV_Target {
    float2 uv = i.uv;
    const float2 p = i.uv * c.size;
    for (int k = 0; k < c.count; ++k) {
        const float2 d = p - c.ring[k].xy;
        const float r = length(d);
        if (r >= c.ring[k].z && r < c.ring[k].w && r > 0.0)
            uv -= (d / r) * c.shift[k] / c.size;
    }
    if (c.wave.x > 0.0 && c.wave.w > 1.5) { // hot air: 2 a cave (the void filled), 3 under an open sky (kept)
        const float2 huv = heatWarp(uv, c.seconds);
        const float3 col = src.SampleLevel(linearClamp, huv, 0).rgb;
        return float4(c.wave.w > 2.5 ? col : waterBackdrop(col, huv), 1.0);
    }
    if (c.wave.x > 0.0 && c.wave.w > 0.5) {
        const float2 wuv = remasterWarp(uv, c.seconds);
        return float4(waterBackdrop(volumeBlur(wuv), wuv), 1.0);
    }
    float3 tint = float3(1, 1, 1);
    if (c.wave.x > 0.0) {
        const float row = p.y / c.wave.z;                          // the PS1 screen row
        const float t = fmod(floor(row - c.wave.y) + 1280.0, 128.0); // (Y - phase) & 127
        const float step = floor(t * 0.5);                         // >> 1: 64 table entries
        uv.y += sin(step * (6.2831853 / 64.0)) * c.wave.x / c.size.y;
        tint = c.tint.rgb;
    }
    // untouched pictures stay a 1:1 point copy; shifted ones read bilinearly
    const bool shifted = c.count > 0 || c.wave.x > 0.0;
    const float3 rgb = shifted ? src.SampleLevel(linearClamp, uv, 0).rgb : src.SampleLevel(pointClamp, uv, 0).rgb;
    return float4(rgb * tint, 1.0);
}
