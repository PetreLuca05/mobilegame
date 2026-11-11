#include "common.hlsli"
#include "pattern.hlsli"

float4 PSMain(PatOut i) : SV_Target0
{
    dither_clip(i.pos);
    return float4(pattern_shade(i, u_tint.rgb), 1.0);
}
