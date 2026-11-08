/* Renderer interface. Game code talks only to this header; the SDL3 GPU
 * implementation lives in gpu.c (Metal on iOS, Vulkan on Android/Linux).
 *
 * Frame flow:
 *   gfx_frame_begin()          acquire swapchain (false = skip frame)
 *   gfx_set_scene()            camera, sun, sky
 *   gfx_draw() ...             queue meshes (fixed-capacity list, no allocs)
 *   gfx_ui_quads() ...         queue UI quads (pt coordinates)
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

/* 28-byte mesh vertex. For sprite quads, nx/ny carry the quad UV (-1..1). */
typedef struct GfxVertex {
    float px, py, pz;
    float nx, ny, nz;
    uint8_t r, g, b, a;
} GfxVertex;

/* 20-byte UI vertex; x/y in points from the top-left of the window. */
typedef struct GfxUiVertex {
    float x, y;
    float u, v;
    uint8_t r, g, b, a;
} GfxUiVertex;

typedef enum GfxMaterialKind {
    GFX_MAT_UNLIT,  /* vertex colour x tint */
    GFX_MAT_TOON,   /* 3-band cel ramp + hard rim (+ specular spot when glossy) + flat ambient */
    GFX_MAT_OUTLINE, /* inverted-hull toon outline: flat colour, mesh pushed out, front faces culled */
    GFX_MAT_FLOOR,  /* grid slab with planar reflection */
    GFX_MAT_TEXT,   /* world-space SDF glyph quads (signs), UV in the normal, alpha-blended */
    GFX_MAT_SPRITE, /* alpha-blended procedural sprite (disc / cloud / glow) */
    GFX_MAT_GHOST,  /* translucent x-ray overlay (debug colliders) */
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
    GFX_DRAW_DYNAMIC = 1u << 4,   /* moves (avatars): dirties cached shadow tiles it touches */
};

/* Shadow maps (M3). Sun/moon: 2 cascades (0-12 m, 12-45 m). Spot and
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
    float reflection_y;   /* height of the mirror plane */
    float reflection_strength; /* 0 disables the reflection pass */
    float time;
} GfxSceneParams;

typedef struct GfxStats {
    uint32_t draws;
    uint32_t reflect_draws;
    uint32_t ui_quads;
    uint32_t lights;
    uint32_t shadow_views;   /* depth views rendered this frame (cascades + tiles) */
    float shadow_cpu_ms;     /* CPU time encoding them (SDL GPU has no timestamps) */
    float main_cpu_ms;       /* CPU time encoding the main + reflection passes */
    int shadowed_lights;     /* spot/point lights with a shadow tile this frame */
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
/* UI coordinate space size in points (window pixels / display scale). */
void gfx_set_ui_size(GfxContext *g, float w_pt, float h_pt);
/* Lights for this frame (after gfx_frame_begin). False when the list is full. */
bool gfx_add_light(GfxContext *g, const GfxLight *light);
/* 4 on low-tier GPUs, 8 on high (default 8). */
void gfx_set_lights_per_draw(GfxContext *g, int n);
/* (Re)allocates shadow textures when sizes change; call outside a frame. */
bool gfx_set_shadows(GfxContext *g, const GfxShadowConfig *cfg);
/* Returns room for `quad_count` quads (4 vertices each) or NULL if full. */
GfxUiVertex *gfx_ui_quads(GfxContext *g, uint32_t quad_count);
/* Frosted panel behind this frame's UI: the 3D image under it, blurred at
 * quarter resolution, mixed towards tint.rgb by tint.a, rounded corners.
 * Drawn before (under) all UI quads. Up to 4 per frame. */
void gfx_ui_blur_panel(GfxContext *g, float x, float y, float w, float h, float radius, HMM_Vec4 tint);
/* Clip the UI quads that follow to a rect (pt) until gfx_ui_clip_off. */
void gfx_ui_clip(GfxContext *g, float x, float y, float w, float h);
void gfx_ui_clip_off(GfxContext *g);
/* Render this frame's GFX_DRAW_PREVIEW draws with a second camera into an
 * offscreen square, shown at (x, y) `size` pt wide with rounded corners,
 * over the UI quads (keep other UI out of that square). Lighting comes from
 * the main scene. */
void gfx_preview(GfxContext *g, const GfxSceneParams *camera, float x, float y, float size, float radius,
                 HMM_Vec4 background);
void gfx_frame_end(GfxContext *g);

/* Render the 3D scene at a fraction of native resolution (0.5..1) and
 * upscale; UI stays native. Cuts fill cost on older phones. */
void gfx_set_render_scale(GfxContext *g, float scale);
/* 1.0 on desktop; 0.8 on phones (tuned for iPhone 12 / Galaxy S20 class). */
float gfx_default_render_scale(void);
/* Drop GPU work while the app is in the background (iOS forbids it). */
void gfx_set_paused(GfxContext *g, bool paused);
void gfx_get_stats(const GfxContext *g, GfxStats *out);

#endif
