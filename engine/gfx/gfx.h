/* Renderer interface. Game code talks only to this header; the SDL3 GPU
 * implementation lives in gpu.c (Metal on iOS, Vulkan on Android/Linux).
 *
 * Frame flow:
 *   gfx_frame_begin()          acquire swapchain (false = skip frame)
 *   gfx_set_scene()            camera, sun, sky
 *   gfx_draw() ...             queue meshes (fixed-capacity list, no allocs)
 *   gfx_ui_push() ...          queue UI instances (pt coordinates)
 *   gfx_frame_end()            reflection pass, main pass, UI, present
 */
#ifndef MG_GFX_H
#define MG_GFX_H

#include "core/arena.h"
#include "core/mg_types.h"
#include "gfx/mesh_pool.h"

struct SDL_Window;
typedef struct GfxContext GfxContext;
typedef uint16_t GfxMeshId;
#define GFX_MESH_NONE ((GfxMeshId)0xFFFF)
#define GFX_DEFAULT_MSAA 2 /* 4x puts the iPhone over 300 MB: docs/PERF.md */

/* 28-byte mesh vertex. For sprite quads, nx/ny carry the quad UV (-1..1). */
typedef struct GfxVertex {
    float px, py, pz;
    float nx, ny, nz;
    uint8_t r, g, b, a;
} GfxVertex;

/* UI-1: one instance per UI primitive, all of a frame's UI in one instanced draw (ui.vert / ui.frag).
 * Points from the window's top-left. The quad is `rect` turned and scaled about its centre by the
 * 2 x 2 `xf` (identity 1 0 0 1; squash, stretch, lines at an angle), then clipped to `clip`.
 *   GFX_UI_GLYPH  `uv` is the SDF atlas rect, colour = face
 *   GFX_UI_SHAPE  rounded rectangle by signed distance: shape.x radius, shape.y stroke width (0 = filled),
 *                 shape.z top highlight band (pt, colour2 over the face), shape.w dashes around a circle (0 none)
 *   GFX_UI_HEAD   `uv` + 2 in the head atlas, a circle with a colour ring (head_icons.c)
 *   GFX_UI_FIGURE `uv` in the resident book figure (gfx_set_figure, gfx_figure_uv), times the colour */
enum { GFX_UI_GLYPH, GFX_UI_SHAPE, GFX_UI_HEAD, GFX_UI_FIGURE };

typedef struct GfxUiInst {
    float rect[4];  /* x, y, w, h */
    float uv[4];    /* u0, v0, u1, v1 */
    float xf[4];    /* a, b, c, d: x' = a x + c y, y' = b x + d y */
    float clip[4];  /* x0, y0, x1, y1; set by gfx from gfx_ui_clip */
    float shape[4];
    uint8_t color[4], color2[4]; /* rgba */
    float kind, fade; /* fade: set by gfx from gfx_ui_clip_fade */
    float tilt[4];    /* TILT-1: depth shift (pt, set by gfx_ui_depth), lip height L (a lip shape), flags (GFX_TILT_*), 0 */
} GfxUiInst; /* 112 bytes */
enum { GFX_TILT_GLOSS = 1, GFX_TILT_TITLE = 2 }; /* a gloss spot on the face; the title's 1.5x lip swing */

