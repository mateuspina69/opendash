// Scene pass: PS1 room / actor / sky geometry into colour + motion + normal + reactive targets (D24 depth, LESS).
cbuffer Frame : register(b0) {
    float4x4 viewProj;      // jittered
    float4x4 viewProjNoJit; // unjittered current
    float4x4 prevViewProj;  // unjittered previous
    float3 eye;
    float cullSign;
    float3 prevEye;
    float fogStart;
    float3 fwd;
    float fogEnd;
    float3 fogColor;
    float reactive;   // value this draw writes to the reactive mask (semi-transparent ranges, 2D sky)
    float2 renderSize;
    float motionMode; // 1: no motion (2D screen-space sky)
    float blendMode;  // the draw's PS1 blend (DynamicObject::blend): 0 opaque, 1 avg, 2 add, 3 B*(1-F), 4 B+F/4
    float3 sunDir;     // toward the sun, world (normalised)
    float sunStrength; // darkening of a pixel the sun cannot reach
    float shadowMode;  // 1: this draw receives sun shadows (opaque world / actors); 0: sky, blended effects, sun off
    float semiPass;    // a semi-transparent range: 1 its opaque texels (no STP bit), 2 its blended ones; 0 any draw else
    float2 pad1;
    float fogMode;          // 0 off, 1 classic (the original's linear table fog), 2 remaster (exponential height fog)
    float fogDensity;       // remaster: extinction per world unit
    float fogHeightFalloff; // remaster: density e-folds per world unit of height above the eye
    float fogMaxOpacity;    // remaster: the far field never fogs more than this
    float sunScatter;       // remaster: fog brightened toward the sun (0 = sun off)
    float fogSky;           // remaster fog of a sky draw (the ray never ends): 0 not sky, 1 dome, 2 screen-space layer
    float2 pad2;
    float4 water;           // remaster medium: x on, y clock (seconds), z 1 = sky draw (replaced), w 0 water / 1 hot air
                            // (cave) / 2 hot air under an open sky (waterTint = the sky colour)
    float4 waterTint;       // the room's water colour (the game's wave tint / 0x80)
    float4x4 skyRay;        // screen-space sky: NDC -> view direction (inverse unjittered rotation-only viewProj)
};
Texture2DArray pages : register(t0);
SamplerState pointClamp : register(s0);

#ifdef OD_RT
// ray-traced sun shadows (DXR 1.1 inline ray queries). Instance 0 = room (opaque ranges), 1 = dynamic actors;
// primitive i = vertices 3i..3i+2 of that instance's triangle list, attributes (u, v, slice, -) per vertex
RaytracingAccelerationStructure sceneAS : register(t1);
StructuredBuffer<float4> staticAttr : register(t2);
StructuredBuffer<float4> dynAttr : register(t3);

// candidate hit: PS1 colour 0 (alpha test) lets the light through, like it lets the background show
bool blocksLight(uint inst, uint prim, float2 bary) {
    const uint v = prim * 3;
    const float4 a = inst == 0 ? staticAttr[v] : dynAttr[v];
    if (a.z < 0) return true; // untextured polygon
    const float4 b = inst == 0 ? staticAttr[v + 1] : dynAttr[v + 1];
    const float4 c = inst == 0 ? staticAttr[v + 2] : dynAttr[v + 2];
    const float2 uv = a.xy * (1.0 - bary.x - bary.y) + b.xy * bary.x + c.xy * bary.y;
    return pages.SampleLevel(pointClamp, float3(uv, a.z), 0).a > 0.25;
}

