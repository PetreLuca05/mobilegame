#include "common.hlsli"

VSOut VSMain(VSIn i)
{
    VSOut o;
    float3 pos = i.pos;
    float4 col = i.col;
    zone_apply(pos, col, u_extra);
    float4 w = mul(u_model, float4(pos, 1.0));
    o.wpos = w.xyz;
    o.nrm = mul((float3x3)u_normal, i.nrm);
    o.col = col;
    o.pos = mul(u_view_proj, w);
    return o;
}
