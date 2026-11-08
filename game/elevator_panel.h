/* M8: the lift's 3D button panel, a strip down the left of the safe area while the player stands in
 * the car. Drawn in the main pass ahead of the world (a depth-range inset); the column comes from the
 * lift's floor count. Tap-only through the touch targets. */
#ifndef MG_ELEVATOR_PANEL_H
#define MG_ELEVATOR_PANEL_H

#include "gfx/gfx.h"
#include "ui/layout.h"
#include "ui/text.h"

#define LIFT_PANEL_FLOORS 8
#define LIFT_PANEL_TAP_ID 0xFFFFFF00u /* + floor, in TouchControls.targets */

typedef struct ElevatorPanel {
    GfxMeshId plate, buttons, ring;
    int floors;
    float button_y[LIFT_PANEL_FLOORS]; /* local, -0.5 .. 0.5 */
    float button_r;
    float slide;         /* 0 hidden .. 1 shown */
    float x, y, w, h;    /* the strip, pt */
    int pressed;         /* -1 none */
    float press_t;       /* time until the ride starts */
    float blink_t;       /* tapped the floor we are on */
} ElevatorPanel;

bool elevator_panel_init(ElevatorPanel *p, GfxContext *g, Arena *scratch, int floors);
void elevator_panel_update(ElevatorPanel *p, float dt, bool open, const ui_frame_t *f);
bool elevator_panel_visible(const ElevatorPanel *p);
/* A button's centre on screen, pt */
void elevator_panel_button_pos(const ElevatorPanel *p, int floor, float *x, float *y);
/* A press: lights the button. Returns the floor to ride to once its 0.4 s are up (see _take). */
void elevator_panel_press(ElevatorPanel *p, int floor, int current_floor);
/* The floor whose press has come due, -1 if none. */
int elevator_panel_take(ElevatorPanel *p);
void elevator_panel_draw_3d(const ElevatorPanel *p, GfxContext *g, int current_floor);
void elevator_panel_draw_ui(const ElevatorPanel *p, GfxContext *g, const TextFont *f, int current_floor, const char *(*name)(int floor));

#endif
