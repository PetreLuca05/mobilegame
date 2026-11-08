// Shadow lookups for lit materials. The including shader declares
// u_csm0/u_csm1/u_atlas (+ *_s samplers, point filtered); depth is compared
// by hand (portable PCF, no comparison samplers needed). Needs common.hlsli
// and lighting.hlsli (LightsFS) first.

static const float2 k_poisson[4] = {float2(-0.94, -0.40), float2(0.95, -0.77), float2(-0.09, -0.93), float2(0.34, 0.92)};

float cascade_shadow(int c, float3 p)
{
    float4 lp = mul(c == 0 ? u_csm_vp[0] : u_csm_vp[1], float4(p, 1.0));
    float3 ndc = lp.xyz / lp.w;
    float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    if (any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0)
        return 1.0;
    float texel = u_csm_params.z * 1.5;
    float lit = 0.0;
    [unroll] for (int k = 0; k < 4; k++)
    {
        float2 t = uv + k_poisson[k] * texel;
        float d = c == 0 ? u_csm0.SampleLevel(u_csm0_s, t, 0).r : u_csm1.SampleLevel(u_csm1_s, t, 0).r;
        lit += ndc.z <= d ? 1.0 : 0.0;
    }
    return lit * 0.25;
}

// 1 = sunlit, 0 = in the sun's (or moon's) shadow
float sun_shadow(float3 wpos, float3 n)
{
    if (u_csm_params.w < 0.5)
        return 1.0;
    float depth = dot(wpos - u_eye.xyz, u_csm_fwd.xyz);
    if (depth >= u_csm_params.y)
        return 1.0;
    bool near = depth < u_csm_params.x;
    float3 p = wpos + n * (near ? 0.02 : 0.06); // normal offset against acne
    if (!near)
        return cascade_shadow(1, p);
    float s = cascade_shadow(0, p);
    float blend = saturate((depth - u_csm_params.x * 0.9) / (u_csm_params.x * 0.1)); // last 10 %
    return blend > 0.0 ? lerp(s, cascade_shadow(1, p), blend) : s;
}

// debug tint: cascade 0 red, 1 green
float3 cascade_tint(float3 wpos)
{
    float depth = dot(wpos - u_eye.xyz, u_csm_fwd.xyz);
    return depth < u_csm_params.x ? float3(1.0, 0.6, 0.6) : (depth < u_csm_params.y ? float3(0.6, 1.0, 0.6) : 1.0);
}
