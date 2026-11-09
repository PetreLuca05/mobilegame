#include "common.hlsli"
#include "inst.hlsli"

// X-ray silhouette, instanced: see xray.frag.
float4 PSMain(InstOut i) : SV_Target0
{
    dither_clip_hide(i.pos, i.params.w);
    return float4(i.tint.rgb, 1.0);
}
