/* M8: the lift's 3D button panel, a floating 3D rectangle in the bottom right of the safe area while the player
 * stands in the car. Drawn in the main pass ahead of the world (a depth-range inset); the column of
 * buttons comes from the lift's floor count. Tap-only through the touch targets. */
#ifndef MG_ELEVATOR_PANEL_H
#define MG_ELEVATOR_PANEL_H

#include "gfx/gfx.h"
#include "ui/layout.h"
#include "ui/text.h"

#define LIFT_PANEL_FLOORS 8
#define LIFT_PANEL_TAP_ID 0xFFFFFF00u /* + floor, in TouchControls.targets */

typedef struct ElevatorPanel {
    GfxMeshId slab, button, ring;
    GfxMeshId label[LIFT_PANEL_FLOORS]; /* one per floor, 1 unit tall */
    int floors;
    float button_y[LIFT_PANEL_FLOORS]; /* on the slab's face, -0.5 .. 0.5 */
    float button_r;      /* base_r, less on big screens (clamped in pt) */
    float base_r;
    float press_anim[LIFT_PANEL_FLOORS]; /* 1 just pressed .. 0 */
    float slide;         /* 0 hidden .. 1 shown */
    float x, y, w, h;    /* the area the slab is drawn in, pt */
    int pressed;         /* -1 none */
    float press_t;       /* time until the ride starts */
    float blink_t;       /* tapped the floor we are on */
} ElevatorPanel;

/* name: a floor's name, its first letter goes on the button */
bool elevator_panel_init(ElevatorPanel *p, GfxContext *g, Arena *scratch, int floors, const TextFont *f, const char *(*name)(int));
void elevator_panel_update(ElevatorPanel *p, float dt, bool open, const ui_frame_t *f);
bool elevator_panel_visible(const ElevatorPanel *p);
/* A button's centre on screen, pt */
void elevator_panel_button_pos(const ElevatorPanel *p, int floor, float *x, float *y);
/* A button's radius on screen, pt */
float elevator_panel_button_radius(const ElevatorPanel *p);
/* A press: the button sinks and lights. The ride starts once its 0.4 s are up (see _take). */
void elevator_panel_press(ElevatorPanel *p, int floor, int current_floor);
/* The floor whose press has come due, -1 if none. */
int elevator_panel_take(ElevatorPanel *p);
void elevator_panel_draw_3d(const ElevatorPanel *p, GfxContext *g, int current_floor);

#endif
