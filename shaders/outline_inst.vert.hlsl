#include "common.hlsli"
#include "inst.hlsli"

// Inverted hull, see outline.vert: width grows with view depth.
static const float k_width_per_m = 0.005;

InstOut VSMain(InstIn i)
{
    InstOut o;
    zone_apply(i.pos, i.col, i.extra);
    float3 w = inst_world(i);
    float3 n = normalize(inst_normal(i));
    float depth = mul(u_view_proj, float4(w, 1.0)).w;
    w += n * clamp(depth * k_width_per_m, 0.009, 0.04);
    o.wpos = w;
    o.nrm = n;
    o.col = (min16float4)i.col;
    o.tint = (min16float4)i.tint;
    o.params = (min16float4)i.params;
    o.pos = mul(u_view_proj, float4(w, 1.0));
    return o;
}