// 1 = the sun reaches wp, 0 = something opaque is in the way; the ray starts `lift` units off the surface along n
float sunVisibility(float3 wp, float3 n, float lift) {
    RayDesc r;
    r.Origin = wp + n * lift; // world units: Megaman is ~250 tall
    r.Direction = sunDir;
    r.TMin = 0.5;
    r.TMax = 1.0e6;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
    q.TraceRayInline(sceneAS, RAY_FLAG_NONE, 0xFF, r);
    while (q.Proceed())
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            blocksLight(q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
            q.CommitNonOpaqueTriangleHit();
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0 : 1.0;
}
#endif

struct VSIn {
    float3 pos : POSITION;
    float2 uv : UV;
    float slice : SLICE;
    float shade : SHADE;
    float cull : CULL;
    float layer : LAYER;
    float3 color : COLOR;
    float3 normal : NORMAL;
    float3 prevPos : PREVPOS;
    float3 faceN : FACEN; // winding normal of this triangle
    float3 pairN : PAIRN; // ... and of the quad's other half (cull 2..5)
};
struct VSOut {
    float4 pos : SV_Position;
    float3 uvs : TEXCOORD0;
    float shade : TEXCOORD1;
    float cull : TEXCOORD2;
    float depth : TEXCOORD3;
    float4 curClip : TEXCOORD4;
    float4 prevClip : TEXCOORD5;
    float3 normal : NORMAL;
    float3 color : COLOR0;
    float3 world : TEXCOORD6; // before the decal push
    nointerpolation float3 faceN : TEXCOORD7;
    nointerpolation float3 pairN : TEXCOORD8;
};

VSOut vs_main(VSIn i) {
    VSOut o;
    // no z-buffer on the PS1: decals win by OT order; here they move a few units toward the eye
    float3 p = i.pos + normalize(eye - i.pos) * (i.layer * 3.0);
    float3 pp = i.prevPos + normalize(prevEye - i.prevPos) * (i.layer * 3.0);
    o.pos = mul(float4(p, 1.0), viewProj);
    o.curClip = mul(float4(p, 1.0), viewProjNoJit);
    o.prevClip = mul(float4(pp, 1.0), prevViewProj);
    o.uvs = float3(i.uv, i.slice);
    o.shade = i.shade;
    o.cull = i.cull;
    o.depth = dot(i.pos - eye, fwd); // camera depth, what the original's z = depth / 16384 encodes
    o.normal = i.normal;
    o.color = i.color;
    o.world = i.pos;
    o.faceN = i.faceN;
    o.pairN = i.pairN;
    return o;
}

// remaster fog: optical depth along the eye -> p ray through a fog whose density is fogDensity at the eye's height and
// falls off exponentially above it (rises below), integrated analytically; distance counted from fogStart
float remasterFog(float3 p) {
    const float3 v = p - eye;
    const float dist = length(v);
    const float d = max(dist - fogStart, 0.0);
    const float k = fogHeightFalloff * v.y; // height change over the whole ray, in e-folds
    // mean density along the ray relative to the eye's: (1 - e^-k) / k, -> 1 for level rays; looking down into a
    // pit is capped so it does not fill in at once
    const float heightTerm = abs(k) > 1e-3 ? min((1.0 - exp(-k)) / k, 3.0) : 1.0;
    return min(1.0 - exp(-fogDensity * d * heightTerm), fogMaxOpacity);
}

// ... seen through to the sky: the ray never ends, so the optical depth is the limit of remasterFog for dist -> inf,
// fogDensity / (fogHeightFalloff * dir.y) above the horizon; a level or downward ray reaches the far-field cap. k is
// per world unit (falloff ~1e-3): any positive k is a finite column, the optical depth only grows toward the horizon
float remasterSkyFog(float3 dir) {
    const float k = fogHeightFalloff * dir.y;
    return k > 0.0 ? min(1.0 - exp(-fogDensity / k), fogMaxOpacity) : fogMaxOpacity;
}

// view direction of a sky pixel: the dome's world point around the eye, or the screen-space layer's NDC position
float3 skyDir(float3 world) {
    if (fogSky < 1.5) return normalize(world - eye);
    const float4 r = mul(float4(world.xy, 1.0, 1.0), skyRay);
    return normalize(r.xyz / r.w);
}

float3 remasterFogColor(float3 dir) {
    const float s = pow(saturate(dot(dir, sunDir)), 6.0);
    return fogColor + (1.0 - fogColor) * sunScatter * s;
}

// remaster underwater look, in the world: light through the water is absorbed with distance (red first), the water
// colour scattered in its place, and the surface's caustics are projected from above onto the room and the actors
float2 hash22(float2 p) {
    float3 q = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yzx + 33.33);
    return frac((q.xx + q.yz) * q.zy);
}

