#include "elevator.h"

#define DOOR_TIME 0.4f
#define FADE_TIME 0.3f
#define HOLD_TIME 0.5f

void elevator_init(Elevator *e, int floors, float storey, float x0, float x1, float z0, float z1, float height)
{
    *e = (Elevator){0};
    e->floors = floors < ELEV_MAX_FLOORS ? floors : ELEV_MAX_FLOORS;
    e->storey = storey;
    e->x0 = x0;
    e->x1 = x1;
    e->z0 = z0;
    e->z1 = z1;
    e->height = height;
    e->door_open[0] = 1.0f;
}

bool elevator_contains(const Elevator *e, HMM_Vec3 feet, float radius)
{
    float y = (float)e->car_floor * e->storey;
    return feet.X >= e->x0 + radius && feet.X <= e->x1 - radius && feet.Z >= e->z0 + radius &&
           feet.Z <= e->z1 - radius && feet.Y > y - 0.5f && feet.Y < y + e->height;
}

bool elevator_request(Elevator *e, int floor)
{
    if (e->state != ELEV_IDLE || floor < 0 || floor >= e->floors || floor == e->car_floor) {
        return false;
    }
    e->target = floor;
    e->state = ELEV_CLOSING;
    e->t = 0.0f;
    return true;
}

void elevator_place(Elevator *e, int floor)
{
    if (floor < 0 || floor >= e->floors) {
        return;
    }
    e->car_floor = floor;
    e->state = ELEV_IDLE;
    e->t = 0.0f;
    for (int f = 0; f < e->floors; f++) {
        e->door_open[f] = f == floor ? 1.0f : 0.0f;
    }
}

static void enter(Elevator *e, ElevState s)
{
    e->state = s;
    e->t = 0.0f;
}

void elevator_tick(Elevator *e, float dt)
{
    const float speed = dt / DOOR_TIME;
    e->t += dt;
    float *car = &e->door_open[e->car_floor];
    switch (e->state) {
    case ELEV_IDLE:
        for (int f = 0; f < e->floors; f++) {
            float want = (f == e->car_floor && !e->hold_shut[f]) ? 1.0f : 0.0f;
            e->door_open[f] = want > e->door_open[f] ? mg_minf(e->door_open[f] + speed, want)
                                                     : mg_maxf(e->door_open[f] - speed, want);
        }
        break;
    case ELEV_CLOSING:
        *car = mg_maxf(*car - speed, 0.0f);
        if (*car <= 0.0f) {
            enter(e, ELEV_FADE_OUT);
        }
        break;
    case ELEV_FADE_OUT:
        if (e->t >= FADE_TIME) {
            e->from_floor = e->car_floor;
            e->car_floor = e->target;
            e->arrived = true;
            e->rides++;
            enter(e, ELEV_HOLD);
        }
        break;
    case ELEV_HOLD:
        if (e->t >= HOLD_TIME) {
            enter(e, ELEV_FADE_IN);
        }
        break;
    case ELEV_FADE_IN:
        if (e->t >= FADE_TIME) {
            enter(e, ELEV_OPENING);
        }
        break;
    case ELEV_OPENING:
        *car = mg_minf(*car + speed, 1.0f);
        if (*car >= 1.0f) {
            enter(e, ELEV_IDLE);
        }
        break;
    }
}

bool elevator_busy(const Elevator *e) { return e->state != ELEV_IDLE; }

float elevator_fade(const Elevator *e)
{
    switch (e->state) {
    case ELEV_FADE_OUT:
        return mg_saturate(e->t / FADE_TIME);
    case ELEV_HOLD:
        return 1.0f;
    case ELEV_FADE_IN:
        return 1.0f - mg_saturate(e->t / FADE_TIME);
    default:
        return 0.0f;
    }
}
