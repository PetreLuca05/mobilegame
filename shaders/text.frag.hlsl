#include "common.hlsli"

// World-space SDF text (signs). The glyph's atlas UV arrives in nrm.xy
// (identity normal matrix, as for sprites). Premultiplied alpha out.
Texture2D u_atlas : register(t0, space2);
SamplerState u_atlas_sampler : register(s0, space2);

float4 PSMain(VSOut i) : SV_Target0
{
    float d = u_atlas.Sample(u_atlas_sampler, i.nrm.xy).r;
    float w = max(fwidth(d), 1e-3) * 0.7;
    float a = smoothstep(0.5 - w, 0.5 + w, d) * i.col.a * u_tint.a;
    return float4(i.col.rgb * u_tint.rgb * a, a);
}
