// Upscale the 3D image at the start of the UI pass (no separate blit pass). LIB-1: a captured frame can be
// blended over it (the Reduced-motion cross-dissolve between the library and the map).
Texture2D u_src : register(t0, space2);
SamplerState u_src_sampler : register(s0, space2);
Texture2D u_prev : register(t1, space2);
SamplerState u_prev_sampler : register(s1, space2);

cbuffer PresentFS : register(b0, space3)
{
    float4 u_mix; // x: how much of the captured frame
};

struct FsOut
{
    float4 pos : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

float4 PSMain(FsOut i) : SV_Target0
{
    float4 now = u_src.Sample(u_src_sampler, i.uv);
    if (u_mix.x <= 0.0)
        return now;
    return lerp(now, u_prev.Sample(u_prev_sampler, i.uv), u_mix.x);
}
