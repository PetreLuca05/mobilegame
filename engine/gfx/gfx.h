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
    GFX_MAT_TOON,   /* 2-step ramp + rim + flat ambient */
    GFX_MAT_FLOOR,  /* grid slab with planar reflection */
    GFX_MAT_SPRITE, /* alpha-blended procedural sprite (disc / cloud / glow) */
    GFX_MAT_GHOST,  /* translucent x-ray overlay (debug colliders) */
    GFX_MAT_COUNT
} GfxMaterialKind;

/* Sprite shapes for GFX_MAT_SPRITE (params.x) */
#define GFX_SPRITE_DISC 0.0f
#define GFX_SPRITE_CLOUD 1.0f
#define GFX_SPRITE_GLOW 2.0f

typedef struct GfxMaterial {
    GfxMaterialKind kind;
    HMM_Vec4 tint;   /* multiplied with vertex colour */
    HMM_Vec4 params; /* per-kind, see material.c */
} GfxMaterial;

enum {
    GFX_DRAW_REFLECT = 1u << 0, /* also render into the planar reflection */
    GFX_DRAW_NO_MAIN = 1u << 1, /* reflection only */
};

typedef struct GfxSceneParams {
    HMM_Mat4 view;
    HMM_Mat4 proj;
    HMM_Vec3 eye;
    HMM_Vec3 sun_dir;     /* direction towards the sun, normalised */
    HMM_Vec3 sun_color;
    HMM_Vec3 ambient;
    HMM_Vec4 sky_horizon;
    HMM_Vec4 sky_zenith;
    float reflection_y;   /* height of the mirror plane */
    float reflection_strength; /* 0 disables the reflection pass */
    float time;
} GfxSceneParams;

typedef struct GfxStats {
    uint32_t draws;
    uint32_t reflect_draws;
    uint32_t ui_quads;
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
/* Returns room for `quad_count` quads (4 vertices each) or NULL if full. */
GfxUiVertex *gfx_ui_quads(GfxContext *g, uint32_t quad_count);
void gfx_frame_end(GfxContext *g);

/* Render the 3D scene at a fraction of native resolution (0.5..1) and
 * upscale; UI stays native. Cuts fill cost on older phones. */
void gfx_set_render_scale(GfxContext *g, float scale);
/* Drop GPU work while the app is in the background (iOS forbids it). */
void gfx_set_paused(GfxContext *g, bool paused);
void gfx_get_stats(const GfxContext *g, GfxStats *out);

#endif
