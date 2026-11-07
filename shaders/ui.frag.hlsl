Texture2D u_atlas : register(t0, space2);
SamplerState u_sampler : register(s0, space2);

struct UiOut
{
    float4 pos : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 col : TEXCOORD1;
};

// Single-channel SDF; 0.5 is the glyph edge. Premultiplied alpha out.
float4 PSMain(UiOut i) : SV_Target0
{
    float d = u_atlas.Sample(u_sampler, i.uv).r;
    float w = max(fwidth(d), 1e-3) * 0.7;
    float a = smoothstep(0.5 - w, 0.5 + w, d) * i.col.a;
    return float4(i.col.rgb * a, a);
}
