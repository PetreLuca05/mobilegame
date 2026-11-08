/* M8: the bottom-right panel with a live 3D preview of the active interactable. The model and the
 * panel's background are drawn in the main pass (depth-range viewports); label and rim are UI.
 * Tap-only, through the touch targets. */
#ifndef MG_PREVIEW_PANEL_H
#define MG_PREVIEW_PANEL_H

#include "interact.h"
#include "ui/layout.h"
#include "ui/text.h"

#define PREVIEW_TAP_ID 0xFFFFFFFEu /* in TouchControls.targets */

typedef struct PreviewPanel {
    float slide;         /* 0 hidden .. 1 shown */
    uint16_t shown;      /* slot being shown */
    float crossfade;     /* 1 just switched .. 0 */
    float punch;         /* tap feedback */
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
