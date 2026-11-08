#include "elevator.h"

#include <math.h>

#define DOOR_TIME 0.4f
#define STOREY_TIME 0.6f

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
    float y = e->car_y;
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
    e->car_y = (float)floor * e->storey;
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
            e->from_floor = e->car_floor;
            enter(e, ELEV_MOVING);
        }
        break;
    case ELEV_MOVING: {
        int n = e->target > e->from_floor ? e->target - e->from_floor : e->from_floor - e->target;
        float k = mg_saturate(e->t / ((float)n * STOREY_TIME));
        k = k * k * (3.0f - 2.0f * k); /* ease in-out */
        e->car_y = ((float)e->from_floor + (float)(e->target - e->from_floor) * k) * e->storey;
        if (e->t >= (float)n * STOREY_TIME) {
            e->car_floor = e->target;
            e->car_y = (float)e->target * e->storey;
            e->arrived = true;
            e->rides++;
            enter(e, ELEV_OPENING);
        }
        break;
    }
    case ELEV_OPENING:
        *car = mg_minf(*car + speed, 1.0f);
        if (*car >= 1.0f) {
            enter(e, ELEV_IDLE);
        }
        break;
    }
}

bool elevator_busy(const Elevator *e) { return e->state != ELEV_IDLE; }

bool elevator_moving(const Elevator *e) { return e->state == ELEV_MOVING; }

float elevator_ride_time(int floors) { return 2.0f * DOOR_TIME + (float)floors * STOREY_TIME; }
