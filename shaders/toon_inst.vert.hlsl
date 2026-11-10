#include "common.hlsli"
#include "inst.hlsli"

InstOut VSMain(InstIn i)
{
    InstOut o;
    zone_apply(i.pos, i.col, i.extra);
    float3 w = inst_world(i);
    o.wpos = w;
    o.nrm = inst_normal(i);
    o.col = (min16float4)i.col;
    o.tint = (min16float4)i.tint;
    o.params = (min16float4)i.params;
    o.pos = mul(u_view_proj, float4(w, 1.0));
    return o;
}
