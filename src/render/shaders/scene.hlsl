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
    float shadowMode;  // 1: this draw receives sun shadows (opaque world / actors); 2: ... all of it in shadow (a room with
                       // no sky); 0: sky, blended effects, sun off
    float semiPass;    // a semi-transparent range: 1 its opaque texels (no STP bit), 2 its blended ones; 0 any draw else
    float modelNear;  // the game's near plane: a model's face is clipped there (surface kSurfaceModel)
    float seeLens;  // 1: the see-through's lens pass (its open pieces drawn as a lens); 0 any draw else
    float fogMode;          // 0 off, 1 classic (the original's linear table fog), 2 remaster (exponential height fog), 3 the PS1's depth cue
    float fogDensity;       // remaster: extinction per world unit
    float fogHeightFalloff; // remaster: density e-folds per world unit of height above the eye
    float fogMaxOpacity;    // remaster: the far field never fogs more than this
    float sunScatter;       // remaster: fog brightened toward the sun (0 = sun off)
    float fogSky;           // remaster fog of a sky draw (the ray never ends): 0 not sky, 1 dome, 2 screen-space layer
    float seeClock;         // the see-through lens's clock (seconds): its rim's slow ripple
    float pageBias;         // the page filter's mip bias: log2(render size / output size), sharp when upscaled
    float4 water;           // remaster medium: x on, y clock (seconds), z 1 = sky draw (replaced), w 0 water / 1 hot air
                            // (cave) / 2 hot air under an open sky (waterTint = the sky colour)
    float4 waterTint;       // the room's water colour (the game's wave tint / 0x80)
    float4x4 skyRay;        // screen-space sky: NDC -> view direction (inverse unjittered rotation-only viewProj)
    float4 seeBody[2];      // the see-through (OpenDash): a character's hole on screen: NDC centre x, y, radius x, y
    float4 seeInfo;         // ... the characters' nearest view depths (x, y; 0 no hole), the open pieces (z), debug (w)
    float4 seePiece[16];    // ... an open piece: its number (RenderVertex::piece), opening, whole (1), its bodies' bits
    float4 heightBounds;  // precipitation height map: min x, min z, 1 / its width, 1 / its depth (world units)
    float4 heightRange;   // ... its top y, its height span, its size in texels (x, z)
    float4 precip;        // precipitation: snow, blizzard (0..1), the gusts' swing, the game's clock (seconds)
    float4 precipWind;    // ... the field's velocity (world units a second), world units a game unit
    float4 precipView;    // ... tan(fovY / 2), the flakes' light
    float4x4 invViewProj; // the blown snow's pass: NDC (x, y, depth) -> world (jittered, as the depth was drawn)
    float4 softDepth;     // the depth copy's near / far planes (its raw depth -> view depth); the effects' near fade:
                          // gone at z, whole from w
    float4 precipLook0;   // the flakes' look (F1, 1 = the defaults): density, size, brightness, the eddies' swirl
    float4 precipLook1;   // ... each flake's flutter, how far they reach, the blown snow's streaks, the motion's chaos
    float4 precipLayer;   // the flake field drawn: its cells' scale, flakes a cell, 1 = the far field
    float4 skyPoleUp;     // the dome's poles (SkyPoles): the colour, a = where the fade starts (a sine)
    float4 skyPoleDown;   // ... a = where it is whole (a sine); 0: a draw that is not the dome, or no fade
    float4 lightPos[32];   // the particle lights (renderer.hpp kMaxParticleLights): position, radius
    float4 lightColor[32]; // ... colour, a = how near it no character stops it (ParticleLight::clearing)
    float4 lightInfo;      // ... how many this draw receives (x), the gain (y)
    float4 lightTerms;     // the traced light (lightTex) on this draw: x 1 = the sun's visibility comes from it (soft
                           // shadows), y the ambient occlusion's strength (0 off), z the shadows' colour (0..1); w 1 =
                           // the light its glowing effects add also goes to the glow target (the bloom)
    float4 lightTrace;     // ps_light: x the tangent of the sun's angular radius (< 0: no sun ray), y the occlusion
                           // rays' reach (world units), z how many a pixel (0 none), w the sample pattern's turn (radians)
    float4 toon;           // the anime look (OpenDash experiment): z 1 = the sun's light falls on a model in two flat
                           // bands; x how much deeper than the scenery's shadow its dark band is, y its rim light,
                           // w its third band: the bright one, on what faces the sun most
    float4 outline;        // ps_outline: x the line's width in pixels, y its strength, z 1 = around the scenery too,
                           // w the view depth it is gone at (world units)
    float4 outlineLook;    // ... x how much of the colour of what it goes around the line takes (0 = the dark ink)
    float4 sunDisc;        // the sun in the sky (OpenDash): x its angular radius (radians), y the sky draw it is on (1
                           // the dome, 2 a screen-space layer; 0 none)
    float4x4 sunShadowVp;  // the sun's shadow map without ray tracing (OD_SM): world -> the map's NDC (orthographic)
    float4 sunShadow;      // ... x its texels a side (0: no map this frame), y world units a texel, z its depth span
                           // (world units), w the tangent of the sun's angular radius (0: hard shadows)
    float4 traceInfo;      // x 1 = rays may be traced this frame (OD_RT with ray tracing on; off: the shadow map)
};
Texture2DArray pages : register(t0);
SamplerState pointClamp : register(s0);
SamplerState pageFilter : register(s1); // a page's colour: anisotropic with a texture pack, nearest (pointClamp) without
Texture2D<float> heightMap : register(t4);  // precipitation: the room seen from above (depth 0 = top, 1 = no surface)
Texture2D<float> sceneDepth : register(t5); // the opaque scene's raw depth (R32F copy, before the blended draws)
Texture2D<float4> sceneColor : register(t6); // the scene's colour before the see-through's lens (what lies behind it)
// The traced light (OpenDash experiments: the sun's soft shadows, the ambient occlusion). A prepass draws the opaque
// surfaces' view depth and normals (ps_main, OD_PRE); ps_light traces each pixel's rays from them, one sample of a 4 x 4
// pattern a pixel; ps_lightBlurX / Y average the pattern away along each surface; the opaque draws read the result
Texture2D<float2> lightTex : register(t7);  // x the sun's visibility, y the ambient occlusion (1 = open)
Texture2D<float> preDepth : register(t8);   // the prepass: view depth (0 = no surface)
Texture2D<float4> preNormal : register(t9); // ... octahedral normals: xy the face's, zw the shading one
Texture2D<float2> lightIn : register(t10);  // a blur pass's input

float2 octEncode(float3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    const float2 s = float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * s;
}

float3 octDecode(float2 e) {
    float3 n = float3(e, 1.0 - abs(e.x) - abs(e.y));
    const float t = saturate(-n.z);
    n.xy += float2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
    return normalize(n);
}

// the world point of the prepass's pixel (px: its centre) at that view depth
float3 preWorld(float2 px, float depth) {
    const float2 ndc = px / renderSize * float2(2.0, -2.0) + float2(-1.0, 1.0);
    const float4 h = mul(float4(ndc, 0.5, 1.0), invViewProj);
    const float3 dir = h.xyz / h.w - eye;
    return eye + dir * (depth / dot(dir, fwd));
}

#ifdef OD_RT
// ray-traced sun shadows (DXR 1.1 inline ray queries). Instance 0 = room (opaque ranges), 1 = dynamic actors;
// primitive i = vertices 3i..3i+2 of that instance's triangle list, attributes (u, v, slice, the dynamic object) per
// vertex
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

// the sun on a model of the anime look (its object: VSIn::object): the same ray, past the model's own faces. Its dark
// band is its form (the smooth normal against the sun); its own low-poly faces would cut that band along their edges
float sunVisibilityPastSelf(float3 wp, float3 n, float self) {
    RayDesc r;
    r.Origin = wp + n * 2.0;
    r.Direction = sunDir;
    r.TMin = 0.5;
    r.TMax = 1.0e6;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
    q.TraceRayInline(sceneAS, RAY_FLAG_NONE, 0xFF, r);
    while (q.Proceed())
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE) {
            const uint inst = q.CandidateInstanceID(), prim = q.CandidatePrimitiveIndex();
            if (inst == 1 && dynAttr[prim * 3].w == self) continue;
            if (blocksLight(inst, prim, q.CandidateTriangleBarycentrics())) q.CommitNonOpaqueTriangleHit();
        }
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0 : 1.0;
}

