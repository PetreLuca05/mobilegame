#include "common.hlsli"
#include "inst.hlsli"

// UVs arrive in the normal (see GfxVertex): not transformed
InstOut VSMain(InstIn i)
{
    InstOut o;
    float3 w = inst_world(i);
    o.wpos = w;
    o.nrm = i.nrm;
    o.col = (min16float4)i.col;
    o.tint = (min16float4)i.tint;
    o.params = (min16float4)i.params;
    o.pos = mul(u_view_proj, float4(w, 1.0));
    return o;
}
