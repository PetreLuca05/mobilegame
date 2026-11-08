#include "daycycle.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL_time.h>

#include "cJSON.h"
#include "core/assets.h"
#include "core/log.h"

static HMM_Vec3 hex3(const char *s)
{
    if (!s || s[0] != '#' || strlen(s) < 7) {
        return HMM_V3(0, 0, 0);
    }
    unsigned long v = strtoul(s + 1, NULL, 16);
    return HMM_V3((float)((v >> 16) & 255) / 255.0f, (float)((v >> 8) & 255) / 255.0f, (float)(v & 255) / 255.0f);
}

static float num(const cJSON *o, const char *k, float def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? (float)v->valuedouble : def;
}

static const char *str(const cJSON *o, const char *k)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static bool load(DayCycle *d)
{
    const MgAsset *a = mg_asset_find("assets/daycycle.json");
    cJSON *j = a ? cJSON_ParseWithLength((const char *)a->data, a->size) : NULL;
    const cJSON *keys = j ? cJSON_GetObjectItemCaseSensitive(j, "keys") : NULL;
    if (!cJSON_IsArray(keys) || cJSON_GetArraySize(keys) < 2) {
        cJSON_Delete(j);
        return false;
    }
    d->sunrise = num(j, "sunrise", 6.5f);
    d->sunset = num(j, "sunset", 19.5f);
    d->sun_max_elev = num(j, "sun_max_elevation", 70.0f);
    d->sun_scale = num(j, "sun_scale", 0.5f);
    d->ambient_scale = num(j, "ambient_scale", 0.72f);
    d->moon = HMM_MulV3F(hex3(str(j, "moon_color")), num(j, "moon_intensity", 0.15f));
    d->ambient_floor = hex3(str(j, "ambient_floor"));
    d->key_count = 0;
    const cJSON *k;
    cJSON_ArrayForEach(k, keys)
    {
        if (d->key_count >= DAY_MAX_KEYS) {
            break;
        }
        DayKey *dk = &d->keys[d->key_count++];
        dk->t = num(k, "t", 0.0f);
        dk->sun = HMM_MulV3F(hex3(str(k, "sun")), num(k, "sun_i", 0.0f));
        dk->ambient = HMM_MulV3F(hex3(str(k, "ambient")), num(k, "ambient_i", 0.5f));
        dk->horizon = hex3(str(k, "horizon"));
        dk->zenith = hex3(str(k, "zenith"));
        dk->fog = hex3(str(k, "fog"));
        dk->cloud = hex3(str(k, "cloud"));
        dk->fog_density = num(k, "fog_density", 0.0f);
        dk->lamps = num(k, "lamps", 0.0f);
    }
    cJSON_Delete(j);
    return true;
}

void daycycle_init(DayCycle *d)
{
    memset(d, 0, sizeof(*d));
    if (!load(d)) { /* noon only, so the game still looks right */
        mg_log_warn("daycycle: assets/daycycle.json missing or invalid, using a fixed noon");
        d->sunrise = 6.5f;
        d->sunset = 19.5f;
        d->sun_max_elev = 70.0f;
        d->sun_scale = 0.5f;
        d->ambient_scale = 0.72f;
        d->key_count = 1;
        d->keys[0] = (DayKey){12.0f, {{1, 1, 1}}, {{0.5f, 0.57f, 0.68f}}, {{0.75f, 0.89f, 1}}, {{0.25f, 0.56f, 0.91f}},
                              {{0.75f, 0.89f, 1}}, {{1, 1, 1}}, 0.0015f, 0.0f};
    }
    d->follow_device = true;
    d->speed = 1.0f;
    d->t = daycycle_device_hours();
}

double daycycle_device_hours(void)
{
    SDL_Time now;
    SDL_DateTime dt;
    if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &dt, true)) {
        return 12.0;
    }
    return (double)dt.hour + (double)dt.minute / 60.0 + ((double)dt.second + dt.nanosecond * 1e-9) / 3600.0;
}

void daycycle_update(DayCycle *d, double now, float dt)
{
    if (d->follow_device && d->speed <= 1.0f) {
        if (now >= d->next_clock_read) { /* once a second is plenty */
            d->t = daycycle_device_hours();
            d->next_clock_read = now + 1.0;
        }
        return;
    }
    d->t = fmod(d->t + (double)dt * (double)d->speed / 3600.0, 24.0);
}

