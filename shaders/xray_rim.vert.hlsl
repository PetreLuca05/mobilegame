#include "common.hlsli"

// X-ray rim (M5-X): the avatar mesh pushed out ~2 px along its normals (the
// fixed camera: 32 deg FOV, so a pixel is ~0.00045 m per metre of depth),
// drawn in black behind geometry before the flat colour pass covers its
// inside - what is left is a 2 px dark rim round the silhouette.
static const float k_two_px_per_m = 0.0009;

VSOut VSMain(VSIn i)
{
    VSOut o;
    float3 pos = i.pos;
    float4 col = i.col;
    zone_apply(pos, col, u_extra);
    float4 w = mul(u_model, float4(pos, 1.0));
    float3 n = normalize(mul((float3x3)u_normal, i.nrm));
    float depth = mul(u_view_proj, w).w;
    w.xyz += n * depth * k_two_px_per_m;
    o.wpos = w.xyz;
    o.nrm = n;
    o.col = col;
    o.pos = mul(u_view_proj, w);
    return o;
}