typedef enum GfxMaterialKind {
    GFX_MAT_UNLIT,  /* vertex colour x tint */
    GFX_MAT_TOON,   /* 3-band cel ramp + hard rim (+ specular spot when glossy) + flat ambient */
    GFX_MAT_OUTLINE, /* inverted-hull toon outline: flat colour, mesh pushed out, front faces culled */
    GFX_MAT_FLOOR,  /* grid slab with planar reflection */
    GFX_MAT_PATTERN,  /* the institute's building: vertex colour x tint x a detail pattern (gfx_set_patterns), world-scale UVs
                       * from the face's dominant axis; vertex alpha = pattern id | 16 lit (game/inst_patterns.h): the
                       * reference page's two-step toon light (hemisphere + sun, no shadows), or unlit without the bit. Opaque; instanced; dithered when hide > 0.
                       * Before TEXT: the main pass sorts by kind, so opaque floors go down before the blended labels and discs */
    GFX_MAT_TEXT,   /* world-space SDF glyph quads (signs), UV in the normal, alpha-blended */
    GFX_MAT_SPRITE, /* alpha-blended procedural sprite (disc / cloud / glow) */
    GFX_MAT_GHOST,  /* translucent x-ray overlay (debug colliders) */
    GFX_MAT_XRAY_RIM, /* x-ray: 2 px black rim of what is hidden behind geometry (drawn first) */
    GFX_MAT_XRAY,     /* x-ray: flat dithered fill of what is hidden behind geometry */
    GFX_MAT_DEPTH,    /* depth only, no colour: the first half of a whole-object fade (drawn after everything opaque) */
    GFX_MAT_TOON_FADE, /* toon, alpha-blended at 1 - hide over the depth GFX_MAT_DEPTH left: each pixel blends the object's
                        * nearest surface once (a true fade, no screen-door pattern) */
    GFX_MAT_ALPHA,    /* unlit vertex colour x tint, alpha tint.a x vertex alpha, blended (premultiplied), no depth write, both
                       * faces, after the avatars (bucket 3), grouped by mesh then back to front: light pools and cones, glass
                       * (LIB-1 alpha meshes); not lit, so no band to flip. params.x 1: additive (mat_alpha_add). Instanced on
                       * the main pass when hide is 0 */
    GFX_MAT_GLASS,    /* the institute's see-through toon glass (the reference page's glass shader): stepped sky reflection,
                       * sliding glints, hard rim, alpha 0.3..1; blended (premultiplied), no depth write, both faces, drawn
                       * with the dithered geometry (after the avatars), back to front; hide scales its alpha */
    GFX_MAT_COUNT
} GfxMaterialKind;

/* Sprite shapes for GFX_MAT_SPRITE (params.x) */
#define GFX_SPRITE_DISC 0.0f
#define GFX_SPRITE_CLOUD 1.0f
#define GFX_SPRITE_GLOW 2.0f

/* Lit materials (toon, floor) treat vertex alpha x tint alpha as "outdoor":
 * 1 = sun + sky, 0 = indoor lighting. */
typedef struct GfxMaterial {
    GfxMaterialKind kind;
    HMM_Vec4 tint;   /* multiplied with vertex colour */
    HMM_Vec4 params; /* per-kind, see material.c */
    float hide;      /* M5 screen-door fade: 0 drawn .. 1 gone (4x4 Bayer clip); > 0.5 casts no shadow */
    /* Zone-coded meshes (LIB-1 avatar suits; all 0 = off): outfit, trim, accent as 0xRRGGBB in floats, w a mask
     * of the zones drawn (bit z; vertex alpha = 255 - zone) | GFX_ZONE_BODY1. The vertex shader colours each
     * zone and collapses the ones not in the mask; pass a white tint (its alpha still picks the light). */
    HMM_Vec4 extra;
} GfxMaterial;
#define GFX_ZONE_BODY1 256u /* body type 1: the widest ring 8 % narrower, the top ring 6 % wider */

enum {
    GFX_DRAW_REFLECT = 1u << 0, /* also render into the planar reflection */
    GFX_DRAW_NO_MAIN = 1u << 1, /* reflection only */
    GFX_DRAW_PREVIEW = 1u << 2, /* settings preview camera only (see gfx_preview) */
    GFX_DRAW_NO_SHADOW = 1u << 3, /* never drawn into shadow maps */
    GFX_DRAW_DYNAMIC = 1u << 4,   /* avatars: drawn after the x-ray passes (and, full: dirty shadow tiles) */
    GFX_DRAW_GROUND = 1u << 5,    /* flat sprite on the ground: drawn before other sprites, in one batch */
};

#ifdef MG_RENDER_FULL /* the M3 lighting path (MG_RENDER=full, reference only) */
/* Shadow maps (M3). Sun/moon: 2 cascades (0-12 m, 12-45 m from the eye;
 * with GfxSceneParams.shadow_focus set: up to focus + 8 m, then to
 * focus + 30 m, so a top-down camera 16 m away does not waste cascade 0). Spot and
 * point lights with cast_shadow: tiles of one atlas (6 per point light). */
