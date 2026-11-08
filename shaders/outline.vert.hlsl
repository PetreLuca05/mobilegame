#include "common.hlsli"

// Toon outline, inverted hull: the mesh pushed out along its normals and
// drawn with front faces culled, so only a dark rim shows at the silhouette.
// Width grows with view depth so the line stays roughly constant on screen.
static const float k_width_per_m = 0.005;

VSOut VSMain(VSIn i)
{
    VSOut o;
    float4 w = mul(u_model, float4(i.pos, 1.0));
    float3 n = normalize(mul((float3x3)u_normal, i.nrm));
    float depth = mul(u_view_proj, w).w;
    w.xyz += n * clamp(depth * k_width_per_m, 0.009, 0.04);
    o.wpos = w.xyz;
    o.nrm = n;
    o.col = i.col;
    o.pos = mul(u_view_proj, w);
    return o;
}
