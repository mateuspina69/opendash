// The 2D pass (ui_pass.cpp): the game canvas and the F1 panels (Dear ImGui) -- textured x vertex colour.
struct Transform {
    float2 scale;
    float2 translate;
};
#ifdef __spirv__ // a push-constant block in Vulkan (the binding layout's PushConstants)
[[vk::push_constant]]
#endif
ConstantBuffer<Transform> xf : register(b0);
Texture2D tex : register(t0);
SamplerState linearClamp : register(s0);

struct VSIn {
    float2 pos : POSITION;
    float2 uv : UV;
    float4 col : COLOR;
};
struct VSOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 col : COLOR0;
};

VSOut vs_main(VSIn i) {
    VSOut o;
    o.pos = float4(i.pos * xf.scale + xf.translate, 0, 1);
    o.uv = i.uv;
    o.col = i.col;
    return o;
}

float4 ps_main(VSOut i) : SV_Target { return i.col * tex.Sample(linearClamp, i.uv); }
