#include "common.hlsli"

// LIB-1 alpha meshes (light pools and cones, glass): vertex colour x tint, opacity tint.a, premultiplied. Unlit, so a change of
// light never steps it; no depth write, drawn back to front with the other blended kinds.
// params.x > 0.5: additive (the phone's screen light): the colour is added at the opacity, nothing behind it is dimmed.
float4 PSMain(VSOut i) : SV_Target0
{
    float a = saturate(u_tint.a);
    float3 c = i.col.rgb * u_tint.rgb;
    return float4(c * a, u_params.x > 0.5 ? 0.0 : a);
}
