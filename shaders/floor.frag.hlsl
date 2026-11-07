#include "common.hlsli"

// Grid floor. 1 m major lines, 10 cm minor lines, anti-aliased with fwidth.
// u_params: x major width (m), y minor width (m), z minor line alpha
float grid(float2 p, float spacing, float width)
{
    float2 d = abs(frac(p / spacing - 0.5) - 0.5) * spacing; // metres to nearest line
    float2 fw = max(fwidth(p), 1e-4);
    float2 a = 1.0 - smoothstep(width * 0.5 - fw, width * 0.5 + fw, d);
    // fade lines that get thinner than a pixel to avoid moire
    float2 fade = saturate(width / (fw * 1.5));
    return max(a.x * fade.x, a.y * fade.y);
}

float4 PSMain(VSOut i) : SV_Target0
{
    float2 p = i.wpos.xz;
    float major = grid(p, 1.0, u_params.x);
    float minor = grid(p, 0.1, u_params.y) * u_params.z;
    float3 base = i.col.rgb * u_tint.rgb;
    float3 n = normalize(i.nrm);
    float lit = smoothstep(0.0, 0.04, dot(n, u_sun_dir.xyz));
    float3 c = base * (u_ambient.rgb + u_sun_color.rgb * lit);
    c = lerp(c, c * 0.55, minor);
    c = lerp(c, float3(0.05, 0.05, 0.06), major);
    return float4(c, 1.0);
}
