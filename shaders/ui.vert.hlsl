cbuffer UiVS : register(b0, space1)
{
    float4 u_screen; // x = 2 / width_pt, y = 2 / height_pt
};

struct UiIn
{
    [[vk::location(0)]] float2 pos : TEXCOORD0;
    [[vk::location(1)]] float2 uv : TEXCOORD1;
    [[vk::location(2)]] float4 col : TEXCOORD2;
};

struct UiOut
{
    float4 pos : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 col : TEXCOORD1;
};

UiOut VSMain(UiIn i)
{
    UiOut o;
    o.pos = float4(i.pos.x * u_screen.x - 1.0, 1.0 - i.pos.y * u_screen.y, 0.0, 1.0);
    o.uv = i.uv;
    o.col = i.col;
    return o;
}
