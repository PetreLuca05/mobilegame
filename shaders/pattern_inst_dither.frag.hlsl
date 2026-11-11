#include "common.hlsli"
#include "inst.hlsli"
#include "pattern.hlsli"

// fading storeys and wall pieces (0 < hide < 0.98): the floor fade's screen-door
float4 PSMain(PatOut i) : SV_Target0
{
    dither_clip_hide(i.pos, i.params.w);
    return float4(pattern_shade(i, i.tint.rgb, i.params.x), 1.0);
}
