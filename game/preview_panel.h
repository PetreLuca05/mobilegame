/* M8: the bottom-right panel with a live 3D preview of the active interactable. The model and the
 * panel's background are drawn in the main pass (depth-range viewports); label and rim are UI.
 * An NPC is just a floating head, with no card: the game draws its speech cloud beside it.
 * Tap-only, through the touch targets. */
#ifndef MG_PREVIEW_PANEL_H
#define MG_PREVIEW_PANEL_H

#include "interact.h"
#include "ui/layout.h"
#include "ui/text.h"

#define PREVIEW_TAP_ID 0xFFFFFFFEu /* in TouchControls.targets */

typedef struct PreviewPanel {
    float slide;         /* 0 hidden .. 1 shown */
    float anim_dt;       /* this frame's time for the head's motion (preview_panel_update) */
    uint16_t shown;      /* slot being shown */
    float crossfade;     /* 1 just switched .. 0 */
    float punch;         /* tap feedback */
    float head_tap;      /* an NPC's head being tapped: 1 .. 0 */
    float t;
    float x, y, size;    /* where it is, pt */
    GfxMeshId rounded;   /* unit rounded rect for the background */
    AvatarAnim anim;
} PreviewPanel;

bool preview_panel_init(PreviewPanel *p, GfxContext *g, Arena *scratch);
void preview_panel_update(PreviewPanel *p, const Interact *ia, float dt, const ui_frame_t *f);
/* Call while the frame's 3D is being queued. */
void preview_panel_draw_3d(PreviewPanel *p, const Interact *ia, GfxContext *g, const AvatarMeshes *m);
/* UI: rim, label, action chip. */
void preview_panel_draw_ui(const PreviewPanel *p, const Interact *ia, GfxContext *g, const TextFont *f);
bool preview_panel_visible(const PreviewPanel *p);

#endif
