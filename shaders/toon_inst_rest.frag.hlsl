// toon_inst without the second light set's code: every toon draw while the mix is 0 (gpu.c toon_variant)
#define CEL_ONE_SET 1
#include "common.hlsli"
#include "inst.hlsli"
#include "toon_inst.hlsli"

float4 PSMain(InstOut i) : SV_Target0
{
    return toon_shade(i);
}
