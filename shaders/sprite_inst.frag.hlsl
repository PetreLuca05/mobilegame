#include "common.hlsli"
#include "inst.hlsli"

// See sprite.frag. Clouds and glows stay on the per-draw path; instances are
// flat soft discs (contact shadows). UV in nrm.xy, params.x unused.
float4 PSMain(InstOut i) : SV_Target0
{
    float2 uv = i.nrm.xy;
    min16float a = (min16float)(1.0 - smoothstep(0.35, 1.0, length(uv))) * i.tint.a;
    return min16float4(i.tint.rgb * a, a);
}
