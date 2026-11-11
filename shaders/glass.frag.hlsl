#include "common.hlsli"
#include "glass.hlsli"

// GFX_MAT_GLASS: the reference page's see-through glass, blended (premultiplied), no depth write, both faces. A fading
// storey or wall piece (hide) scales its alpha instead of a screen-door. The vertex colour is not used (as on the page).
float4 PSMain(VSOut i) : SV_Target0
{
    float4 c = glass_color(i.wpos, i.nrm);
    float a = c.a * saturate(1.0 - u_draw_misc.x);
    return float4(c.rgb * a, a);
}
