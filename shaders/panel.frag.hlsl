// Frosted panel: the blurred frame under a tint, rounded-rect mask with a
// one-pixel anti-aliased edge. Premultiplied alpha out.
Texture2D u_blur : register(t0, space2);
SamplerState u_blur_sampler : register(s0, space2);

cbuffer PanelFS : register(b0, space3)
{
    float4 u_rect;   // x, y, w, h in pt (top-left origin)
    float4 u_params; // x corner radius pt, y px per pt, zw 1 / target size px
    float4 u_tint;   // rgb tint, a = tint amount
};

float4 PSMain(float4 pos : SV_Position) : SV_Target0
{
    float2 pt = pos.xy / u_params.y;
    float2 half_size = u_rect.zw * 0.5;
    float2 q = abs(pt - (u_rect.xy + half_size)) - (half_size - u_params.x);
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - u_params.x;
    float a = saturate(0.5 - d * u_params.y);
    float3 c = u_blur.Sample(u_blur_sampler, pos.xy * u_params.zw).rgb;
    c = lerp(c, u_tint.rgb, u_tint.a);
    return float4(c * a, a);
}
