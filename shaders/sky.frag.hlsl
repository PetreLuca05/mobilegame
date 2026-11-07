cbuffer SkyFS : register(b0, space3)
{
    float4x4 u_inv_view_proj;
    float4 u_eye;
    float4 u_horizon;
    float4 u_zenith;
};

struct SkyOut
{
    float4 pos : SV_Position;
    [[vk::location(0)]] float2 ndc : TEXCOORD0;
};

float4 PSMain(SkyOut i) : SV_Target0
{
    float4 w = mul(u_inv_view_proj, float4(i.ndc, 1.0, 1.0));
    float3 dir = normalize(w.xyz / w.w - u_eye.xyz);
    float t = smoothstep(0.0, 0.65, dir.y);
    float3 c = lerp(u_horizon.rgb, u_zenith.rgb, t);
    return float4(c, 1.0);
}
