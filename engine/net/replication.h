/* Client-authoritative state replication (M1).
 *
 * Wire format, 34 bytes little endian, sent base64 inside a Broadcast:
 *   seq u16 | tick u32 | pos 3 x f32 | vel 3 x f32 | yaw i16 | flags u8 | floor u8
 * (M1 clients sent 33 bytes without `floor`; those still parse as floor 0.)
 *
 * Sender: rate adapts to room size to stay under the Supabase quota
 * (fan-out = N x (N-1) x Hz; Free plan ~100 msg/s): 10 Hz for 2 players,
 * 8 Hz for 3-4, less beyond. Nothing is sent while alone; while standing
 * still only a 1 Hz keepalive goes out.
 * Receiver: 100 ms interpolation buffer on the sender's tick clock,
 * extrapolation from velocity for up to 200 ms, then hold. Corrections when
 * fresh data arrives fade out over ~100 ms; only jumps over 1.5 m snap. Jump state rides in the flags so remote jumps animate even
 * if a packet is lost. */
#ifndef MG_REPLICATION_H
#define MG_REPLICATION_H

#include "core/mg_types.h"

#define REP_PACKET_SIZE 34
#define REP_PACKET_SIZE_M1 33
#define REP_BUFFER 16

enum {
    REP_GROUNDED = 1u << 0,
    REP_JUMPING = 1u << 1,
    REP_SPRINTING = 1u << 2,
    REP_LEAVING = 1u << 3, /* last packet before leaving: peers drop us at once */
    REP_RIDING = 1u << 4,  /* in a moving elevator: peers close the doors on that floor */
};

typedef struct RepState {
    uint16_t seq;
    uint32_t tick;
    HMM_Vec3 pos;
    HMM_Vec3 vel;
    float yaw; /* radians */
    uint8_t flags;
    uint8_t floor; /* storey; avatars are culled by floor, not distance */
} RepState;

int rep_pack(const RepState *s, uint8_t out[REP_PACKET_SIZE]);
bool rep_unpack(const uint8_t *data, int len, RepState *out);

/* Sends per second for a room of `players` (including us). */
float rep_send_rate(int players);

typedef struct RepSender {
    uint16_t seq;
    double next_send;
    double last_send;
    RepState last;
    bool have_last;
} RepSender;

/* True when `cur` should go out now; fills seq. */
bool rep_sender_due(RepSender *s, RepState *cur, int players, double now);

typedef struct RepSnapshot {
    double t;   /* sender time, seconds (tick / 60) */
    RepState s;
} RepSnapshot;

typedef struct RepReceiver {
    RepSnapshot snaps[REP_BUFFER]; /* oldest first */
    int count;
    /* clock: local_time - sender_time. The target follows the minimum delay;
     * the offset used for rendering slews towards it at <= 5% so remote
     * players never lurch when a faster packet arrives. */
    double offset_target;
    double offset;
    double offset_time;
    bool have_offset;
    /* corrections caused by new data fade out instead of popping */
    HMM_Vec3 err;
    double err_time;
    double last_recv;   /* local time of the newest packet */
    uint16_t last_seq;
    uint32_t packets, dropped, out_of_order, snaps_taken;
} RepReceiver;

typedef struct RepSample {
    HMM_Vec3 pos;
    HMM_Vec3 vel;
    float yaw;
    uint8_t flags;
    uint8_t floor;
    bool extrapolating;
    bool valid;
} RepSample;

void rep_receiver_reset(RepReceiver *r);
void rep_receiver_push(RepReceiver *r, const RepState *s, double local_now);
/* Where to draw the remote player at local time `now`. */
RepSample rep_receiver_sample(const RepReceiver *r, double now);

#endif
