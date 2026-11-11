#include "common.hlsli"
#include "pattern.hlsli"

// opaque: no clip, so the tile GPU's hidden-surface removal stays on
float4 PSMain(PatOut i) : SV_Target0
{
    return float4(pattern_shade(i, i.tint.rgb, i.params.x), 1.0);
}
