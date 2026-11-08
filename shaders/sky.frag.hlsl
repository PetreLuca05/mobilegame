// Gradient sky with the day cycle's sun and moon discs and a star layer.
cbuffer SkyFS : register(b0, space3)
{
    float4x4 u_inv_view_proj;
    float4 u_eye;
    float4 u_horizon;
    float4 u_zenith;
    float4 u_sun;   // xyz direction, w visibility
    float4 u_moon;  // xyz direction, w visibility
    float4 u_stars; // x amount 0..1, y time
};

struct SkyOut
{
    float4 pos : SV_Position;
    [[vk::location(0)]] float2 ndc : TEXCOORD0;
};

float hash3(float3 p)
{
    return frac(sin(dot(p, float3(12.9898, 78.233, 37.719))) * 43758.5453);
}

float4 PSMain(SkyOut i) : SV_Target0
{
    float4 w = mul(u_inv_view_proj, float4(i.ndc, 1.0, 1.0));
    float3 dir = normalize(w.xyz / w.w - u_eye.xyz);
    float t = smoothstep(0.0, 0.65, dir.y);
    float3 c = lerp(u_horizon.rgb, u_zenith.rgb, t);

    // stars: one in a few hundred cells of a direction grid, above the horizon
    if (u_stars.x > 0.0 && dir.y > 0.0)
    {
        float3 cell = floor(dir * 150.0);
        float h = hash3(cell);
        float star = step(0.9965, h) * smoothstep(0.0, 0.15, dir.y);
        float twinkle = 0.75 + 0.25 * sin(u_stars.y * 2.0 + h * 400.0);
        c += star * twinkle * u_stars.x * float3(0.9, 0.93, 1.0);
    }
    // flat toon discs with a soft halo
    float ds = dot(dir, u_sun.xyz);
    float sun_disc = smoothstep(0.99955, 0.99970, ds);
    float halo = pow(saturate(ds), 300.0) * 0.35;
    c = lerp(c, float3(1.0, 0.96, 0.85), sun_disc * u_sun.w) + halo * u_sun.w * float3(1.0, 0.85, 0.6);
    float dm = dot(dir, u_moon.xyz);
    float moon_disc = smoothstep(0.99970, 0.99980, dm);
    c = lerp(c, float3(0.86, 0.9, 1.0), moon_disc * u_moon.w) + pow(saturate(dm), 600.0) * 0.15 * u_moon.w;
    return float4(c, 1.0);
}