// gradient noise, -1..1
float gnoise(float2 p) {
    const float2 i = floor(p), f = frac(p);
    const float2 u = f * f * (3.0 - 2.0 * f);
    const float a = dot(hash22(i) * 2.0 - 1.0, f);
    const float b = dot(hash22(i + float2(1, 0)) * 2.0 - 1.0, f - float2(1, 0));
    const float c = dot(hash22(i + float2(0, 1)) * 2.0 - 1.0, f - float2(0, 1));
    const float d = dot(hash22(i + float2(1, 1)) * 2.0 - 1.0, f - float2(1, 1));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y) * 1.4;
}

float fbm(float2 p) {
    return gnoise(p) * 0.55 + gnoise(p * 2.03 + 17.1) * 0.3 + gnoise(p * 4.11 - 9.7) * 0.15;
}

// one caustic network: the edges of a cell pattern whose points drift on their own, the domain bent by slow noise so
// no two cells match (bright irregular lines; 0..1); soft 0..1 spreads them (0 thin and sharp, 1 a wide, dim glow)
float causticLayer(float2 p, float t, float soft) {
    p += float2(fbm(p * 0.3 + t * 0.07), fbm(p * 0.3 - t * 0.06 + 31.7)) * 1.3;
    const float2 cell = floor(p), f = frac(p);
    float f1 = 8.0, f2 = 8.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            const float2 g = float2(x, y);
            const float2 h = hash22(cell + g);
            const float2 o = 0.5 + 0.42 * sin(t * (0.6 + h.yx * 0.5) + 6.2832 * h);
            const float d = length(g + o - f);
            if (d < f1) { f2 = f1; f1 = d; }
            else if (d < f2) f2 = d;
        }
    const float edge = f2 - f1;
    const float w = (0.17 + 0.09 * fbm(p * 0.5 + 5.3)) * (1.0 + 2.2 * soft); // width varies along the network
    const float band = exp(-(edge / w) * (edge / w) * 2.2);  // soft-edged band
    const float glow = exp(-edge * 3.0 / (1.0 + 1.6 * soft)); // the light spread around it
    // a spread line carries the same light over more area: dimmer at its peak
    return (band * lerp(0.7, 0.45, soft) + glow * lerp(0.3, 0.4, soft)) / (1.0 + 0.6 * soft);
}

