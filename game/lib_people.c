/* LIB-1 §9 background people and §7 walking for the hall: see lib_people.h. A port of the prototype's updNpc (routes,
 * stops, the acts' weights), its door rule, and Av.go / follow / walkTo through lib_nav's agents. */
#include "lib_people.h"

#include <math.h>
#include <string.h>

#include "cJSON.h"
#include "core/log.h"

#define DEG 0.017453293f

static const char *const k_act_names[LIB_ACT_COUNT] = {"", "browse", "read", "read_own", "shelve", "ladder", "take", "checkout", "return",
                                                       "stamp", "talk", "look", "push_cart"};

const char *lib_act_name(int act) { return act >= 0 && act < LIB_ACT_COUNT ? k_act_names[act] : "?"; }

static float jnum(const cJSON *o, const char *k, float def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? (float)v->valuedouble : def;
}

static HMM_Vec2 jv2(const cJSON *o, const char *k)
{
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_GetArraySize(a) >= 2 ? HMM_V2((float)cJSON_GetArrayItem(a, 0)->valuedouble, (float)cJSON_GetArrayItem(a, 1)->valuedouble) : HMM_V2(0, 0);
}

static bool jv3(const cJSON *o, const char *k, HMM_Vec3 *out)
{
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(o, k);
    if (cJSON_GetArraySize(a) < 3) return false;
    *out = HMM_V3((float)cJSON_GetArrayItem(a, 0)->valuedouble, (float)cJSON_GetArrayItem(a, 1)->valuedouble, (float)cJSON_GetArrayItem(a, 2)->valuedouble);
    return true;
}

static int act_of(const char *s, bool own)
{
    if (!s) return LIB_ACT_NONE;
    if (strcmp(s, "read") == 0) return own ? LIB_ACT_READ_OWN : LIB_ACT_READ;
    for (int a = 1; a < LIB_ACT_COUNT; a++)
        if (strcmp(s, k_act_names[a]) == 0) return a;
    return -1;
}

