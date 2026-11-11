// UI-1: glyphs, rounded shapes, head icons and the book figure in one instanced draw (ui.vert.hlsl). Premultiplied alpha out.
Texture2D u_atlas : register(t0, space2);
SamplerState u_sampler : register(s0, space2);
Texture2D u_heads : register(t1, space2);
SamplerState u_heads_sampler : register(s1, space2);
Texture2D u_figure : register(t2, space2); // BOOK-ENGINE: the one resident figure (gfx_set_figure), straight alpha
SamplerState u_figure_sampler : register(s2, space2);

struct UiOut
{
    float4 pos : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float2 local : TEXCOORD1;
    [[vk::location(2)]] float2 half_size : TEXCOORD2;
    [[vk::location(3)]] float4 shape : TEXCOORD3;
    [[vk::location(4)]] float4 col : TEXCOORD4;
    [[vk::location(5)]] float4 col2 : TEXCOORD5;
    [[vk::location(6)]] float4 clip : TEXCOORD6;
    [[vk::location(7)]] float4 ptk : TEXCOORD7; // screen pt, kind, clip fade (pt)
    [[vk::location(8)]] float4 tiltv : TEXCOORD8; // tx, ty (x strength), S, flags (1 gloss, 2 title)
};

// signed distance to a rounded box centred on the origin (negative inside)
float rounded_box(float2 p, float2 half_size, float r)
{
    r = min(r, min(half_size.x, half_size.y));
    float2 q = abs(p) - half_size + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float4 shade(UiOut i);

float4 PSMain(UiOut i) : SV_Target0
{
    float fade = 1.0;
    if (i.clip.z >= i.clip.x)
    {
        float edge = min(min(i.ptk.x - i.clip.x, i.clip.z - i.ptk.x), min(i.ptk.y - i.clip.y, i.clip.w - i.ptk.y));
        if (edge < 0.0)
        {
            discard;
        }
        if (i.ptk.w > 0.0) // a scroll area's ends: dissolve over the last ptk.w pt
        {
            fade = saturate(edge / i.ptk.w);
        }
    }
    return shade(i) * fade; // premultiplied: rgb and alpha together
}

float4 shade(UiOut i)
{
    float kind = i.ptk.z;
    if (kind > 2.5) // book figure: the texture times the colour
    {
        float4 f = u_figure.Sample(u_figure_sampler, i.uv) * i.col;
        return float4(f.rgb * f.a, f.a);
    }
    if (kind > 1.5) // head icon: 8 x 8 cells, a circle with a ring of the colour
    {
        float2 uv = i.uv;
        float r = length(frac(uv * 8.0) - 0.5) * 2.0;
        float e = max(fwidth(r), 1e-3);
        float disc = 1.0 - smoothstep(1.0 - e, 1.0, r);
        float ring = smoothstep(0.9 - e, 0.9, r) * disc;
        float4 head = u_heads.Sample(u_heads_sampler, uv);
        float3 c = lerp(head.rgb, i.col.rgb, ring);
        float a = max(head.a * disc, ring);
        return float4(c * disc * i.col.a, a * i.col.a);
    }
    if (kind < 0.5) // glyph: single-channel SDF, 0.5 is the edge
    {
        float d = u_atlas.Sample(u_sampler, i.uv).r;
        float w = max(fwidth(d), 1e-3) * 0.7;
        float a = smoothstep(0.5 - w, 0.5 + w, d) * i.col.a;
        return float4(i.col.rgb * a, a);
    }
    // shape: rounded box, filled or stroked, an optional dashed ring and top highlight band
    float d = rounded_box(i.local, i.half_size, i.shape.x);
    float aa = max(fwidth(d), 1e-3);
    float border = 0.0; // stroke < 0: filled, with a border of -stroke in colour 2
    if (i.shape.y < 0.0)
    {
        border = saturate(0.5 - (abs(d - i.shape.y * 0.5) + i.shape.y * 0.5) / aa);
    }
    if (i.shape.y > 0.0)
    {
        d = abs(d + i.shape.y * 0.5) - i.shape.y * 0.5;
    }
    float cover = saturate(0.5 - d / aa);
    if (i.shape.w > 0.5) // dashes around a circle: on for the first half of each period
    {
        float ang = atan2(i.local.y, i.local.x) / 6.2831853 + 0.5;
        float t = frac(ang * i.shape.w);
        float wa = max(fwidth(ang * i.shape.w), 1e-3);
        cover *= smoothstep(0.0, wa, t) * (1.0 - smoothstep(0.5 - wa, 0.5, t));
    }
    float3 rgb = i.col.rgb;
    float a = i.col.a;
    if (i.shape.z > 0.0) // highlight: a band under the top edge, inside the shape, in colour 2
    {
        float inner = rounded_box(i.local, i.half_size, i.shape.x);
        float2 hl = i.local - 2.0 * i.tiltv.xy; // TILT-1: up to 2 pt against the lip (k_ui.tilt_highlight_pt)
        float band = saturate((-i.half_size.y + i.shape.z - hl.y) / max(fwidth(i.local.y), 1e-3)) * saturate(-inner / aa);
        float k = band * i.col2.a;
        rgb = lerp(rgb, i.col2.rgb, k);
        if (fmod(i.tiltv.w, 2.0) >= 1.0 && i.tiltv.z > 0.0) // TILT-1 gloss: a soft white spot sliding across the face
        {
            float w = i.half_size.x * 2.0;
            float2 c = float2(0.3 * w * i.tiltv.x, 0.1 * w * i.tiltv.y);
            float e = length((i.local - c) / float2(0.2 * w, 0.6 * i.half_size.y));
            float spot = (1.0 - smoothstep(0.55, 1.0, e)) * 0.18 * saturate(length(i.tiltv.xy) * 4.0) * saturate(-inner / aa);
            rgb = lerp(rgb, float3(1.0, 1.0, 1.0), spot);
        }
    }
    if (border > 0.0) // premultiplied mix of the fill and the border
    {
        float4 fill = float4(rgb * a, a) * (1.0 - border);
        float4 edge = float4(i.col2.rgb * i.col2.a, i.col2.a) * border;
        return (fill + edge) * cover;
    }
    a *= cover;
    return float4(rgb * a, a);
}