typedef struct GfxShadowConfig {
    bool enabled;
    int csm_size;          /* 1024 low tier, 2048 high */
    int atlas_w, atlas_h;  /* 2048x1024 low, 4096x2048 high */
    int max_spots;         /* shadowed spot lights per frame: 4 / 8 */
    int max_points;        /* shadowed point lights per frame: 1 / 2 */
    bool show_cascades;    /* debug tint per cascade */
    bool freeze;           /* debug: stop re-rendering shadow maps */
} GfxShadowConfig;

/* Point / spot light, added every frame with gfx_add_light (max
 * GFX_MAX_LIGHTS). Each draw gets the few lights that overlap its bounds
 * (gfx_set_lights_per_draw); their sum goes through the toon ramp. */
#define GFX_MAX_LIGHTS 64
typedef enum GfxLightType { GFX_LIGHT_POINT, GFX_LIGHT_SPOT } GfxLightType;
typedef struct GfxLight {
    GfxLightType type;
    HMM_Vec3 pos;
    HMM_Vec3 dir;          /* spot: unit, pointing out of the lamp */
    float range;           /* metres; contribution is 0 beyond */
    HMM_Vec3 color;        /* linear-ish 0..1 */
    float intensity;
    float cos_inner, cos_outer; /* spot cone (cosines of half-angles) */
    bool cast_shadow;
    uint32_t id;           /* stable per light (shadow caching) */
} GfxLight;
#endif


typedef struct GfxSceneParams {
    HMM_Mat4 view;
    HMM_Mat4 proj;
    HMM_Vec3 eye;
    HMM_Vec3 sun_dir;     /* direction towards the sun, normalised */
    HMM_Vec3 sun_color;
    HMM_Vec3 ambient;
    HMM_Vec4 sky_horizon;
    HMM_Vec4 sky_zenith;
    HMM_Vec3 fog_color;
    float fog_density;    /* per metre, 0 = none */
    HMM_Vec4 sun_disc;    /* xyz direction, w visibility (sky) */
    HMM_Vec4 moon_disc;
    float stars;          /* 0..1 */
    float two_band;       /* LIB-1, 0..1: towards the library prototype's toon ramp (lit at n.l >= 0, else 105/255 of the sun); blends */
    /* A second light set and the mix towards it (0 = only the one above): each surface is lit with both and the results
     * mixed, so a change of light (the library -> the map) never flips a toon band. */
    HMM_Vec3 sun2_dir, sun2_color, ambient2;
    float two_band2, light_mix;
#ifdef MG_RENDER_FULL
    float reflection_y;   /* height of the mirror plane */
    float shadow_focus;   /* eye-to-player distance for the cascade splits, 0 = fixed 12 / 45 m */
    float reflection_strength; /* 0 disables the reflection pass */
#endif
    float time;
} GfxSceneParams;

/* Triangle categories for the perf counters (what a frame's triangles are): set the current one around a group of draws */
enum { GFX_CAT_OTHER, GFX_CAT_ROOM, GFX_CAT_PROP, GFX_CAT_AVATAR, GFX_CATS };

typedef struct GfxStats {
    uint32_t draws;
    uint32_t reflect_draws;
    uint32_t ui_quads;
    uint32_t ui_draws;       /* UI-1: the whole UI is one instanced draw */
    uint32_t head_passes;    /* of `passes`: the head-icon atlas (a new head rendered this frame) */
#ifdef MG_RENDER_FULL
    uint32_t lights;
    uint32_t shadow_views;   /* depth views rendered this frame (cascades + tiles) */
    float shadow_cpu_ms;     /* CPU time encoding them (SDL GPU has no timestamps) */
    int shadowed_lights;     /* spot/point lights with a shadow tile this frame */
#endif
    float main_cpu_ms;       /* CPU time encoding the main pass */
    uint32_t tris, instances, pipe_binds, tex_binds, uniform_bytes, passes;
    uint32_t tris_cat[GFX_CATS]; /* tris by GFX_CAT_* (the main pass; the UI counts as other) */
    uint32_t fade_draws, fade_violations; /* whole-object fade draws taken / refused (outside gfx_allow_fade) */
    uint32_t fade_binds;   /* binds of the whole-object fade pipelines (GFX_MAT_DEPTH / GFX_MAT_TOON_FADE) in the main pass */
    uint32_t light2_draws; /* main-pass draws with the second light set on (GfxSceneParams.light_mix > 0: u_sun2_dir.w > 0) */
    /* Approximate GPU time, submit -> fence on a helper thread (SDL GPU has no
     * timestamps; includes queueing). 0 until gfx_set_gpu_timing. */
    float gpu_main_ms, gpu_ui_ms;
    uint32_t width_px, height_px;
    int msaa;
    const char *backend; /* "metal", "vulkan", ... */
} GfxStats;

