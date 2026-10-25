// Model previews (Renderer::renderPreview): textured, lit triangles into a small target with depth. Flat shading
// from the screen-space derivatives of the position (low-poly models read best faceted), lit from both sides.
struct Params {
    float4x4 viewProj;
    float4 eye;   // xyz
    float4 light; // xyz: toward the light
};
#ifdef __spirv__ // a push-constant block in Vulkan (the binding layout's PushConstants)
[[vk::push_constant]]
#endif
ConstantBuffer<Params> params : register(b0);
Texture2D<float4> atlas : register(t0);
SamplerState pointClamp : register(s0);

struct VSIn {
    float3 pos : POSITION;
    float2 uv : UV;
    float3 color : COLOR;
};
struct VSOut {
    float4 pos : SV_Position;
    float3 world : WORLD;
    float2 uv : TEXCOORD0;
    float3 color : COLOR;
};

VSOut vs_main(VSIn i) {
    VSOut o;
    o.pos = mul(float4(i.pos, 1.0), params.viewProj);
    o.world = i.pos;
    o.uv = i.uv;
    o.color = i.color;
    return o;
}

float4 ps_main(VSOut i) : SV_Target {
    const float4 t = atlas.SampleLevel(pointClamp, i.uv, 0);
    if (t.a < 0.5) discard;
    float3 n = normalize(cross(ddx(i.world), ddy(i.world)));
    const float3 toEye = normalize(params.eye.xyz - i.world);
    if (dot(n, toEye) < 0.0) n = -n;
    const float3 l = normalize(params.light.xyz);
    const float diffuse = saturate(dot(n, l));
    const float rim = pow(1.0 - saturate(dot(n, toEye)), 3.0) * 0.3;
    const float3 lit = t.rgb * saturate(i.color) * (0.75 + 0.55 * diffuse) + rim;
    return float4(saturate(lit), 1.0);
}
