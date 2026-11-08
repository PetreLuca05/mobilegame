#include "common.hlsli"
#include "cel.hlsli"

// MG_RENDER=minimal floors: flat toon colour lit like toon_flat, plus an
// optional procedural tile pattern on world XZ (hospital vinyl tiles):
// thin darker grout lines and a faint checker between neighbouring tiles.
// u_params: x tile size in metres (0 = plain), y grout darkening, z checker amount
float4 PSMain(VSOut i) : SV_Target0
{
    dither_clip(i.pos);
    float3 base = i.col.rgb * u_tint.rgb;
    float tile = u_params.x;
    if (tile > 0.0)
    {
        float2 p = i.wpos.xz / tile;
        float2 f = abs(frac(p) - 0.5);          // 0 at a tile centre, 0.5 on its edge
        float2 w = max(fwidth(p), 1e-4);
        const float grout = 0.012;              // half-width, in tiles
        float2 g = smoothstep(0.5 - grout - w, 0.5 - grout, f);
        float edge = max(g.x, g.y);
        float2 cell = floor(p);
        float checker = fmod(abs(cell.x + cell.y), 2.0);
        base *= (1.0 - u_params.y * edge) * (1.0 - u_params.z * checker);
    }
    float3 n = normalize(i.nrm);
    float outdoor = saturate(i.col.a * u_tint.a);
    float3 c = base * cel_light_day(n, outdoor);
    return float4(c, 1.0);
}