bool gfx_init(GfxContext **out, struct SDL_Window *window, Arena *perm);
void gfx_shutdown(GfxContext *g);

/* Mesh upload; init-time only (uses a transfer buffer + copy pass). */
GfxMeshId gfx_mesh_upload(GfxContext *g, const GfxVertex *verts, uint32_t vert_count,
                          const uint32_t *indices, uint32_t index_count);
/* LIB-1 cross-dissolve: keep this frame's 3D image (copied at the end of the main pass); then blend it over
 * the live 3D image by `alpha` (1 = all captured, 0 = off) until set back to 0. */
void gfx_dissolve_capture(GfxContext *g);
void gfx_dissolve(GfxContext *g, float alpha);
/* Upload the single-channel SDF UI atlas (font + shapes). */
bool gfx_set_ui_atlas(GfxContext *g, const uint8_t *pixels, uint32_t w, uint32_t h);
/* Detail patterns for GFX_MAT_PATTERN (the institute's textures): `layers` square single-channel images of `size` px,
 * each with its whole mip chain (size, size / 2 .. 1, tightly packed), layer after layer; one 2D array texture with
 * a repeating, trilinear, anisotropic sampler. Uploaded once (load time); later calls keep the first. */
bool gfx_set_patterns(GfxContext *g, const uint8_t *data, uint32_t size, uint32_t layers, uint32_t levels);
bool gfx_has_patterns(const GfxContext *g);

bool gfx_frame_begin(GfxContext *g);
void gfx_set_scene(GfxContext *g, const GfxSceneParams *scene);
/* The category the next draws count their triangles under (GFX_CAT_*); returns the previous one. */
int gfx_set_category(GfxContext *g, int cat);
/* The whole-object fade (GFX_MAT_DEPTH / GFX_MAT_TOON_FADE) is allowed only while this is on: the library's shell on the
 * Maps move. Anything else is opaque: such a draw is dropped, logged once and counted (GfxStats.fade_violations; the
 * perf gate fails on it). */
void gfx_allow_fade(GfxContext *g, bool on);
/* Test only (--gpu-off, GPU breakdowns): a filter on what gfx_draw queues; the caller's CPU work is unchanged. Bit k (k <
 * GFX_MAT_COUNT) drops draws of GfxMaterialKind k, GFX_FILTER_ALL drops every draw, GFX_FILTER_FLAT queues toon and pattern
 * draws outside GFX_CAT_AVATAR as GFX_MAT_UNLIT (vertex colour x tint). 0 = off. Returns the previous filter. */
enum { GFX_FILTER_FLAT = 1u << 30, GFX_FILTER_ALL = 1u << 31 };
uint32_t gfx_set_filter(GfxContext *g, uint32_t filter);
/* draws queued so far this frame (counting one system's draws: the difference around them) */
uint32_t gfx_queued_draws(const GfxContext *g);
/* Once after a batch of mesh uploads (the library at start, the map's preload, the hospital's build): waits for the GPU
 * (0.2-0.8 ms on the Mac) so the uploads' staging buffers are freed now. Otherwise Metal keeps them as reclaimable
 * driver memory that still counts in the footprint for a while: 5-12 MB, run to run. */
void gfx_settle_uploads(GfxContext *g);
/* A mesh that is rebuilt now and then (LIB-1: a lobby's table dressing) without growing the pools: reserve room once,
 * then update it in place (it waits for the GPU: not for every frame). Draws nothing until the first update. */
GfxMeshId gfx_mesh_reserve(GfxContext *g, uint32_t max_verts, uint32_t max_indices);
/* Level scenes: the pools are linear, so meshes are freed as a stack. Take a mark before a level's meshes go in;
 * gfx_mesh_reset_to(mark) drops every mesh uploaded after it (waits for the GPU first; their ids are invalid from
 * then on, gfx_draw ignores them) and keeps every one below it (UI, avatar, library, world map). False on a stale
 * mark (the pool is already below it). */