// 1 = nothing of the instances in `mask` lies along the ray up to tMax
float rayClear(float3 from, float3 dir, float tMax, uint mask) {
    RayDesc r;
    r.Origin = from;
    r.Direction = dir;
    r.TMin = 0.5;
    r.TMax = max(tMax, 0.6);
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
    q.TraceRayInline(sceneAS, RAY_FLAG_NONE, mask, r);
    while (q.Proceed())
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            blocksLight(q.CandidateInstanceID(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
            q.CommitNonOpaqueTriangleHit();
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0 : 1.0;
}

// 1 = a particle light `dist` away along `dir` reaches `from`. The room stops it short of its last tenth (an effect is
// often born on the surface it hit), a character short of the light's own clearing (the body an explosion is born in,
// the hand a shot leaves, must not put the light out)
float lightVisibility(float3 from, float3 dir, float dist, float clearing) {
    if (rayClear(from, dir, dist * 0.9, 1) < 0.5) return 0.0;
    return rayClear(from, dir, dist - clearing, 2);
}

// the traced light at a pixel of the scene pass, of view depth `depth`: its own pixel of lightTex, or (an edge the
// prepass gave to another surface: MSAA, a see-through lens) the neighbour whose surface lies nearest in depth
float2 lightAt(float2 px, float depth) {
    static const int2 kTap[5] = {int2(0, 0), int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1)};
    const int2 p = int2(px);
    float2 terms = float2(1.0, 1.0);
    float best = 1.0e30;
    for (int k = 0; k < 5; ++k) {
        const float d = preDepth.Load(int3(p + kTap[k], 0));
        const float off = abs(d - depth);
        if (d <= 0.0 || off >= best) continue;
        best = off;
        terms = lightTex.Load(int3(p + kTap[k], 0));
        if (off < 0.5 + depth * 0.002) break;
    }
    return terms;
}
#endif

#ifdef OD_SM
// Sun shadows without ray tracing (OpenDash): the opaque room and characters seen from the sun (ps_sun), x the depth
// nearest the sun (0..1 over sunShadow.z), y the object there (VSIn::object; 0 the room, -1 nothing)
Texture2D<float2> sunMap : register(t12);

static const float2 kSunTaps[16] = {
    float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725), float2(-0.09418410, -0.92938870),
    float2(0.34495938, 0.29387760),   float2(-0.91588581, 0.45771432),  float2(-0.81544232, -0.87912464),
    float2(-0.38277543, 0.27676845),  float2(0.97484398, 0.75648379),   float2(0.44323325, -0.97511554),
    float2(0.53742981, -0.47373420),  float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
    float2(-0.24188840, 0.99706507),  float2(-0.81409955, 0.91437590),  float2(0.19984126, 0.78641367),
    float2(0.14383161, -0.14100790)};

// 1 = the sun reaches wp (0..1 at a shadow's soft edge); the point looked up starts `lift` units (and a texel and a
// half) off the surface along n, as the rays do. A texel of the object `self` never shadows (the anime look's models
// pass their own faces, as sunVisibilityPastSelf; -1 = none). With the sun's disc (sunShadow.w) the edge widens with the
// distance from what casts it (the blockers' mean depth around the point). The shadow fades out toward the map's edge
float sunMapVisibility(float3 wp, float3 n, float lift, float self, float2 px) {
    const float texel = sunShadow.y;
    const float4 c = mul(float4(wp + n * (lift + 1.5 * texel), 1.0), sunShadowVp);
    const float2 uv = c.xy * float2(0.5, -0.5) + 0.5;
    const float edge = saturate(min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y)) * 20.0);
    if (edge <= 0.0 || c.z >= 1.0) return 1.0;
    const float z = c.z - texel / sunShadow.z;
    const float2 tap = 1.0 / sunShadow.x;
    // the taps turn from pixel to pixel (and frame to frame with a temporal upscaler, lightTrace.w)
    const float turn = frac(52.9829189 * frac(dot(floor(px), float2(0.06711056, 0.00583715)))) * 6.2831853 + lightTrace.w;
    const float2x2 rot = float2x2(cos(turn), -sin(turn), sin(turn), cos(turn));
    float radius = 1.0; // texels
    if (sunShadow.w > 0.0) {
        const float search = min(sunShadow.w * 800.0 / texel, 16.0) + 1.0;
        float sum = 0.0, count = 0.0;
        for (int k = 0; k < 16; k += 2) {
            const float2 s = sunMap.SampleLevel(pointClamp, uv + mul(kSunTaps[k], rot) * search * tap, 0);
            if (s.x < z && abs(s.y - self) >= 0.5) {
                sum += s.x;
                count += 1.0;
            }
        }
        if (count == 0.0) return 1.0;
        radius = clamp(sunShadow.w * (z - sum / count) * sunShadow.z / texel, 1.0, 16.0);
    }
    float lit = 0.0;
    for (int k = 0; k < 16; ++k) {
        const float2 s = sunMap.SampleLevel(pointClamp, uv + mul(kSunTaps[k], rot) * radius * tap, 0);
        lit += s.x < z && abs(s.y - self) >= 0.5 ? 0.0 : 1.0;
    }
    return lerp(1.0, lit / 16.0, edge);
}
#endif

// what the particle lights give a surface at wp (facing n; ns its shading normal): a soft window falloff to the light's
// radius, soft at the terminator (a low shot still lights the floor it flies over), saturating as lights overlap. 0..1
// per channel
// rim: what they give an edge turned toward them (the anime look's rim light), unsaturated
// cel (a model of the anime look; nc its band normal): no light on what turns away from it, none wrapping past that
// edge, and the light fades to nothing before it: a light near a rounded model would ring a hard circle around itself
// where the model turns away. Its shadows (the rays) stay hard
float3 particleLight(float3 wp, float3 n, float3 ns, bool cel, float3 nc, out float3 rim) {
    float3 sum = float3(0, 0, 0);
    rim = float3(0, 0, 0);
    for (int k = 0; k < int(lightInfo.x + 0.5); ++k) {
        const float3 to = lightPos[k].xyz - wp;
        const float d2 = dot(to, to), r2 = lightPos[k].w * lightPos[k].w;
        if (d2 >= r2) continue;
        const float dist = sqrt(d2);
        const float3 dir = dist > 1e-3 ? to / dist : ns;
        const float window = 1.0 - d2 / r2;
        const float reach = window * window;
        const float facing = cel ? smoothstep(0.0, 0.35, dot(nc, dir)) : saturate((dot(ns, dir) + 0.25) / 1.25);
        const float gain = reach * facing;
        const float3 add = lightColor[k].rgb * gain;
        if (dot(add, float3(1, 1, 1)) < 0.01) continue; // too little to pay a ray for
#ifdef OD_RT
        // as the sun's: a low-poly face turned away from the light starts further out, past its own neighbours
        const float lift = dot(n, dir) > 0.0 ? 2.0 : 12.0;
        if (traceInfo.x > 0.5 && lightVisibility(wp + ns * lift, dir, dist, lightColor[k].w) < 0.5) continue;
#endif
        sum += add;
        rim += lightColor[k].rgb * reach * (cel ? facing : saturate(dot(ns, dir)));
    }
    return 1.0 - exp(-sum);
}

// Soft effects (OpenDash): an effect's polygon (a billboard, a particle, a ring) cut by the scenery it passes into shows
// the cut as a hard line; instead it fades out as it nears the opaque surface behind it (0 at the surface, 1 a softness
// away: wider with the distance, where a polygon's depth spans more). The view depth of the opaque surface at a pixel:
// the depth copy made before the blended draws
float sceneViewDepth(float2 px) {
    const float z = sceneDepth.Load(int3(int2(px), 0));
    return softDepth.x * softDepth.y / (softDepth.y - z * (softDepth.y - softDepth.x));
}

float softEdge(float2 px, float viewDepth) {
    return saturate((sceneViewDepth(px) - viewDepth) / (16.0 + viewDepth * 0.012));
}

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
    float surface : SURFACE; // bits: 1 a model's face (actors, the player, bank models), 4 an effect, 8 an effect that
                             // gives light; * 16 the see-through's scenery piece (RenderVertex::piece)
    float4 uvRect : UVRECT;  // the triangle's UVs' bounds (min u, v, max u, v)
    float object : OBJECT;   // the dynamic object it belongs to (1 on; 0 the room, the sky)
    float3 pieceN : PIECEN;  // the anime look's shading normal of a model's piece, not unit (0: none)
};
struct VSOut {
    float4 pos : SV_Position;
    centroid float3 uvs : TEXCOORD0; // MSAA: an edge pixel samples inside its triangle, not past it into VRAM
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
    nointerpolation float surface : TEXCOORD9;
    nointerpolation float4 uvRect : TEXCOORD10;
    nointerpolation float object : TEXCOORD11;
    float3 pieceN : TEXCOORD12;
};

