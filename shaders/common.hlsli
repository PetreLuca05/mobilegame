// Shared declarations. SDL3 GPU binding convention:
//   vertex   uniforms: register(bN, space1)   textures/samplers: space0
//   fragment uniforms: register(bN, space3)   textures/samplers: space2

cbuffer DrawVS : register(b0, space1)
{
    float4x4 u_model;
    float4x4 u_normal; // inverse-transpose of model (upper 3x3 used)
    float4 u_extra;    // zone-coded meshes, see zone_apply
};

cbuffer FrameVS : register(b1, space1)
{
    float4x4 u_view_proj;
};

cbuffer DrawFS : register(b0, space3)
{
    float4 u_tint;
    float4 u_params;
    float4 u_light_index[2]; // 8 indices into LightsFS, -1 = none (lit materials)
    float4 u_draw_misc;      // x hide: screen-door fade 0 drawn .. 1 gone
};

// M5 floor fade: ordered 4x4 Bayer screen-door, one render-target pixel per
// cell; no blending, so no sorting and the depth pre-pass stays valid.
void dither_clip(float4 svpos)
{
    if (u_draw_misc.x <= 0.0)
        return;
    const float bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    uint2 p = uint2(svpos.xy) & 3;
    clip((bayer[p.y * 4 + p.x] + 0.5) / 16.0 - u_draw_misc.x);
}

cbuffer FrameFS : register(b1, space3)
{
    float4 u_sun_dir;    // xyz towards sun
    float4 u_sun_color;
    float4 u_ambient;
    float4 u_eye;        // camera position
    float4 u_misc;       // x time, y reflection strength, z 1/target width, w 1/target height
    float4 u_fog;        // rgb colour, a density (per metre)
};

// Zone-coded meshes (LIB-1 avatar suits). extra = outfit, trim, accent as 0xRRGGBB, w = zone mask (bit z) |
// 256 body type 1; all 0 = an ordinary mesh. Vertex alpha = 255 - zone. Zones outside the mask collapse to a
// point (zero-area triangles); each zone takes its colour here and the caller's tint is white.
float3 zone_rgb(float v)
{
    uint c = (uint)v;
    return float3((c >> 16) & 255u, (c >> 8) & 255u, c & 255u) / 255.0;
}

void zone_apply(inout float3 pos, inout float4 col, float4 extra)
{
    uint mask = (uint)extra.w;
    if (mask == 0u)
        return;
    uint zone = 255u - (uint)(col.a * 255.0 + 0.5);
    if ((mask & 256u) != 0u) // body type 1: widest ring (y 0.16) x 0.92 .. top ring (y 0.56) x 1.06
        pos.xz *= lerp(0.92, 1.06, saturate((pos.y - 0.16) / 0.40));
    if (zone != 0u && ((mask >> zone) & 1u) == 0u)
        pos = float3(0.0, 0.0, 0.0);
    // 1 trim (V-neck), 4 cross and 6 badge accent, 3 buttons and 7 stethoscope their own colour, the rest the outfit
    float3 src = zone == 1u ? zone_rgb(extra.y)
               : (zone == 4u || zone == 6u) ? zone_rgb(extra.z)
               : (zone == 3u || zone == 7u) ? float3(1.0, 1.0, 1.0)
               : zone_rgb(extra.x);
    col = float4(col.rgb * src, 1.0);
}

struct VSIn
{
    [[vk::location(0)]] float3 pos : TEXCOORD0;
    [[vk::location(1)]] float3 nrm : TEXCOORD1;
    [[vk::location(2)]] float4 col : TEXCOORD2;
};

struct VSOut
{
    float4 pos : SV_Position;
    [[vk::location(0)]] float3 wpos : TEXCOORD0;
    [[vk::location(1)]] float3 nrm : TEXCOORD1;
    [[vk::location(2)]] float4 col : TEXCOORD2;
};
