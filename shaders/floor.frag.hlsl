#include "common.hlsli"
#include "cel.hlsli"

// Glossy grid floor: light blue-grey slab, thick black 1 m lines, faint 10 cm
// lines, planar reflection blended with Fresnel.
// u_params: x major width (m), y minor width (m), z minor alpha, w reflection mix
Texture2D u_reflection : register(t0, space2);
SamplerState u_reflection_sampler : register(s0, space2);

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
    float3 v = normalize(u_eye.xyz - i.wpos);
    float3 c = base * cel_light(n, saturate(i.col.a * u_tint.a));
    if (u_misc.y > 0.0 && n.y > 0.9) {
        float2 uv = i.pos.xy * u_misc.zw;
        float3 refl = u_reflection.Sample(u_reflection_sampler, uv).rgb;
        float fresnel = pow(1.0 - saturate(dot(n, v)), 4.0);
        float k = saturate(u_params.w * (0.6 + 0.9 * fresnel)) * u_misc.y;
        c = lerp(c, refl, k);
    }
    c = lerp(c, c * 0.6, minor);
    c = lerp(c, float3(0.04, 0.045, 0.06), major);
    return float4(c, 1.0);
}
