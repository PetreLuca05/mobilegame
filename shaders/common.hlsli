// Shared declarations. SDL3 GPU binding convention:
//   vertex   uniforms: register(bN, space1)   textures/samplers: space0
//   fragment uniforms: register(bN, space3)   textures/samplers: space2

cbuffer DrawVS : register(b0, space1)
{
    float4x4 u_model;
    float4x4 u_normal; // inverse-transpose of model (upper 3x3 used)
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
};

cbuffer FrameFS : register(b1, space3)
{
    float4 u_sun_dir;    // xyz towards sun
    float4 u_sun_color;
    float4 u_ambient;
    float4 u_eye;        // camera position
    float4 u_misc;       // x time, y reflection strength, z 1/target width, w 1/target height
    float4 u_fog;        // rgb colour, a density (per metre)
};

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
