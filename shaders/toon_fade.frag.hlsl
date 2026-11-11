#include "common.hlsli"
#include "cel.hlsli"

// A whole-object fade (LIB-1: the library's shell on the Maps move): the toon_flat shading, alpha-blended (premultiplied)
// at 1 - u_draw_misc.x over the depth GFX_MAT_DEPTH left, so each pixel blends the object's nearest surface once. No
// screen-door pattern, nothing that crawls while the camera moves.
// u_params: x rim strength, y unlit (1 = flat colour), z emissive boost, w gloss (0 matte .. 1 glossy)
float4 PSMain(VSOut i) : SV_Target0
{
    float a = saturate(1.0 - u_draw_misc.x);
    float3 base = i.col.rgb * u_tint.rgb;
    if (u_params.y > 0.5)
        return float4(base * a, a);

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
    return float4(c * a, a); // no distance fog: from a fixed top-down camera it only darkens a ring round the eye
}
