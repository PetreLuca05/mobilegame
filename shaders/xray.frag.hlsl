#include "common.hlsli"

// X-ray silhouette (M5-X): flat colour, screen-door dithered (the draw's
// hide = 0.4 -> 60 % of the pixels), drawn only where the avatar is behind
// something (depth GREATER, no depth write). Used for the rim (black) and
// the fill (the avatar's accent colour).
float4 PSMain(VSOut i) : SV_Target0
{
    dither_clip(i.pos);
    return float4(u_tint.rgb, 1.0);
}
