#include "common.hlsli"
#include "inst.hlsli"
#include "pattern.hlsli"

// instanced GFX_MAT_PATTERN (the institute's building, site, lift cars and doors)
PatOut VSMain(InstIn i)
{
    PatOut o;
    float3 w = inst_world(i);
    o.wpos = w;
    o.nrm = inst_normal(i);
    o.col = i.col;
    o.lpos = i.pos;
    o.tint = (min16float4)i.tint;
    o.params = float4(i.params.x, 0.0, 0.0, i.params.w);
    o.pos = mul(u_view_proj, float4(w, 1.0));
    return o;
}
