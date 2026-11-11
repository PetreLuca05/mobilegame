// Cel shading helpers (fragment stage only: uses fwidth).

// Hard step at `edge`, anti-aliased over one pixel.
float cel_step(float x, float edge)
{
    float w = max(fwidth(x), 1e-4);
    return smoothstep(edge - w, edge + w, x);
}

// Sun term quantised to three flat bands: shade, half-lit, lit.
float cel_lit(float ndl)
{
    return 0.55 * cel_step(ndl, 0.0) + 0.45 * cel_step(ndl, 0.45);
}

// Distance fog towards the day cycle's fog colour.
float3 apply_fog(float3 c, float3 wpos)
{
    float d = length(wpos - u_eye.xyz);
    return lerp(c, u_fog.rgb, 1.0 - exp(-d * u_fog.a));
}

// Banded light for a surface. `outdoor` 1 = sun + sky; 0 = indoors: no sun,
// a bit less sky and a warm overhead fill instead (the per-vertex indoor
// factor baked into building meshes is vertex alpha x tint alpha).
float3 cel_light(float3 n, float outdoor, float shadow)
{
    // a shadow drops the sun term exactly one toon step
    float ndl = dot(n, u_sun_dir.xyz);
    float lit = lerp(0.55 * cel_step(ndl, 0.45), cel_lit(ndl), cel_step(shadow, 0.5));
    float3 sun = u_ambient.rgb + u_sun_color.rgb * lit;
    float3 inside = float3(0.62, 0.62, 0.65) + float3(0.20, 0.19, 0.17) * cel_lit(dot(n, normalize(float3(0.25, 1.0, 0.35))));
    return lerp(inside, sun, outdoor);
}

// Always-day version (minimal renderer): indoor faces step on the sun
// direction too, so rooms read as boxes from the fixed camera. One light set:
float3 cel_light_set(float3 n, float outdoor, float3 sun_dir, float3 sun_color, float3 ambient, float two_band)
{
    float ndl = dot(n, sun_dir);
    // two_band 0..1: towards the library prototype's ramp (three.js MeshToonMaterial, gradient 105 / 255): two bands
    float lit = lerp(cel_lit(ndl), 0.41 + 0.59 * cel_step(ndl, 0.0), saturate(two_band));
    float3 sun = ambient + sun_color * lit;
    float3 inside = float3(0.62, 0.62, 0.65) + float3(0.20, 0.19, 0.17) * lit;
    return lerp(inside, sun, outdoor);
}

// Both light sets, mixed by u_sun2_dir.w (0 most of the time: the second set is not evaluated)
float3 cel_light_day(float3 n, float outdoor)
{
    float3 a = cel_light_set(n, outdoor, u_sun_dir.xyz, u_sun_color.rgb, u_ambient.rgb, u_ambient.w);
    if (u_sun2_dir.w <= 0.0)
        return a;
    float3 b = cel_light_set(n, outdoor, u_sun2_dir.xyz, u_sun2_color.rgb, u_ambient2.rgb, u_ambient2.w);
    return lerp(a, b, saturate(u_sun2_dir.w));
}

// The sunny factor (rim strength) and the toon highlight, mixed over the two sets the same way
float cel_sunny_dir(float3 n, float outdoor, float3 sun_dir)
{
    return cel_step(dot(n, sun_dir), 0.0) * outdoor + (1.0 - outdoor) * cel_step(n.y, -0.2);
}

float cel_sunny(float3 n, float outdoor)
{
    float a = cel_sunny_dir(n, outdoor, u_sun_dir.xyz);
    return u_sun2_dir.w <= 0.0 ? a : lerp(a, cel_sunny_dir(n, outdoor, u_sun2_dir.xyz), saturate(u_sun2_dir.w));
}

float cel_spec_dir(float3 n, float3 v, float3 sun_dir, float sunny_k)
{
    float ndh = dot(n, normalize(sun_dir + v));
    return sunny_k * (0.5 * cel_step(ndh, 0.95) + 0.5 * cel_step(ndh, 0.99));
}

float cel_spec(float3 n, float3 v, float outdoor)
{
    float a = cel_spec_dir(n, v, u_sun_dir.xyz, cel_sunny_dir(n, outdoor, u_sun_dir.xyz));
    if (u_sun2_dir.w <= 0.0)
        return a;
    float b = cel_spec_dir(n, v, u_sun2_dir.xyz, cel_sunny_dir(n, outdoor, u_sun2_dir.xyz));
    return lerp(a, b, saturate(u_sun2_dir.w));
}