bool lib_people_config(LibPeopleCfg *c, const cJSON *root)
{
    memset(c, 0, sizeof *c);
    const cJSON *life = cJSON_GetObjectItemCaseSensitive(root, "life");
    const cJSON *npcs = cJSON_GetObjectItemCaseSensitive(root, "npcs");
    const cJSON *door = cJSON_GetObjectItemCaseSensitive(root, "door");
    const cJSON *hall = cJSON_GetObjectItemCaseSensitive(root, "hall");
    const cJSON *rs = cJSON_GetObjectItemCaseSensitive(life, "read_standing"), *sh = cJSON_GetObjectItemCaseSensitive(life, "shelve");
    const cJSON *ld = cJSON_GetObjectItemCaseSensitive(life, "ladder"), *dk = cJSON_GetObjectItemCaseSensitive(life, "desk");
    const cJSON *st = cJSON_GetObjectItemCaseSensitive(life, "stamp"), *tk = cJSON_GetObjectItemCaseSensitive(life, "talk");
    const cJSON *lk = cJSON_GetObjectItemCaseSensitive(life, "look"), *sl = cJSON_GetObjectItemCaseSensitive(life, "sleeper");
    bool ok = cJSON_IsArray(npcs) && cJSON_IsObject(rs) && cJSON_IsObject(sh) && cJSON_IsObject(ld) && cJSON_IsObject(dk) && cJSON_IsObject(st) &&
              cJSON_IsObject(tk) && cJSON_IsObject(lk) && cJSON_IsObject(sl) && cJSON_IsObject(door) && cJSON_IsObject(hall);
    if (!ok) {
        mg_log_error("lib_people: the scene lacks npcs / life.{read_standing, shelve, ladder, desk, stamp, talk, look, sleeper} / door / hall");
        return false;
    }
    ok &= jv3(rs, "hands", &c->read.hands) && jv3(rs, "book", &c->read.book);
    c->read.head_pitch = jnum(rs, "head_pitch", 0.45f), c->read.book_tilt = jnum(rs, "book_tilt", -0.6f);
    c->read.book_scale = jnum(rs, "book_scale", 0.7f), c->read.from = jnum(rs, "from", 0.2f), c->read.to = jnum(rs, "to", 0.88f);
    ok &= jv3(sh, "low", &c->shelve.low) && jv3(sh, "high", &c->shelve.high);
    c->shelve.period_s = jnum(sh, "period_s", 1.6f);
    c->ladder.height = jnum(ld, "height", 1.1f), c->ladder.up_until = jnum(ld, "up_until", 0.25f), c->ladder.down_from = jnum(ld, "down_from", 0.8f);
    ok &= jv3(dk, "hands", &c->desk.hands) && jv3(dk, "book", &c->desk.book);
    ok &= jv3(st, "hand", &c->stamp.hand);
    c->stamp.dip_m = jnum(st, "dip_m", 0.09f), c->stamp.period_s = jnum(st, "period_s", 1.2f), c->stamp.head_pitch = jnum(st, "head_pitch", 0.3f);
    c->talk_turn_s = jnum(tk, "turn_s", 2.4f);
    c->look.lean = jnum(lk, "lean", 0.22f);
    ok &= jv3(lk, "hands", &c->look.hands);
    ok &= jv3(life, "carry_book", &c->carry_book);
    c->sleeper.lean = jnum(sl, "lean", 0.95f), c->sleeper.head_pitch = jnum(sl, "head_pitch", 0.35f), c->sleeper.head_roll = jnum(sl, "head_roll", 0.5f);
    c->sleeper.breath_scale = jnum(sl, "breath_scale", 0.03f), c->sleeper.zee_period_s = jnum(sl, "zee_period_s", 2.6f);
    c->sleeper.zee_rise_m = jnum(sl, "zee_rise_m", 0.5f);
    ok &= jv3(sl, "hand_left", &c->sleeper.hand_left) && jv3(sl, "hand_right", &c->sleeper.hand_right);
    c->door_centre = jv2(door, "centre");
    c->door_open_rad = jnum(door, "open_rad", 1.75f), c->door_open_radius = jnum(door, "open_radius", 1.7f), c->door_rate = jnum(door, "rate", 8.0f);
    const cJSON *bx = cJSON_GetObjectItemCaseSensitive(hall, "bx"), *bz = cJSON_GetObjectItemCaseSensitive(hall, "bz");
    c->bays = cJSON_GetArraySize(bx);
    if (c->bays < 1 || c->bays > LIB_PEOPLE_BAYS || cJSON_GetArraySize(bz) != c->bays) ok = false;
    for (int b = 0; ok && b < c->bays; b++) {
        c->bx[b] = (float)cJSON_GetArrayItem(bx, b)->valuedouble;
        c->bz[b] = (float)cJSON_GetArrayItem(bz, b)->valuedouble;
    }
    c->walk_speed = jnum(cJSON_GetObjectItemCaseSensitive(root, "study"), "walk_speed", 2.4f);
    const cJSON *nj;
    cJSON_ArrayForEach(nj, npcs)
    {
        if (c->n >= LIB_PEOPLE_MAX) {
            mg_log_error("lib_people: more than %d npcs", LIB_PEOPLE_MAX);
            return false;
        }
        LibPersonDef *d = &c->def[c->n++];
        const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(nj, "id"));
        strncpy(d->id, id ? id : "", sizeof d->id - 1);
        d->speed = jnum(nj, "speed", 0.0f);
        d->push_cart = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(nj, "push_cart"));
        d->uses_door = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(nj, "uses_door"));
        d->sleeper = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(nj, "sleeper"));
        const cJSON *seated = cJSON_GetObjectItemCaseSensitive(nj, "seated");
        if (cJSON_IsObject(seated)) {
            d->seated = true;
            d->seat_pos = jv2(seated, "pos");
            d->seat_yaw_deg = jnum(seated, "yaw", 0.0f);
        }
        const cJSON *rsj = cJSON_GetObjectItemCaseSensitive(nj, "read_s");
        if (cJSON_GetArraySize(rsj) == 2) d->read_s[0] = (float)cJSON_GetArrayItem(rsj, 0)->valuedouble, d->read_s[1] = (float)cJSON_GetArrayItem(rsj, 1)->valuedouble;
        const cJSON *rj;
        cJSON_ArrayForEach(rj, cJSON_GetObjectItemCaseSensitive(nj, "route"))
        {
            if (d->route_n >= LIB_PEOPLE_STOPS_MAX) {
                mg_log_error("lib_people: %s has more than %d stops", d->id, LIB_PEOPLE_STOPS_MAX);
                return false;
            }
            LibStop *s = &d->route[d->route_n++];
            s->p = jv2(rj, "p");
            s->wait_s = jnum(rj, "wait_s", 0.0f);
            const cJSON *face = cJSON_GetObjectItemCaseSensitive(rj, "face_deg");
            s->has_face = cJSON_IsNumber(face);
            s->face_deg = s->has_face ? (float)face->valuedouble : 0.0f;
            s->pitch = jnum(rj, "pitch", 0.0f);
            s->outside = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(rj, "outside"));
            s->leave_cart = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(rj, "leave_cart"));
            const cJSON *bk = cJSON_GetObjectItemCaseSensitive(rj, "book");
            s->book = cJSON_IsBool(bk) ? (cJSON_IsTrue(bk) ? 1 : 0) : -1;
            s->turn = (int)jnum(rj, "turn", 0.0f);
            s->act = act_of(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(rj, "act")), cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(rj, "own")));
            if (s->act < 0) {
                mg_log_error("lib_people: %s: unknown act %s", d->id, cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(rj, "act")));
                return false;
            }
        }
    }
    if (!ok) mg_log_error("lib_people: a life.* vector is missing");
    return ok;
}

