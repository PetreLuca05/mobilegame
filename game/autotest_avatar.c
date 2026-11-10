#include "autotest_avatar.h"

#include <math.h>

#include "autotest.h"
#include "avatar.h"
#include "core/log.h"

#define DT (1.0f / 60.0f)

static int failf(AvatarTest *t, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(t->why, sizeof(t->why), fmt, ap);
    va_end(ap);
    return -1;
}

static AvatarPose standing(void)
{
    AvatarPose p;
    SDL_zero(p);
    p.grounded = true;
    p.flat_ground = true;
    p.breath_k = 1e-6f; /* no breathing: the assembly is measured exactly */
    return p;
}

static HMM_Vec3 to_local(const AvatarSolved *s, HMM_Vec3 w)
{
    const HMM_Mat4 inv = HMM_InvGeneralM4(s->root);
    return HMM_MulM4V4(inv, HMM_V4V(w, 1.0f)).XYZ;
}

static int assembly(AvatarTest *t, const AvatarMeshes *m)
{
    const AvatarLook look = avatar_look_from_skin(AVATAR_DEFAULT_SKIN);
    static const float leans[2] = {0.0f, 0.30f};
    for (int i = 0; i < 2; i++) {
        AvatarPose p = standing();
        p.lean = leans[i];
        AvatarAnim a;
        SDL_zero(a);
        avatar_animate(m, &a, &p, DT);
        AvatarSolved s;
        avatar_solve(m, &look, &p, &a, &s);
        const HMM_Vec3 base = s.body.Columns[3].XYZ, up = HMM_NormV3(s.body.Columns[1].XYZ);
        const HMM_Vec3 d = HMM_SubV3(s.head_centre, base);
        const float along = HMM_DotV3(d, up), off = HMM_LenV3(HMM_SubV3(d, HMM_MulV3F(up, along)));
        mg_log_info("autotest avatar: lean %.2f: head %.4f m up the body axis, %.4f m off it", (double)leans[i], (double)along, (double)off);
        if (fabsf(along - m->rig.head_up) > 0.002f || off > 0.002f) {
            return failf(t, "assembly: at lean %.2f the head is %.4f m up the body axis and %.4f m off it (want %.2f, 0)", (double)leans[i],
                         (double)along, (double)off, (double)m->rig.head_up);
        }
        if (i == 0) {
            for (int h = 0; h < 2; h++) {
                const HMM_Vec3 want = HMM_V3(h == 0 ? m->rig.hands_rest.X : -m->rig.hands_rest.X, s.base_y + m->rig.hands_rest.Y, m->rig.hands_rest.Z);
                const float err = HMM_LenV3(HMM_SubV3(to_local(&s, s.hand[h]), want));
                if (err > 0.002f) {
                    return failf(t, "assembly: idle hand %d is %.4f m from its rest position", h, (double)err);
                }
            }
        }
    }
    return 0;
}

static float wrapf(float a)
{
    while (a > HMM_PI32) {
        a -= 2.0f * HMM_PI32;
    }
    while (a < -HMM_PI32) {
        a += 2.0f * HMM_PI32;
    }
    return a;
}

static int walk(AvatarTest *t, const AvatarMeshes *m)
{
    const AvatarLook look = avatar_look_from_skin(AVATAR_DEFAULT_SKIN);
    AvatarPose p = standing();
    p.vel = HMM_V3(0.0f, 0.0f, -4.5f); /* north, the way yaw 0 faces */
    AvatarAnim a;
    SDL_zero(a);
    float peak = 0.0f;
    int same_sign = 0, swings = 0;
    for (int f = 0; f < 180; f++) {
        p.feet = HMM_AddV3(p.feet, HMM_MulV3F(p.vel, DT));
        avatar_animate(m, &a, &p, DT);
        AvatarSolved s;
        avatar_solve(m, &look, &p, &a, &s);
        if (f < 60) {
            continue; /* move has eased in (12 /s) */
        }
        peak = mg_maxf(peak, s.base_y - m->rig.stand_y);
        const float zl = to_local(&s, s.hand[0]).Z - m->rig.hands_rest.Z, zr = to_local(&s, s.hand[1]).Z - m->rig.hands_rest.Z;
        if (fabsf(zl) > 0.03f && fabsf(zr) > 0.03f) {
            swings++;
            same_sign += (zl > 0.0f) == (zr > 0.0f);
        }
    }
    mg_log_info("autotest avatar: walk bob peak %.4f m, %d swing samples, %d in phase", (double)peak, swings, same_sign);
    if (fabsf(peak - m->rig.walk_bob_m) > 0.005f) {
        return failf(t, "walk: the bob peaks at %.4f m (want %.3f +- 0.005)", (double)peak, (double)m->rig.walk_bob_m);
    }
    if (swings < 60 || same_sign > 0) {
        return failf(t, "walk: hands not in opposite phase (%d of %d samples swing the same way)", same_sign, swings);
    }
    /* a 90 degree turn */
    const float target = p.yaw + HMM_PI32 * 0.5f;
    p.yaw = target;
    float settled = -1.0f, worst_step = 0.0f, prev = a.yaw;
    for (int f = 1; f <= 120; f++) {
        avatar_animate(m, &a, &p, DT);
        worst_step = mg_maxf(worst_step, fabsf(wrapf(a.yaw - prev)));
        prev = a.yaw;
        if (settled < 0.0f && fabsf(wrapf(target - a.yaw)) < HMM_AngleDeg(3.0f)) {
            settled = (float)f * DT;
        }
    }
    mg_log_info("autotest avatar: a 90 deg turn settles in %.3f s, largest frame step %.2f deg", (double)settled, (double)(worst_step * 57.29578f));
    if (settled < 0.2f || settled > 0.5f) {
        return failf(t, "turn: settles in %.3f s (want 0.2 .. 0.5)", (double)settled);
    }
    if (worst_step > HMM_AngleDeg(15.0f)) {
        return failf(t, "turn: one frame turned %.1f deg (a snap)", (double)(worst_step * 57.29578f));
    }
    return 0;
}

