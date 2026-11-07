/* Replication: wire format, send-rate policy, interpolation under jitter
 * and loss, extrapolation limits. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "net/replication.h"

static int g_fail;

#define CHECK(cond, ...)                                                                                     \
    do {                                                                                                     \
        printf("  [%s] ", (cond) ? " ok " : "FAIL");                                                         \
        printf(__VA_ARGS__);                                                                                 \
        printf("\n");                                                                                        \
        if (!(cond)) {                                                                                       \
            g_fail++;                                                                                        \
        }                                                                                                    \
    } while (0)

static float frand(unsigned *seed)
{
    *seed = *seed * 1664525u + 1013904223u;
    return (float)((*seed >> 8) & 0xFFFFFF) / (float)0xFFFFFF;
}

static void test_wire(void)
{
    RepState a = {513, 123456, {1.25f, 2.5f, -3.75f}, {4.5f, -1.0f, 0.25f}, -2.9f, REP_GROUNDED | REP_SPRINTING};
    uint8_t buf[REP_PACKET_SIZE];
    int n = rep_pack(&a, buf);
    RepState b;
    bool ok = rep_unpack(buf, n, &b);
    CHECK(n == 33, "packet is %d bytes (seq u16, tick u32, pos 3f, vel 3f, yaw i16, flags u8)", n);
    CHECK(ok && b.seq == a.seq && b.tick == a.tick && b.pos.Z == a.pos.Z && b.vel.X == a.vel.X &&
              fabsf(b.yaw - a.yaw) < 0.0002f && b.flags == a.flags,
          "round trip exact (yaw error %.5f rad)", fabsf(b.yaw - a.yaw));
    buf[6] = 0xFF, buf[7] = 0xFF, buf[8] = 0xC0, buf[9] = 0x7F; /* NaN x */
    CHECK(!rep_unpack(buf, n, &b), "rejects NaN positions");
}

static void test_rates(void)
{
    CHECK(rep_send_rate(1) == 0.0f && rep_send_rate(2) == 10.0f && rep_send_rate(3) == 8.0f && rep_send_rate(4) == 8.0f,
          "rates: alone 0 Hz, 2 players 10 Hz, 3-4 players 8 Hz");
    float worst = 0.0f;
    for (int n = 2; n <= 8; n++) {
        float fan = (float)(n * (n - 1)) * rep_send_rate(n);
        worst = fan > worst ? fan : worst;
    }
    CHECK(worst <= 100.0f, "fan-out N x (N-1) x Hz stays <= 100 msg/s up to 8 players (worst %.0f)", worst);

    RepSender s = {0};
    RepState st = {0, 0, {0, 0, 0}, {0, 0, 0}, 0, REP_GROUNDED};
    int sent = 0;
    for (int f = 0; f < 600; f++) { /* 10 s standing still at 60 fps */
        st.tick = (uint32_t)f;
        if (rep_sender_due(&s, &st, 2, f / 60.0)) {
            sent++;
        }
    }
    CHECK(sent >= 10 && sent <= 12, "standing still: only 1 Hz keepalives (%d in 10 s)", sent);
    sent = 0;
    for (int f = 0; f < 600; f++) {
        st.tick = (uint32_t)(600 + f);
        st.pos.X += 4.5f / 60.0f;
        st.vel.X = 4.5f;
        if (rep_sender_due(&s, &st, 2, (600 + f) / 60.0)) {
            sent++;
        }
    }
    CHECK(sent >= 98 && sent <= 101, "walking with 2 players: 10 Hz (%d in 10 s)", sent);
}

/* Sender walks a circle at 4.5 m/s, sends at 10 Hz; network adds 30-110 ms
 * latency and drops `loss` of packets; receiver renders at 60 Hz. */
