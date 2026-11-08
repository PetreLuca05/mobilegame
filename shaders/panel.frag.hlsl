// Rounded UI panel with a one-pixel anti-aliased edge, premultiplied alpha:
//   mode 0  frosted: the blurred frame (screen-space) under a tint
//   mode 1  image: a texture stretched over the rect (the avatar preview)
Texture2D u_blur : register(t0, space2);
SamplerState u_blur_sampler : register(s0, space2);

cbuffer PanelFS : register(b0, space3)
{
    float4 u_rect;   // x, y, w, h in pt (top-left origin)
    float4 u_params; // x corner radius pt, y px per pt, zw 1 / target size px
    float4 u_tint;   // rgb tint, a = tint amount
    float4 u_mode;   // x: 0 frosted, 1 image
};

float4 PSMain(float4 pos : SV_Position) : SV_Target0
{
    float2 pt = pos.xy / u_params.y;
    float2 half_size = u_rect.zw * 0.5;
    float2 q = abs(pt - (u_rect.xy + half_size)) - (half_size - u_params.x);
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - u_params.x;
    float a = saturate(0.5 - d * u_params.y);
    float2 uv = u_mode.x > 0.5 ? (pt - u_rect.xy) / u_rect.zw : pos.xy * u_params.zw;
    float3 c = u_blur.Sample(u_blur_sampler, uv).rgb;
    c = lerp(c, u_tint.rgb, u_tint.a);
    return float4(c * a, a);
}
