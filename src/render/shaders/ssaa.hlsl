// SSAA's downsample (upscaler_builtin.cpp): the scene rendered at 2x2 the output, one bilinear tap at the centre of
// each 2x2 block of it (the average of the four).
struct Params {
    uint2 outSize;
    uint2 pad;
};
#ifdef __spirv__ // a push-constant block in Vulkan (the binding layout's PushConstants)
[[vk::push_constant]]
#endif
ConstantBuffer<Params> params : register(b0);
Texture2D<float4> src : register(t0);
SamplerState linearClamp : register(s0);
RWTexture2D<float4> dst : register(u0);

[numthreads(8, 8, 1)] void cs_main(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= params.outSize)) return;
    dst[id.xy] = src.SampleLevel(linearClamp, (float2(id.xy) + 0.5) / float2(params.outSize), 0);
}
