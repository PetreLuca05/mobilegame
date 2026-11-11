// The institute reference page's glass (its THREE.ShaderMaterial glassMat), ported line for line: a toon window you
// can see through - the sky reflection in three steps, two hard glint bands that slide as the eye moves, a hard
// fresnel rim, the sun in two steps. Its sun is the page's (-45, 85, 55), not the scene's. Needs FrameFS (u_eye, u_misc).
static const float3 k_glass_deep = float3(0x3a, 0x86, 0xc0) / 255.0;
static const float3 k_glass_light = float3(0xa6, 0xe0, 0xf8) / 255.0;
static const float3 k_glass_sun = float3(-45.0, 85.0, 55.0) / 110.7926;

// colour (straight, not premultiplied) and alpha
float4 glass_color(float3 wpos, float3 nrm)
{
    float3 n = normalize(nrm), v = normalize(u_eye.xyz - wpos);
    if (dot(n, v) < 0.0)
        n = -n;
    float3 r = reflect(-v, n);
    float sky = floor(saturate(r.y * 0.5 + 0.5) * 3.0 + 0.5) / 3.0;
    float3 col = lerp(k_glass_deep, k_glass_light, sky);
    float d = (wpos.x + wpos.z) * 0.28 + wpos.y * 0.42 + dot(v, float3(0.7, 0.0, 0.7)) * 1.4 + u_misc.x * 0.015;
    float f = frac(d);
    float g1 = step(0.78, f) - step(0.92, f), g2 = step(0.95, f) - step(0.985, f);
    col = lerp(col, float3(1.0, 1.0, 1.0), 0.5 * g1 + 0.8 * g2);
    float rim = step(0.74, 1.0 - dot(n, v));
    col *= lerp(0.84, 1.0, step(0.15, dot(n, k_glass_sun)));
    col = lerp(col, float3(0.92, 0.98, 1.0), rim * 0.45);
    return float4(col, saturate(0.3 + 0.45 * g1 + 0.6 * g2 + 0.3 * rim)); // see-through tint; glints and rim stay solid
}
