#include "cel.hlsli"

// The toon_flat shading, per-instance. params: x rim, y gloss, z emissive (< 0 flat)
min16float4 toon_shade(InstOut i)
{
    min16float3 base = i.col.rgb * i.tint.rgb;
    if (i.params.z < 0.0)
        return min16float4(base, 1.0);
#ifdef TOON_FLAT
    // flat-shaded, rim 0, gloss 0 (gpu.c toon_variant): rim and highlight are 0 x something, only the light ramp is left
    return min16float4(base * cel_light_day(normalize(i.nrm), saturate(i.col.a * i.tint.a)) + base * i.params.z, 1.0);
#else
    g_shade_off = (i.params.x == 0.0 && i.params.y == 0.0) ? (uint)u_shade_off.x : 0u;
    float3 n = normalize(i.nrm);
    float3 v = normalize(u_eye.xyz - i.wpos);
    float outdoor = saturate(i.col.a * i.tint.a);
    float3 light = shade_off(32u) ? float3(1.0, 1.0, 1.0) : cel_light_day(n, outdoor); // --shade-off light
    float sunny = shade_off(4u) ? 1.0 : cel_sunny(n, outdoor);                           // sunny
    float gloss = i.params.y;
    float side = saturate(1.0 - abs(n.y) * 1.4);
    float rim = shade_off(8u) ? 0.0 : cel_step(1.0 - saturate(dot(n, v)), 0.62) * side * i.params.x * (0.6 + 0.8 * gloss) * (0.35 + 0.65 * sunny); // rim
    float spec = shade_off(16u) ? 0.0 : gloss * cel_spec(n, v, outdoor);                 // spec
    return min16float4(base * light + (rim * 0.45 + spec * 0.9) + base * i.params.z, 1.0);
#endif
}
