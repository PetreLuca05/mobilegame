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
// direction too, so rooms read as boxes from the fixed camera.
float3 cel_light_day(float3 n, float outdoor)
{
    float lit = cel_lit(dot(n, u_sun_dir.xyz));
    float3 sun = u_ambient.rgb + u_sun_color.rgb * lit;
    float3 inside = float3(0.62, 0.62, 0.65) + float3(0.20, 0.19, 0.17) * lit;
    return lerp(inside, sun, outdoor);
}
