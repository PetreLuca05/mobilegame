#include "common.hlsli"
#include "inst.hlsli"

// X-ray rim, instanced: see xray_rim.vert.
static const float k_two_px_per_m = 0.0009;

InstOut VSMain(InstIn i)
{
    InstOut o;
    float3 w = inst_world(i);
    float3 n = normalize(inst_normal(i));
    float depth = mul(u_view_proj, float4(w, 1.0)).w;
    w += n * depth * k_two_px_per_m;
    o.wpos = w;
    o.nrm = n;
    o.col = (min16float4)i.col;
    o.tint = (min16float4)i.tint;
    o.params = (min16float4)i.params;
    o.pos = mul(u_view_proj, float4(w, 1.0));
    return o;
}
