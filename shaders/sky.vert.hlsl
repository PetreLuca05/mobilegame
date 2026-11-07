struct SkyOut
{
    float4 pos : SV_Position;
    [[vk::location(0)]] float2 ndc : TEXCOORD0;
};

// Fullscreen triangle, no vertex buffer.
SkyOut VSMain(uint id : SV_VertexID)
{
    float2 p = float2((float)((id << 1) & 2), (float)(id & 2));
    SkyOut o;
    o.ndc = p * 2.0 - 1.0;
    o.pos = float4(o.ndc, 0.0, 1.0);
    return o;
}
