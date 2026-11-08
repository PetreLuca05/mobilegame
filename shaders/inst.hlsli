// Instanced draws (GFX_MAT_TOON / OUTLINE / UNLIT / SPRITE on the main pass):
// per-instance model matrix, tint and params instead of per-draw uniforms.
struct InstIn
{
    [[vk::location(0)]] float3 pos : TEXCOORD0;
    [[vk::location(1)]] float3 nrm : TEXCOORD1;
    [[vk::location(2)]] float4 col : TEXCOORD2;
    [[vk::location(3)]] float4 row0 : TEXCOORD3;
    [[vk::location(4)]] float4 row1 : TEXCOORD4;
    [[vk::location(5)]] float4 row2 : TEXCOORD5;
    [[vk::location(6)]] float4 tint : TEXCOORD6;
    [[vk::location(7)]] float4 params : TEXCOORD7; // x rim, y gloss, z emissive (< 0: flat colour), w hide
};

struct InstOut
{
    float4 pos : SV_Position;
    [[vk::location(0)]] float3 wpos : TEXCOORD0;
    [[vk::location(1)]] float3 nrm : TEXCOORD1;
    [[vk::location(2)]] min16float4 col : TEXCOORD2;
    [[vk::location(3)]] nointerpolation min16float4 tint : TEXCOORD3;
    [[vk::location(4)]] nointerpolation min16float4 params : TEXCOORD4;
};

float3 inst_world(InstIn i)
{
    float4 p = float4(i.pos, 1.0);
    return float3(dot(i.row0, p), dot(i.row1, p), dot(i.row2, p));
}

// The cofactor matrix: right for any scale, no inverse needed
float3 inst_normal(InstIn i)
{
    float3 a = i.row0.xyz, b = i.row1.xyz, c = i.row2.xyz;
    // these crosses are the cofactor's rows: dot with the normal (as columns they would undo the rotation)
    return float3(dot(cross(b, c), i.nrm), dot(cross(c, a), i.nrm), dot(cross(a, b), i.nrm));
}

// Dithered pipelines only: the opaque ones never call clip() (Apple TBDR hidden-surface removal stays on)
void dither_clip_hide(float4 svpos, float hide)
{
    const float bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    uint2 p = uint2(svpos.xy) & 3;
    clip((bayer[p.y * 4 + p.x] + 0.5) / 16.0 - hide);
}
