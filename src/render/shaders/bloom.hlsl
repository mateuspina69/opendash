// The bloom (OpenDash experiment): the light the glowing effects add (the scene pass's glow target) spread around them.
// A chain of halved copies (ps_down), then each one enlarged and added to the one before it (ps_up), the last onto the
// scene's colour at render size, before the upscaler (the vertex shader: scene.hlsl vs_full).
struct Params {
    float2 target; // 1 / the target's size
    float2 texel;  // the source's texel
    float gain;
    float3 pad;
};
#ifdef __spirv__ // a push-constant block in Vulkan (the binding layout's PushConstants)
[[vk::push_constant]]
#endif
ConstantBuffer<Params> params : register(b0);
Texture2D<float4> src : register(t0);
SamplerState linearClamp : register(s0);

float3 tap(float2 uv) { return src.SampleLevel(linearClamp, uv, 0).rgb; }

float4 ps_down(float4 pos : SV_Position) : SV_Target {
    const float2 uv = pos.xy * params.target, h = params.texel;
    const float3 c = tap(uv) * 4.0 + tap(uv - h) + tap(uv + h) + tap(uv + float2(h.x, -h.y)) + tap(uv - float2(h.x, -h.y));
    return float4(c / 8.0, 1.0);
}

float4 ps_up(float4 pos : SV_Position) : SV_Target {
    const float2 uv = pos.xy * params.target, h = params.texel * 0.5;
    float3 c = tap(uv + float2(-h.x * 2.0, 0.0)) + tap(uv + float2(h.x * 2.0, 0.0)) + tap(uv + float2(0.0, h.y * 2.0)) +
               tap(uv + float2(0.0, -h.y * 2.0));
    c += (tap(uv + float2(-h.x, h.y)) + tap(uv + float2(h.x, h.y)) + tap(uv + float2(h.x, -h.y)) + tap(uv - float2(h.x, h.y))) * 2.0;
    return float4(c / 12.0 * params.gain, 1.0);
}