// no z-buffer on the PS1: decals win by OT order; here they move a few units toward the eye. A negative layer (the
// lock-on reticle, kLayerInFront) is in front of everything: the vertex slid along its own eye ray to twice the near
// plane, so its screen position is unchanged and no model covers it
float3 layerPush(float3 q, float3 from, float layer) {
    if (layer >= 0.0) return q + normalize(from - q) * (layer * 3.0);
    const float front = modelNear * 2.0;
    const float d = dot(q - from, fwd);
    return d > front ? from + (q - from) * (front / d) : q;
}

VSOut vs_main(VSIn i) {
    VSOut o;
    float3 p = layerPush(i.pos, eye, i.layer);
    float3 pp = layerPush(i.prevPos, prevEye, i.layer);
    o.pos = mul(float4(p, 1.0), viewProj);
    o.curClip = mul(float4(p, 1.0), viewProjNoJit);
    o.prevClip = mul(float4(pp, 1.0), prevViewProj);
    o.uvs = float3(i.uv, i.slice);
    o.uvRect = i.uvRect;
    o.shade = i.shade;
    o.cull = i.cull;
    // camera depth, what the original's z = depth / 16384 encodes; in front of everything: the slid depth (an
    // effect's soft edge would fade it at the model it is drawn over)
    o.depth = dot((i.layer < 0.0 ? p : i.pos) - eye, fwd);
    o.normal = i.normal;
    o.color = i.color;
    o.world = i.pos;
    o.faceN = i.faceN;
    o.pairN = i.pairN;
    o.surface = i.surface;
    o.object = i.object;
    o.pieceN = i.pieceN;
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

// the sun in the sky (sunDisc): a disc of radius r around sunDir, its edge one pixel soft, and a halo around it. The
// light it adds to a sky pixel seen along dir; the remaster fog in front of the sky dims it by what it lets through
static const float3 kSunDiscColor = float3(1.0, 0.96, 0.86);
float3 sunInSky(float3 dir) {
    const float r = sunDisc.x;
    const float a = acos(clamp(dot(dir, sunDir), -1.0, 1.0));
    const float px = max(fwidth(a), 1e-5);
    const float core = 1.0 - smoothstep(r - px, r + px, a);
    const float halo = 0.45 * exp(-max(a - r, 0.0) / (r * 2.5)) + 0.12 * exp(-max(a - r, 0.0) / (r * 14.0));
    const float through = fogMode > 1.5 && fogSky > 0.5 ? 1.0 - remasterSkyFog(dir) : 1.0;
    return kSunDiscColor * (core * 1.5 + halo * (1.0 - core)) * through;
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

#ifdef OD_PRE // the traced light's prepass
struct PSOut {
    float viewDepth : SV_Target0;
    float4 normals : SV_Target1; // octahedral: xy the face's, zw the shading one
#ifdef __spirv__
    float depth : SV_Depth;
#endif
};
#else
struct PSOut {
    float4 color : SV_Target0;
    float2 motion : SV_Target1;
    float4 normal : SV_Target2;
    float reactive : SV_Target3;
    float4 glow : SV_Target4; // the light a glowing effect adds (lightTerms.w): what the bloom spreads
#ifdef __spirv__
    float depth : SV_Depth; // Vulkan's depth is D32 (no D24 on AMD): written at D24's precision (kDepthFormat)
#endif
};
#endif

// the normals of a fragment at `world`: n the geometric one of the visible side (double-sided faces have no reliable
// winding), ns the shading one (smooth on actors, per vertex, see toGpu; the face normal on the room), same side as n
void surfaceNormals(float3 world, float3 vertexNormal, out float3 n, out float3 ns) {
    n = normalize(cross(ddy(world), ddx(world)));
    if (dot(n, eye - world) < 0) n = -n;
    ns = normalize(vertexNormal);
    if (dot(ns, n) < 0) ns = -ns;
}

// the shadows' colour (lightTerms.z): what the sun does not reach keeps the sky's light, what it reaches the sun's
static const float3 kShadowHue = float3(0.86, 0.95, 1.08), kSunHue = float3(1.0, 0.97, 0.9);

// A page's colour through pageFilter. With a texture pack the filter's reach stays inside the triangle's UVs: a page
// packs unrelated pieces side by side (the sky's tiles), and one's edge must not read its neighbour. The reach: half the
// footprint of a pixel on the page, at most what the mip levels (down to the PS1's texel) blend
float3 pageColor(float3 uvs, float4 rect) {
    float w, h, count;
    pages.GetDimensions(w, h, count);
    const float2 dx = ddx(uvs.xy), dy = ddy(uvs.xy);
    float2 uv = uvs.xy;
    if (w > 256.0) {
        const float2 reach = min(max(0.5 / w, 0.5 * (abs(dx) + abs(dy))), 0.5 / 256.0 + 0.5 / w);
        const float2 lo = rect.xy + reach, hi = rect.zw - reach;
        uv = lerp(0.5 * (rect.xy + rect.zw), clamp(uv, lo, hi), float2(lo <= hi));
    }
    const float grad = pow(2.0, pageBias);
    return pages.SampleGrad(pageFilter, float3(uv, uvs.z), dx * grad, dy * grad).rgb;
}

PSOut ps_main(VSOut i, bool front : SV_IsFrontFace) {
    // a model's face is clipped at the game's near plane (focal / 5): the exe's model draws (0x5f4980 / 0x5f3820)
    // drop a face whose vertices all lie before it (0x40d0c0) and clip the others there (0x40d670), so a model
    // pressed against a cutscene camera vanishes (ST08 sub 1, Joe's lab). The room draws are not clipped (the
    // renderer keeps depth clip off)
    const uint surface = uint(round(i.surface)), piece = surface >> 4;
    if ((surface & 1u) && i.depth < modelNear) discard;
    // a world effect is gone this close to the camera (opaque texels have no level to fade through; below, the blended)
    if ((surface & 2u) && i.depth < softDepth.z) discard;
    // the see-through (OpenDash, Baldur's Gate style; app/see_through decides which pieces stand between the camera and
    // a character): an open piece turns into a lens, either whole (a piece smaller than the hole) or in a soft round
    // hole around each character it hides, there only on its fragments nearer than that character; the hole's rim
    // ripples slowly. The opaque draws skip where it opens (seeOpen > 0); the lens pass draws only there
    float3 seeTint = float3(1, 1, 1);
    float seeOpen = 0.0, seeRing = 0.0; // how open this fragment is; its distance from the hole's middle (0..1+)
    float2 seeMiddle = float2(0, 0);    // the hole's middle (NDC); seeRing < 0: no hole (a whole piece)
    if (piece != 0u) {
        static const float kHoleEdge = 0.35; // the hole's soft rim, of its radius
        static const float kRimWave = 0.05;  // how far the rim ripples, of the radius
        const float2 here = i.curClip.xy / i.curClip.w;
        bool listed = false;
        for (int p = 0; p < int(seeInfo.z + 0.5); ++p) {
            if (uint(seePiece[p].x + 0.5) != piece) continue;
            listed = true;
            if (seePiece[p].z > 0.5) {
                seeOpen = seePiece[p].y;
                seeRing = -1.0;
                break;
            }
            const uint bodies = uint(seePiece[p].w + 0.5);
            for (int k = 0; k < 2; ++k) {
                const float nearest = k == 0 ? seeInfo.x : seeInfo.y;
                if (!((bodies >> k) & 1u) || nearest <= 0.0 || i.depth >= nearest) continue;
                const float2 q = (here - seeBody[k].xy) / max(seeBody[k].zw, 1e-4);
                const float angle = atan2(q.y, q.x);
                const float d = length(q) + kRimWave * (sin(angle * 5.0 + seeClock * 1.7) + 0.6 * sin(angle * 9.0 - seeClock * 2.3)) * 0.5;
                const float open = saturate((1.0 - d) / kHoleEdge) * seePiece[p].y;
                if (open > seeOpen) {
                    seeOpen = open;
                    seeRing = d;
                    seeMiddle = seeBody[k].xy;
                }
            }
            break;
        }
        if (seeInfo.w > 0.5) { // debug: every piece its own hue, the open ones red; nothing dissolves
            const float h = frac(float(piece) * 0.61803398875);
            seeTint = listed ? float3(1.0, 0.25, 0.25) : lerp(float3(1, 1, 1), saturate(abs(frac(h + float3(0, 2, 1) / 3.0) * 6.0 - 3.0) - 1.0), 0.45);
            seeOpen = 0.0;
        }
    }
    if (seeLens > 0.5 ? seeOpen <= 0.0 : seeOpen > 0.0) discard;
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
#ifdef OD_PRE
    // the prepass: the texels the opaque draws keep (as below), their view depth and normals
    const float texel = i.uvs.z < 0 ? 1.0 : pages.SampleLevel(pointClamp, i.uvs, 0).a;
    clip(texel - 0.25);
    if (semiPass > 0.5 && (semiPass < 1.5) == (i.uvs.z < 0 || texel < 0.75)) discard;
    float3 n, ns;
    surfaceNormals(i.world, i.normal, n, ns);
    PSOut o;
    o.viewDepth = i.depth;
    o.normals = float4(octEncode(n), octEncode(ns));
#ifdef __spirv__
    o.depth = round(saturate(i.pos.z) * 16777215.0) / 16777215.0;
#endif
    return o;
#else
    // slice < 0: untextured. The alpha is the texel's own (0 / the STP bit / 255: a meaning, not a level to filter)
    float4 c = i.uvs.z < 0 ? float4(1, 1, 1, 1)
                           : float4(pageColor(i.uvs, i.uvRect), pages.SampleLevel(pointClamp, i.uvs, 0).a);
    clip(c.a - 0.25); // PS1 colour 0 = transparent (alpha 0; a texel with the STP bit has alpha Vram::kStpAlpha)
    // a semi-transparent polygon blends only its texels with the STP bit (an untextured one all of it); the others are
    // opaque: drawn by the opaque pass over the same range, skipped by the blended one
    const bool blends = i.uvs.z < 0 || c.a < 0.75;
    if (semiPass > 0.5 && (semiPass < 1.5) == blends) discard;
    float3 rgb = saturate(c.rgb * i.shade * i.color) * seeTint;
    if (skyPoleDown.a > 0.0) { // the dome: toward a pole, the sky's own colour there instead of the squeezed tiles
        const float up = normalize(i.world - eye).y;
        rgb = lerp(rgb, up > 0.0 ? skyPoleUp.rgb : skyPoleDown.rgb, smoothstep(skyPoleUp.a, skyPoleDown.a, abs(up)));
    }
    const float3 unlit = rgb; // the texture's colour: what the particle lights multiply, shadowed or not
    float3 n, ns;
    surfaceNormals(i.world, i.normal, n, ns);
    // the anime look: the sun's light falls on a model in two flat bands, by its smooth normal, with no gradient on it
    // (no soft terminator, no occlusion, no penumbra). Its dark band is the scenery's shadow (the sun's strength, the
    // shadows' colour), deeper by toon.x; in a room with no sky, by the sun's direction with no shadow cast, the room's
    // light and that light deeper by toon.x; with the sun off there is no band. rimEdge: the
    // edge turned away from the eye, which the lights that are there brighten (the sun, the sky, the particle lights)
    const bool cel = toon.z > 0.5 && (surface & 1u);
    const float rimEdge = cel ? toon.y * smoothstep(0.62, 0.7, 1.0 - saturate(dot(ns, normalize(eye - i.world)))) : 0.0;
    // which band: by the normal of the model's piece as one solid (VSIn::pieceN), so the bands' border runs a piece's
    // whole length whatever its faces and its pose; a face with none: by the shading normal
    float3 nc = ns;
    if (cel && dot(i.pieceN, i.pieceN) > 0.0) {
        nc = normalize(i.pieceN);
        if (dot(nc, n) < 0.0) nc = -nc;
    }
#if defined(OD_RT) || defined(OD_SM)
    // the traced light of this pixel: the sun through its disc, the ambient occlusion (1 = open)
    float2 traced = float2(1.0, 1.0);
#ifdef OD_RT
    if (lightTerms.x + lightTerms.y > 0.0) traced = lightAt(i.pos.xy, i.depth);
#endif
    const float open = cel ? 1.0 : 1.0 - lightTerms.y * (1.0 - traced.y);
    if (shadowMode > 1.5) { // a room with no sky: the sun reaches nothing in it
        const float room = (1.0 - sunStrength) * open;
        if (cel) {
            // a model still parts in its bands by a light from the sun's direction that casts nothing (no ray): its lit
            // band is the room's light, its dark band deeper by toon.x; the rim and the bright band on the lit side
            const float lit = smoothstep(0.02, 0.08, dot(nc, sunDir));
            rgb *= room * lerp(1.0 - toon.x, 1.0, lit);
            rgb += unlit * room * lit * (rimEdge * saturate(dot(nc, sunDir) + 0.5) +
                                         toon.w * smoothstep(0.72, 0.78, dot(nc, sunDir)));
        } else rgb *= room;
    }
    else if (shadowMode > 0.5) {
        // soft terminator: surfaces turned away get no sun
        const float facing = cel ? smoothstep(0.02, 0.08, dot(nc, sunDir)) : saturate(dot(ns, sunDir) * 3.0);
        float vis = 1.0;
        // a model of the anime look: on its side away from the sun too, whether it stands in a cast shadow (its bands
        // there are those of a room with no sky)
#ifdef OD_SM // the shadow map: a model of the anime look keeps its bands flat (no soft edge on it)
        if (sunShadow.x > 0.5) {
            if (cel) vis = step(0.5, sunMapVisibility(i.world, ns, 2.0, i.object, i.pos.xy));
            else if (facing > 0.0) vis = sunMapVisibility(i.world, ns, dot(n, sunDir) > 0.0 ? 2.0 : 12.0, -1.0, i.pos.xy);
        }
#endif
#ifdef OD_RT
#ifdef OD_SM
        else
#endif
        if (cel) vis = sunVisibilityPastSelf(i.world, ns, i.object);
        else if (facing > 0.0) {
            // a face turned away from the sun while its smooth normal still faces it would shadow itself on its
            // low-poly neighbours (the "shadow terminator" stair-steps) from the surface: its ray starts further out
            // along the smooth normal, past those neighbours, but still meets everything else (a roof keeps it dark)
            if (lightTerms.x > 0.5) vis = traced.x; // (ps_light's ray, the same way)
            else vis = sunVisibility(i.world, ns, dot(n, sunDir) > 0.0 ? 2.0 : 12.0);
        }
#endif
        const float lit = facing * vis;
        const float3 hue = lerp(float3(1.0, 1.0, 1.0), lerp(kShadowHue, kSunHue, lit), lightTerms.z);
        if (cel) {
            // in the sun: the dark band (the scenery's shadow, deeper by half of toon.x) and the lit one. In a cast
            // shadow: the bands of a room with no sky: the scenery's shadow, and that deeper by toon.x.
            // That light comes from the open side, away from what casts the shadow: the sun turned around, as high
            const float shadowLevel = 1.0 - sunStrength;
            const float3 fillDir = normalize(float3(-sunDir.x, sunDir.y, -sunDir.z));
            const float fillFacing = smoothstep(0.02, 0.08, dot(nc, fillDir));
            const float inSun = lerp(shadowLevel * (1.0 - 0.5 * toon.x), 1.0, facing);
            const float inShadow = shadowLevel * lerp(1.0 - toon.x, 1.0, fillFacing);
            rgb *= lerp(inShadow, inSun, vis) * hue;
            // in a cast shadow, the rim and the bright band on the side toward that light, as in a room with no sky
            rgb += unlit * shadowLevel * (1.0 - vis) * fillFacing *
                   (rimEdge * saturate(dot(nc, fillDir) + 0.5) + toon.w * smoothstep(0.72, 0.78, dot(nc, fillDir)));
            // the rim: the sun's on the lit side; the sky's, fainter, on what faces up out of the sun
            const float3 sunRim = lit * saturate(dot(nc, sunDir) + 0.5) * lerp(float3(1.0, 1.0, 1.0), kSunHue, lightTerms.z);
            const float3 skyRim = 0.35 * (1.0 - lit) * saturate(ns.y) * (1.0 - sunStrength) * kShadowHue;
            rgb += unlit * rimEdge * (sunRim + skyRim);
            // the bright band: fixed on the model by the sun's direction, wherever the eye is
            rgb += unlit * toon.w * lit * smoothstep(0.72, 0.78, dot(nc, sunDir)) *
                   lerp(float3(1.0, 1.0, 1.0), kSunHue, lightTerms.z);
        }
        // what the shadow keeps is the sky's light: the occlusion takes from it, not from the sun's
        else rgb *= ((1.0 - sunStrength) * open + sunStrength * lit) * hue;
    } else rgb *= open;
#endif
    // the particle lights on the opaque surfaces (an effect's own texels give light, they don't take it): on top of the
    // sun's shadow, so a shot lights up a shadowed corner; on a model of the anime look, its edge toward them too
    if (lightInfo.x > 0.5 && !(surface & 4u)) {
        float3 rim;
        const float3 light = particleLight(i.world, n, ns, cel, nc, rim);
        rgb = saturate(rgb + unlit * (lightInfo.y * light + rimEdge * 2.0 * (1.0 - exp(-rim))));
    }
    // fog: an opaque / averaged surface turns into the fog colour; light a draw adds (B+F, B+F/4) or takes away
    // (B*(1-F)) fades out instead -- pulled toward the fog colour it would add a fog-coloured veil of its own
    const bool fadeOut = blendMode > 1.5;
    if (fogMode > 2.5) { // the PS1's depth cue (GTE DPCS: SLUS 0x80025b74's meshes, the ground's grid)
        if (!fadeOut) rgb = max(rgb + (rgb * fogColor * (255.0 / 128.0) - rgb) * (max(i.depth, 0.0) / fogEnd), 0.0);
    } else if (fogMode > 0.5 && fogMode < 1.5 && fogEnd > fogStart) { // D3D linear table fog (Maybe_SetD3DFog)
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
    // the sun in the sky: the dome's world point around the eye, or the screen-space layer's NDC position (skyRay);
    // not under water (the water seen far away replaces the sky)
    if (sunDisc.y > 0.5 && !(water.x > 0.5 && water.w < 0.5)) {
        float3 dir;
        if (sunDisc.y < 1.5) dir = normalize(i.world - eye);
        else {
            const float4 r = mul(float4(i.world.xy, 1.0, 1.0), skyRay);
            dir = normalize(r.xyz / r.w);
        }
        rgb += sunInSky(dir);
    }
    PSOut o;
    // the PC exe's vertex alpha of a semi-transparent polygon (0x402100): 0x80 average, 0x40 B + F/4, else 0xff
    float alpha = blendMode > 0.5 && blendMode < 1.5 ? 0.5 : blendMode > 3.5 ? 0.25 : 1.0;
    // an effect's blended texels fade at the scenery (softEdge) and, a world effect, toward the camera: the light it
    // adds / takes away, or its alpha
    if ((surface & 4u) && semiPass > 1.5) {
        const float soft = softEdge(i.pos.xy, i.depth) * ((surface & 2u) ? smoothstep(softDepth.z, softDepth.w, i.depth) : 1.0);
        if (blendMode > 1.5 && blendMode < 3.5) rgb *= soft;
        else alpha *= soft;
    }
    if (seeLens > 0.5) {
        // the lens: what lies behind (the scene so far), clear in the middle and bent outward toward the rim like
        // the edge of thick glass, a thin bright rim; the surface shows only as the opening fades it in
        static const float kRimBend = 0.08; // how far the rim bends what is behind, of the hole's radius
        float2 at = i.pos.xy / renderSize;
        float rim = 0.0;
        if (seeRing >= 0.0) {
            const float r = saturate(seeRing);
            const float2 middle = seeMiddle * float2(0.5, -0.5) + 0.5;
            const float bend = kRimBend * smoothstep(0.55, 1.0, r);
            at += (at - middle) * bend;
            rim = smoothstep(0.75, 1.0, r) * (1.0 - smoothstep(1.0, 1.15, r));
        }
        const float3 behind = sceneColor.SampleLevel(pointClamp, saturate(at), 0).rgb;
        const float3 lens = behind * float3(0.97, 0.99, 1.02) + rim * 0.18;
        rgb = lerp(rgb, lens, seeOpen);
        alpha = 1.0;
    }
    o.color = float4(rgb, saturate(alpha));
    float2 cur = (i.curClip.xy / i.curClip.w) * float2(0.5, -0.5) + 0.5;
    float2 prv = (i.prevClip.xy / i.prevClip.w) * float2(0.5, -0.5) + 0.5;
    o.motion = motionMode > 0.5 ? float2(0, 0) : (prv - cur) * renderSize; // pixels, prev - cur
    // a: what the surface is (ps_outline): 1 a model, 2/3 an effect's opaque texels, 1/3 the scenery (0: nothing drawn)
    o.normal = float4(normalize(i.normal) * 0.5 + 0.5, (surface & 1u) ? 1.0 : (surface & 4u) ? 2.0 / 3.0 : 1.0 / 3.0);
    o.reactive = reactive;
    // the bloom's source: the light a glowing effect's draw adds (B + F, B + F/4: the semi pipelines add it there too)
    o.glow = float4(lightTerms.w > 0.5 && (surface & 8u) ? rgb * saturate(alpha) : float3(0, 0, 0), 1.0);
#ifdef __spirv__
    // the D24 quantisation (coplanar faces tie as on D3D12), of the depth clamped to the viewport's 0..1 as D3D12 does
    // with depth clip off (a face past the near plane is drawn at depth 0)
    o.depth = round(saturate(i.pos.z) * 16777215.0) / 16777215.0;
#endif
    return o;
#endif
}

// ---- the traced light (lightTex above) ----

#ifdef OD_RT
// a pixel's place in the 4 x 4 sample pattern (an ordered dither: neighbours take samples far apart)
static const float kLightSlot[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

float2 ps_light(float4 pos : SV_Position) : SV_Target {
    const int2 p = int2(pos.xy);
    const float depth = preDepth.Load(int3(p, 0));
    if (depth <= 0.0) return float2(1.0, 1.0);
    const float3 wp = preWorld(pos.xy, depth);
    const float4 normals = preNormal.Load(int3(p, 0));
    const float3 n = octDecode(normals.xy), ns = octDecode(normals.zw);
    const float slot = kLightSlot[(p.x & 3) + (p.y & 3) * 4];
    // the sun: one point of its disc a pixel (a spiral over the pattern's 16), from where ps_main's ray starts
    float sun = 1.0;
    if (lightTrace.x >= 0.0 && dot(ns, sunDir) > 0.0) {
        const float a = slot * 2.39996323 + lightTrace.w, r = sqrt((slot + 0.5) / 16.0) * lightTrace.x;
        const float3 t = normalize(cross(abs(sunDir.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0), sunDir));
        const float3 b = cross(sunDir, t);
        const float3 d = normalize(sunDir + (t * cos(a) + b * sin(a)) * r);
        sun = rayClear(wp + ns * (dot(n, sunDir) > 0.0 ? 2.0 : 12.0), d, 1.0e6, 0xFF);
    }
    // the ambient occlusion: how much of the hemisphere over the surface is open within the rays' reach
    // (cosine-weighted directions, stratified over the pattern's pixels and each pixel's rays)
    float open = 1.0;
    const int rays = int(lightTrace.z + 0.5);
    if (rays > 0) {
        const float3 t = normalize(cross(abs(ns.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0), ns));
        const float3 b = cross(ns, t);
        float shut = 0.0;
        for (int k = 0; k < rays; ++k) {
            const float j = float(k) * 16.0 + slot;
            const float u = (j + 0.5) / (16.0 * float(rays)), a = frac(j * 0.61803399) * 6.2831853 + lightTrace.w;
            float3 d = (t * cos(a) + b * sin(a)) * sqrt(u) + ns * sqrt(1.0 - u);
            const float under = dot(d, n); // below the face itself (a smooth normal's hemisphere dips under it)
            if (under < 0.0) d -= 2.0 * under * n;
            shut += 1.0 - rayClear(wp + n * 2.0, d, lightTrace.y, 0xFF);
        }
        open = 1.0 - shut / float(rays);
    }
    return float2(sun, open);
}
#endif

// one axis of the blur that averages ps_light's pattern away: 9 pixels whose weights give each of the pattern's 4
// columns (rows) the same share, only those on the centre's surface (near its plane, facing the same way)
float2 lightBlur(float2 px, int2 axis) {
    const int2 p = int2(px);
    const float depth = preDepth.Load(int3(p, 0));
    if (depth <= 0.0) return float2(1.0, 1.0);
    const float3 wp = preWorld(px, depth), ns = octDecode(preNormal.Load(int3(p, 0)).zw);
    const float reach = 3.0 + depth * 0.004; // world units off the centre's plane
    float2 sum = float2(0.0, 0.0);
    float total = 0.0;
    for (int k = -4; k <= 4; ++k) {
        const int2 q = p + axis * k;
        const float d = preDepth.Load(int3(q, 0));
        if (d <= 0.0) continue;
        const float3 w = preWorld(px + float2(axis * k), d);
        const float facing = saturate(dot(ns, octDecode(preNormal.Load(int3(q, 0)).zw)));
        const float weight = (abs(k) == 4 ? 0.5 : 1.0) * saturate(1.0 - abs(dot(ns, w - wp)) / reach) * pow(facing, 8.0);
        sum += lightIn.Load(int3(q, 0)) * weight;
        total += weight;
    }
    return total > 0.0 ? sum / total : lightIn.Load(int3(p, 0));
}

float2 ps_lightBlurX(float4 pos : SV_Position) : SV_Target { return lightBlur(pos.xy, int2(1, 0)); }
float2 ps_lightBlurY(float4 pos : SV_Position) : SV_Target { return lightBlur(pos.xy, int2(0, 1)); }

// ---- the anime look's line (OpenDash experiment) ----

// Drawn over the opaque scene, before the blended draws: a dark line just outside a model's silhouette (the scenery's
// too with outline.z), on the pixels behind it -- those with a model's pixel within the line's width that stands in
// front of their own surface, or (the scenery under a model's feet) lies as near. From the opaque surfaces' depth
// (sceneDepth) and what they are (the normal target: the shading normal, a = the kind, ps_main)
#ifdef OD_MSAA // aa=msaa: sample 0, as the depth copy's
Texture2DMS<float4> surfaceTex : register(t11);
float4 surfaceAt(int2 p) { return surfaceTex.Load(p, 0); }
#else
Texture2D<float4> surfaceTex : register(t11);
float4 surfaceAt(int2 p) { return surfaceTex.Load(int3(p, 0)); }
#endif

// the view depth of a raw depth of the depth copy
float viewDepthOf(float raw) { return softDepth.x * softDepth.y / (softDepth.y - raw * (softDepth.y - softDepth.x)); }

struct OutlineOut {
    float4 color : SV_Target0; // the line's colour, a = how much of it (SrcAlpha / InvSrcAlpha)
    float reactive : SV_Target1;
};

OutlineOut ps_outline(float4 pos : SV_Position) {
    static const float2 kRing[8] = {float2(1, 0), float2(0.7071, 0.7071), float2(0, 1), float2(-0.7071, 0.7071),
                                    float2(-1, 0), float2(-0.7071, -0.7071), float2(0, -1), float2(0.7071, -0.7071)};
    const int2 p = int2(pos.xy), last = int2(renderSize) - 1;
    const float raw = sceneDepth.Load(int3(p, 0));
    const float4 here = surfaceAt(p);
    const bool sky = raw >= 1.0, model = here.a > 0.83;
    const float depth = sky ? softDepth.y : viewDepthOf(raw);
    const float2 ndc = pos.xy / renderSize * float2(2.0, -2.0) + float2(-1.0, 1.0);
    const float4 h = mul(float4(ndc, min(raw, 0.99999), 1.0), invViewProj);
    const float3 wp = h.xyz / h.w, n = normalize(here.rgb * 2.0 - 1.0);
    float ink = 0.0, nearest = softDepth.y;
    float3 own = float3(0, 0, 0); // the colour of what the line goes around (the scene so far: sceneColor)
    for (int ring = 1; ring <= 2; ++ring)
        for (int k = 0; k < 8; ++k) {
            const float2 at = pos.xy + kRing[k] * (outline.x * 0.5 * float(ring));
            const int2 q = clamp(int2(at), int2(0, 0), last);
            const float rawQ = sceneDepth.Load(int3(q, 0));
            if (rawQ >= 1.0) continue;
            const float kind = surfaceAt(q).a;
            const bool modelQ = kind > 0.83;
            if (!modelQ && !(outline.z > 0.5 && kind > 0.17 && kind < 0.5)) continue;
            const float depthQ = viewDepthOf(rawQ);
            bool edge = sky;
            if (!sky && modelQ && !model) edge = depthQ < depth + 6.0 + depth * 0.01; // a model against the scenery
            else if (!sky && depthQ < depth) { // it stands in front of this pixel's surface
                const float2 ndcQ = at / renderSize * float2(2.0, -2.0) + float2(-1.0, 1.0);
                const float4 hq = mul(float4(ndcQ, rawQ, 1.0), invViewProj);
                edge = dot(n, hq.xyz / hq.w - wp) > 4.0 + depth * 0.012;
            }
            if (!edge) continue;
            ink += 1.0 / 16.0;
            own += sceneColor.Load(int3(q, 0)).rgb;
            nearest = min(nearest, depthQ);
        }
    OutlineOut o;
    o.color = float4(0, 0, 0, 0);
    o.reactive = 0.0;
    if (ink <= 0.0) return o;
    // gone with the distance and into the fog, as the surface it goes around
    float a = saturate(ink * 2.5) * outline.y * saturate(2.0 - 2.0 * nearest / outline.w);
    if (fogMode > 0.5 && fogMode < 1.5 && fogEnd > fogStart) a *= saturate((fogEnd - nearest) / (fogEnd - fogStart));
    else if (fogMode > 1.5 && fogMode < 2.5) a *= 1.0 - remasterFog(eye + normalize(wp - eye) * nearest);
    else if (fogMode > 2.5) a *= saturate(1.0 - nearest / fogEnd);
    // its colour: the dark ink, or (outlineLook.x) the colour of what it goes around, more saturated and darker
    const float3 mean = own / (ink * 16.0);
    const float grey = dot(mean, float3(0.3, 0.59, 0.11));
    const float3 tone = saturate(grey + (mean - grey) * 1.8) * 0.6;
    o.color = float4(lerp(float3(0.07, 0.06, 0.11), tone, outlineLook.x), a);
    o.reactive = a;
    return o;
}

// ---- precipitation (OpenDash remaster: renderer.hpp Precipitation) ----

// the sun's shadow map (OD_SM's sunMap): the opaque room and characters seen from the sun, orthographic (sunShadowVp);
// as the rays, PS1 colour 0 (alpha test) lets the light through and both sides of every face cast
struct SunOut {
    float4 pos : SV_Position;
    float3 uvs : TEXCOORD0;
    nointerpolation float object : TEXCOORD1;
};

SunOut vs_sun(VSIn i) {
    SunOut o;
    o.pos = mul(float4(i.pos, 1.0), sunShadowVp);
    o.uvs = float3(i.uv, i.slice);
    o.object = i.object;
    return o;
}

float2 ps_sun(SunOut i) : SV_Target {
    if (i.uvs.z >= 0) clip(pages.SampleLevel(pointClamp, i.uvs, 0).a - 0.25);
    return float2(i.pos.z, i.object);
}

// the height map (made once per room, upload()): the room's surfaces seen from above, the highest one per texel; the
// alpha-tested texels (PS1 colour 0: railings, foliage) let the snow through
struct HeightOut {
    float4 pos : SV_Position;
    float3 uvs : TEXCOORD0;
};

HeightOut vs_height(VSIn i) {
    HeightOut o;
    const float2 uv = (i.pos.xz - heightBounds.xy) * heightBounds.zw;
    o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, saturate((heightRange.x - i.pos.y) / heightRange.y), 1.0);
    o.uvs = float3(i.uv, i.slice);
    return o;
}

void ps_height(HeightOut i) {
    if (i.uvs.z >= 0) clip(pages.SampleLevel(pointClamp, i.uvs, 0).a - 0.25);
}

// the first surface below the point (x, z) from above; -1e30 = none (outside the room, or open to the void)
float groundAt(float2 xz) {
    const float2 uv = (xz - heightBounds.xy) * heightBounds.zw;
    if (any(uv < 0.0) || any(uv >= 1.0)) return -1e30;
    const float d = heightMap.Load(int3(int2(uv * heightRange.zw), 0));
    return d >= 1.0 ? -1e30 : heightRange.x - d * heightRange.y;
}

uint4 pcg4d(uint4 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;
    v ^= v >> 16u;
    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;
    return v;
}

float4 rand4(int3 cell, uint k) { return float4(pcg4d(uint4(asuint(cell), k))) * (1.0 / 4294967296.0); }

// the light of the snow: white, dimmed with the place (precipView.y), fogged at that point a little less than the
// scenery (kSnowFog: a flake turned into the fog's colour would vanish in the white haze)
static const float kSnowFog = 0.6;

float3 snowLight(float3 wp, float viewDepth) {
    float3 c = float3(0.93, 0.95, 0.98) * precipView.y;
    if (fogMode > 0.5 && fogMode < 1.5 && fogEnd > fogStart)
        c = lerp(c, fogColor, (1.0 - saturate((fogEnd - viewDepth) / (fogEnd - fogStart))) * kSnowFog);
    else if (fogMode > 1.5 && fogMode < 2.5)
        c = lerp(c, remasterFogColor(normalize(wp - eye)), remasterFog(wp) * kSnowFog);
    return c;
}

// The flakes: points fixed in their own field, in cells of kFlakeCell game units around the eye, up to kFlakesPerCell
// random points each; a field moves with the wind (each flake keeps its place in the world while the camera moves or
// turns; the cells at the edge of the range come and go faded out by the distance). Three fields fall at their own
// speeds (kFlakeFall: light flakes drift, heavy ones drop). The wind gusts (it speeds up and slackens) and its slow
// eddies swirl the flakes, holding them up or lifting them a little before they sink again; each flake also flutters.
// A flake is a soft dot like the game's snow sprite (exe 0x591c10), about kFlakeRadius game units across in the world:
// the sprite's size at the player's distance, larger nearer and smaller further on (the sprite's 1..4 pixels in the
// screen's box read as snow stuck to the lens) -- most small, a few big ones; equally bright at any distance (the
// sprite's grey (z >> 3) + 0x40 darkened the near flakes into grey blots). They fill the view out to kFlakeReach, none
// hugging the lens (nearer than kFlakeNear). Two fields of kFlakeCells cells: the near one (kFlakeCell, up to
// precipLayer.y = 32 flakes a cell: density up to 2) and the far one beyond it (cells precipLayer.x = 3 times larger,
// up to 16 flakes: the reach out to ~12000), crossfaded where they meet; each draw is cells x flakes a cell x 6
// vertices (NvrhiRenderer drawScene)
static const int3 kFlakeCells = int3(24, 12, 24);
static const float kFlakeCell = 260.0;
static const uint kFlakesDense = 16; // the flakes a cell at density 1
static const float kFlakeRadius = 3.5;
static const float kFlakeNear = 150.0;
static const float kFlakeFall[3] = {0.65, 1.0, 1.4};
static const float kFlakeRest = 1.2;    // seconds a flake lies on the surface it fell on, fading
static const float kFlakeReach = 4000.0; // game units of view depth the flakes reach
// seconds of travel a flake's streak shows (the eye's motion blur): none for calm snow (a streak reads as a raindrop),
// a short one for blown snow, never longer than kFlakeMaxStreak flake widths
static const float2 kFlakeExposure = float2(0.0, 0.008);
static const float kFlakeMaxStreak = 3.0;

// the wind's eddies at a point of the field: a swirl across the ground and up / down drafts (displacement, game units x
// unit), larger and slower than one flake's flutter; neighbours a few metres apart already drift differently
float3 eddies(float3 q, float t, float unit, float strength) {
    const float3 s = q / (380.0 * unit);
    const float2 swirl = float2(sin(s.z * 1.7 + t * 0.35) + 0.5 * sin(s.z * 3.9 - t * 0.6 + s.y),
                                cos(s.x * 1.9 - t * 0.31) + 0.5 * cos(s.x * 4.3 + t * 0.55 - s.y));
    const float draft = sin(s.x * 2.3 + s.z * 1.7 + t * 0.45) + 0.5 * sin(s.z * 3.1 - s.x * 1.3 - t * 0.7);
    return float3(swirl.x, draft * 0.45, swirl.y) * (strength * unit);
}

// a flake's place at time t: its point carried by its field's wind (gusting: the travel's speed swings around the
// wind's), bent by the eddies (precipLook0.w); then its own way: its flutter (precipLook1.x) and the chaos
// (precipLook1.w): a slow wander off the common course (each flake's speed and heading differ: no two move in parallel)
// and a quick jitter (sines of unrelated rates: the path never repeats nor runs smooth)
float3 flakeAt(float3 home, float3 vel, float4 r2, float t, float unit, float blown) {
    const float gust = (0.35 + 0.15 * blown) * min(1.0 / 0.21, 900.0 * unit / max(length(vel.xz), 1.0));
    float3 q = home + vel * t;
    q.xz += vel.xz * gust * (sin(t * 0.21) + 0.4 * sin(t * 0.53 + 1.3)); // the travel's swing (bounded)
    const float phase = r2.x * 6.2832, rate = 0.7 + r2.y;
    const float chaos = precipLook1.w;
    float3 p = q + eddies(q, t, unit, lerp(120.0, 260.0, blown) * precipLook0.w);
    // the wander: a slow, wide swing around the course, its own rates and phases per axis
    const float wander = lerp(90.0, 260.0, blown) * unit * chaos;
    p.x += sin(t * (0.23 + 0.31 * r2.y) + phase) * wander;
    p.z += sin(t * (0.19 + 0.37 * r2.x) + phase * 2.1) * wander;
    p.y += sin(t * (0.17 + 0.29 * r2.w) + phase * 3.3) * wander * 0.35;
    // the flutter and the jitter
    const float flutter = 20.0 * unit * (1.0 - 0.5 * blown) * precipLook1.x;
    p.x += (sin(t * rate * 1.3 + phase) * 0.6 + sin(t * rate * 2.9 + phase * 2.3) * 0.4) * flutter;
    p.z += (cos(t * rate * 1.1 + phase * 1.7) * 0.6 + sin(t * rate * 2.3 + phase * 0.6) * 0.4) * flutter;
    p.y += sin(t * rate * 0.9 + phase * 1.3) * flutter * 0.4;
    const float jitter = lerp(6.0, 16.0, blown) * unit * chaos;
    p.x += (sin(t * 3.7 * rate + phase * 5.1) + sin(t * 7.9 * rate + phase * 1.9) * 0.5) * jitter;
    p.y += (sin(t * 5.3 * rate + phase * 2.7) + sin(t * 9.1 * rate + phase * 4.3) * 0.5) * jitter;
    p.z += (sin(t * 4.1 * rate + phase * 3.9) + sin(t * 8.3 * rate + phase * 6.1) * 0.5) * jitter;
    return p;
}

struct FlakeOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;   // -1..1 across the flake, x along its streak
    float ratio : TEXCOORD1; // the streak's half length / half width
    float alpha : TEXCOORD2;
    float depth : TEXCOORD3; // the flake's view depth (softEdge)
    float3 color : COLOR0;
};

FlakeOut vs_flake(uint vid : SV_VertexID) {
    FlakeOut o;
    o.pos = float4(0.0, 0.0, -1.0, 1.0); // culled (outside the depth range)
    o.uv = float2(0.0, 0.0);
    o.ratio = 1.0;
    o.alpha = 0.0;
    o.depth = 0.0;
    o.color = float3(0.0, 0.0, 0.0);
    const uint flake = vid / 6, corner = vid % 6;
    const uint perCell = uint(precipLayer.y + 0.5), k = flake % perCell, cell = flake / perCell;
    const bool farField = precipLayer.z > 0.5;
    const float unit = precipWind.w, t = precip.w, blown = saturate(precip.y);
    // this flake's field: its fall speed (light flakes also drift a little more with the wind)
    const float fall = kFlakeFall[k % 3];
    const float3 vel = float3(precipWind.x * (1.2 - 0.2 * fall), precipWind.y * fall, precipWind.z * (1.2 - 0.2 * fall));
    const float size = kFlakeCell * precipLayer.x * unit;
    const int3 base = int3(floor((eye - vel * t) / size)) - kFlakeCells / 2;
    const int3 ci = base + int3(cell % kFlakeCells.x, (cell / kFlakeCells.x) % kFlakeCells.y,
                                cell / (kFlakeCells.x * kFlakeCells.y));
    const int3 seed = farField ? ci + int3(611953, 999331, 314159) : ci; // the far field's own points
    const float4 r = rand4(seed, k), r2 = rand4(seed + int3(7919, 104729, 1299709), k);
    // how many of a cell's points fall: the snow's density, the blown snow's on top (kFlakesDense at density 1)
    const float dense = (precip.x * 0.8 + precip.y * 1.2) * precipLook0.x * float(kFlakesDense) / float(perCell);
    if (r.w >= saturate(dense)) return o;
    const float3 home = (float3(ci) + r.xyz) * size;
    float3 p = flakeAt(home, vel, r2, t, unit, blown);
    float3 v = (p - flakeAt(home, vel, r2, t - 0.04, unit, blown)) / 0.04; // its real travel (the streak)
    // landed: under the first surface below it (a roof, the ground, a table) it lay there since it reached it, where the
    // wind left it, for kFlakeRest seconds; under a roof it never was
    float fade = 1.0;
    const float ground = groundAt(p.xz);
    if (p.y < ground) {
        const float since = (ground - p.y) / max(-vel.y, 1.0);
        if (since >= kFlakeRest) return o;
        p.xz -= v.xz * since;
        const float at = groundAt(p.xz);
        if (at < ground - size) return o; // it would have gone on falling past an edge: not drawn lying
        p.y = at + unit * 6.0; // its soft edge (softEdge) leaves its top on the surface
        fade = 1.0 - since / kFlakeRest;
        v = float3(0.0, 0.0, 0.0);
    }
    const float3 toEye = p - eye;
    const float dist = length(toEye);
    const float depth = dot(toEye, fwd);
    const float z = depth / unit; // the original's z (game units of view depth)
    const float reach = kFlakeReach * precipLook1.y;
    if (z <= 0.0 || z >= reach) return o;
    fade *= 1.0 - smoothstep(0.6 * reach, reach, z);
    fade *= smoothstep(kFlakeNear * unit, 2.0 * kFlakeNear * unit, dist); // none hugging the lens
    // the fields' edges: the near one hands over to the far one, the far one fades out at its own
    const float nearRange = kFlakeCell * unit * float(kFlakeCells.x) * 0.5, range = size * float(kFlakeCells.x) * 0.5;
    const float handover = smoothstep(0.55 * nearRange, 0.9 * nearRange, dist);
    fade *= farField ? handover * (1.0 - smoothstep(0.6 * range, 0.95 * range, dist)) : 1.0 - handover;
    if (fade <= 0.0) return o;
    // its size in the world (most small, a few big clumps); at least ~a pixel (a smaller one is drawn that big,
    // fainter by its width)
    float radius = kFlakeRadius * unit * lerp(0.6, 2.0, r2.z * r2.z) * precipLook0.y;
    const float pixel = dist * 2.0 * precipView.x / renderSize.y; // world units a picture pixel at the flake
    const float drawn = max(radius, pixel * 0.75);
    fade *= radius / drawn;
    radius = drawn;
    // a streak along its travel (the eye's exposure), foreshortened when it comes toward the eye
    const float3 view = toEye / dist;
    const float speed = length(v);
    float3 a = speed > 1e-3 ? v / speed : float3(0.0, 1.0, 0.0);
    float3 b = cross(a, view);
    float across = length(b);
    if (across < 0.05) { // travelling along the view: no streak
        b = normalize(cross(float3(0.0, 1.0, 0.0), fwd));
        a = cross(view, b);
        across = 0.0;
    } else {
        b /= across;
    }
    const float exposure = lerp(kFlakeExposure.x, kFlakeExposure.y, blown) * precipLook1.z;
    const float halfLength = min(radius + 0.5 * speed * exposure * across, radius * kFlakeMaxStreak);
    static const float2 kCorner[6] = {float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, -1), float2(1, 1),
                                      float2(-1, 1)};
    const float2 c = kCorner[corner];
    const float3 wp = p + a * (c.x * halfLength) + b * (c.y * radius);
    o.pos = mul(float4(wp, 1.0), viewProj);
    o.uv = c;
    o.ratio = halfLength / radius;
    o.alpha = fade / pow(o.ratio, 0.25);
    o.depth = depth;
    // as bright at any distance, a little more or less per flake
    o.color = snowLight(p, depth) * lerp(0.9, 1.05, r2.w) * precipLook0.z;
    return o;
}

struct FlakePSOut {
    float4 color : SV_Target0;
    float reactive : SV_Target3;
};

FlakePSOut ps_flake(FlakeOut i) {
    // a soft dot (a soft capsule along a blown flake's streak), its edge about a pixel wide at least
    const float2 q = float2(max(abs(i.uv.x) * i.ratio - (i.ratio - 1.0), 0.0), i.uv.y);
    const float m = 1.0 - length(q);
    const float cover = smoothstep(0.0, max(fwidth(m) * 1.5, 0.5), m);
    const float a = i.alpha * cover * softEdge(i.pos.xy, i.depth);
    if (a <= 0.002) discard;
    FlakePSOut o;
    o.color = float4(i.color, a);
    o.reactive = a;
    return o;
}

// The blown snow (blizzard): sheets of snow carried by the wind, a medium marched along the view ray up to the scene
// (the sky: kBlowReach), absent where the height map shelters (under a roof); drawn over the resolved scene colour
static const float kBlowReach = 3600.0; // game units
static const float kBlowScale = 520.0;  // game units: the sheets' size across the wind (3 x along it)
static const int kBlowSteps = 14;

float vnoise3(float3 p) {
    const float3 i = floor(p), f = frac(p);
    const float3 u = f * f * (3.0 - 2.0 * f);
    const int3 c = int3(i);
    float n[8];
    for (int k = 0; k < 8; ++k) n[k] = rand4(c + int3(k & 1, (k >> 1) & 1, k >> 2), 0u).x;
    return lerp(lerp(lerp(n[0], n[1], u.x), lerp(n[2], n[3], u.x), u.y),
                lerp(lerp(n[4], n[5], u.x), lerp(n[6], n[7], u.x), u.y), u.z);
}

float blowDensity(float3 q) {
    if (q.y < groundAt(q.xz)) return 0.0; // sheltered
    const float unit = precipWind.w, t = precip.w;
    const float3 vel = precipWind.xyz;
    const float2 w = length(vel.xz) > 1e-3 ? normalize(vel.xz) : float2(1.0, 0.0);
    // the sheets move with the wind (a little slower than the flakes) and stretch along it
    float3 p = (q - vel * (t * 0.85)) / (kBlowScale * unit);
    p = float3(dot(p.xz, w) / 3.0, p.y * 1.5, dot(p.xz, float2(-w.y, w.x)));
    const float n = vnoise3(p) * 0.65 + vnoise3(p * 2.1 + 17.3) * 0.35;
    // the gusts: the whole field thickens and thins
    const float gust = 1.0 + precip.z * sin(t * 0.9 + sin(t * 0.37) * 2.0);
    return saturate(n * 1.8 - 0.55) * gust;
}

float4 vs_full(uint id : SV_VertexID) : SV_Position {
    const float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

struct BlowOut {
    float4 color : SV_Target0; // premultiplied: the scattered light, the transmittance (blend One / SrcAlpha)
    float reactive : SV_Target1;
};

BlowOut ps_blizzard(float4 pos : SV_Position) {
    const float raw = sceneDepth.Load(int3(int2(pos.xy), 0));
    const float2 ndc = pos.xy / renderSize * float2(2.0, -2.0) + float2(-1.0, 1.0);
    const float4 h = mul(float4(ndc, min(raw, 0.99999), 1.0), invViewProj);
    float3 dir = h.xyz / h.w - eye;
    float dist = length(dir);
    dir /= max(dist, 1e-3);
    const float reach = kBlowReach * precipWind.w;
    dist = raw >= 1.0 ? reach : min(dist, reach);
    // optical depth ~1.6 over the whole reach at full density and mean sheet cover (the far scenery a pale shape)
    const float sigma = precip.y * 1.6 / (reach * 0.35);
    const float jitter = frac(52.9829189 * frac(dot(pos.xy, float2(0.06711056, 0.00583715)))); // per pixel
    const float stepLen = dist / kBlowSteps;
    float tr = 1.0, scatter = 0.0;
    for (int s = 0; s < kBlowSteps; ++s) {
        const float3 q = eye + dir * ((s + jitter) * stepLen);
        const float e = exp(-blowDensity(q) * sigma * stepLen);
        scatter += tr * (1.0 - e);
        tr *= e;
    }
    BlowOut o;
    o.color = float4(snowLight(eye + dir * (dist * 0.5), dot(dir, fwd) * dist * 0.5) * scatter, tr);
    o.reactive = 1.0 - tr;
    return o;
}
