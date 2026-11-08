// Quad for one blurred UI panel, corners from a uniform (NDC), no vertex buffer.
cbuffer PanelVS : register(b0, space1)
{
    float4 u_rect; // x0, y0, x1, y1 in NDC
};

float4 VSMain(uint id : SV_VertexID) : SV_Position
{
    // two triangles: 0 1 2, 2 1 3 over corners (x0,y0) (x1,y0) (x0,y1) (x1,y1)
    uint c = id < 3 ? id : (id == 3 ? 2 : (id == 4 ? 1 : 3));
    float x = (c & 1) ? u_rect.z : u_rect.x;
    float y = (c & 2) ? u_rect.w : u_rect.y;
    return float4(x, y, 0.0, 1.0);
}
