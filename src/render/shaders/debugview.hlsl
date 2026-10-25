// Debug views of the scene targets (F1 / --view) and the raw depth copy used by the motion self-test.
struct Params {
    int mode; // 0 colour, 1 motion, 2 normal, 3 depth, 4 reactive, 5 raw depth (R32F copy)
    int pad[3];
};
#ifdef __spirv__ // a push-constant block in Vulkan (the binding layout's PushConstants)
[[vk::push_constant]]
#endif
ConstantBuffer<Params> params : register(b0);
Texture2D<float4> src : register(t0);
SamplerState pointClamp : register(s0);

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

float4 ps_main(VSOut i) : SV_Target {
    const float4 v = src.SampleLevel(pointClamp, i.uv, 0);
    switch (params.mode) {
    case 1: return float4(saturate(0.5 + v.xy / 32.0), 0.5, 1.0); // 16 px of motion = full scale
    case 2: return float4(v.rgb, 1.0);
    case 3: return float4((saturate((1.0 - v.r) * 50.0)).xxx, 1.0); // near = bright
    case 4: return float4(v.rrr, 1.0);
    case 5: return float4(v.r, 0.0, 0.0, 1.0);
    default: return float4(v.rgb, 1.0);
    }
}
