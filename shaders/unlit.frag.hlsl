#include "common.hlsli"

float4 PSMain(VSOut i) : SV_Target0
{
    return float4(i.col.rgb * u_tint.rgb, 1.0);
}