/* key pair around `t` (wrapping) and the blend between them */
static void bracket(const DayCycle *d, float t, const DayKey **a, const DayKey **b, float *k)
{
    int n = d->key_count;
    *a = &d->keys[n - 1];
    *b = &d->keys[0];
    for (int i = 0; i < n; i++) {
        if (d->keys[i].t > t) {
            *b = &d->keys[i];
            *a = &d->keys[i == 0 ? n - 1 : i - 1];
            break;
        }
        *a = &d->keys[i];
        *b = &d->keys[(i + 1) % n];
    }
    float span = (*b)->t - (*a)->t;
    float into = t - (*a)->t;
    if (span <= 0.0f) {
        span += 24.0f;
    }
    if (into < 0.0f) {
        into += 24.0f;
    }
    *k = span > 0.0f ? mg_saturate(into / span) : 0.0f;
}

/* towards a body at elevation e (deg), azimuth phi (deg from south, east < 0) */
static HMM_Vec3 sky_dir(float e, float phi)
{
    float er = HMM_AngleDeg(e), pr = HMM_AngleDeg(phi);
    return HMM_V3(HMM_CosF(er) * -HMM_SinF(pr), HMM_SinF(er), HMM_CosF(er) * HMM_CosF(pr));
}

static float smooth(float e0, float e1, float x)
{
    float t = mg_saturate((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

DayState daycycle_state(const DayCycle *d, float t)
{
    const DayKey *a, *b;
    float k;
    bracket(d, t, &a, &b, &k);
    DayState s;
    HMM_Vec3 sun = HMM_LerpV3(a->sun, k, b->sun);
    HMM_Vec3 amb = HMM_MulV3F(HMM_LerpV3(a->ambient, k, b->ambient), d->ambient_scale);
    for (int i = 0; i < 3; i++) {
        amb.Elements[i] = mg_maxf(amb.Elements[i], d->ambient_floor.Elements[i]);
    }
    s.ambient = amb;
    s.horizon = HMM_V4V(HMM_LerpV3(a->horizon, k, b->horizon), 1.0f);
    s.zenith = HMM_V4V(HMM_LerpV3(a->zenith, k, b->zenith), 1.0f);
    s.fog = HMM_LerpV3(a->fog, k, b->fog);
    s.fog_density = mg_lerpf(a->fog_density, b->fog_density, k);
    s.cloud = HMM_LerpV3(a->cloud, k, b->cloud);
    s.lamp_on = mg_lerpf(a->lamps, b->lamps, k);

    /* sun: fixed path; moon: same east-south-west arc through the night */
    float day_len = d->sunset - d->sunrise;
    float u = (t - d->sunrise) / day_len;
    float elev = HMM_SinF(HMM_PI32 * u) * d->sun_max_elev;
    if (u < 0.0f || u > 1.0f) { /* below the horizon: mirror the path down */
        elev = -fabsf(HMM_SinF(HMM_PI32 * u)) * 30.0f;
    }
    float phi = -90.0f + 180.0f * u;
    HMM_Vec3 sun_dir = sky_dir(elev, phi);
    float night_len = 24.0f - day_len;
    float v = fmodf(t - d->sunset + 24.0f, 24.0f) / night_len;
    float moon_elev = v <= 1.0f ? HMM_SinF(HMM_PI32 * v) * 55.0f : -10.0f;
    HMM_Vec3 moon_dir = sky_dir(moon_elev, -90.0f + 180.0f * mg_saturate(v));

    float day = smooth(-4.0f, 4.0f, elev);
    s.key_dir = day > 0.5f ? sky_dir(mg_maxf(elev, 3.0f), phi) : sky_dir(mg_maxf(moon_elev, 8.0f), -90.0f + 180.0f * mg_saturate(v));
    s.key_color = HMM_AddV3(HMM_MulV3F(sun, d->sun_scale * day), HMM_MulV3F(d->moon, (1.0f - day)));
    s.sun_disc = HMM_V4V(sun_dir, smooth(-3.0f, 2.0f, elev));
    s.moon_disc = HMM_V4V(moon_dir, smooth(-3.0f, 3.0f, moon_elev) * (1.0f - day));
    s.stars = 1.0f - smooth(-8.0f, 10.0f, elev);
    s.sun_elev_deg = elev;
    return s;
}
