#include "common.hlsli"

VSOut VSMain(VSIn i)
{
    VSOut o;
    float4 w = mul(u_model, float4(i.pos, 1.0));
    o.wpos = w.xyz;
    o.nrm = mul((float3x3)u_normal, i.nrm);
    o.col = i.col;
    o.pos = mul(u_view_proj, w);
    return o;
}
