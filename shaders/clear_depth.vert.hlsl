// Fullscreen triangle at the far plane: resets one shadow-atlas tile to depth 1
// (a render pass can only clear the whole texture).
float4 VSMain(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 1.0, 1.0);
}
