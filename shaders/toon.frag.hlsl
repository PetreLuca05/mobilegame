#include "common.hlsli"

// Toon: 2-step light ramp + rim + flat ambient.
// u_params: x rim strength, y unlit (1 = flat colour), z emissive boost
float4 PSMain(VSOut i) : SV_Target0
{
    float3 base = i.col.rgb * u_tint.rgb;
    if (u_params.y > 0.5)
        return float4(base, 1.0);

    float3 n = normalize(i.nrm);
    float3 v = normalize(u_eye.xyz - i.wpos);
    float ndl = dot(n, u_sun_dir.xyz);
    float lit = smoothstep(0.0, 0.04, ndl);              // two steps: shade / lit
    float3 light = u_ambient.rgb + u_sun_color.rgb * lit;
    float rim = pow(1.0 - saturate(dot(n, v)), 3.0) * u_params.x;
    rim *= 0.35 + 0.65 * lit;
    float3 c = base * light + rim * float3(1.0, 1.0, 1.0) * 0.45 + base * u_params.z;
    return float4(c, 1.0);
}
