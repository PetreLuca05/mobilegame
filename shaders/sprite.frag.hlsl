#include "common.hlsli"

// Procedural alpha sprites, premultiplied alpha output.
// Quad UV (-1..1) arrives in nrm.xy (normal matrix is identity for sprites).
// u_params.x: 0 soft disc (contact shadow), 1 cloud, 2 additive glow
float blob(float2 uv, float2 c, float r)
{
    return 1.0 - smoothstep(r * 0.25, r, length(uv - c));
}

float4 PSMain(VSOut i) : SV_Target0
{
    float2 uv = i.nrm.xy;
    float r = length(uv);
    float a;
    float3 c = u_tint.rgb;
    if (u_params.x < 0.5) {
        a = 1.0 - smoothstep(0.35, 1.0, r);
    } else if (u_params.x < 1.5) {
        float b = blob(uv, float2(-0.45, -0.15), 0.55) + blob(uv, float2(0.0, 0.05), 0.7)
                + blob(uv, float2(0.45, -0.1), 0.55) + blob(uv, float2(0.15, 0.3), 0.45)
                + blob(uv, float2(-0.25, 0.25), 0.4);
        a = saturate(b) * (1.0 - smoothstep(0.0, 0.35, -uv.y - 0.25));
        c *= lerp(0.86, 1.0, saturate(uv.y * 0.6 + 0.6));
    } else {
        a = exp(-r * r * 5.0) + 0.35 * exp(-r * r * 1.2);
        return float4(c * a * u_tint.a, 0.0); // additive
    }
    a *= u_tint.a;
    return float4(c * a, a);
}