// where the surface focuses light into a shaft (sparse, slowly drifting; 0..1): the god rays and the stronger patches
// on the floor below them share it, so each ray lands on its patch (p: the caustic domain, the light's slant built in)
float shaftMask(float2 p, float t) {
    const float a = fbm(p * 0.35 + float2(t * 0.03, -t * 0.022));
    const float b = gnoise(p * 0.13 + 7.7 + t * 0.01); // gaps: only some patches get a ray
    return smoothstep(0.0, 0.4, a) * smoothstep(-0.4, 0.1, b);
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

// remaster hot air (the lava rooms): a warm haze growing with the distance, into the far smoke / glow
float3 hotAir(float3 rgb, float3 wp, bool blended) {
    const float3 hue = waterTint.rgb / max(max(waterTint.r, waterTint.g), max(waterTint.b, 1e-3));
    const float dist = length(wp - eye);
    if (water.w > 1.5) { // open sky: the sky is kept, the distance fades lightly into a warm, pale sky haze
        if (water.z > 0.5) return rgb;
        const float3 sky = lerp(waterTint.rgb, float3(0.95, 0.85, 0.7), 0.35);
        const float haze = (1.0 - exp(-dist / 14000.0)) * 0.55;
        return blended ? rgb * (1.0 - haze) : lerp(rgb, sky, haze);
    }
    const float3 medium = heatMedium(hue, (wp.y - eye.y) / max(dist, 1e-3));
    if (water.z > 0.5) return medium; // the sky
    const float haze = (1.0 - exp(-dist / 9000.0)) * 0.85;
    return blended ? rgb * (1.0 - haze) : lerp(rgb, medium, haze);
}

float3 underwater(float3 rgb, float3 wp, bool lit, bool blended) {
    const float t = water.y;
    const float3 hue = waterTint.rgb / max(max(waterTint.r, waterTint.g), max(waterTint.b, 1e-3));
    const float dist = length(wp - eye);
    const float3 medium = waterMedium(hue, (wp.y - eye.y) / max(dist, 1e-3));
    if (water.z > 0.5) return medium; // the sky: all water
    if (lit) {
        float3 n = normalize(cross(ddy(wp), ddx(wp)));
        const float up = 0.35 + 0.65 * abs(n.y);           // floors catch most, walls some
        const float fade = saturate(1.0 - dist / 7000.0);   // far patterns would only shimmer
        // the surface pattern carried down: world x / z, sheared a little by the height so walls get it too
        const float2 p = (wp.xz + wp.y * float2(0.25, 0.18)) / 300.0;
        // two networks at unrelated scales / angles / speeds; where they cross the light focuses most
        const float2 r = float2(p.x * 0.799 - p.y * 0.602, p.x * 0.602 + p.y * 0.799) * 1.618 + 11.3;
        // random diffusion: how spread the lines are wanders over the floor and in time (some areas crisp, most soft)
        const float soft = saturate(0.55 + 0.75 * fbm(p * 0.21 + float2(-t * 0.02, t * 0.017) + 3.1));
        const float a = causticLayer(p, t * 0.8, soft), b = causticLayer(r, t * 0.55, saturate(soft + 0.2));
        float c = a * 0.5 + b * 0.3 + a * b * 0.5;
        // large patches of stronger / weaker light (the surface's own unevenness), strongest under a shaft
        c *= 0.35 + 0.9 * smoothstep(-0.35, 0.45, fbm(p * 0.12 + float2(t * 0.015, -t * 0.011)));
        c *= 1.0 + 0.8 * shaftMask(p, t);
        rgb += rgb * float3(0.75, 0.95, 1.0) * c * 0.8 * up * fade;
    }
    // absorption per channel (world units: Megaman is ~250 tall)
    const float3 k = float3(1.6, 0.85, 0.65) / 5000.0;
    const float3 tr = exp(-k * dist);
    if (blended) return rgb * tr;
    rgb = rgb * tr + medium * (1.0 - tr);
    // god rays: light scattered toward the eye inside the shafts, marched along the first stretch of the view ray; the
    // shafts follow the caustics' slanted projection (constant p), so they meet the floor on the bright patches
    const float len = min(dist, 5000.0);
    const int N = 10;
    const float jitter = hash22(wp.xz * 1.7 + wp.y * 0.61).x; // per pixel: breaks the step banding
    float rays = 0.0;
    for (int s = 0; s < N; ++s) {
        const float d = (s + jitter) / N * len;
        const float3 q = eye + (wp - eye) * (d / max(dist, 1e-3));
        const float2 pq = (q.xz + q.y * float2(0.25, 0.18)) / 300.0;
        // thin sheets of light inside a shaft: the ridges of a slow noise, extruded along the light
        const float ridge = 1.0 - abs(gnoise(pq * 0.9 + float2(t * 0.03, -t * 0.02)));
        const float sheet = ridge * ridge * ridge * ridge;
        rays += shaftMask(pq, t) * (0.25 + 1.5 * sheet) * exp(-d / 2600.0);
    }
    rays *= len / N / 2600.0;
    return rgb + hue * float3(0.35, 0.75, 0.85) * rays * 0.8;
}

struct PSOut {
    float4 color : SV_Target0;
    float2 motion : SV_Target1;
    float4 normal : SV_Target2;
    float reactive : SV_Target3;
#ifdef __spirv__
    float depth : SV_Depth; // Vulkan's depth is D32 (no D24 on AMD): written at D24's precision (kDepthFormat)
#endif
};

PSOut ps_main(VSOut i, bool front : SV_IsFrontFace) {
    // culling (RenderVertex::cull): 1 the triangle's own facing; quads by both halves: the other half's facing from its
    // winding normal, with the sign that relates this triangle's winding normal to its raster facing
    const int cullMode = int(round(i.cull));
    if (cullMode > 0) {
        const bool own = (front ? cullSign : -cullSign) >= 0;
        bool pair = own;
        if (cullMode >= 2) {
            const float3 v = eye - i.world;
            const float m = (dot(i.faceN, v) >= 0) == front ? 1.0 : -1.0;
            pair = ((dot(i.pairN, v) * m >= 0) ? cullSign : -cullSign) >= 0;
        }
        const bool visible = cullMode == 1 || cullMode == 4 ? own : cullMode == 5 ? pair : (own || pair);
        if (!visible) discard;
    }
    float4 c = i.uvs.z < 0 ? float4(1, 1, 1, 1) : pages.Sample(pointClamp, i.uvs); // slice < 0: untextured
    clip(c.a - 0.25); // PS1 colour 0 = transparent (alpha 0; a texel with the STP bit has alpha Vram::kStpAlpha)
    // a semi-transparent polygon blends only its texels with the STP bit (an untextured one all of it); the others are
    // opaque: drawn by the opaque pass over the same range, skipped by the blended one
    const bool blends = i.uvs.z < 0 || c.a < 0.75;
    if (semiPass > 0.5 && (semiPass < 1.5) == blends) discard;
    float3 rgb = saturate(c.rgb * i.shade * i.color);
#ifdef OD_RT
    if (shadowMode > 0.5) {
        // geometric normal of the visible side (double-sided faces have no reliable winding)
        float3 n = normalize(cross(ddy(i.world), ddx(i.world)));
        if (dot(n, eye - i.world) < 0) n = -n;
        // shading normal: smooth on actors (per-vertex, see toGpu), the face normal on the room; same side as n
        float3 ns = normalize(i.normal);
        if (dot(ns, n) < 0) ns = -ns;
        const float facing = saturate(dot(ns, sunDir) * 3.0); // soft terminator: surfaces turned away get no sun
        // a face turned away from the sun while its smooth normal still faces it would shadow itself on its low-poly
        // neighbours (the "shadow terminator" stair-steps) from the surface: its ray starts further out along the
        // smooth normal, past those neighbours, but still meets everything else (a roof keeps it dark)
        const float vis = facing > 0.0 ? sunVisibility(i.world, ns, dot(n, sunDir) > 0.0 ? 2.0 : 12.0) : 1.0;
        const float lit = facing * vis;
        rgb *= 1.0 - sunStrength * (1.0 - lit);
    }
#endif
    // fog: an opaque / averaged surface turns into the fog colour; light a draw adds (B+F, B+F/4) or takes away
    // (B*(1-F)) fades out instead -- pulled toward the fog colour it would add a fog-coloured veil of its own
    const bool fadeOut = blendMode > 1.5;
    if (fogMode > 0.5 && fogMode < 1.5 && fogEnd > fogStart) { // D3D linear table fog (Maybe_SetD3DFog)
        const float keep = saturate((fogEnd - i.depth) / (fogEnd - fogStart));
        rgb = fadeOut ? rgb * keep : lerp(fogColor, rgb, keep);
    } else if (fogMode > 1.5) {
        const bool sky = fogSky > 0.5;
        const float3 dir = sky ? skyDir(i.world) : normalize(i.world - eye);
        const float f = sky ? remasterSkyFog(dir) : remasterFog(i.world);
        rgb = fadeOut ? rgb * (1.0 - f) : lerp(rgb, remasterFogColor(dir), f);
    }
    if (water.x > 0.5 && water.w > 0.5) rgb = hotAir(rgb, i.world, reactive > 0.0 && water.z < 0.5);
    else if (water.x > 0.5) rgb = underwater(rgb, i.world, reactive == 0.0 && water.z < 0.5, reactive > 0.0 && water.z < 0.5);
    PSOut o;
    // the PC exe's vertex alpha of a semi-transparent polygon (0x402100): 0x80 average, 0x40 B + F/4, else 0xff
    o.color = float4(rgb, blendMode > 0.5 && blendMode < 1.5 ? 0.5 : blendMode > 3.5 ? 0.25 : 1.0);
    float2 cur = (i.curClip.xy / i.curClip.w) * float2(0.5, -0.5) + 0.5;
    float2 prv = (i.prevClip.xy / i.prevClip.w) * float2(0.5, -0.5) + 0.5;
    o.motion = motionMode > 0.5 ? float2(0, 0) : (prv - cur) * renderSize; // pixels, prev - cur
    o.normal = float4(normalize(i.normal) * 0.5 + 0.5, 1.0);
    o.reactive = reactive;
#ifdef __spirv__
    // the D24 quantisation (coplanar faces tie as on D3D12), of the depth clamped to the viewport's 0..1 as D3D12 does
    // with depth clip off (a face past the near plane is drawn at depth 0)
    o.depth = round(saturate(i.pos.z) * 16777215.0) / 16777215.0;
#endif
    return o;
}
