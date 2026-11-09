/* FRIENDS-1 A1: every account is shown as the head of its selected skin. A 1024 x 1024
 * atlas of 64 cells, LRU, keyed by a hash of the skin: same look, same cell. A head not
 * there yet is queued (at most 2 rendered per frame, nothing on frames with an empty
 * queue) and a disc with the initial stands in. Drawn as quads in the HUD batch. */
#ifndef MG_HEAD_ICONS_H
#define MG_HEAD_ICONS_H

#include "gfx/gfx.h"
#include "net/net_transport.h"
#include "text.h"

#define HEAD_ICON_CELLS 64

typedef struct HeadUv {
    bool ready;
    float u0, v0, u1, v1;
} HeadUv;

typedef struct HeadIcons {
    struct {
        uint32_t hash, last_used;
        NetSkin skin;
        uint8_t state;   /* 0 free, 1 queued, 2 ready */
        uint8_t waited;  /* frames shown as a placeholder */
    } cell[HEAD_ICON_CELLS];
    uint8_t queue[HEAD_ICON_CELLS];
    int queue_count;
    uint32_t frame;
    /* draws one head centred on the origin, facing +Z */
    void (*draw)(void *user, GfxContext *g, const NetSkin *skin);
    uint32_t (*skin_rgb)(void *user, const NetSkin *skin); /* 0xRRGGBB, for the placeholder */
    void *user;
    /* for the tests */
    uint32_t generated;
    int most_placeholder_frames;
} HeadIcons;

void head_icons_init(HeadIcons *h, void (*draw)(void *, GfxContext *, const NetSkin *),
                     uint32_t (*skin_rgb)(void *, const NetSkin *), void *user);
HeadUv head_icon_get(HeadIcons *h, const NetSkin *skin);
/* A round head (or its placeholder) of diameter `size`, at centre (cx, cy). */
void head_icon_draw(HeadIcons *h, GfxContext *g, const TextFont *f, const NetSkin *skin, const char *name, float cx, float cy,
                    float size, bool faded);
/* Once per frame, after the UI: renders what is queued. */
void head_icons_update(HeadIcons *h, GfxContext *g);
/* Memory warning / lost textures: whatever is still on screen queues itself again. */
void head_icons_clear(HeadIcons *h);
int head_icons_used(const HeadIcons *h);

#endif
