#include "common.hlsli"
#include "inst.hlsli"

float4 PSMain(InstOut i) : SV_Target0
{
    dither_clip_hide(i.pos, i.params.w);
    return min16float4(i.col.rgb * i.tint.rgb, 1.0);
}