GfxMeshMark gfx_mesh_mark(const GfxContext *g);
/* A mesh's bounds in its own space (gfx_mesh_upload computes them); false for none / a reserved mesh never filled. */
bool gfx_mesh_bounds(const GfxContext *g, GfxMeshId id, HMM_Vec3 *lo, HMM_Vec3 *hi);
bool gfx_mesh_reset_to(GfxContext *g, GfxMeshMark mark);
void gfx_mesh_usage(const GfxContext *g, uint32_t *meshes, uint32_t *vb_bytes, uint32_t *ib_bytes);
bool gfx_mesh_update(GfxContext *g, GfxMeshId id, const GfxVertex *verts, uint32_t vert_count, const uint32_t *indices, uint32_t index_count);
void gfx_draw(GfxContext *g, GfxMeshId mesh, const HMM_Mat4 *model, const GfxMaterial *mat,
              uint32_t flags);
/* A slice of a mesh: `first` and `count` are indices inside it. */
void gfx_draw_range(GfxContext *g, GfxMeshId mesh, uint32_t first, uint32_t count, const HMM_Mat4 *model, const GfxMaterial *mat,
                    uint32_t flags);
/* A small 3D picture inside the main pass: the gfx_draw calls between begin and end go to this
 * screen rect (pt) with their own camera and depth range. before_world: drawn ahead of the world, so
 * the tile GPU can reject what it covers. Up to 4 per frame. */
void gfx_inset_begin(GfxContext *g, float x, float y, float w, float h, const HMM_Mat4 *view_proj, HMM_Vec3 eye, float min_depth, float max_depth,
                     bool before_world);
void gfx_inset_end(GfxContext *g);
/* UI coordinate space size in points (window pixels / display scale). */
void gfx_set_ui_size(GfxContext *g, float w_pt, float h_pt);
#ifdef MG_RENDER_FULL
/* Lights for this frame (after gfx_frame_begin). False when the list is full. */
bool gfx_add_light(GfxContext *g, const GfxLight *light);
/* 4 on low-tier GPUs, 8 on high (default 8). */
void gfx_set_lights_per_draw(GfxContext *g, int n);
/* (Re)allocates shadow textures when sizes change; call outside a frame. */
bool gfx_set_shadows(GfxContext *g, const GfxShadowConfig *cfg);
#endif
/* Queue one UI instance (its clip comes from gfx_ui_clip); false when the frame's UI is full. */
bool gfx_ui_push(GfxContext *g, const GfxUiInst *inst);
/* A group transform for the instances that follow (a widget's squash: lip, face and label together):
 * x' = o + M (x - o) with M = (a c; b d). gfx_ui_group_off() ends it. */
void gfx_ui_group(GfxContext *g, float a, float b, float c, float d, float ox, float oy);
void gfx_ui_group_off(GfxContext *g);
/* The screen layer over everything (enter / exit slides and fades, the UI shake): offset and alpha. */
void gfx_ui_screen(GfxContext *g, float dx, float dy, float alpha);
/* The UI's size in points (gfx_set_ui_size). */
void gfx_ui_size(const GfxContext *g, float *w_pt, float *h_pt);
/* Head icons (ui/head_icons.c): draws between begin and end land in one cell of the
 * 8 x 8 atlas, through a small offscreen pass that only exists on frames that use it.
 * In UI quads, u + 2 samples this atlas. */
#define GFX_HEAD_PX 128
#define GFX_HEADS_PER_FRAME 2
void gfx_head_cell_begin(GfxContext *g, int cell, const HMM_Mat4 *view_proj, HMM_Vec3 eye);
#define gfx_head_cell_end gfx_inset_end
/* BOOK-ENGINE figures (decision 7): one figure texture resident at a time, sampled by GFX_UI_FIGURE quads inside the one
 * UI draw (a third texture of ui.frag). The texture is made once at its largest size (gfx_figure_reserve, at startup) and a
 * figure goes into its top-left corner through a fixed 2 MB staging buffer (RGBA8 larger than that goes up in strips of
 * rows): no allocation when the figure changes. Data is top row first; ASTC 4x4 = ceil(w/4) x ceil(h/4) blocks of 16 bytes.
 * gfx_figure_reserve returns false (and logs) when the GPU cannot sample the format: draw a placeholder instead. */