static void run_link(float loss, float *max_step, float *max_err, int *extrap_frames)
{
    RepReceiver rx;
    rep_receiver_reset(&rx);
    unsigned seed = 7;
    typedef struct InFlight {
        double arrive;
        RepState s;
    } InFlight;
    InFlight q[256];
    int qn = 0;
    *max_step = 0.0f;
    *max_err = 0.0f;
    *extrap_frames = 0;
    HMM_Vec3 prev = {0};
    bool have_prev = false;
    const float R = 5.0f, w = 4.5f / R;
    for (int f = 0; f < 60 * 20; f++) {
        double t = f / 60.0;
        if (f % 6 == 0) { /* 10 Hz */
            float a = w * (float)t;
            RepState s = {(uint16_t)(f / 6), (uint32_t)f, {R * cosf(a), 0, R * sinf(a)},
                          {-R * w * sinf(a), 0, R * w * cosf(a)}, a, REP_GROUNDED};
            if (frand(&seed) >= loss && qn < 256) {
                q[qn].arrive = t + 0.03 + 0.08 * frand(&seed);
                q[qn].s = s;
                qn++;
            }
        }
        for (int i = 0; i < qn;) {
            if (q[i].arrive <= t) {
                rep_receiver_push(&rx, &q[i].s, t);
                q[i] = q[--qn];
            } else {
                i++;
            }
        }
        RepSample smp = rep_receiver_sample(&rx, t);
        if (!smp.valid || t < 1.0) {
            continue;
        }
        if (smp.extrapolating) {
            (*extrap_frames)++;
        }
        if (have_prev) {
            float step = HMM_LenV3(HMM_SubV3(smp.pos, prev));
            *max_step = step > *max_step ? step : *max_step;
        }
        prev = smp.pos;
        have_prev = true;
        /* compare against where the sender was ~(delay + min latency) ago */
        double ts = t - 0.1 - 0.03;
        float a = w * (float)ts;
        HMM_Vec3 truth = {R * cosf(a), 0, R * sinf(a)};
        float err = HMM_LenV3(HMM_SubV3(smp.pos, truth));
        *max_err = err > *max_err ? err : *max_err;
    }
}

static void test_interpolation(void)
{
    float step, err;
    int ex;
    run_link(0.0f, &step, &err, &ex);
    CHECK(step < 0.11f && err < 0.45f, "jittery link: smooth (max step %.3f m/frame at 0.075 nominal), error %.2f m",
          step, err);
    run_link(0.2f, &step, &err, &ex);
    CHECK(step < 0.2f && err < 0.8f, "20%% packet loss: no teleports (max step %.3f m), error %.2f m, %d extrapolated frames",
          step, err, ex);

    /* extrapolate up to 200 ms then hold */
    RepReceiver rx;
    rep_receiver_reset(&rx);
    for (int i = 0; i < 10; i++) {
        RepState s = {(uint16_t)i, (uint32_t)(i * 6), {i * 0.45f, 0, 0}, {4.5f, 0, 0}, 0, REP_GROUNDED};
        rep_receiver_push(&rx, &s, i * 0.1 + 0.05);
    }
    double last = 0.9 + 0.05;
    RepSample a = rep_receiver_sample(&rx, last + 0.1 + 0.15);
    RepSample b = rep_receiver_sample(&rx, last + 0.1 + 0.5);
    RepSample c = rep_receiver_sample(&rx, last + 0.1 + 2.0);
    CHECK(a.extrapolating && fabsf(a.pos.X - (4.05f + 4.5f * 0.15f)) < 0.02f, "extrapolates from velocity (x=%.3f)", a.pos.X);
    CHECK(fabsf(b.pos.X - (4.05f + 4.5f * 0.2f)) < 0.02f && fabsf(c.pos.X - b.pos.X) < 1e-4f,
          "stops extrapolating after 200 ms and holds (x=%.3f)", c.pos.X);

    /* a lost packet must not hide a jump */
    rep_receiver_reset(&rx);
    RepState s0 = {0, 0, {0, 0, 0}, {0, 0, 0}, 0, REP_GROUNDED};
    RepState s2 = {2, 12, {0, 0.6f, 0}, {0, 3.0f, 0}, 0, REP_JUMPING};
    rep_receiver_push(&rx, &s0, 0.05);
    rep_receiver_push(&rx, &s2, 0.25); /* seq 1 (jump start) lost */
    RepSample j = rep_receiver_sample(&rx, 0.05 + 0.1 + 0.1);
    CHECK((j.flags & REP_JUMPING) && rx.dropped == 1, "jump flag survives a dropped packet (dropped=%u)", (unsigned)rx.dropped);
}

int test_replication(void)
{
    g_fail = 0;
    printf("replication\n");
    test_wire();
    test_rates();
    test_interpolation();
    return g_fail;
}
