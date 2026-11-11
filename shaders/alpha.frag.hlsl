#include "common.hlsli"

// LIB-1 alpha meshes (light pools and cones, glass): vertex colour x tint, opacity tint.a, premultiplied. Unlit, so a change of
// light never steps it; no depth write, drawn back to front with the other blended kinds.
float4 PSMain(VSOut i) : SV_Target0
{
    float a = saturate(u_tint.a);
    float3 c = i.col.rgb * u_tint.rgb;
    return float4(c * a, a);
}
