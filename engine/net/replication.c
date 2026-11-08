#include "replication.h"

#include <math.h>
#include <string.h>

#define INTERP_DELAY 0.100   /* s */
#define MAX_EXTRAPOLATE 0.200 /* s */
#define KEEPALIVE_INTERVAL 1.0

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static uint16_t get_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_f32(uint8_t *p, float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    put_u32(p, u);
}

static float get_f32(const uint8_t *p)
{
    uint32_t u = get_u32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

int rep_pack(const RepState *s, uint8_t out[REP_PACKET_SIZE])
{
    put_u16(out + 0, s->seq);
    put_u32(out + 2, s->tick);
    for (int i = 0; i < 3; i++) {
        put_f32(out + 6 + 4 * i, s->pos.Elements[i]);
        put_f32(out + 18 + 4 * i, s->vel.Elements[i]);
    }
    float yaw = s->yaw;
    while (yaw > HMM_PI32) {
        yaw -= 2.0f * HMM_PI32;
    }
    while (yaw < -HMM_PI32) {
        yaw += 2.0f * HMM_PI32;
    }
    int16_t q = (int16_t)lrintf(yaw / HMM_PI32 * 32767.0f);
    put_u16(out + 30, (uint16_t)q);
    out[32] = s->flags;
    out[33] = s->floor;
    return REP_PACKET_SIZE;
}

bool rep_unpack(const uint8_t *d, int len, RepState *s)
{
    if (len < REP_PACKET_SIZE_M1) {
        return false;
    }
    s->seq = get_u16(d);
    s->tick = get_u32(d + 2);
    for (int i = 0; i < 3; i++) {
        s->pos.Elements[i] = get_f32(d + 6 + 4 * i);
        s->vel.Elements[i] = get_f32(d + 18 + 4 * i);
    }
    s->yaw = (float)(int16_t)get_u16(d + 30) / 32767.0f * HMM_PI32;
    s->flags = d[32];
    s->floor = len >= REP_PACKET_SIZE ? d[33] : 0;
    for (int i = 0; i < 3; i++) {
        if (!isfinite(s->pos.Elements[i]) || !isfinite(s->vel.Elements[i]) || fabsf(s->pos.Elements[i]) > 1e4f) {
            return false; /* reject garbage */
        }
    }
    return true;
}

float rep_send_rate(int players)
{
    if (players <= 1) {
        return 0.0f;
    }
    if (players == 2) {
        return 10.0f;
    }
    if (players <= 4) {
        return 8.0f;
    }
    /* larger open-world shards must stay inside the project quota */
    return mg_maxf(1.0f, 100.0f / (float)(players * (players - 1)));
}

static bool moved(const RepState *a, const RepState *b)
{
    HMM_Vec3 d = HMM_SubV3(a->pos, b->pos);
    return HMM_LenSqrV3(d) > 0.01f * 0.01f || HMM_LenSqrV3(a->vel) > 0.01f || HMM_LenSqrV3(b->vel) > 0.01f ||
           fabsf(a->yaw - b->yaw) > 0.01f || a->flags != b->flags || a->floor != b->floor;
}

bool rep_sender_due(RepSender *s, RepState *cur, int players, double now)
{
    float hz = rep_send_rate(players);
    if (hz <= 0.0f || now < s->next_send) {
        return false;
    }
    bool changed = !s->have_last || moved(cur, &s->last);
    if (!changed && now - s->last_send < KEEPALIVE_INTERVAL) {
        return false; /* stationary: keepalive only */
    }
    cur->seq = ++s->seq;
    s->last = *cur;
    s->have_last = true;
    s->last_send = now;
    s->next_send = now + 1.0 / (double)hz;
    return true;
}

void rep_receiver_reset(RepReceiver *r) { memset(r, 0, sizeof(*r)); }

#define OFFSET_SLEW 0.05     /* s of clock adjustment per s */
#define ERR_DECAY 10.0       /* 1/s */
#define SNAP_DISTANCE 1.5f   /* m */

static double offset_at(const RepReceiver *r, double now)
{
    double d = r->offset_target - r->offset;
    double lim = OFFSET_SLEW * (now - r->offset_time);
    if (lim < 0.0) {
        lim = 0.0;
    }
    return r->offset + (d > lim ? lim : (d < -lim ? -lim : d));
}

static HMM_Vec3 err_at(const RepReceiver *r, double now)
{
    double dt = now - r->err_time;
    float k = dt > 0.0 ? (float)exp(-ERR_DECAY * dt) : 1.0f;
    return HMM_MulV3F(r->err, k);
}

static float lerp_angle(float a, float b, float t)
{
    float d = b - a;
    while (d > HMM_PI32) {
        d -= 2.0f * HMM_PI32;
    }
    while (d < -HMM_PI32) {
        d += 2.0f * HMM_PI32;
    }
    return a + d * t;
}

static RepSample raw_sample(const RepReceiver *r, double now, double offset)
{
    RepSample out;
    memset(&out, 0, sizeof(out));
    if (r->count == 0) {
        return out;
    }
    out.valid = true;
    double t = now - offset - INTERP_DELAY; /* render time on the sender's clock */
    const RepSnapshot *first = &r->snaps[0];
    const RepSnapshot *last = &r->snaps[r->count - 1];
    if (t <= first->t) {
        out.pos = first->s.pos;
        out.vel = first->s.vel;
        out.yaw = first->s.yaw;
        out.flags = first->s.flags;
        out.floor = first->s.floor;
        return out;
    }
    if (t >= last->t) {
        double dt = t - last->t;
        out.extrapolating = dt > 1e-3;
        dt = dt > MAX_EXTRAPOLATE ? MAX_EXTRAPOLATE : dt; /* then hold */
        out.pos = HMM_AddV3(last->s.pos, HMM_MulV3F(last->s.vel, (float)dt));
        if (last->s.flags & REP_GROUNDED) {
            out.pos.Y = last->s.pos.Y + mg_maxf(last->s.vel.Y, 0.0f) * (float)dt;
        }
        out.vel = last->s.vel;
        out.yaw = last->s.yaw;
        out.flags = last->s.flags;
        out.floor = last->s.floor;
        return out;
    }
    for (int i = 0; i + 1 < r->count; i++) {
        const RepSnapshot *a = &r->snaps[i];
        const RepSnapshot *b = &r->snaps[i + 1];
        if (t >= a->t && t <= b->t) {
            float k = (float)((t - a->t) / (b->t - a->t));
            out.pos = HMM_LerpV3(a->s.pos, k, b->s.pos);
            out.vel = HMM_LerpV3(a->s.vel, k, b->s.vel);
            out.yaw = lerp_angle(a->s.yaw, b->s.yaw, k);
            /* any jump in the bracket counts, so short hops are not lost */
            out.flags = (uint8_t)((k < 0.5f ? a->s.flags : b->s.flags) | ((a->s.flags | b->s.flags) & REP_JUMPING));
            out.floor = k < 0.5f ? a->s.floor : b->s.floor;
            return out;
        }
    }
    out.pos = last->s.pos;
    out.yaw = last->s.yaw;
    out.flags = last->s.flags;
    out.floor = last->s.floor;
    return out;
}

RepSample rep_receiver_sample(const RepReceiver *r, double now)
{
    RepSample s = raw_sample(r, now, offset_at(r, now));
    if (s.valid) {
        s.pos = HMM_AddV3(s.pos, err_at(r, now));
    }
    return s;
}

static void insert_snapshot(RepReceiver *r, const RepState *s, double t)
{
    int i = r->count;
    while (i > 0 && r->snaps[i - 1].t > t) {
        i--;
    }
    if (i > 0 && r->snaps[i - 1].t == t) {
        return; /* duplicate */
    }
    if (r->count == REP_BUFFER) {
        if (i == 0) {
            return; /* older than everything we keep */
        }
        memmove(&r->snaps[0], &r->snaps[1], sizeof(RepSnapshot) * (REP_BUFFER - 1));
        r->count--;
        i--;
    }
    memmove(&r->snaps[i + 1], &r->snaps[i], sizeof(RepSnapshot) * (size_t)(r->count - i));
    r->snaps[i].t = t;
    r->snaps[i].s = *s;
    r->count++;
    r->snaps_taken++;
}

void rep_receiver_push(RepReceiver *r, const RepState *s, double now)
{
    double t = (double)s->tick / 60.0;
    if (r->packets > 0) {
        uint16_t expect = (uint16_t)(r->last_seq + 1);
        int16_t gap = (int16_t)(s->seq - expect);
        if (gap > 0) {
            r->dropped += (uint32_t)gap;
        } else if (gap < 0) {
            r->out_of_order++;
        }
    }
    r->packets++;
    r->last_seq = s->seq;
    r->last_recv = now;

    double sample = now - t;
    bool reset = !r->have_offset || (r->count > 0 && fabs(t - r->snaps[r->count - 1].t) > 5.0);
    if (reset) {
        /* first packet, or the peer restarted: start clean, no smoothing */
        r->count = 0;
        r->offset_target = r->offset = sample;
        r->offset_time = now;
        r->have_offset = true;
        r->err = HMM_V3(0, 0, 0);
        r->err_time = now;
        insert_snapshot(r, s, t);
        return;
    }
    RepSample before = rep_receiver_sample(r, now);
    /* commit the slewed offset, then update the min-delay target */
    r->offset = offset_at(r, now);
    r->offset_time = now;
    if (sample < r->offset_target) {
        r->offset_target = sample;
    } else {
        r->offset_target += (sample - r->offset_target) * 0.01; /* follow slow drift */
    }
    insert_snapshot(r, s, t);
    RepSample after = raw_sample(r, now, r->offset);
    HMM_Vec3 e = before.valid ? HMM_SubV3(before.pos, after.pos) : HMM_V3(0, 0, 0);
    r->err = HMM_LenV3(e) > SNAP_DISTANCE ? HMM_V3(0, 0, 0) : e;
    r->err_time = now;
}
