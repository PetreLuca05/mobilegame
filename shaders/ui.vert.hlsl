// UI-1: one instance per UI primitive (GfxUiInst, gfx.h); six vertices per instance, corners from the vertex index.
cbuffer UiVS : register(b0, space1)
{
    float4 u_screen; // x = 2 / width_pt, y = 2 / height_pt
    float4 u_tilt;   // TILT-1: tx, ty (x strength), strength S
    float4 u_safe;   // TILT-1: the safe rect l, t, r, b (pt): a shift never crosses it
};

struct UiIn
{
    [[vk::location(0)]] float4 rect : TEXCOORD0;   // x, y, w, h (pt)
    [[vk::location(1)]] float4 uv : TEXCOORD1;     // u0, v0, u1, v1
    [[vk::location(2)]] float4 xf : TEXCOORD2;     // 2 x 2 about the centre: a, b, c, d
    [[vk::location(3)]] float4 clip : TEXCOORD3;   // x0, y0, x1, y1 (x1 < x0: none)
    [[vk::location(4)]] float4 shape : TEXCOORD4;  // radius, stroke, highlight band, dashes
    [[vk::location(5)]] float4 col : TEXCOORD5;
    [[vk::location(6)]] float4 col2 : TEXCOORD6;
    [[vk::location(7)]] float2 kind : TEXCOORD7;   // x: 0 glyph, 1 shape, 2 head; y: clip fade width (pt, 0 = hard edge)
    [[vk::location(8)]] float4 tilt : TEXCOORD8;   // depth shift (pt), lip height L (a lip shape), flags (1 gloss, 2 title)
    uint vid : SV_VertexID;
};

struct UiOut
{
    float4 pos : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;     // atlas uv (glyph, head)
    [[vk::location(1)]] float2 local : TEXCOORD1;  // pt from the box centre, before the transform
    [[vk::location(2)]] float2 half_size : TEXCOORD2;
    [[vk::location(3)]] float4 shape : TEXCOORD3;
    [[vk::location(4)]] float4 col : TEXCOORD4;
    [[vk::location(5)]] float4 col2 : TEXCOORD5;
    [[vk::location(6)]] float4 clip : TEXCOORD6;
    [[vk::location(7)]] float4 ptk : TEXCOORD7;    // screen pt (for the clip), kind, clip fade
    [[vk::location(8)]] float4 tiltv : TEXCOORD8;  // tx, ty, S, flags
};

UiOut VSMain(UiIn i)
{
    static const float2 corners[6] = {float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 0), float2(1, 1), float2(0, 1)};
    float2 c = corners[i.vid % 6];
    float2 half_size = i.rect.zw * 0.5;
    float margin = i.kind.x > 0.5 && i.kind.x < 1.5 ? 1.5 : 0.0; // shapes: room for the edge's anti-aliasing
    float2 local = (c - 0.5) * (i.rect.zw + 2.0 * margin);
    float2 centre = i.rect.xy + half_size;
    float2 p = centre + float2(i.xf.x * local.x + i.xf.z * local.y, i.xf.y * local.x + i.xf.w * local.y);
    // TILT-1: nearer layers shift more, against the tilt; a lip swings to the side you would see
    // (-0.7 L tx, -0.4 L ty) from rest (k_ui.tilt_lip_x / _y), 1.5x for the title. All zero at tilt 0.
    float2 shift = -u_tilt.xy * i.tilt.x;
    if (i.tilt.y > 0.0)
    {
        float k = i.tilt.z >= 2.0 ? 1.5 : 1.0;
        shift += float2(-0.7 * i.tilt.y * u_tilt.x, -0.4 * i.tilt.y * u_tilt.y) * k;
    }
    // nothing crosses the safe rect: a rect inside it may move up to its edges, one outside (a full-screen
    // background, a scrim) never moves further out; so edge elements clamp to the room they have
    float2 lo = min(float2(0.0, 0.0), u_safe.xy - i.rect.xy);
    float2 hi = max(float2(0.0, 0.0), u_safe.zw - (i.rect.xy + i.rect.zw));
    shift = clamp(shift, lo, hi);
    p += shift;
    UiOut o;
    o.pos = float4(p.x * u_screen.x - 1.0, 1.0 - p.y * u_screen.y, 0.0, 1.0);
    o.uv = lerp(i.uv.xy, i.uv.zw, c);
    o.local = local;
    o.half_size = half_size;
    o.shape = i.shape;
    o.col = i.col;
    o.col2 = i.col2;
    o.clip = i.clip;
    o.ptk = float4(p, i.kind.x, i.kind.y);
    o.tiltv = float4(u_tilt.xyz, i.tilt.z);
    return o;
}
