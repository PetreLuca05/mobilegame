// Upscale the 3D image at the start of the UI pass (no separate blit pass).
Texture2D u_src : register(t0, space2);
SamplerState u_src_sampler : register(s0, space2);

struct FsOut
{
    float4 pos : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

float4 PSMain(FsOut i) : SV_Target0
{
    return u_src.Sample(u_src_sampler, i.uv);
}
