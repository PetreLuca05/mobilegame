// One direction of a 9-tap Gaussian (sigma ~2 texels, linear taps).
Texture2D u_src : register(t0, space2);
SamplerState u_src_sampler : register(s0, space2);

cbuffer BlurFS : register(b0, space3)
{
    float4 u_dir; // xy = texel step along the blur direction
};

struct FsOut
{
    float4 pos : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

float4 PSMain(FsOut i) : SV_Target0
{
    const float w0 = 0.2270270270;
    const float w1 = 0.3162162162, o1 = 1.3846153846;
    const float w2 = 0.0702702703, o2 = 3.2307692308;
    float3 c = u_src.Sample(u_src_sampler, i.uv).rgb * w0;
    c += u_src.Sample(u_src_sampler, i.uv + u_dir.xy * o1).rgb * w1;
    c += u_src.Sample(u_src_sampler, i.uv - u_dir.xy * o1).rgb * w1;
    c += u_src.Sample(u_src_sampler, i.uv + u_dir.xy * o2).rgb * w2;
    c += u_src.Sample(u_src_sampler, i.uv - u_dir.xy * o2).rgb * w2;
    return float4(c, 1.0);
}
