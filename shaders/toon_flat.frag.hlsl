#include "common.hlsli"
#include "cel.hlsli"

// MG_RENDER=minimal toon (M5b): one directional key light + flat ambient
// through the cel ramp, hard rim, glossy spot. No shadows, no point/spot
// lights, no reflection, no fog.
// u_params: x rim strength, y unlit (1 = flat colour), z emissive boost, w gloss (0 matte .. 1 glossy)
float4 PSMain(VSOut i) : SV_Target0
{
    dither_clip(i.pos);
    float3 base = i.col.rgb * u_tint.rgb;
    if (u_params.y > 0.5)
        return float4(base, 1.0);

    float3 n = normalize(i.nrm);
    float3 v = normalize(u_eye.xyz - i.wpos);
    float outdoor = saturate(i.col.a * u_tint.a);
    float3 light = cel_light_day(n, outdoor);
    float sunny = cel_sunny(n, outdoor);
    float gloss = u_params.w;
    // rim only on side-facing surfaces (no ring on flat ground)
    float side = saturate(1.0 - abs(n.y) * 1.4);
    float rim = cel_step(1.0 - saturate(dot(n, v)), 0.62) * side * u_params.x * (0.6 + 0.8 * gloss) * (0.35 + 0.65 * sunny);
    float spec = gloss * cel_spec(n, v, outdoor);
    float3 c = base * light + (rim * 0.45 + spec * 0.9) * float3(1.0, 1.0, 1.0) + base * u_params.z;
    return float4(c, 1.0); // no distance fog: from a fixed top-down camera it only darkens a ring round the eye
}
