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
 *   GFX_UI_HEAD   `uv` + 2 in the head atlas, a circle with a colour ring (head_icons.c) */
enum { GFX_UI_GLYPH, GFX_UI_SHAPE, GFX_UI_HEAD };

typedef struct GfxUiInst {
    float rect[4];  /* x, y, w, h */
    float uv[4];    /* u0, v0, u1, v1 */
    float xf[4];    /* a, b, c, d: x' = a x + c y, y' = b x + d y */
    float clip[4];  /* x0, y0, x1, y1; set by gfx from gfx_ui_clip */
    float shape[4];
    uint8_t color[4], color2[4]; /* rgba */
    float kind, pad;
} GfxUiInst; /* 96 bytes */

typedef enum GfxMaterialKind {
    GFX_MAT_UNLIT,  /* vertex colour x tint */
    GFX_MAT_TOON,   /* 3-band cel ramp + hard rim (+ specular spot when glossy) + flat ambient */
    GFX_MAT_OUTLINE, /* inverted-hull toon outline: flat colour, mesh pushed out, front faces culled */
    GFX_MAT_FLOOR,  /* grid slab with planar reflection */
    GFX_MAT_TEXT,   /* world-space SDF glyph quads (signs), UV in the normal, alpha-blended */
    GFX_MAT_SPRITE, /* alpha-blended procedural sprite (disc / cloud / glow) */
    GFX_MAT_GHOST,  /* translucent x-ray overlay (debug colliders) */
    GFX_MAT_XRAY_RIM, /* x-ray: 2 px black rim of what is hidden behind geometry (drawn first) */
    GFX_MAT_XRAY,     /* x-ray: flat dithered fill of what is hidden behind geometry */
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
} GfxMaterial;

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
#ifdef MG_RENDER_FULL
    float reflection_y;   /* height of the mirror plane */
    float shadow_focus;   /* eye-to-player distance for the cascade splits, 0 = fixed 12 / 45 m */
    float reflection_strength; /* 0 disables the reflection pass */
#endif
    float time;
} GfxSceneParams;

typedef struct GfxStats {
    uint32_t draws;
    uint32_t reflect_draws;
    uint32_t ui_quads;
#ifdef MG_RENDER_FULL
    uint32_t lights;
    uint32_t shadow_views;   /* depth views rendered this frame (cascades + tiles) */
    float shadow_cpu_ms;     /* CPU time encoding them (SDL GPU has no timestamps) */
    int shadowed_lights;     /* spot/point lights with a shadow tile this frame */
#endif
    float main_cpu_ms;       /* CPU time encoding the main pass */
    uint32_t tris, instances, pipe_binds, tex_binds, uniform_bytes, passes;
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
/* Upload the single-channel SDF UI atlas (font + shapes). */
bool gfx_set_ui_atlas(GfxContext *g, const uint8_t *pixels, uint32_t w, uint32_t h);

bool gfx_frame_begin(GfxContext *g);
void gfx_set_scene(GfxContext *g, const GfxSceneParams *scene);
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
/* Clip the UI instances that follow to a rect (pt) until gfx_ui_clip_off (in the shader: no extra draw). */
void gfx_ui_clip(GfxContext *g, float x, float y, float w, float h);
void gfx_ui_clip_off(GfxContext *g);
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
} GfxMemory;
void gfx_get_memory(const GfxContext *g, GfxMemory *out);
/* Submit 3D and UI separately with fences to time them (profiler / perf runs). */
void gfx_set_gpu_timing(GfxContext *g, bool on);
/* Main pass samples: 1, 2 or 4 (1 if the GPU lacks it); applied at the end of this frame. */
void gfx_set_msaa(GfxContext *g, int samples);
/* Default on; off draws every mesh separately (--no-instancing, to measure the saving). */
void gfx_set_instancing(GfxContext *g, bool on);

#endif
