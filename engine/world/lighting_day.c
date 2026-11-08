#include "lighting_day.h"

/* Sun vector (towards the sun), elevation 55 deg, azimuth south-south-east:
 *   (+0.28, +0.819, +0.50). The fixed camera looks north, so the wall faces
 * it sees point south (+Z): dot 0.50 is above the 0.45 toon step = lit. Faces
 * pointing north (-Z) dot -0.50 = dark; east (+X) 0.28 = middle step; west
 * (-X) -0.28 = dark; floors and tops 0.82 = lit. Rooms read as boxes. */
DayState lighting_day(void)
{
    DayState d = {0};
    d.key_dir = HMM_NormV3(HMM_V3(0.28f, 0.819f, 0.5f));
    /* authored colour x intensity, mapped onto the toon ramp by the same
     * scales the day/night curves used (else sun + ambient adds up to 1.5) */
    const float sun_scale = 0.5f, ambient_scale = 0.72f;
    d.key_color = HMM_MulV3F(HMM_V3(1.0f, 1.0f, 1.0f), 1.0f * sun_scale); /* #FFFFFF x 1.0 */
    d.ambient = HMM_MulV3F(HMM_V3(0x9F / 255.0f, 0xB7 / 255.0f, 0xD9 / 255.0f), 0.8f * ambient_scale); /* #9FB7D9 x 0.8 */
    d.horizon = HMM_V4(0xBF / 255.0f, 0xE3 / 255.0f, 1.0f, 1.0f);             /* #BFE3FF */
    d.zenith = HMM_V4(0x3F / 255.0f, 0x8F / 255.0f, 0xE8 / 255.0f, 1.0f);     /* #3F8FE8 */
    d.cloud = HMM_V3(1.0f, 1.0f, 1.0f);
    d.sun_disc = HMM_V4(d.key_dir.X, d.key_dir.Y, d.key_dir.Z, 1.0f);
    return d;
}
