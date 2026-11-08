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
