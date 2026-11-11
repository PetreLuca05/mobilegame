#include "common.hlsli"
#include "pattern.hlsli"

// per-draw GFX_MAT_PATTERN (insets, --no-instancing, MG_RENDER=full): tint and hide come from DrawFS
PatOut VSMain(VSIn i)
{
    PatOut o;
    float4 w = mul(u_model, float4(i.pos, 1.0));
    o.wpos = w.xyz;
    o.nrm = mul((float3x3)u_normal, i.nrm);
    o.col = i.col;
    o.lpos = i.pos;
    o.tint = min16float4(1.0, 1.0, 1.0, 1.0);
    o.params = float4(0.0, 0.0, 0.0, 0.0);
    o.pos = mul(u_view_proj, w);
    return o;
}