static int options(AvatarTest *t, const AvatarMeshes *m)
{
    uint32_t rng = 0x5EED1A1u;
    int checked = 0;
    for (int i = 0; i < 200; i++) {
        rng = rng * 1664525u + 1013904223u;
        const uint32_t r = rng >> 4;
        NetSkin s = {(uint8_t)(r % 2u), (uint8_t)((r / 2u) % 8u), (uint8_t)((r / 16u) % 6u), (uint8_t)((r / 96u) % 8u), (uint8_t)((r / 768u) % 8u),
                     (uint8_t)((r / 6144u) % 5u), 0};
        const AvatarLook l = avatar_look_from_skin(s);
        if (l.hair_style != s.hair || l.accessory != s.accessory || l.body != s.body || l.skin != avatar_skin_colors()[s.skin] ||
            l.hair != avatar_hair_colors()[s.hair_color]) {
            return failf(t, "options: sample %d (%u %u %u %u %u %u) does not keep its choices", i, s.body, s.skin, s.hair, s.hair_color, s.suit, s.accessory);
        }
        if (m->body == GFX_MESH_NONE || m->head == GFX_MESH_NONE || m->hand == GFX_MESH_NONE || m->eye == GFX_MESH_NONE) {
            return failf(t, "options: a base part is missing");
        }
        if (l.hair_style != HAIR_NONE && (m->hair[l.hair_style] == GFX_MESH_NONE || m->hair_gap[l.hair_style] > 0.03f)) {
            return failf(t, "options: hair style %d missing or detached (gap %.3f m)", l.hair_style, (double)m->hair_gap[l.hair_style]);
        }
        const int acc = l.accessory == ACC_SUNGLASSES ? 0 : l.accessory == ACC_CAT_EARS ? 1 : l.accessory == ACC_MASK ? 2 : -1;
        if (acc >= 0) {
            const GfxMeshId mesh = acc == 0 ? m->glasses : acc == 1 ? m->ears : m->mask;
            if (mesh == GFX_MESH_NONE || m->acc_gap[acc] > 0.03f) {
                return failf(t, "options: extra %d missing or detached (gap %.3f m)", l.accessory, (double)m->acc_gap[acc]);
            }
        }
        checked++;
    }
    for (int s = HAIR_SHORT; s < AVATAR_HAIRS; s++) {
        for (int o = HAIR_SHORT; o < s; o++) {
            if (m->hair[s] == m->hair[o]) {
                return failf(t, "options: hair styles %d and %d draw the same mesh", o, s);
            }
        }
    }
    mg_log_info("autotest avatar: %d option samples drawn with every part, hair gaps %.3f %.3f %.3f %.3f %.3f m", checked, (double)m->hair_gap[1],
                (double)m->hair_gap[2], (double)m->hair_gap[3], (double)m->hair_gap[4], (double)m->hair_gap[5]);
    return 0;
}

static int skins(AvatarTest *t)
{
    static const struct {
        const char *json;
        NetSkin want;
    } rows[] = {
        {"{\"palette\":2,\"hair\":1,\"accessory\":3}", {0, 0, 1, 0, 0, 3, 2}}, /* M2: palette, hair, accessory only */
        {"{\"body\":1,\"skin\":5,\"hair\":4,\"hair_color\":7,\"suit\":6,\"accessory\":4,\"palette\":0}", {1, 5, 4, 7, 6, 4, 0}},
        {"{\"skin\":1,\"hair\":1,\"hair_color\":1,\"suit\":3,\"accessory\":3}", {0, 1, 1, 1, 3, 3, 0}}, /* the default doctor */
        {"{\"body\":0,\"skin\":7,\"hair\":0,\"hair_color\":0,\"suit\":4,\"accessory\":0}", {0, 7, 0, 0, 4, 0, 0}},
    };
    for (int i = 0; i < (int)SDL_arraysize(rows); i++) {
        NetSkin s;
        if (!net_skin_parse(rows[i].json, &s) || SDL_memcmp(&s, &rows[i].want, sizeof(s)) != 0) {
            return failf(t, "skins: row %d does not parse to its choices", i);
        }
        const AvatarLook l = avatar_look_from_skin(s);
        uint32_t outfit, accent;
        avatar_suit_colors(s.suit, &outfit, &accent);
        if (l.hair_style != s.hair || l.accessory != s.accessory || l.body != s.body || l.outfit != outfit || l.accent != accent) {
            return failf(t, "skins: row %d shows other choices than it saved", i);
        }
    }
    mg_log_info("autotest avatar: %d old profiles.skin rows load with the same choices", (int)SDL_arraysize(rows));
    return 0;
}

int at_run(AvatarTest *t, MgApp *app, const AutotestProbe *p, AutotestOut *out, double now)
{
    (void)app, (void)out, (void)now;
    const AvatarMeshes *m = p->avatar_meshes;
    if (!m) {
        return failf(t, "no avatar meshes");
    }
    int r;
    if ((r = assembly(t, m)) || (r = walk(t, m)) || (r = options(t, m)) || (r = skins(t))) {
        return r;
    }
    return 1;
}