/* ------------------------------------------------------------------ helpers */

static float ease(float dt, float rate) { return 1.0f - expf(-rate * dt); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float smooth(float x)
{
    x = x < 0 ? 0 : x > 1 ? 1 : x;
    return x * x * (3 - 2 * x);
}
static float alerp(float a, float b, float t)
{
    const float TAU = 6.28318531f;
    float d = fmodf(b - a + 3.14159265f, TAU);
    if (d < 0) d += TAU;
    return a + (d - 3.14159265f) * t;
}
static float dist(HMM_Vec2 a, HMM_Vec2 b) { return sqrtf((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y)); }

/* is p (radius r) clear of every active agent but `self` at 96 % of the radii sum */
static bool spot_clear(const LibPeople *P, int self, HMM_Vec2 p, float r)
{
    for (int i = 0; i < P->ag.count; i++) {
        const LibAgent *a = &P->ag.a[i];
        if (i == self || !a->active) continue;
        if (dist(a->pos, p) < (a->radius + r) * LIB_HARD_RULE) return false;
    }
    return true;
}

static void go(LibPeople *P, const LibNav *n, int agent, HMM_Vec2 goal)
{
    lib_agent_go(&P->ag, n, agent, goal);
    if (P->straight) { /* the A/B: no search, the stop in a straight line */
        LibAgent *a = &P->ag.a[agent];
        lib_nav_cancel(&P->ag.q, a->ticket);
        a->ticket = 0;
        a->path.pts[0] = goal, a->path.n = 1, a->path.last_straight = true;
        a->has_path = true;
        a->next = 0;
    }
}

/* ------------------------------------------------------------------ init */

void lib_people_init(LibPeople *P, const LibPeopleCfg *c, const LibNav *n)
{
    memset(P, 0, sizeof *P);
    P->cfg = c;
    lib_agents_init(&P->ag, n);
    P->ag.service_searches = 1; /* one search per tick, oldest first: the same on every device */
    P->min_ratio = 1e9f;
    P->stuck_who = -1;
    for (int i = 0; i < c->n; i++) {
        const LibPersonDef *d = &c->def[i];
        LibPerson *p = &P->p[i];
        p->agent = p->cart = -1;
        if (d->seated) {
            p->pos = d->seat_pos;
            p->yaw = d->seat_yaw_deg * DEG;
            continue;
        }
        if (d->route_n == 0) continue;
        const LibStop *s0 = &d->route[0];
        p->pos = s0->p;
        p->yaw = s0->has_face ? s0->face_deg * DEG : 0.0f;
        p->waiting = true;
        p->wait = s0->wait_s < 0 ? -1.0f : (s0->wait_s > 0 ? s0->wait_s : 0.01f);
        p->hidden = s0->outside;
        p->agent = lib_agent_add(&P->ag, p->pos, n->radius, d->speed);
        if (p->agent < 0) continue;
        P->ag.a[p->agent].yaw = p->yaw;
        P->ag.a[p->agent].active = !p->hidden;
        if (d->push_cart) {
            p->cart = lib_agent_add_cart(&P->ag, p->agent);
            P->ag.a[p->agent].cart_pushed = !s0->leave_cart;
            p->push = s0->leave_cart ? 0.0f : 0.6f;
        }
    }
    for (int k = 0; k < LIB_PEOPLE_WALKERS; k++) { /* the members' agents, kept out of the hall until a walk */
        P->walker_agent[k] = lib_agent_add(&P->ag, HMM_V2(0, 0), n->radius, c->walk_speed);
        if (P->walker_agent[k] >= 0) P->ag.a[P->walker_agent[k]].active = false;
    }
}

/* ------------------------------------------------------------------ members */

void lib_people_walker_start(LibPeople *P, int slot, HMM_Vec2 start, float yaw, HMM_Vec2 goal)
{
    if (slot < 0 || slot >= LIB_PEOPLE_WALKERS || P->walker_agent[slot] < 0) return;
    LibWalker *w = &P->w[slot];
    w->start = start, w->goal = goal, w->start_yaw = yaw;
    w->on = false;
    w->pending = true; /* placed on the next tick that finds its spot clear */
}

void lib_people_walker_goal(LibPeople *P, int slot, HMM_Vec2 goal)
{
    if (slot < 0 || slot >= LIB_PEOPLE_WALKERS || P->walker_agent[slot] < 0) return;
    LibWalker *w = &P->w[slot];
    w->goal = goal;
    if (w->on) go(P, P->ag.q.nav, P->walker_agent[slot], goal);
}

void lib_people_walker_stop(LibPeople *P, int slot)
{
    if (slot < 0 || slot >= LIB_PEOPLE_WALKERS || P->walker_agent[slot] < 0) return;
    P->w[slot].on = P->w[slot].pending = false;
    lib_agent_stop(&P->ag, P->walker_agent[slot]);
    P->ag.a[P->walker_agent[slot]].active = false;
}

bool lib_people_walker_arrived(const LibPeople *P, int slot)
{
    if (slot < 0 || slot >= LIB_PEOPLE_WALKERS || !P->w[slot].on) return false;
    const LibAgent *a = &P->ag.a[P->walker_agent[slot]];
    return a->arrived && !a->ticket;
}

const LibAgent *lib_people_walker(const LibPeople *P, int slot)
{
    return slot >= 0 && slot < LIB_PEOPLE_WALKERS && P->w[slot].on ? &P->ag.a[P->walker_agent[slot]] : NULL;
}

bool lib_people_at_door(const LibPeople *P, int b)
{
    const LibPeopleCfg *c = P->cfg;
    if (b < 0 || b >= c->bays) return false;
    const HMM_Vec2 dc = HMM_V2(c->bx[b] + c->door_centre.X, c->bz[b] + c->door_centre.Y);
    for (int i = 0; i < c->n; i++) {
        const LibPerson *p = &P->p[i];
        if (c->def[i].uses_door && !p->hidden && p->agent >= 0 && HMM_LenV2(p->vel) > 0.2f && dist(p->pos, dc) < c->door_open_radius) return true;
    }
    return false;
}

/* ------------------------------------------------------------------ the tick */

/* the prototype's updNpc for one person over pdt (its own rate) */
static void person_step(LibPeople *P, const LibNav *n, int i, float pdt)
{
    const LibPeopleCfg *c = P->cfg;
    const LibPersonDef *d = &c->def[i];
    LibPerson *p = &P->p[i];
    LibAgent *a = &P->ag.a[p->agent];
    const float time = (float)P->time;
    const LibStop *st = &d->route[p->step];
    int act = LIB_ACT_NONE;
    float q = 0.0f;
    if (p->waiting) {
        if (st->wait_s >= 0.0f) p->wait -= pdt;
        if (st->has_face) p->yaw = alerp(p->yaw, st->face_deg * DEG, ease(pdt, 8));
        a->yaw = p->yaw;
        p->hidden = st->outside;
        act = st->act;
        q = st->wait_s < 0.0f ? 0.5f : 1.0f - fmaxf(0.0f, p->wait) / fmaxf(st->wait_s, 1e-3f);
        if (st->book >= 0 && q < 0.1f) p->has_book = st->book == 1;
        if (act == LIB_ACT_TAKE && q > 0.7f) p->has_book = true;
        if (act == LIB_ACT_RETURN && q > 0.55f) p->has_book = false;
        /* the wait is over: on to the next stop (coming in from outside only once the doorway is clear) */
        if (st->wait_s >= 0.0f && p->wait <= 0.0f && d->speed > 0.0f && (!st->outside || spot_clear(P, p->agent, p->pos, a->radius))) {
            p->step = (p->step + 1) % d->route_n;
            if (p->step == 0) p->loops++;
            st = &d->route[p->step];
            p->waiting = false;
            p->hidden = false;
            a->active = true;
            if (p->cart >= 0) { /* the stop it walks to says whether the cart comes */
                a->cart_catching = !a->cart_pushed && !st->leave_cart;
                a->cart_pushed = !st->leave_cart;
            }
            go(P, n, p->agent, st->p);
        }
    } else {
        p->yaw = a->yaw;
        if (a->arrived && !a->ticket) {
            p->waiting = true;
            p->wait = st->wait_s < 0.0f ? -1.0f : (st->wait_s > 0.0f ? st->wait_s : 0.01f);
        }
    }
    p->hidden = p->waiting && st->outside;
    a->active = !p->hidden;
    p->act = act;
    p->q = q;
    /* what the hands and head do at a stop (updNpc) */
    const bool rd = (act == LIB_ACT_READ && q > c->read.from && q < c->read.to) || act == LIB_ACT_READ_OWN;
    const bool top = act == LIB_ACT_LADDER && q > c->ladder.up_until && q < c->ladder.down_from;
    const float e5 = ease(pdt, 5);
    p->browse = lerpf(p->browse, (act == LIB_ACT_BROWSE || act == LIB_ACT_TAKE || (act == LIB_ACT_READ && !rd) || top) ? 1.0f : 0.0f, e5);
    p->read = lerpf(p->read, rd ? 1.0f : 0.0f, ease(pdt, 6));
    p->stamp = lerpf(p->stamp, act == LIB_ACT_STAMP ? 1.0f : 0.0f, ease(pdt, 4));
    p->shelve = lerpf(p->shelve, act == LIB_ACT_SHELVE ? 1.0f : 0.0f, e5);
    p->talk = lerpf(p->talk, act == LIB_ACT_TALK ? 1.0f : 0.0f, e5);
    const bool speaks = act == LIB_ACT_TALK && ((int)floorf(time / c->talk_turn_s)) % 2 == st->turn;
    p->spk = lerpf(p->spk, speaks ? 1.0f : 0.0f, ease(pdt, 8));
    p->look = lerpf(p->look, act == LIB_ACT_LOOK ? 1.0f : 0.0f, e5);
    if (act == LIB_ACT_LOOK) p->look_pitch = st->pitch;
    p->desk = lerpf(p->desk, (act == LIB_ACT_CHECKOUT || act == LIB_ACT_RETURN) ? 1.0f : 0.0f, e5);
    p->climb = lerpf(p->climb, act == LIB_ACT_LADDER && !top ? 1.0f : 0.0f, ease(pdt, 8));
    p->lift = act == LIB_ACT_LADDER ? c->ladder.height * smooth(q / c->ladder.up_until) * (1.0f - smooth((q - c->ladder.down_from) / (1.0f - c->ladder.down_from))) : 0.0f;
    if (p->cart >= 0) {
        const bool moving = HMM_LenV2(a->vel) > 0.3f * fmaxf(d->speed, 0.1f);
        p->push = lerpf(p->push, st->leave_cart ? 0.0f : (moving ? 1.0f : 0.6f), ease(pdt, 6));
        if (moving && a->cart_pushed) P->acts_seen |= 1u << LIB_ACT_PUSH;
    }
    if (act != LIB_ACT_NONE && q > 0.0f && q < 1.0f) P->acts_seen |= 1u << act;
}

void lib_people_tick(LibPeople *P, const LibNav *n, float dt, uint32_t visible)
{
    const LibPeopleCfg *c = P->cfg;
    P->visible = visible;
    /* members waiting for their spot */
    for (int k = 0; k < LIB_PEOPLE_WALKERS; k++) {
        LibWalker *w = &P->w[k];
        const int id = P->walker_agent[k];
        if (!w->pending || id < 0) continue;
        if (!spot_clear(P, id, w->start, P->ag.a[id].radius)) continue;
        LibAgent *a = &P->ag.a[id];
        a->pos = w->start;
        a->yaw = w->start_yaw;
        a->vel = HMM_V2(0, 0);
        a->blocked_s = a->ignore_others_s = 0.0f;
        a->active = true;
        w->pending = false;
        w->on = true;
        go(P, n, id, w->goal);
    }
    /* each person at its rate: every tick in a bay on screen, one tick in six (6 x dt) elsewhere */
    for (int i = 0; i < c->n; i++) {
        LibPerson *p = &P->p[i];
        if (p->agent < 0) continue;
        p->bay = lib_nav_bay_of(n, p->pos.X, p->pos.Y);
        p->rate = (visible >> p->bay) & 1u ? 1 : LIB_PEOPLE_SLOW;
        P->ag.a[p->agent].rate = p->rate;
    }
    HMM_Vec2 before[LIB_PEOPLE_MAX];
    for (int i = 0; i < c->n; i++) {
        LibPerson *p = &P->p[i];
        before[i] = p->pos;
        if (p->agent < 0 || (p->rate > 1 && (P->ag.tick + (uint32_t)p->agent) % (uint32_t)p->rate)) continue;
        person_step(P, n, i, dt * (float)p->rate);
    }
    lib_agents_step(&P->ag, n, dt); /* the same phase test inside: a person and its agent step on the same ticks */
    for (int i = 0; i < c->n; i++) {
        LibPerson *p = &P->p[i];
        if (p->agent < 0) continue;
        const LibAgent *a = &P->ag.a[p->agent];
        p->pos = a->pos;
        if (!p->waiting) p->yaw = a->yaw;
        p->vel = a->vel;
        /* stuck: walking towards a stop and not moving (the blocked rule's idle pose) */
        if (!p->waiting && !a->arrived && dist(before[i], p->pos) < 1e-5f) {
            p->still_s += dt;
            if (p->still_s > p->still_max) p->still_max = p->still_s;
            if (p->still_s > P->stuck_max) P->stuck_max = p->still_s, P->stuck_who = i;
        } else {
            p->still_s = 0.0f;
        }
    }
    for (int i = 0; i < P->ag.count && i < LIB_AGENTS_MAX; i++) {
        const float g = P->ag.a[i].ignore_others_s;
        if (g > P->ghost_prev[i] + 1e-4f) P->ghosts++;
        P->ghost_prev[i] = g;
    }
    /* the pair check: every two agents in the hall (a pusher and its own cart excepted) */
    for (int i = 0; i < P->ag.count; i++) {
        const LibAgent *a = &P->ag.a[i];
        if (!a->active) continue;
        for (int j = i + 1; j < P->ag.count; j++) {
            const LibAgent *b = &P->ag.a[j];
            if (!b->active || a->cart == j || b->cart == i) continue;
            const float r = dist(a->pos, b->pos) / (a->radius + b->radius);
            if (r < P->min_ratio) P->min_ratio = r;
        }
    }
    /* doors: rate /s towards open_rad while anyone walking is within open_radius (the caller adds the members) */
    for (int b = 0; b < c->bays; b++) {
        const bool want = P->door_want_extra[b] || lib_people_at_door(P, b);
        P->door[b] = lerpf(P->door[b], want ? c->door_open_rad : 0.0f, ease(dt, c->door_rate));
    }
    P->tick++;
    P->time += dt;
}
