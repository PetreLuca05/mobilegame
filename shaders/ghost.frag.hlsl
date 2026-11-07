#include "common.hlsli"

// Translucent x-ray overlay (debug colliders): silhouettes stronger than faces.
float4 PSMain(VSOut i) : SV_Target0
{
    float3 n = normalize(i.nrm);
    float3 v = normalize(u_eye.xyz - i.wpos);
    float edge = pow(1.0 - abs(dot(n, v)), 2.0);
    float a = u_tint.a * (0.3 + 0.7 * edge);
    return float4(u_tint.rgb * a, a);
}
