/* Raw input state: touches in points (pt), keyboard, text. Touches from a
 * mouse on desktop arrive through SDL_HINT_MOUSE_TOUCH_EVENTS. */
#ifndef MG_INPUT_H
#define MG_INPUT_H

#include <SDL3/SDL_events.h>

#include "core/mg_types.h"

#define MG_MAX_TOUCHES 10
#define MG_TOUCH_FREE (-1)

typedef struct MgTouch {
    SDL_FingerID id;
    bool active;  /* finger is down */
    bool began;   /* went down this frame */
    bool ended;   /* lifted this frame (slot freed next frame) */
    float x, y;   /* current position, pt from top-left */
    float x0, y0; /* where it went down */
    float dx, dy; /* movement since last frame */
    int owner;    /* which control claimed it, MG_TOUCH_FREE if none */
} MgTouch;

typedef struct MgInput {
    MgTouch touches[MG_MAX_TOUCHES];
    bool key_down[SDL_SCANCODE_COUNT];
    bool key_pressed[SDL_SCANCODE_COUNT]; /* edge, this frame */
    float wheel;
    float mouse_dx, mouse_dy; /* relative motion with right button held (desktop camera) */
    bool mouse_right;
    char text[32]; /* text typed this frame (ASCII subset) */
    int text_len;
} MgInput;

void input_begin_frame(MgInput *in);
void input_handle_event(MgInput *in, const SDL_Event *e, float width_pt, float height_pt);

#endif
