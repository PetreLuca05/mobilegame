#include "common.hlsli"
#include "inst.hlsli"
#include "toon_inst.hlsli"

float4 PSMain(InstOut i) : SV_Target0
{
    dither_clip_hide(i.pos, i.params.w);
    return toon_shade(i);
}
