#include "common.hlsli"
#include "cel.hlsli"

// Toon: 3-band cel ramp + hard rim + flat ambient.
// Matte surfaces stop there; glossy ones add a hard two-step specular spot
// (highlight + white hot spot) and a stronger rim.
// u_params: x rim strength, y unlit (1 = flat colour), z emissive boost, w gloss (0 matte .. 1 glossy)
float4 PSMain(VSOut i) : SV_Target0
{
    float3 base = i.col.rgb * u_tint.rgb;
    if (u_params.y > 0.5)
        return float4(base, 1.0);

    float3 n = normalize(i.nrm);
    float3 v = normalize(u_eye.xyz - i.wpos);
    float ndl = dot(n, u_sun_dir.xyz);
    float3 light = u_ambient.rgb + u_sun_color.rgb * cel_lit(ndl);
    float sunny = cel_step(ndl, 0.0);
    float gloss = u_params.w;
    float rim = cel_step(1.0 - saturate(dot(n, v)), 0.62) * u_params.x * (0.6 + 0.8 * gloss) * (0.35 + 0.65 * sunny);
    float ndh = dot(n, normalize(u_sun_dir.xyz + v));
    float spec = gloss * sunny * (0.5 * cel_step(ndh, 0.95) + 0.5 * cel_step(ndh, 0.99));
    float3 c = base * light + (rim * 0.45 + spec * 0.9) * float3(1.0, 1.0, 1.0) + base * u_params.z;
    return float4(c, 1.0);
}