enum { GFX_FIG_RGBA8 = 0, GFX_FIG_ASTC4 = 1 };
#define GFX_FIG_STAGING_BYTES (2u * 1024u * 1024u)
bool gfx_figure_reserve(GfxContext *g, int max_w, int max_h, int format);
bool gfx_set_figure(GfxContext *g, int w, int h, int format, const void *data, uint32_t size);
/* The resident figure's corner in uv (u1, v1: its share of the texture); false when none is resident. */
bool gfx_figure_uv(const GfxContext *g, float *u1, float *v1);
/* Clip the UI instances that follow to a rect (pt) until gfx_ui_clip_off (in the shader: no extra draw). */
void gfx_ui_clip(GfxContext *g, float x, float y, float w, float h);
void gfx_ui_clip_off(GfxContext *g);
/* With a clip on: dissolve the instances over the last `pt` inside its edges (0 = hard edge); cleared by clip_off. */
void gfx_ui_clip_fade(GfxContext *g, float pt);
/* TILT-1: the tilt for this frame's UI (tx, ty already x strength; s = the strength) and the safe rect;
 * the depth shift and flags for the instances that follow; an extra offset (a pressed control's frozen shift). */
void gfx_ui_set_tilt(GfxContext *g, float tx, float ty, float s, const float safe[4]);
void gfx_ui_depth(GfxContext *g, float shift_pt, int flags);
void gfx_ui_offset(GfxContext *g, float dx, float dy);
void gfx_ui_lip(GfxContext *g, float lip); /* instances that follow are a lip of this height (0: none) */
/* This frame's UI instances so far (autotests read back what a widget emitted). */
const GfxUiInst *gfx_ui_instances(const GfxContext *g, uint32_t *count);
/* Render this frame's GFX_DRAW_PREVIEW draws with a second camera into an
 * offscreen square, shown at (x, y) `size` pt wide with rounded corners,
 * over the UI quads (keep other UI out of that square). Lighting comes from
 * the main scene. */
void gfx_preview(GfxContext *g, const GfxSceneParams *camera, float x, float y, float size, float radius,
                 HMM_Vec4 background);
void gfx_frame_end(GfxContext *g);
/* Save the next finished frame (3D + UI, native size) as a PNG at `path`
 * (GPU readback; stalls that frame). Debug / autotests. */
void gfx_request_screenshot(GfxContext *g, const char *path);

/* Render the 3D scene at a fraction of native resolution (0.5..1) and
 * upscale; UI stays native. Cuts fill cost on older phones. */
void gfx_set_render_scale(GfxContext *g, float scale);
/* 1.0 on desktop; 0.8 on phones (tuned for iPhone 12 / Galaxy S20 class). */
float gfx_default_render_scale(void);
/* Drop GPU work while the app is in the background (iOS forbids it). */
void gfx_set_paused(GfxContext *g, bool paused);
void gfx_get_stats(const GfxContext *g, GfxStats *out);
/* What the renderer holds, in bytes (the swapchain is an estimate: three drawables). */
typedef struct GfxMemory {
    uint64_t msaa_color, depth, scene, blur, preview, swapchain, ui_atlas;
    uint64_t mesh_vb_reserved, mesh_ib_reserved, mesh_vb_used, mesh_ib_used;
    uint64_t ui_buffers, instance_buffers;
    uint64_t figure, figure_staging; /* BOOK-ENGINE: the resident figure texture and its staging buffer */
    uint64_t update_staging;         /* gfx_mesh_update's reserved staging buffer */
} GfxMemory;
void gfx_get_memory(const GfxContext *g, GfxMemory *out);
/* Submit 3D and UI separately with fences to time them (profiler / perf runs). */
void gfx_set_gpu_timing(GfxContext *g, bool on);
/* Main pass samples: 1, 2 or 4 (1 if the GPU lacks it); applied at the end of this frame. */
void gfx_set_msaa(GfxContext *g, int samples);
/* Default on; off draws every mesh separately (--no-instancing, to measure the saving). */
void gfx_set_instancing(GfxContext *g, bool on);

#endif
