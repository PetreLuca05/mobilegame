#include "cel.hlsli"

// The toon_flat shading, per-instance. params: x rim, y gloss, z emissive (< 0 flat)
min16float4 toon_shade(InstOut i)
{
    min16float3 base = i.col.rgb * i.tint.rgb;
    if (i.params.z < 0.0)
        return min16float4(base, 1.0);
    float3 n = normalize(i.nrm);
    float3 v = normalize(u_eye.xyz - i.wpos);
    float ndl = dot(n, u_sun_dir.xyz);
    float outdoor = saturate(i.col.a * i.tint.a);
    float3 light = cel_light_day(n, outdoor);
    float sunny = cel_step(ndl, 0.0) * outdoor + (1.0 - outdoor) * cel_step(n.y, -0.2);
    float gloss = i.params.y;
    float side = saturate(1.0 - abs(n.y) * 1.4);
    float rim = cel_step(1.0 - saturate(dot(n, v)), 0.62) * side * i.params.x * (0.6 + 0.8 * gloss) * (0.35 + 0.65 * sunny);
    float ndh = dot(n, normalize(u_sun_dir.xyz + v));
    float spec = gloss * sunny * (0.5 * cel_step(ndh, 0.95) + 0.5 * cel_step(ndh, 0.99));
    return min16float4(base * light + (rim * 0.45 + spec * 0.9) + base * i.params.z, 1.0);
}
