#include "common.hlsli"
#include "inst.hlsli"

// Instanced GFX_MAT_ALPHA (alpha.frag.hlsl): vertex colour x tint, opacity tint.a, premultiplied; params.x > 0.5 additive
// (LIB-1 §10: the table lamps' light pools and every phone_glow are one draw each).
float4 PSMain(InstOut i) : SV_Target0
{
    float a = saturate((float)i.tint.a);
    float3 c = (float3)(i.col.rgb * i.tint.rgb);
    return float4(c * a, i.params.x > 0.5 ? 0.0 : a);
}
