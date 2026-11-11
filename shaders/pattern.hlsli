// GFX_MAT_PATTERN: the institute reference page's building and site materials. Vertex alpha = pattern id (bits 0-3:
// 0 none, 1..10 tile wood paving plaster brick asphalt grass gravel water hedge, 11 glass drawn opaque) | 16 lit
// | 32 in the building's shadow (baked at load into the site's ground: the sun's 0.37 share only).
//   unlit (THREE.MeshBasicMaterial, the building): vertex colour x pattern, no light at all
//   lit (THREE.MeshToonMaterial, the site and the roof deck): x (hemisphere + sun through a two-texel ramp)
// UVs as the page's mesh(): the face's dominant axis, floor / ceiling (x, z), x-facing (z, y), z-facing (x, y), over the
// pattern's size in metres, from the mesh's own position (a moving lift car keeps its texture). The canvas' top row is
// v = 1 on the page (flipY), row 0 here: v is negated.
#include "glass.hlsli"

Texture2DArray u_pat : register(t0, space2);
SamplerState u_pat_sampler : register(s0, space2);

struct PatOut
{
    float4 pos : SV_Position;
    [[vk::location(0)]] float3 wpos : TEXCOORD0;
    [[vk::location(1)]] float3 nrm : TEXCOORD1;
    [[vk::location(2)]] float4 col : TEXCOORD2;
    [[vk::location(3)]] float3 lpos : TEXCOORD3;
    [[vk::location(4)]] nointerpolation min16float4 tint : TEXCOORD4;
    [[vk::location(5)]] nointerpolation min16float4 params : TEXCOORD5; // w hide (instanced)
};

// metres per repeat, by id (TEX sizes)
static const float k_pat_size[12] = {1.0, 1.2, 1.6, 2.0, 3.0, 1.2, 4.0, 11.0, 3.0, 5.0, 2.5, 1.0};

// The page's lights: HemisphereLight(0xffffff, 0xb8d6a0, 0.5) + DirectionalLight(0xfff6e0) 0.25 (shadowed) + 0.37
// (not), both from (-45, 85, 55), through the gradient map {165, 255} (nearest): n.l < 0 takes 165 / 255. No shadow
// maps: the building's shadow on the ground is baked into the mesh (bit 32: the 0.25 share is off there).
float3 pattern_light(float3 n, bool shaded)
{
    float3 hemi = lerp(float3(0xb8, 0xd6, 0xa0) / 255.0, float3(1.0, 1.0, 1.0), n.y * 0.5 + 0.5) * 0.5;
    float ramp = dot(n, k_glass_sun) < 0.0 ? 165.0 / 255.0 : 1.0;
    return hemi + ramp * float3(1.0, 0xf6 / 255.0, 0xe0 / 255.0) * (shaded ? 0.37 : 0.25 + 0.37);
}

float3 pattern_shade(PatOut i, float3 tint)
{
    uint code = (uint)(i.col.a * 255.0 + 0.5);
    uint id = code & 15u;
    float3 n = normalize(i.nrm);
    float3 an = abs(n);
    float2 uv = (an.y >= an.x && an.y >= an.z) ? i.lpos.xz : (an.x >= an.z ? i.lpos.zy : i.lpos.xy);
    uv /= k_pat_size[min(id, 11u)];
    uv.y = -uv.y;
    // sampled for every pixel (no derivatives in a branch); an id without a layer reads layer 0 and ignores it
    float p = u_pat.Sample(u_pat_sampler, float3(uv, (float)clamp((int)id - 1, 0, 9))).r;
    float3 c = i.col.rgb * (id >= 1u && id <= 10u ? p : 1.0);
    if (id == 11u)
        c = glass_color(i.wpos, i.nrm).rgb;
    if ((code & 16u) != 0u)
        c *= pattern_light(n, (code & 32u) != 0u);
    return saturate(c * tint);
}
