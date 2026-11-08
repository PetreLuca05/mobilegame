// Point and spot lights for lit materials (needs common.hlsli + cel.hlsli).
// The frame's lights live in LightsFS; each draw names up to 8 of them.
// Contributions are summed, then quantised into toon steps: one bright lamp
// gives one hard step, not a gradient.
#define MAX_LIGHTS 64

cbuffer LightsFS : register(b2, space3)
{
    float4 u_light_count;
    float4 u_light_pos_range[MAX_LIGHTS];  // xyz position, w range
    float4 u_light_color_type[MAX_LIGHTS]; // rgb * intensity, w 0 point / 1 spot
    float4 u_light_dir_cone[MAX_LIGHTS];   // xyz spot direction, w cos outer
    float4 u_light_extra[MAX_LIGHTS];      // x cos inner, y shadow slot
};

// windowed inverse square: 1 at the light, 0 at the range
float light_falloff(float d, float range)
{
    float q = d / range;
    float w = saturate(1.0 - q * q * q * q);
    return w * w / (d * d + 1.0);
}

float3 local_lights(float3 wpos, float3 n)
{
    float3 sum = 0.0;
    [unroll] for (int k = 0; k < 8; k++)
    {
        int i = (int)u_light_index[k / 4][k % 4];
        if (i < 0)
            continue;
        float3 to = u_light_pos_range[i].xyz - wpos;
        float d = length(to);
        float3 l = to / max(d, 1e-4);
        float w = light_falloff(d, u_light_pos_range[i].w) * saturate(dot(n, l) * 1.2 + 0.1);
        if (u_light_color_type[i].w > 0.5)
            w *= smoothstep(u_light_dir_cone[i].w, u_light_extra[i].x, dot(-l, u_light_dir_cone[i].xyz));
        sum += u_light_color_type[i].rgb * w;
    }
    // toon steps on the total: hue kept, brightness quantised
    float lum = max(sum.r, max(sum.g, sum.b));
    float q = 0.22 * cel_step(lum, 0.06) + 0.22 * cel_step(lum, 0.4) + 0.3 * cel_step(lum, 1.3);
    return sum / max(lum, 1e-4) * q;
}
