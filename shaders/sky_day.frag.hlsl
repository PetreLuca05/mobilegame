// Always-day sky: gradient + the sun disc. (sky.frag, with moon and stars, is
// the MG_DAYCYCLE=continuous version; this keeps the same uniform block.)
cbuffer SkyFS : register(b0, space3)
{
    float4x4 u_inv_view_proj;
    float4 u_eye;
    float4 u_horizon;
    float4 u_zenith;
    float4 u_sun;   // xyz direction, w visibility
    float4 u_moon;  // unused
    float4 u_stars; // unused
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
    float3 c = lerp(u_horizon.rgb, u_zenith.rgb, smoothstep(0.0, 0.65, dir.y));
    float ds = dot(dir, u_sun.xyz);
    c = lerp(c, float3(1.0, 0.96, 0.85), smoothstep(0.99955, 0.99970, ds) * u_sun.w);
    return float4(c + pow(saturate(ds), 300.0) * 0.35 * u_sun.w * float3(1.0, 0.85, 0.6), 1.0);
}
