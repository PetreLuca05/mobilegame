Texture2D u_atlas : register(t0, space2);
SamplerState u_sampler : register(s0, space2);
Texture2D u_heads : register(t1, space2);
SamplerState u_heads_sampler : register(s1, space2);

struct UiOut
{
    float4 pos : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 col : TEXCOORD1;
};

// Single-channel SDF; 0.5 is the glyph edge. Premultiplied alpha out.
// u >= 2: a head icon from the second page (u - 2): 8 x 8 cells, clipped to a
// circle with a ring of the vertex colour. In the same batch as the text.
float4 PSMain(UiOut i) : SV_Target0
{
    if (i.uv.x >= 2.0)
    {
        float2 uv = float2(i.uv.x - 2.0, i.uv.y);
        float r = length(frac(uv * 8.0) - 0.5) * 2.0;
        float e = max(fwidth(r), 1e-3);
        float disc = 1.0 - smoothstep(1.0 - e, 1.0, r);
        float ring = smoothstep(0.9 - e, 0.9, r) * disc;
        float4 head = u_heads.Sample(u_heads_sampler, uv);
        float3 c = lerp(head.rgb, i.col.rgb, ring);
        float a = max(head.a * disc, ring);
        return float4(c * disc * i.col.a, a * i.col.a);
    }
    float d = u_atlas.Sample(u_sampler, i.uv).r;
    float w = max(fwidth(d), 1e-3) * 0.7;
    float a = smoothstep(0.5 - w, 0.5 + w, d) * i.col.a;
    return float4(i.col.rgb * a, a);
}
