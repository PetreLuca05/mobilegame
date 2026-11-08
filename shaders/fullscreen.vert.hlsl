// Fullscreen triangle from the vertex id (no vertex buffer). uv 0..1, y down.
struct FsOut
{
    float4 pos : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

FsOut VSMain(uint id : SV_VertexID)
{
    FsOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = uv;
    return o;
}
