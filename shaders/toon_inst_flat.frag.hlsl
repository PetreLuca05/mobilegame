// toon_inst for flat-shaded meshes with no rim and no gloss while the mix is 0 (gpu.c toon_variant): the light ramp alone, hard steps
#define CEL_ONE_SET 1
#define CEL_HARD 1
#define TOON_FLAT 1
#include "common.hlsli"
#include "inst.hlsli"
#include "toon_inst.hlsli"

float4 PSMain(InstOut i) : SV_Target0
{
    return toon_shade(i);
}
