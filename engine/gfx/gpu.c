/* SDL3 GPU implementation of gfx.h.
 *
 * Passes per frame:
 *   copy pass        UI vertices for this frame
 *   reflection pass  half-res mirrored scene (sky + GFX_DRAW_REFLECT draws)
 *   main pass        sky, opaque by material, floor (samples reflection),
 *                    sprites back-to-front, UI; 4x MSAA resolved to swapchain
 */
#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>

#include "core/assets.h"
#include "core/log.h"
#include "core/png_write.h"
#include "core/prof.h"
#include "core/time.h"
#include "gfx.h"
#include "material.h"
#ifdef MG_RENDER_FULL
#include "reflection.h"
#include "shadows.h"
#endif

#define GFX_MAX_MESHES 512
#define GFX_MAX_DRAWS 16384
#define GFX_VB_BYTES (4u << 20)
#define GFX_IB_BYTES (2u << 20)
#define GFX_MAX_UI 8192 /* UI instances per frame (glyphs + shapes + heads) */
#define GFX_MAX_SHADERS 32
#define GFX_PREVIEW_PX 512

typedef struct GfxMeshRec {
    uint32_t first_index;
    uint32_t index_count;
    int32_t vertex_offset;
    HMM_Vec3 bmin, bmax; /* model-space bounds, for light selection */
} GfxMeshRec;

#define GFX_LIGHTS_PER_DRAW 8
#define GFX_SHADOW_SLOTS 20 /* atlas tiles: spots, then 6 per point light */
#define CSM_SPLIT0 12.0f
#define CSM_SPLIT1 45.0f

typedef struct DrawItem {
    HMM_Mat4 model;
    HMM_Vec4 tint;
    HMM_Vec4 params;
    HMM_Vec4 extra;
    float hide;
    float sort;
    uint32_t range_first, range_count; /* count 0 = the whole mesh */
    uint8_t inset;   /* 0 the world, else the inset it belongs to, + 1 */
    GfxMeshId mesh;
    uint8_t kind;
    uint8_t flags;
#ifdef MG_RENDER_FULL
    int8_t lights[GFX_LIGHTS_PER_DRAW]; /* indices into the frame's light list, -1 = none */
#endif
} DrawItem;

/* Per-instance data of the main pass (shaders/inst.hlsli) */
typedef struct GfxInstance {
    HMM_Vec4 row[3];
    HMM_Vec4 tint;
    HMM_Vec4 params; /* x rim, y gloss, z emissive (< 0 flat colour), w hide */
    HMM_Vec4 extra;  /* GfxMaterial.extra */
} GfxInstance;
#define GFX_MAX_INSTANCES 4096
#define GFX_BUFFER_RING 4 /* more than the frames in flight: uploads never need SDL to allocate another buffer */

typedef struct DrawVSUniforms {
    HMM_Mat4 model;
    HMM_Mat4 normal;
    HMM_Vec4 extra; /* GfxMaterial.extra (zone-coded meshes) */
} DrawVSUniforms;

typedef struct DrawFSUniforms {
    HMM_Vec4 tint;
    HMM_Vec4 params;
    HMM_Vec4 lights[2]; /* 8 light indices as floats, -1 = none */
    HMM_Vec4 misc;      /* x hide (screen-door fade) */
} DrawFSUniforms;

#ifdef MG_RENDER_FULL
/* fragment b2: the frame's light list (see shaders/lighting.hlsli) */
typedef struct LightsFSUniforms {
    HMM_Vec4 count;                     /* x = lights */
    HMM_Vec4 pos_range[GFX_MAX_LIGHTS]; /* xyz position, w range */
    HMM_Vec4 color_type[GFX_MAX_LIGHTS];/* rgb * intensity, w 0 point / 1 spot */
    HMM_Vec4 dir_cone[GFX_MAX_LIGHTS];  /* xyz spot direction, w cos outer */
    HMM_Vec4 extra[GFX_MAX_LIGHTS];     /* x cos inner, y shadow slot (-1), z 1 = point (6 slots) */
    HMM_Mat4 csm_vp[2];
    HMM_Vec4 csm_params;                /* x cascade 0 far, y cascade 1 far, z 1/size, w enabled */
    HMM_Vec4 csm_fwd;                   /* xyz camera forward, w show cascades */
    HMM_Mat4 shadow_vp[GFX_SHADOW_SLOTS];
    HMM_Vec4 shadow_rect[GFX_SHADOW_SLOTS]; /* atlas uv offset xy, scale zw */
    HMM_Vec4 atlas_params;              /* xy 1/atlas size */
} LightsFSUniforms;

#endif /* MG_RENDER_FULL */

typedef struct FrameFSUniforms {
    HMM_Vec4 sun_dir;
    HMM_Vec4 sun_color;
    HMM_Vec4 ambient;
    HMM_Vec4 eye;
    HMM_Vec4 misc;
    HMM_Vec4 fog;
} FrameFSUniforms;

typedef struct SkyFSUniforms {
    HMM_Mat4 inv_view_proj;
    HMM_Vec4 eye;
    HMM_Vec4 horizon;
    HMM_Vec4 zenith;
    HMM_Vec4 sun, moon, stars;
} SkyFSUniforms;

#ifdef MG_RENDER_FULL
enum { TARGET_MAIN, TARGET_REFL, TARGET_COUNT };
#else
enum { TARGET_MAIN, TARGET_COUNT }; /* minimal: no reflection pass */
#endif
enum { LAYOUT_NONE, LAYOUT_MESH, LAYOUT_UI };

typedef struct PipelineSet {
    SDL_GPUGraphicsPipeline *mat[GFX_MAT_COUNT];
    SDL_GPUGraphicsPipeline *xray_always[2]; /* rim, fill with depth test ALWAYS */
    SDL_GPUGraphicsPipeline *sky;
} PipelineSet;


typedef struct ShaderCacheEntry {
    const char *name;
    SDL_GPUShader *shader;
} ShaderCacheEntry;

#define GPU_TIMING_RING 8
#define GFX_MAX_INSETS 6
typedef struct GfxInset {
    float x, y, w, h;      /* pt */
    HMM_Mat4 view_proj;
    HMM_Vec3 eye;
    float min_depth, max_depth;
    bool before_world;
    int head_cell;         /* >= 0: a head icon for this atlas cell (offscreen, not the main pass) */
} GfxInset;

typedef struct GpuTimingSlot {
    SDL_GPUFence *main_fence, *ui_fence;
    double submitted;
} GpuTimingSlot;

struct GfxContext {
    bool gpu_timing;
    SDL_Thread *timing_thread;
    SDL_Semaphore *timing_sem;
    SDL_AtomicInt timing_quit, timing_head, timing_tail;
    SDL_AtomicInt gpu_main_us, gpu_ui_us;
    GpuTimingSlot timing[GPU_TIMING_RING];
    SDL_GPUDevice *dev;
    SDL_Window *win;
    SDL_GPUShaderFormat shader_fmt;
    const char *backend;
    SDL_GPUTextureFormat color_fmt;
    SDL_GPUTextureFormat depth_fmt;
    SDL_GPUTextureFormat refl_fmt;
    SDL_GPUSampleCount msaa;
    bool no_instancing;
    uint64_t ui_atlas_bytes;
    GfxInset insets[GFX_MAX_INSETS];
    int inset_count, current_inset; /* current: index + 1, 0 none */
    int msaa_wanted;     /* gfx_set_msaa, applied at the next frame end; -1 none */

    PipelineSet pipes[TARGET_COUNT];
    SDL_GPUGraphicsPipeline *ui_pipe;        /* UI inside the MSAA main pass */
    SDL_GPUGraphicsPipeline *ui_pipe_direct; /* UI pass on the swapchain (render scale < 1) */
    SDL_GPUGraphicsPipeline *present_pipe;   /* upscales the 3D image inside the UI pass */
    /* LIB-1: a cross-dissolve from a captured frame (Reduced motion's Maps / Back) */
    SDL_GPUTexture *dissolve_tex;
    uint32_t dissolve_w, dissolve_h;
    bool dissolve_capture;  /* copy this frame's 3D image at the end of the main pass */
    float dissolve_alpha;   /* > 0: present blends the captured image over the live one */
    bool passes_logged;
    SDL_GPUGraphicsPipeline *panel_pipe;     /* the settings preview composite (rounded, on the swapchain) */
    bool keep_scaled; /* once a frame needed the offscreen scene, keep its textures (no target churn); passes stay per frame */
    float ui_clip[4];     /* x0, y0, x1, y1 for the instances that follow; x1 < x0 = none */
    float ui_clip_fade;   /* pt dissolved inside the clip's edges */
    float ui_tilt[3];     /* TILT-1 this frame: tx, ty (x strength), strength */
    float ui_depth;       /* depth shift (pt) of the instances that follow */
    int ui_tilt_flags;
    float ui_offset[2];
    float ui_lip;
    float ui_safe[4];
    float ui_group[6];    /* a, b, c, d, ox, oy */
    bool ui_group_on;
    float ui_screen[3];   /* dx, dy, alpha */
    /* settings preview: second camera into an offscreen square */
    bool preview_on;
    bool preview_lit; /* false: preview pass gets no scene lights/shadows */
    GfxSceneParams preview_scene;
    float preview_rect[4]; /* x, y, size, corner radius (pt) */
    HMM_Vec4 preview_bg;
    SDL_GPUTexture *preview_color, *preview_msaa, *preview_depth;
    SDL_GPUTexture *head_atlas;                  /* 8 x 8 cells of GFX_HEAD_PX */
    SDL_GPUTexture *head_color, *head_msaa, *head_depth; /* room for GFX_HEADS_PER_FRAME cells */
    ShaderCacheEntry shaders[GFX_MAX_SHADERS];
    uint32_t shader_count;

    SDL_GPUBuffer *vb;
    SDL_GPUBuffer *ib;
    uint32_t vb_used;
    uint32_t ib_used;
    GfxMeshRec meshes[GFX_MAX_MESHES];
    uint32_t mesh_count;

    SDL_GPUBuffer *ui_vb_ring[GFX_BUFFER_RING];
    SDL_GPUTransferBuffer *ui_tb_ring[GFX_BUFFER_RING];
    GfxUiInst *ui_insts;
    uint32_t ui_count;
    float ui_w_pt, ui_h_pt;
    SDL_GPUTexture *ui_atlas;
    SDL_GPUSampler *ui_sampler;
    SDL_GPUSampler *linear_clamp;

    SDL_GPUTexture *msaa_color;
    SDL_GPUTexture *depth;
#ifdef MG_RENDER_FULL
    SDL_GPUTexture *refl_color;
    SDL_GPUTexture *refl_depth;
#endif
    SDL_GPUTexture *dummy_tex;
    SDL_GPUTexture *scene_tex; /* resolved 3D image when rendering below native size */
    uint32_t target_w, target_h;
    float render_scale;

    SDL_GPUCommandBuffer *cmd;
    SDL_GPUTexture *swap;
    uint32_t swap_w, swap_h;
    bool in_frame;
    bool paused;
    char shot_path[512]; /* screenshot after this frame ("" = none) */
    uint32_t ui_px_w, ui_px_h; /* pixel size of the target the UI is drawn into */
    GfxSceneParams scene;

#ifdef MG_RENDER_FULL
    /* shadows */
    GfxShadowConfig shadow_cfg;
    SDL_GPUTextureFormat shadow_fmt;
    SDL_GPUTexture *csm[2];
    SDL_GPUTexture *atlas;
    SDL_GPUGraphicsPipeline *shadow_pipe;
    SDL_GPUGraphicsPipeline *tile_clear_pipe;
    uint32_t slot_light[GFX_SHADOW_SLOTS]; /* light id whose depth is in each atlas slot, 0 = none */
    bool slot_dynamic[GFX_SHADOW_SLOTS];   /* a moving caster was inside when it was rendered */
    SDL_GPUSampler *point_clamp;
    HMM_Mat4 csm_vp[2]; /* as last rendered: sampled with the matrix it was made with */
    bool csm_valid[2];
    bool atlas_valid;
    uint32_t frame_no;
    GfxLight lights[GFX_MAX_LIGHTS];
    uint32_t light_count;
    int lights_per_draw;
    LightsFSUniforms light_ubo;
#endif
    DrawItem *draws;
    uint16_t *order, *order_tmp;
    uint64_t *keys, *keys_tmp;
    uint16_t *run_len;     /* per position in `order`: instances from here, 0 inside a run or per-draw */
    uint32_t *run_first;   /* first instance of that run */
    GfxInstance *inst;
    uint32_t inst_count;
    SDL_GPUBuffer *inst_vb[GFX_BUFFER_RING];
    SDL_GPUTransferBuffer *inst_tb[GFX_BUFFER_RING];
    uint32_t ring;
    SDL_GPUGraphicsPipeline *inst_pipe[GFX_MAT_COUNT][2]; /* opaque, dithered; NULL = per-draw path */
    SDL_GPUGraphicsPipeline *inst_xray_always[2];         /* rim, fill: "X-ray always" (depth test ALWAYS) */
    SDL_GPUGraphicsPipeline *floor_dither;
    uint32_t draw_count;
    GfxStats stats;
};

#ifdef MG_RENDER_FULL
static void release_shadow_maps(GfxContext *g);
#endif

/* ---------------------------------------------------------------- shaders */

static SDL_GPUShader *load_shader(GfxContext *g, const char *name, uint32_t ubos, uint32_t samplers)
{
    for (uint32_t i = 0; i < g->shader_count; i++) {
        if (strcmp(g->shaders[i].name, name) == 0) {
            return g->shaders[i].shader;
        }
    }
    char path[128];
    const bool msl = (g->shader_fmt == SDL_GPU_SHADERFORMAT_MSL);
    SDL_snprintf(path, sizeof(path), "shaders/compiled/%s/%s.%s", msl ? "msl" : "spv", name,
                 msl ? "msl" : "spv");
    const MgAsset *a = mg_asset_find(path);
    if (!a) {
        mg_log_error("gfx: missing shader asset %s", path);
        return NULL;
    }
    const bool vert = SDL_strstr(name, ".vert") != NULL;
    SDL_GPUShaderCreateInfo ci;
    SDL_zero(ci);
    ci.code = a->data;
    ci.code_size = a->size;
    ci.entrypoint = vert ? "VSMain" : "PSMain";
    ci.format = g->shader_fmt;
    ci.stage = vert ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    ci.num_uniform_buffers = ubos;
    ci.num_samplers = samplers;
    SDL_GPUShader *s = SDL_CreateGPUShader(g->dev, &ci);
    if (!s) {
        mg_log_error("gfx: shader %s failed: %s", name, SDL_GetError());
        return NULL;
    }
    if (g->shader_count < GFX_MAX_SHADERS) {
        g->shaders[g->shader_count].name = name;
        g->shaders[g->shader_count].shader = s;
        g->shader_count++;
    }
    return s;
}

static void release_shaders(GfxContext *g)
{
    for (uint32_t i = 0; i < g->shader_count; i++) {
        SDL_ReleaseGPUShader(g->dev, g->shaders[i].shader);
    }
    g->shader_count = 0;
}

/* -------------------------------------------------------------- pipelines */

typedef struct PipeDesc {
    SDL_GPUShader *vs;
    SDL_GPUShader *fs;
    int layout;
    bool blend;
    bool depth_test;
    bool depth_write;
    SDL_GPUCullMode cull;
    SDL_GPUFrontFace front;
    SDL_GPUTextureFormat color_fmt;
    SDL_GPUSampleCount samples;
    bool no_depth;
    bool depth_only;       /* shadow maps: no colour target */
    SDL_GPUTextureFormat depth_override;
    bool depth_bias;
    bool depth_always;     /* tile clear: write depth whatever is there */
    bool depth_greater;    /* x-ray material */
    bool instanced;        /* LAYOUT_MESH + GfxInstance in slot 1 */
} PipeDesc;

static SDL_GPUGraphicsPipeline *make_pipeline(GfxContext *g, const PipeDesc *d)
{
    if (!d->vs || !d->fs) {
        return NULL;
    }
    SDL_GPUVertexBufferDescription vbds[2];
    SDL_zeroa(vbds);
    SDL_GPUVertexBufferDescription vbd = vbds[0];
    SDL_GPUVertexAttribute attrs[9];
    SDL_zeroa(attrs);
    uint32_t nattr = 0;
    if (d->layout == LAYOUT_MESH) {
        vbd.slot = 0;
        vbd.pitch = sizeof(GfxVertex);
        vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        attrs[0] = (SDL_GPUVertexAttribute){0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GfxVertex, px)};
        attrs[1] = (SDL_GPUVertexAttribute){1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GfxVertex, nx)};
        attrs[2] = (SDL_GPUVertexAttribute){2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(GfxVertex, r)};
        nattr = 3;
    } else if (d->layout == LAYOUT_UI) { /* one instance per primitive; the corners come from the vertex index */
        vbd.slot = 0;
        vbd.pitch = sizeof(GfxUiInst);
        vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
        attrs[0] = (SDL_GPUVertexAttribute){0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GfxUiInst, rect)};
        attrs[1] = (SDL_GPUVertexAttribute){1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GfxUiInst, uv)};
        attrs[2] = (SDL_GPUVertexAttribute){2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GfxUiInst, xf)};
        attrs[3] = (SDL_GPUVertexAttribute){3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GfxUiInst, clip)};
        attrs[4] = (SDL_GPUVertexAttribute){4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GfxUiInst, shape)};
        attrs[5] = (SDL_GPUVertexAttribute){5, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(GfxUiInst, color)};
        attrs[6] = (SDL_GPUVertexAttribute){6, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(GfxUiInst, color2)};
        attrs[7] = (SDL_GPUVertexAttribute){7, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(GfxUiInst, kind)};
        attrs[8] = (SDL_GPUVertexAttribute){8, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GfxUiInst, tilt)};
        nattr = 9;
    }

    SDL_GPUColorTargetDescription ctd;
    SDL_zero(ctd);
    ctd.format = d->color_fmt;
    if (d->blend) {
        ctd.blend_state.enable_blend = true;
        ctd.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        ctd.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ctd.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        ctd.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        ctd.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ctd.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    }

    SDL_GPUGraphicsPipelineCreateInfo ci;
    SDL_zero(ci);
    ci.vertex_shader = d->vs;
    ci.fragment_shader = d->fs;
    vbds[0] = vbd;
    if (d->instanced) {
        vbds[1].slot = 1;
        vbds[1].pitch = sizeof(GfxInstance);
        vbds[1].input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
        for (uint32_t r = 0; r < 6; r++) {
            attrs[3 + r] = (SDL_GPUVertexAttribute){3 + r, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, r * (uint32_t)sizeof(HMM_Vec4)};
        }
        nattr += 6;
    }
    ci.vertex_input_state.vertex_buffer_descriptions = nattr ? vbds : NULL;
    ci.vertex_input_state.num_vertex_buffers = nattr ? (d->instanced ? 2 : 1) : 0;
    ci.vertex_input_state.vertex_attributes = nattr ? attrs : NULL;
    ci.vertex_input_state.num_vertex_attributes = nattr;
    ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode = d->cull;
    ci.rasterizer_state.front_face = d->front;
    ci.multisample_state.sample_count = d->samples;
    ci.depth_stencil_state.enable_depth_test = d->depth_test;
    ci.depth_stencil_state.enable_depth_write = d->depth_write;
    ci.depth_stencil_state.compare_op = d->depth_always    ? SDL_GPU_COMPAREOP_ALWAYS
                                        : d->depth_greater ? SDL_GPU_COMPAREOP_GREATER
                                                           : SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    ci.target_info.color_target_descriptions = d->depth_only ? NULL : &ctd;
    ci.target_info.num_color_targets = d->depth_only ? 0 : 1;
    ci.target_info.depth_stencil_format = d->no_depth          ? SDL_GPU_TEXTUREFORMAT_INVALID
                                          : d->depth_override ? d->depth_override
                                                              : g->depth_fmt;
    if (d->depth_bias) { /* slope-scaled: against acne on surfaces facing away from the light */
        ci.rasterizer_state.enable_depth_bias = true;
        ci.rasterizer_state.depth_bias_constant_factor = 1.5f;
        ci.rasterizer_state.depth_bias_slope_factor = 2.0f;
    }
    ci.target_info.has_depth_stencil_target = !d->no_depth;

    SDL_GPUGraphicsPipeline *p = SDL_CreateGPUGraphicsPipeline(g->dev, &ci);
    if (!p) {
        mg_log_error("gfx: pipeline failed: %s", SDL_GetError());
    }
    return p;
}

static bool create_pipelines(GfxContext *g)
{
    for (int t = 0; t < TARGET_COUNT; t++) {
#ifdef MG_RENDER_FULL
        const bool refl = (t == TARGET_REFL);
#else
        const bool refl = false;
#endif
        const SDL_GPUTextureFormat fmt = refl ? g->refl_fmt : g->color_fmt;
        const SDL_GPUSampleCount samples = refl ? SDL_GPU_SAMPLECOUNT_1 : g->msaa;
        /* the mirror flips winding, so the reflection pass treats CW as front */
        const SDL_GPUFrontFace front =
            refl ? SDL_GPU_FRONTFACE_CLOCKWISE : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        for (int k = 0; k < GFX_MAT_COUNT; k++) {
            const MaterialProgram *mp = material_program((GfxMaterialKind)k);
            PipeDesc d = {
                .vs = load_shader(g, mp->vert, mp->vert_ubos, 0),
                .fs = load_shader(g, mp->frag, mp->frag_ubos, mp->frag_samplers),
                .layout = LAYOUT_MESH,
                .blend = mp->blend,
                .depth_test = true,
                .depth_write = mp->depth_write,
                .cull = mp->cull_front  ? SDL_GPU_CULLMODE_FRONT
                        : mp->cull_back ? SDL_GPU_CULLMODE_BACK
                                        : SDL_GPU_CULLMODE_NONE,
                .front = front,
                .color_fmt = fmt,
                .samples = samples,
                .depth_greater = mp->depth_greater,
            };
            g->pipes[t].mat[k] = make_pipeline(g, &d);
            if (!g->pipes[t].mat[k]) {
                return false;
            }
            if (k == GFX_MAT_XRAY_RIM || k == GFX_MAT_XRAY) {
                d.depth_greater = false;
                d.depth_always = true;
                g->pipes[t].xray_always[k == GFX_MAT_XRAY] = make_pipeline(g, &d);
            }
        }
#ifndef MG_RENDER_FULL
        if (t == TARGET_MAIN) { /* instanced, opaque (no clip) and dithered (fade between 0 and 1 only) */
            static const struct {
                GfxMaterialKind kind;
                const char *vert, *frag, *dither;
            } k_inst[] = {
                {GFX_MAT_TOON, "toon_inst.vert", "toon_inst.frag", "toon_inst_dither.frag"},
                {GFX_MAT_OUTLINE, "outline_inst.vert", "unlit_inst.frag", "unlit_inst_dither.frag"},
                {GFX_MAT_UNLIT, "toon_inst.vert", "unlit_inst.frag", "unlit_inst_dither.frag"},
                {GFX_MAT_SPRITE, "sprite_inst.vert", "sprite_inst.frag", NULL},
            };
            for (size_t k = 0; k < MG_ARRAY_COUNT(k_inst); k++) {
                const MaterialProgram *mp = material_program(k_inst[k].kind);
                PipeDesc d = {
                    .vs = load_shader(g, k_inst[k].vert, 2, 0),
                    .fs = load_shader(g, k_inst[k].frag, 2, 0),
                    .layout = LAYOUT_MESH,
                    .instanced = true,
                    .blend = mp->blend,
                    .depth_test = true,
                    .depth_write = mp->depth_write,
                    .cull = mp->cull_front ? SDL_GPU_CULLMODE_FRONT : (mp->cull_back ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE),
                    .front = front,
                    .color_fmt = fmt,
                    .samples = samples,
                };
                g->inst_pipe[k_inst[k].kind][0] = make_pipeline(g, &d);
                if (!g->inst_pipe[k_inst[k].kind][0]) {
                    return false;
                }
                if (k_inst[k].dither) {
                    d.fs = load_shader(g, k_inst[k].dither, 2, 0);
                    g->inst_pipe[k_inst[k].kind][1] = make_pipeline(g, &d);
                }
            }
            /* x-ray: always dithered, so only the second slot; the one thing that scaled with the player count */
            for (int x = 0; x < 2; x++) {
                const GfxMaterialKind kind = x ? GFX_MAT_XRAY : GFX_MAT_XRAY_RIM;
                const MaterialProgram *mp = material_program(kind);
                PipeDesc d = {
                    .vs = load_shader(g, x ? "toon_inst.vert" : "xray_rim_inst.vert", 2, 0),
                    .fs = load_shader(g, "xray_inst.frag", 2, 0),
                    .layout = LAYOUT_MESH,
                    .instanced = true,
                    .depth_test = true,
                    .depth_write = mp->depth_write,
                    .cull = mp->cull_front ? SDL_GPU_CULLMODE_FRONT : (mp->cull_back ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE),
                    .front = front,
                    .color_fmt = fmt,
                    .samples = samples,
                    .depth_greater = mp->depth_greater,
                };
                g->inst_pipe[kind][1] = make_pipeline(g, &d);
                d.depth_greater = false;
                d.depth_always = true;
                g->inst_xray_always[x] = make_pipeline(g, &d);
                if (!g->inst_pipe[kind][1] || !g->inst_xray_always[x]) {
                    return false;
                }
            }
            const MaterialProgram *fp = material_program(GFX_MAT_FLOOR);
            PipeDesc fd = {
                .vs = load_shader(g, fp->vert, fp->vert_ubos, 0),
                .fs = load_shader(g, "floor_flat_dither.frag", fp->frag_ubos, 0),
                .layout = LAYOUT_MESH,
                .depth_test = true,
                .depth_write = fp->depth_write,
                .cull = fp->cull_back ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE,
                .front = front,
                .color_fmt = fmt,
                .samples = samples,
            };
            g->floor_dither = make_pipeline(g, &fd);
        }
#endif
        PipeDesc sky = {
            .vs = load_shader(g, "sky.vert", 0, 0),
#ifdef MG_DAYCYCLE_CONTINUOUS
            .fs = load_shader(g, "sky.frag", 1, 0),
#else
            .fs = load_shader(g, "sky_day.frag", 1, 0),
#endif
            .layout = LAYOUT_NONE,
            .cull = SDL_GPU_CULLMODE_NONE,
            .front = front,
            .color_fmt = fmt,
            .samples = samples,
        };
        g->pipes[t].sky = make_pipeline(g, &sky);
        if (!g->pipes[t].sky) {
            return false;
        }
    }
    PipeDesc ui = {
        .vs = load_shader(g, "ui.vert", 1, 0),
        .fs = load_shader(g, "ui.frag", 0, 2),
        .layout = LAYOUT_UI,
        .blend = true,
        .cull = SDL_GPU_CULLMODE_NONE,
        .front = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
        .color_fmt = g->color_fmt,
        .samples = g->msaa,
    };
    g->ui_pipe = make_pipeline(g, &ui);
    ui.samples = SDL_GPU_SAMPLECOUNT_1;
    ui.no_depth = true;
    g->ui_pipe_direct = make_pipeline(g, &ui);
    PipeDesc present = {
        .vs = load_shader(g, "fullscreen.vert", 0, 0),
        .fs = load_shader(g, "present.frag", 1, 2),
        .layout = LAYOUT_NONE,
        .cull = SDL_GPU_CULLMODE_NONE,
        .front = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
        .color_fmt = g->color_fmt,
        .samples = SDL_GPU_SAMPLECOUNT_1,
        .no_depth = true,
    };
    g->present_pipe = make_pipeline(g, &present);
    PipeDesc panel = present;
    panel.vs = load_shader(g, "panel.vert", 1, 0);
    panel.fs = load_shader(g, "panel.frag", 1, 1);
    panel.blend = true;
    g->panel_pipe = make_pipeline(g, &panel);
#ifdef MG_RENDER_FULL
    PipeDesc sh = {
        .vs = load_shader(g, "mesh.vert", 2, 0),
        .fs = load_shader(g, "shadow.frag", 0, 0),
        .layout = LAYOUT_MESH,
        .depth_test = true,
        .depth_write = true,
        .cull = SDL_GPU_CULLMODE_FRONT, /* back faces into the map: less acne */
        .front = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
        .samples = SDL_GPU_SAMPLECOUNT_1,
        .depth_only = true,
        .depth_override = g->shadow_fmt,
        .depth_bias = true,
    };
    g->shadow_pipe = make_pipeline(g, &sh);
    PipeDesc tc = sh;
    tc.vs = load_shader(g, "clear_depth.vert", 0, 0);
    tc.layout = LAYOUT_NONE;
    tc.cull = SDL_GPU_CULLMODE_NONE;
    tc.depth_bias = false;
    tc.depth_always = true;
    g->tile_clear_pipe = make_pipeline(g, &tc);
    if (!g->shadow_pipe || !g->tile_clear_pipe) {
        return false;
    }
#endif
    release_shaders(g);
    return g->ui_pipe != NULL && g->ui_pipe_direct != NULL && g->panel_pipe != NULL && g->present_pipe != NULL;
}

/* ---------------------------------------------------------------- targets */

static SDL_GPUTexture *make_texture(GfxContext *g, SDL_GPUTextureFormat fmt, uint32_t w, uint32_t h,
                                    SDL_GPUTextureUsageFlags usage, SDL_GPUSampleCount samples)
{
    SDL_GPUTextureCreateInfo ci;
    SDL_zero(ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = fmt;
    ci.usage = usage;
    ci.width = w;
    ci.height = h;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    ci.sample_count = samples;
    SDL_GPUTexture *t = SDL_CreateGPUTexture(g->dev, &ci);
    if (!t) {
        mg_log_error("gfx: texture %ux%u failed: %s", w, h, SDL_GetError());
    }
    return t;
}

static void release_targets(GfxContext *g)
{
    SDL_GPUTexture **ts[] = {&g->msaa_color, &g->depth, &g->scene_tex,
#ifdef MG_RENDER_FULL
                             &g->refl_color, &g->refl_depth,
#endif
    };
    for (size_t i = 0; i < MG_ARRAY_COUNT(ts); i++) {
        if (*ts[i]) {
            SDL_ReleaseGPUTexture(g->dev, *ts[i]);
            *ts[i] = NULL;
        }
    }
}

#define RELEASE_PIPE(p)                                 \
    do {                                                \
        if (p) {                                        \
            SDL_ReleaseGPUGraphicsPipeline(g->dev, p);  \
            (p) = NULL;                                 \
        }                                               \
    } while (0)

static int sample_count(SDL_GPUSampleCount s)
{
    return s == SDL_GPU_SAMPLECOUNT_4 ? 4 : (s == SDL_GPU_SAMPLECOUNT_2 ? 2 : 1);
}

static void release_pipelines(GfxContext *g)
{
    for (int t = 0; t < TARGET_COUNT; t++) {
        for (int k = 0; k < GFX_MAT_COUNT; k++) {
            RELEASE_PIPE(g->pipes[t].mat[k]);
        }
        RELEASE_PIPE(g->pipes[t].sky);
        for (int k = 0; k < 2; k++) {
            RELEASE_PIPE(g->pipes[t].xray_always[k]);
        }
    }
    for (int k = 0; k < GFX_MAT_COUNT; k++) {
        if (k < 2) {
            RELEASE_PIPE(g->inst_xray_always[k]);
        }
        RELEASE_PIPE(g->inst_pipe[k][0]);
        RELEASE_PIPE(g->inst_pipe[k][1]);
    }
    RELEASE_PIPE(g->floor_dither);
    RELEASE_PIPE(g->present_pipe);
    if (g->dissolve_tex) {
        SDL_ReleaseGPUTexture(g->dev, g->dissolve_tex);
        g->dissolve_tex = NULL;
    }
    RELEASE_PIPE(g->ui_pipe);
    RELEASE_PIPE(g->panel_pipe);
    RELEASE_PIPE(g->ui_pipe_direct);
}

void gfx_set_msaa(GfxContext *g, int samples) { g->msaa_wanted = samples; }
void gfx_set_instancing(GfxContext *g, bool on) { g->no_instancing = !on; }

/* Between frames' GPU work: pipelines and targets depend on the sample count. */
static void apply_msaa(GfxContext *g)
{
    SDL_GPUSampleCount want = SDL_GPU_SAMPLECOUNT_1;
    if (g->msaa_wanted == 2 || g->msaa_wanted == 4) {
        SDL_GPUSampleCount w = g->msaa_wanted == 2 ? SDL_GPU_SAMPLECOUNT_2 : SDL_GPU_SAMPLECOUNT_4;
        if (SDL_GPUTextureSupportsSampleCount(g->dev, g->color_fmt, w) && SDL_GPUTextureSupportsSampleCount(g->dev, g->depth_fmt, w)) {
            want = w;
        }
    }
    g->msaa_wanted = -1;
    if (want == g->msaa) {
        return;
    }
    SDL_WaitForGPUIdle(g->dev);
    release_pipelines(g);
    release_targets(g);
    SDL_GPUTexture **pv[] = {&g->preview_color, &g->preview_msaa, &g->preview_depth, &g->head_color, &g->head_msaa, &g->head_depth};
    for (size_t i = 0; i < MG_ARRAY_COUNT(pv); i++) {
        if (*pv[i]) {
            SDL_ReleaseGPUTexture(g->dev, *pv[i]);
            *pv[i] = NULL;
        }
    }
    g->msaa = want;
    if (!create_pipelines(g)) {
        mg_log_error("gfx: pipelines failed after an MSAA change");
    }
    mg_log_info("gfx: MSAA %dx", sample_count(want));
}

static bool ensure_targets(GfxContext *g, uint32_t w, uint32_t h, bool scaled)
{
    if (g->target_w == w && g->target_h == h && g->depth && scaled == (g->scene_tex != NULL)) {
        return true;
    }
    release_targets(g);
    g->target_w = w;
    g->target_h = h;
    if (g->msaa != SDL_GPU_SAMPLECOUNT_1) {
        g->msaa_color = make_texture(g, g->color_fmt, w, h, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, g->msaa);
    }
    g->depth = make_texture(g, g->depth_fmt, w, h, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, g->msaa);
#ifdef MG_RENDER_FULL
    uint32_t rw = w / 2 > 0 ? w / 2 : 1;
    uint32_t rh = h / 2 > 0 ? h / 2 : 1;
    g->refl_color = make_texture(g, g->refl_fmt, rw, rh,
                                 SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                 SDL_GPU_SAMPLECOUNT_1);
    g->refl_depth = make_texture(g, g->depth_fmt, rw, rh, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
                                 SDL_GPU_SAMPLECOUNT_1);
    if (!g->refl_color || !g->refl_depth) {
        return false;
    }
#endif
    if (scaled) {
        g->scene_tex = make_texture(g, g->color_fmt, w, h,
                                    SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                    SDL_GPU_SAMPLECOUNT_1);
    }
    /* every GPU texture the 3D path owns (no shadow or reflection targets in minimal) */
    mg_log_info("gfx: targets %ux%u: depth%s%s%s", w, h, g->msaa_color ? ", msaa colour" : "",
                scaled ? ", scene" : "",
#ifdef MG_RENDER_FULL
                ", reflection colour + depth"
#else
                ""
#endif
    );
    return g->depth && (g->msaa == SDL_GPU_SAMPLECOUNT_1 || g->msaa_color) && (!scaled || g->scene_tex);
}

/* ------------------------------------------------------------------- init */

static bool upload_bytes(GfxContext *g, SDL_GPUBuffer *dst, uint32_t dst_offset, const void *data,
                         uint32_t size)
{
    SDL_GPUTransferBufferCreateInfo tci = {SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size, 0};
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(g->dev, &tci);
    if (!tb) {
        return false;
    }
    void *map = SDL_MapGPUTransferBuffer(g->dev, tb, false);
    memcpy(map, data, size);
    SDL_UnmapGPUTransferBuffer(g->dev, tb);
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g->dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = {tb, 0};
    SDL_GPUBufferRegion region = {dst, dst_offset, size};
    SDL_UploadToGPUBuffer(cp, &src, &region, false);
    SDL_EndGPUCopyPass(cp);
    bool ok = SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(g->dev, tb);
    return ok;
}

static bool upload_texture(GfxContext *g, SDL_GPUTexture *tex, const void *pixels, uint32_t w, uint32_t h,
                           uint32_t bytes_per_pixel)
{
    uint32_t size = w * h * bytes_per_pixel;
    SDL_GPUTransferBufferCreateInfo tci = {SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size, 0};
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(g->dev, &tci);
    if (!tb) {
        return false;
    }
    void *map = SDL_MapGPUTransferBuffer(g->dev, tb, false);
    memcpy(map, pixels, size);
    SDL_UnmapGPUTransferBuffer(g->dev, tb);
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g->dev);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src = {tb, 0, w, h};
    SDL_GPUTextureRegion region;
    SDL_zero(region);
    region.texture = tex;
    region.w = w;
    region.h = h;
    region.d = 1;
    SDL_UploadToGPUTexture(cp, &src, &region, false);
    SDL_EndGPUCopyPass(cp);
    bool ok = SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(g->dev, tb);
    return ok;
}

static SDL_GPUTextureFormat pick_depth_format(SDL_GPUDevice *dev)
{
    const SDL_GPUTextureFormat cands[] = {SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREFORMAT_D24_UNORM,
                                          SDL_GPU_TEXTUREFORMAT_D16_UNORM};
    for (size_t i = 0; i < MG_ARRAY_COUNT(cands); i++) {
        if (SDL_GPUTextureSupportsFormat(dev, cands[i], SDL_GPU_TEXTURETYPE_2D,
                                         SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
            return cands[i];
        }
    }
    return SDL_GPU_TEXTUREFORMAT_D16_UNORM;
}

static void prewarm_gpu_pools(GfxContext *g);

bool gfx_init(GfxContext **out, struct SDL_Window *window, Arena *perm)
{
    GfxContext *g = ARENA_PUSH(perm, GfxContext, 1);
    g->draws = ARENA_PUSH(perm, DrawItem, GFX_MAX_DRAWS);
    g->order = ARENA_PUSH(perm, uint16_t, GFX_MAX_DRAWS);
    g->order_tmp = ARENA_PUSH(perm, uint16_t, GFX_MAX_DRAWS);
    g->keys = ARENA_PUSH(perm, uint64_t, GFX_MAX_DRAWS);
    g->keys_tmp = ARENA_PUSH(perm, uint64_t, GFX_MAX_DRAWS);
    g->run_len = ARENA_PUSH(perm, uint16_t, GFX_MAX_DRAWS);
    g->run_first = ARENA_PUSH(perm, uint32_t, GFX_MAX_DRAWS);
    g->inst = ARENA_PUSH(perm, GfxInstance, GFX_MAX_INSTANCES);
    g->ui_insts = ARENA_PUSH(perm, GfxUiInst, GFX_MAX_UI);
    if (!g || !g->draws || !g->order || !g->order_tmp || !g->keys || !g->keys_tmp || !g->run_len || !g->run_first || !g->inst ||
        !g->ui_insts) {
        mg_log_error("gfx: out of permanent memory");
        return false;
    }
    g->win = window;

    bool debug = false;
#ifdef MG_GPU_DEBUG
    debug = true;
#endif
    g->dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, debug, NULL);
    if (!g->dev) {
        mg_log_error("gfx: SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return false;
    }
    g->backend = SDL_GetGPUDeviceDriver(g->dev);
    SDL_GPUShaderFormat fmts = SDL_GetGPUShaderFormats(g->dev);
    g->shader_fmt = (fmts & SDL_GPU_SHADERFORMAT_SPIRV) ? SDL_GPU_SHADERFORMAT_SPIRV : SDL_GPU_SHADERFORMAT_MSL;
    if (!SDL_ClaimWindowForGPUDevice(g->dev, window)) {
        mg_log_error("gfx: claim window failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetGPUSwapchainParameters(g->dev, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);
    g->color_fmt = SDL_GetGPUSwapchainTextureFormat(g->dev, window);
    g->refl_fmt = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    g->depth_fmt = pick_depth_format(g->dev);
#ifdef MG_RENDER_FULL
    /* shadow maps: sampled depth, D16 where it can be a sampled target */
    g->shadow_fmt = SDL_GPUTextureSupportsFormat(g->dev, SDL_GPU_TEXTUREFORMAT_D16_UNORM, SDL_GPU_TEXTURETYPE_2D,
                                                 SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
                                                     SDL_GPU_TEXTUREUSAGE_SAMPLER)
                        ? SDL_GPU_TEXTUREFORMAT_D16_UNORM
                        : SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
#endif
    g->msaa = SDL_GPU_SAMPLECOUNT_1;
    if (SDL_GPUTextureSupportsSampleCount(g->dev, g->color_fmt, SDL_GPU_SAMPLECOUNT_4) &&
        SDL_GPUTextureSupportsSampleCount(g->dev, g->depth_fmt, SDL_GPU_SAMPLECOUNT_4)) {
        g->msaa = SDL_GPU_SAMPLECOUNT_4;
    }
    g->msaa_wanted = -1;
    mg_log_info("gfx: backend %s, shaders %s, color fmt %d, depth fmt %d, msaa %s", g->backend,
                g->shader_fmt == SDL_GPU_SHADERFORMAT_MSL ? "msl" : "spirv", (int)g->color_fmt,
                (int)g->depth_fmt, g->msaa == SDL_GPU_SAMPLECOUNT_4 ? "4x" : "off");

    if (!create_pipelines(g)) {
        return false;
    }

    SDL_GPUBufferCreateInfo vbci = {SDL_GPU_BUFFERUSAGE_VERTEX, GFX_VB_BYTES, 0};
    SDL_GPUBufferCreateInfo ibci = {SDL_GPU_BUFFERUSAGE_INDEX, GFX_IB_BYTES, 0};
    g->vb = SDL_CreateGPUBuffer(g->dev, &vbci);
    g->ib = SDL_CreateGPUBuffer(g->dev, &ibci);

    SDL_GPUBufferCreateInfo uvci = {SDL_GPU_BUFFERUSAGE_VERTEX, GFX_MAX_UI * sizeof(GfxUiInst), 0};
    SDL_GPUTransferBufferCreateInfo utci = {SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, GFX_MAX_UI * sizeof(GfxUiInst), 0};
    for (int i = 0; i < GFX_BUFFER_RING; i++) {
        g->ui_vb_ring[i] = SDL_CreateGPUBuffer(g->dev, &uvci);
        g->ui_tb_ring[i] = SDL_CreateGPUTransferBuffer(g->dev, &utci);
    }
    SDL_GPUBufferCreateInfo ivci = {SDL_GPU_BUFFERUSAGE_VERTEX, GFX_MAX_INSTANCES * (uint32_t)sizeof(GfxInstance), 0};
    for (int i = 0; i < GFX_BUFFER_RING; i++) {
        g->inst_vb[i] = SDL_CreateGPUBuffer(g->dev, &ivci);
    SDL_GPUTransferBufferCreateInfo itci = {SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, GFX_MAX_INSTANCES * (uint32_t)sizeof(GfxInstance), 0};
        g->inst_tb[i] = SDL_CreateGPUTransferBuffer(g->dev, &itci);
    }
    bool ring_ok = true;
    for (int i = 0; i < GFX_BUFFER_RING; i++) {
        ring_ok = ring_ok && g->ui_vb_ring[i] && g->ui_tb_ring[i] && g->inst_vb[i] && g->inst_tb[i];
    }
    if (!g->vb || !g->ib || !ring_ok) {
        mg_log_error("gfx: buffer creation failed: %s", SDL_GetError());
        return false;
    }

    SDL_GPUSamplerCreateInfo sci;
    SDL_zero(sci);
    sci.min_filter = SDL_GPU_FILTER_LINEAR;
    sci.mag_filter = SDL_GPU_FILTER_LINEAR;
    sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    g->linear_clamp = SDL_CreateGPUSampler(g->dev, &sci);
#ifdef MG_RENDER_FULL
    sci.min_filter = SDL_GPU_FILTER_NEAREST;
    sci.mag_filter = SDL_GPU_FILTER_NEAREST;
    g->point_clamp = SDL_CreateGPUSampler(g->dev, &sci);
#endif
    g->ui_sampler = g->linear_clamp;

    const uint32_t white = 0xFFFFFFFFu;
    g->dummy_tex = make_texture(g, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 1, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                SDL_GPU_SAMPLECOUNT_1);
#ifdef MG_RENDER_FULL
    GfxShadowConfig shadows = {true, 2048, 4096, 2048, 8, 2, false, false};
    if (!g->point_clamp || !gfx_set_shadows(g, &shadows)) {
        return false;
    }
    g->lights_per_draw = GFX_LIGHTS_PER_DRAW;
#endif
    if (!g->linear_clamp || !g->dummy_tex) {
        return false;
    }
    upload_texture(g, g->dummy_tex, &white, 1, 1, 4);
    g->stats.backend = g->backend;
    g->render_scale = 1.0f;
    *out = g;
    prewarm_gpu_pools(g);
    return true;
}

/* Waits for each frame's fences so the render loop never has to. */
static int SDLCALL gpu_timing_thread(void *arg)
{
    GfxContext *g = (GfxContext *)arg;
    for (;;) {
        SDL_WaitSemaphore(g->timing_sem);
        if (SDL_GetAtomicInt(&g->timing_quit)) {
            return 0;
        }
        int tail = SDL_GetAtomicInt(&g->timing_tail);
        GpuTimingSlot *t = &g->timing[tail % GPU_TIMING_RING];
        SDL_WaitForGPUFences(g->dev, true, &t->main_fence, 1);
        double main_done = mg_time_now();
        SDL_WaitForGPUFences(g->dev, true, &t->ui_fence, 1);
        double ui_done = mg_time_now();
        SDL_ReleaseGPUFence(g->dev, t->main_fence);
        SDL_ReleaseGPUFence(g->dev, t->ui_fence);
        SDL_SetAtomicInt(&g->gpu_main_us, (int)((main_done - t->submitted) * 1e6));
        SDL_SetAtomicInt(&g->gpu_ui_us, (int)((ui_done - main_done) * 1e6));
        SDL_SetAtomicInt(&g->timing_tail, tail + 1);
    }
}

void gfx_set_gpu_timing(GfxContext *g, bool on)
{
#ifdef MG_PROF
    if (on && !g->timing_thread) {
        g->timing_sem = SDL_CreateSemaphore(0);
        g->timing_thread = SDL_CreateThread(gpu_timing_thread, "gpu-timing", g);
    }
    g->gpu_timing = on && g->timing_thread;
#else
    (void)g;
    (void)on;
#endif
}

/* SDL grows its command-buffer, uniform-buffer and fence pools when a late frame
 * needs one more: do that now, several command buffers at once, not during play. */
static void prewarm_gpu_pools(GfxContext *g)
{
    enum { PREWARM_CMDS = 8, PREWARM_PUSHES = 128 * 6 };
    SDL_GPUCommandBuffer *cmd[PREWARM_CMDS];
    SDL_GPUFence *fence[PREWARM_CMDS];
    const uint8_t blank[64] = {0};
    for (int i = 0; i < PREWARM_CMDS; i++) {
        cmd[i] = SDL_AcquireGPUCommandBuffer(g->dev);
        /* enough pushes to fill several uniform blocks per slot (32 KB each, 256-byte aligned pushes): each command
         * buffer's "used uniform buffers" list grows now, not on the first busy frame (LIB-1's camera move) */
        for (int k = 0; k < PREWARM_PUSHES && cmd[i]; k++) {
            for (uint32_t slot = 0; slot < 2; slot++) {
                SDL_PushGPUVertexUniformData(cmd[i], slot, blank, sizeof(blank));
                SDL_PushGPUFragmentUniformData(cmd[i], slot, blank, sizeof(blank));
            }
        }
    }
    int fences = 0;
    for (int i = 0; i < PREWARM_CMDS; i++) {
        if (cmd[i]) {
            fence[fences] = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd[i]);
            fences += fence[fences] != NULL;
        }
    }
    if (fences) {
        SDL_WaitForGPUFences(g->dev, true, fence, (uint32_t)fences);
        for (int i = 0; i < fences; i++) {
            SDL_ReleaseGPUFence(g->dev, fence[i]);
        }
    }
}

void gfx_shutdown(GfxContext *g)
{
    if (!g || !g->dev) {
        return;
    }
    if (g->timing_thread) {
        SDL_SetAtomicInt(&g->timing_quit, 1);
        SDL_SignalSemaphore(g->timing_sem);
        SDL_WaitThread(g->timing_thread, NULL);
        SDL_DestroySemaphore(g->timing_sem);
    }
    SDL_WaitForGPUIdle(g->dev);
    release_targets(g);
#ifdef MG_RENDER_FULL
    release_shadow_maps(g);
    if (g->shadow_pipe) {
        SDL_ReleaseGPUGraphicsPipeline(g->dev, g->shadow_pipe);
    }
    if (g->tile_clear_pipe) {
        SDL_ReleaseGPUGraphicsPipeline(g->dev, g->tile_clear_pipe);
    }
    if (g->point_clamp) {
        SDL_ReleaseGPUSampler(g->dev, g->point_clamp);
    }
#endif
    SDL_GPUTexture *pv[] = {g->preview_color, g->preview_msaa, g->preview_depth, g->head_atlas, g->head_color, g->head_msaa, g->head_depth};
    for (size_t i = 0; i < MG_ARRAY_COUNT(pv); i++) {
        if (pv[i]) {
            SDL_ReleaseGPUTexture(g->dev, pv[i]);
        }
    }
    release_pipelines(g);
    SDL_ReleaseGPUBuffer(g->dev, g->vb);
    SDL_ReleaseGPUBuffer(g->dev, g->ib);
    for (int i = 0; i < GFX_BUFFER_RING; i++) {
        SDL_ReleaseGPUBuffer(g->dev, g->ui_vb_ring[i]);
        SDL_ReleaseGPUTransferBuffer(g->dev, g->ui_tb_ring[i]);
        SDL_ReleaseGPUBuffer(g->dev, g->inst_vb[i]);
        SDL_ReleaseGPUTransferBuffer(g->dev, g->inst_tb[i]);
    }
    if (g->ui_atlas) {
        SDL_ReleaseGPUTexture(g->dev, g->ui_atlas);
    }
    SDL_ReleaseGPUTexture(g->dev, g->dummy_tex);
    SDL_ReleaseGPUSampler(g->dev, g->linear_clamp);
    SDL_ReleaseWindowFromGPUDevice(g->dev, g->win);
    SDL_DestroyGPUDevice(g->dev);
    g->dev = NULL;
}

GfxMeshId gfx_mesh_upload(GfxContext *g, const GfxVertex *verts, uint32_t vert_count, const uint32_t *indices,
                          uint32_t index_count)
{
    uint32_t vbytes = vert_count * (uint32_t)sizeof(GfxVertex);
    uint32_t ibytes = index_count * (uint32_t)sizeof(uint32_t);
    if (g->mesh_count >= GFX_MAX_MESHES || g->vb_used + vbytes > GFX_VB_BYTES ||
        g->ib_used + ibytes > GFX_IB_BYTES || index_count == 0) {
        mg_log_error("gfx: mesh pool full (%u meshes, %u/%u vb bytes)", g->mesh_count, g->vb_used, GFX_VB_BYTES);
        return GFX_MESH_NONE;
    }
    if (!upload_bytes(g, g->vb, g->vb_used, verts, vbytes) || !upload_bytes(g, g->ib, g->ib_used, indices, ibytes)) {
        return GFX_MESH_NONE;
    }
    GfxMeshRec *m = &g->meshes[g->mesh_count];
    m->bmin = HMM_V3(1e9f, 1e9f, 1e9f);
    m->bmax = HMM_V3(-1e9f, -1e9f, -1e9f);
    for (uint32_t i = 0; i < vert_count; i++) {
        HMM_Vec3 p = HMM_V3(verts[i].px, verts[i].py, verts[i].pz);
        for (int k = 0; k < 3; k++) {
            m->bmin.Elements[k] = mg_minf(m->bmin.Elements[k], p.Elements[k]);
            m->bmax.Elements[k] = mg_maxf(m->bmax.Elements[k], p.Elements[k]);
        }
    }
    m->vertex_offset = (int32_t)(g->vb_used / sizeof(GfxVertex));
    m->first_index = g->ib_used / (uint32_t)sizeof(uint32_t);
    m->index_count = index_count;
    g->vb_used += vbytes;
    g->ib_used += ibytes;
    return (GfxMeshId)g->mesh_count++;
}

bool gfx_set_ui_atlas(GfxContext *g, const uint8_t *pixels, uint32_t w, uint32_t h)
{
    if (g->ui_atlas) {
        SDL_ReleaseGPUTexture(g->dev, g->ui_atlas);
    }
    g->ui_atlas_bytes = (uint64_t)w * h;
    g->ui_atlas = make_texture(g, SDL_GPU_TEXTUREFORMAT_R8_UNORM, w, h, SDL_GPU_TEXTUREUSAGE_SAMPLER,
                               SDL_GPU_SAMPLECOUNT_1);
    return g->ui_atlas && upload_texture(g, g->ui_atlas, pixels, w, h, 1);
}

/* ------------------------------------------------------------------ frame */

void gfx_set_paused(GfxContext *g, bool paused) { g->paused = paused; }

void gfx_set_render_scale(GfxContext *g, float scale) { g->render_scale = mg_clampf(scale, 0.5f, 1.0f); }

float gfx_default_render_scale(void)
{
#if defined(MG_PLATFORM_DESKTOP)
    return 1.0f;
#else
    return 0.8f;
#endif
}

bool gfx_frame_begin(GfxContext *g)
{
    g->draw_count = 0;
    g->ui_count = 0;
    g->ui_clip[0] = g->ui_clip[1] = 0.0f;
    g->ui_clip[2] = g->ui_clip[3] = -1.0f;
    g->ui_clip_fade = 0.0f;
    g->ui_depth = 0.0f, g->ui_tilt_flags = 0, g->ui_offset[0] = g->ui_offset[1] = 0.0f, g->ui_lip = 0.0f;
    g->ui_group_on = false;
    g->ui_screen[0] = g->ui_screen[1] = 0.0f;
    g->ui_screen[2] = 1.0f;
    g->preview_on = false;
    g->inset_count = g->current_inset = 0;
#ifdef MG_RENDER_FULL
    g->light_count = 0;
#endif
    g->in_frame = false;
    if (g->paused) {
        return false;
    }
    g->cmd = SDL_AcquireGPUCommandBuffer(g->dev);
    if (!g->cmd) {
        return false;
    }
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(g->cmd, g->win, &g->swap, &g->swap_w, &g->swap_h) || !g->swap) {
        SDL_SubmitGPUCommandBuffer(g->cmd);
        g->cmd = NULL;
        return false;
    }
    g->in_frame = true;
    return true;
}

void gfx_set_ui_size(GfxContext *g, float w_pt, float h_pt)
{
    g->ui_w_pt = w_pt;
    g->ui_h_pt = h_pt;
}

void gfx_set_scene(GfxContext *g, const GfxSceneParams *scene) { g->scene = *scene; }

void gfx_draw(GfxContext *g, GfxMeshId mesh, const HMM_Mat4 *model, const GfxMaterial *mat, uint32_t flags)
{
    if (!g->in_frame || mesh == GFX_MESH_NONE || mesh >= g->mesh_count || g->draw_count >= GFX_MAX_DRAWS || mat->hide >= 0.98f) {
        return; /* fully faded draws cost nothing */
    }
    DrawItem *d = &g->draws[g->draw_count++];
    d->model = *model;
    d->tint = mat->tint;
    d->params = mat->params;
    d->extra = mat->extra;
    d->hide = mat->hide;
    d->range_first = d->range_count = 0;
    d->inset = (uint8_t)g->current_inset;
    d->mesh = mesh;
    d->kind = (uint8_t)mat->kind;
    d->flags = (uint8_t)flags;
    HMM_Vec3 pos = model->Columns[3].XYZ;
    d->sort = HMM_LenSqrV3(HMM_SubV3(pos, g->scene.eye));
}

void gfx_draw_range(GfxContext *g, GfxMeshId mesh, uint32_t first, uint32_t count, const HMM_Mat4 *model, const GfxMaterial *mat,
                    uint32_t flags)
{
    const uint32_t before = g->draw_count;
    gfx_draw(g, mesh, model, mat, flags);
    if (g->draw_count > before) {
        g->draws[before].range_first = first;
        g->draws[before].range_count = count;
    }
}

void gfx_dissolve_capture(GfxContext *g) { g->dissolve_capture = true; }

void gfx_dissolve(GfxContext *g, float alpha) { g->dissolve_alpha = alpha < 0.0f ? 0.0f : alpha > 1.0f ? 1.0f : alpha; }

bool gfx_ui_push(GfxContext *g, const GfxUiInst *inst)
{
    if (!g->in_frame || g->ui_count >= GFX_MAX_UI) {
        return false;
    }
    GfxUiInst *o = &g->ui_insts[g->ui_count++];
    *o = *inst;
    memcpy(o->clip, g->ui_clip, sizeof(o->clip));
    o->fade = g->ui_clip_fade;
    o->tilt[0] = g->ui_depth, o->tilt[2] = (float)g->ui_tilt_flags, o->tilt[3] = 0.0f;
    if (o->tilt[1] <= 0.0f) { /* the lip: from the shape, or a lip drawn as text (the title's shadow) */
        o->tilt[1] = g->ui_lip;
    }
    o->rect[0] += g->ui_offset[0], o->rect[1] += g->ui_offset[1];
    if (g->ui_group_on) { /* move the centre about the origin, compose the 2 x 2 */
        const float *m = g->ui_group;
        const float cx = o->rect[0] + o->rect[2] * 0.5f - m[4], cy = o->rect[1] + o->rect[3] * 0.5f - m[5];
        const float nx = m[4] + m[0] * cx + m[2] * cy, ny = m[5] + m[1] * cx + m[3] * cy;
        o->rect[0] = nx - o->rect[2] * 0.5f, o->rect[1] = ny - o->rect[3] * 0.5f;
        const float a = o->xf[0], b = o->xf[1], c = o->xf[2], d = o->xf[3];
        o->xf[0] = m[0] * a + m[2] * b, o->xf[1] = m[1] * a + m[3] * b;
        o->xf[2] = m[0] * c + m[2] * d, o->xf[3] = m[1] * c + m[3] * d;
    }
    if (g->ui_screen[0] != 0.0f || g->ui_screen[1] != 0.0f || g->ui_screen[2] < 1.0f) {
        o->rect[0] += g->ui_screen[0], o->rect[1] += g->ui_screen[1];
        o->color[3] = (uint8_t)((float)o->color[3] * g->ui_screen[2]);
        o->color2[3] = (uint8_t)((float)o->color2[3] * g->ui_screen[2]);
    }
    return true;
}

void gfx_ui_group(GfxContext *g, float a, float b, float c, float d, float ox, float oy)
{
    g->ui_group[0] = a, g->ui_group[1] = b, g->ui_group[2] = c, g->ui_group[3] = d, g->ui_group[4] = ox, g->ui_group[5] = oy;
    g->ui_group_on = !(a == 1.0f && b == 0.0f && c == 0.0f && d == 1.0f);
}

void gfx_ui_group_off(GfxContext *g) { g->ui_group_on = false; }

void gfx_ui_set_tilt(GfxContext *g, float tx, float ty, float s, const float safe[4])
{
    g->ui_tilt[0] = tx, g->ui_tilt[1] = ty, g->ui_tilt[2] = s;
    memcpy(g->ui_safe, safe, sizeof(g->ui_safe));
}
void gfx_ui_depth(GfxContext *g, float shift_pt, int flags) { g->ui_depth = shift_pt, g->ui_tilt_flags = flags; }
void gfx_ui_offset(GfxContext *g, float dx, float dy) { g->ui_offset[0] = dx, g->ui_offset[1] = dy; }
void gfx_ui_lip(GfxContext *g, float lip) { g->ui_lip = lip; }

void gfx_ui_size(const GfxContext *g, float *w_pt, float *h_pt)
{
    *w_pt = g->ui_w_pt;
    *h_pt = g->ui_h_pt;
}

void gfx_ui_screen(GfxContext *g, float dx, float dy, float alpha)
{
    g->ui_screen[0] = dx, g->ui_screen[1] = dy, g->ui_screen[2] = alpha < 0.0f ? 0.0f : alpha > 1.0f ? 1.0f : alpha;
}

void gfx_ui_clip(GfxContext *g, float x, float y, float w, float h)
{
    g->ui_clip[0] = x, g->ui_clip[1] = y, g->ui_clip[2] = x + w, g->ui_clip[3] = y + h;
}

const GfxUiInst *gfx_ui_instances(const GfxContext *g, uint32_t *count)
{
    *count = g->ui_count;
    return g->ui_insts;
}

void gfx_ui_clip_fade(GfxContext *g, float pt) { g->ui_clip_fade = pt > 0.0f ? pt : 0.0f; }

void gfx_ui_clip_off(GfxContext *g)
{
    g->ui_clip_fade = 0.0f;
    g->ui_clip[0] = g->ui_clip[1] = 0.0f;
    g->ui_clip[2] = g->ui_clip[3] = -1.0f;
}

void gfx_preview(GfxContext *g, const GfxSceneParams *camera, float x, float y, float size, float radius,
                 HMM_Vec4 background)
{
    if (!g->in_frame) {
        return;
    }
    g->preview_on = true;
    g->preview_scene = *camera;
    g->preview_rect[0] = x;
    g->preview_rect[1] = y;
    g->preview_rect[2] = size;
    g->preview_rect[3] = radius;
    g->preview_bg = background;
}

/* Bucket 0: the solid world (grouped by material, sprites far-to-near);
 * 1: the x-ray passes, tested against the solid world's depth only; 2: avatars
 * (GFX_DRAW_DYNAMIC), which then paint over the x-ray wherever they are
 * visible - so an avatar's own parts never x-ray through each other; 3:
 * dithered (fading) geometry, so a faded wall's remaining pixels neither
 * trigger the x-ray nor hide the player behind it. */
static int draw_bucket(const DrawItem *d)
{
    if (d->kind == GFX_MAT_XRAY_RIM || d->kind == GFX_MAT_XRAY) {
        return 1;
    }
    if (d->flags & GFX_DRAW_DYNAMIC) {
        return 2;
    }
    return d->hide > 0.0f ? 3 : 0;
}

static bool xray_always(const DrawItem *d) { return (d->kind == GFX_MAT_XRAY_RIM || d->kind == GFX_MAT_XRAY) && d->params.W > 0.5f; }

static bool instanceable(const GfxContext *g, const DrawItem *d)
{
    if ((d->flags & (GFX_DRAW_PREVIEW | GFX_DRAW_NO_MAIN)) || d->inset) {
        return false;
    }
    /* sprites: only the flat ground discs; clouds and glows have other shapes */
    if (d->kind == GFX_MAT_SPRITE && !(d->flags & GFX_DRAW_GROUND)) {
        return false;
    }
    return g->inst_pipe[d->kind][d->hide > 0.0f ? 1 : 0] != NULL;
}

/* bucket | kind | x-ray-always | two 16-bit fields | position:
 *   opaque: mesh then depth (front to back), so one mesh's instances are neighbours
 *   blended: depth (back to front) then mesh; ground sprites all sort as "farthest" */
static uint64_t draw_key(const GfxContext *g, const DrawItem *d, uint32_t index)
{
    const float dist = sqrtf(d->sort);
    const uint64_t near_first = (uint64_t)mg_clampf(dist * 256.0f, 0.0f, 65535.0f);
    uint64_t a, b;
    if (d->kind == GFX_MAT_SPRITE || d->kind == GFX_MAT_TEXT || d->kind == GFX_MAT_GHOST) {
        a = (d->flags & GFX_DRAW_GROUND) ? 0u : 65535u - near_first;
        b = d->mesh;
    } else {
        a = d->mesh;
        b = near_first;
    }
    (void)g;
    const uint64_t always = (d->kind == GFX_MAT_XRAY_RIM || d->kind == GFX_MAT_XRAY) && d->params.W > 0.5f;
    return ((uint64_t)draw_bucket(d) << 62) | ((uint64_t)d->kind << 58) | (always << 57) | (a << 41) | (b << 25) | (uint64_t)index;
}

/* Stable LSD radix sort on the key bits above the position field. */
static void sort_draws(GfxContext *g)
{
    const uint32_t n = g->draw_count;
    for (uint32_t i = 0; i < n; i++) {
        g->keys[i] = draw_key(g, &g->draws[i], i);
    }
    uint64_t *src = g->keys, *dst = g->keys_tmp;
    for (int shift = 25; shift < 64; shift += 8) {
        uint32_t count[256];
        SDL_memset(count, 0, sizeof(count));
        for (uint32_t i = 0; i < n; i++) {
            count[(src[i] >> shift) & 255u]++;
        }
        uint32_t sum = 0;
        for (int c = 0; c < 256; c++) {
            uint32_t t = count[c];
            count[c] = sum;
            sum += t;
        }
        for (uint32_t i = 0; i < n; i++) {
            dst[count[(src[i] >> shift) & 255u]++] = src[i];
        }
        uint64_t *t = src;
        src = dst;
        dst = t;
    }
    for (uint32_t i = 0; i < n; i++) {
        g->order[i] = (uint16_t)(src[i] & 0x1FFFFFFu);
    }
}

/* Neighbours with the same mesh and pipeline become one instanced draw. */
static void build_instances(GfxContext *g)
{
    g->inst_count = 0;
    if (g->no_instancing) {
        return;
    }
    for (uint32_t i = 0; i < g->draw_count;) {
        const DrawItem *d = &g->draws[g->order[i]];
        g->run_len[i] = 0;
        if (!instanceable(g, d)) {
            i++;
            continue;
        }
        const bool dither = d->hide > 0.0f;
        uint32_t j = i;
        g->run_first[i] = g->inst_count;
        for (; j < g->draw_count; j++) {
            const DrawItem *e = &g->draws[g->order[j]];
            if (e->kind != d->kind || e->mesh != d->mesh || (e->hide > 0.0f) != dither || !instanceable(g, e) ||
                draw_bucket(e) != draw_bucket(d) || e->range_first != d->range_first || e->range_count != d->range_count ||
                xray_always(e) != xray_always(d)) {
                break;
            }
            if (g->inst_count >= GFX_MAX_INSTANCES) {
                break;
            }
            GfxInstance *o = &g->inst[g->inst_count++];
            for (int r = 0; r < 3; r++) {
                o->row[r] = HMM_V4(e->model.Columns[0].Elements[r], e->model.Columns[1].Elements[r], e->model.Columns[2].Elements[r],
                                   e->model.Columns[3].Elements[r]);
            }
            o->tint = e->tint;
            o->params = HMM_V4(e->params.X, e->params.W, e->params.Y > 0.5f ? -1.0f : e->params.Z, e->hide);
            o->extra = e->extra;
            if (j > i) {
                g->run_len[j] = 0;
            }
        }
        g->run_len[i] = (uint16_t)(j - i);
        i = j;
    }
}

static void upload_instances(GfxContext *g)
{
    if (g->inst_count == 0) {
        return;
    }
    uint32_t bytes = g->inst_count * (uint32_t)sizeof(GfxInstance);
    const uint32_t r = g->ring % GFX_BUFFER_RING;
    void *map = SDL_MapGPUTransferBuffer(g->dev, g->inst_tb[r], false);
    memcpy(map, g->inst, bytes);
    SDL_UnmapGPUTransferBuffer(g->dev, g->inst_tb[r]);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(g->cmd);
    SDL_GPUTransferBufferLocation src = {g->inst_tb[r], 0};
    SDL_GPUBufferRegion region = {g->inst_vb[r], 0, bytes};
    SDL_UploadToGPUBuffer(cp, &src, &region, false);
    SDL_EndGPUCopyPass(cp);
    g->stats.uniform_bytes += bytes;
}

static void push_frame_uniforms(GfxContext *g, const HMM_Mat4 *view_proj, HMM_Vec3 eye, float target_w,
                                float target_h, float reflection)
{
    SDL_PushGPUVertexUniformData(g->cmd, 1, view_proj, sizeof(HMM_Mat4));
    FrameFSUniforms f;
    f.sun_dir = HMM_V4V(g->scene.sun_dir, 0.0f);
    f.sun_color = HMM_V4V(g->scene.sun_color, 0.0f);
    f.ambient = HMM_V4V(g->scene.ambient, 0.0f);
    f.eye = HMM_V4V(eye, 1.0f);
    f.misc = HMM_V4(g->scene.time, reflection, 1.0f / target_w, 1.0f / target_h);
    f.fog = HMM_V4V(g->scene.fog_color, g->scene.fog_density);
    SDL_PushGPUFragmentUniformData(g->cmd, 1, &f, sizeof(f));
#ifndef MG_RENDER_FULL
    (void)reflection;
#else
    if (target_w == (float)GFX_PREVIEW_PX && reflection == 0.0f && g->preview_on && g->preview_lit == false) {
        /* settings preview: the avatar alone, no scene lights or shadows */
        float saved_count = g->light_ubo.count.X, saved_csm = g->light_ubo.csm_params.W;
        g->light_ubo.count.X = 0.0f;
        g->light_ubo.csm_params.W = 0.0f;
        SDL_PushGPUFragmentUniformData(g->cmd, 2, &g->light_ubo, sizeof(g->light_ubo));
        g->light_ubo.count.X = saved_count;
        g->light_ubo.csm_params.W = saved_csm;
        return;
    }
    SDL_PushGPUFragmentUniformData(g->cmd, 2, &g->light_ubo, sizeof(g->light_ubo));
#endif
}

static uint32_t draw_insets(GfxContext *g, SDL_GPURenderPass *pass, bool before_world);

static void draw_sky(GfxContext *g, SDL_GPURenderPass *pass, SDL_GPUGraphicsPipeline *pipe,
                     const HMM_Mat4 *inv_vp, HMM_Vec3 eye)
{
    SkyFSUniforms s;
    s.inv_view_proj = *inv_vp;
    s.eye = HMM_V4V(eye, 1.0f);
    s.horizon = g->scene.sky_horizon;
    s.zenith = g->scene.sky_zenith;
    s.sun = g->scene.sun_disc;
    s.moon = g->scene.moon_disc;
    s.stars = HMM_V4(g->scene.stars, g->scene.time, 0, 0);
    SDL_BindGPUGraphicsPipeline(pass, pipe);
    SDL_PushGPUFragmentUniformData(g->cmd, 0, &s, sizeof(s));
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
}

/* computed only for the few draws that still take it as a uniform */
static HMM_Mat4 draw_normal(const DrawItem *d)
{
    if (d->kind == GFX_MAT_SPRITE || d->kind == GFX_MAT_TEXT) {
        return HMM_M4D(1.0f); /* sprites and glyphs carry UVs in the normal */
    }
    return HMM_TransposeM4(HMM_InvGeneralM4(d->model));
}

static uint32_t draw_list(GfxContext *g, SDL_GPURenderPass *pass, int target, bool preview, int inset)
{
    SDL_GPUBufferBinding vbb = {g->vb, 0};
    SDL_GPUBufferBinding ibb = {g->ib, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vbb, 1);
    SDL_BindGPUIndexBuffer(pass, &ibb, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    int bound = -1;
    SDL_GPUGraphicsPipeline *bound_pipe = NULL;
    uint32_t count = 0;
    const bool can_instance = target == TARGET_MAIN && !preview && inset == 0 && g->inst_count > 0;
    if (can_instance) {
        SDL_GPUBufferBinding ivb = {g->inst_vb[g->ring % GFX_BUFFER_RING], 0};
        SDL_BindGPUVertexBuffers(pass, 1, &ivb, 1);
    }
    for (uint32_t i = 0; i < g->draw_count; i++) {
        const DrawItem *d = &g->draws[g->order[i]];
        if (((d->flags & GFX_DRAW_PREVIEW) != 0) != preview || d->inset != inset) {
            continue;
        }
        if (can_instance && g->run_len[i] > 0) {
            SDL_GPUGraphicsPipeline *ip = xray_always(d) ? g->inst_xray_always[d->kind == GFX_MAT_XRAY] : g->inst_pipe[d->kind][d->hide > 0.0f ? 1 : 0];
            if (ip != bound_pipe) {
                SDL_BindGPUGraphicsPipeline(pass, ip);
                bound_pipe = ip;
                g->stats.pipe_binds++;
            }
            const GfxMeshRec *im = &g->meshes[d->mesh];
            const uint32_t runs = g->run_len[i];
            const uint32_t ic = d->range_count ? d->range_count : im->index_count;
            SDL_DrawGPUIndexedPrimitives(pass, ic, runs, im->first_index + d->range_first, im->vertex_offset, g->run_first[i]);
            count++;
            g->stats.tris += (ic / 3) * runs;
            g->stats.instances += runs;
            i += runs - 1;
            continue;
        }
#ifdef MG_RENDER_FULL
        if (target == TARGET_REFL) {
            if (!(d->flags & GFX_DRAW_REFLECT) || d->kind == GFX_MAT_FLOOR) {
                continue;
            }
        } else
#else
        (void)target;
#endif
        if (d->flags & GFX_DRAW_NO_MAIN) {
            continue;
        }
        SDL_GPUGraphicsPipeline *pipe = g->pipes[target].mat[d->kind];
#ifndef MG_RENDER_FULL
        if (d->kind == GFX_MAT_FLOOR && d->hide > 0.0f && g->floor_dither) {
            pipe = g->floor_dither;
        }
#endif
        if ((d->kind == GFX_MAT_XRAY_RIM || d->kind == GFX_MAT_XRAY) && d->params.W > 0.5f) {
            pipe = g->pipes[target].xray_always[d->kind == GFX_MAT_XRAY];
        }
        if (pipe != bound_pipe) {
            SDL_BindGPUGraphicsPipeline(pass, pipe);
            bound_pipe = pipe;
            g->stats.pipe_binds++;
        }
        if (bound != d->kind) {
            bound = d->kind;
#ifdef MG_RENDER_FULL
            SDL_GPUTextureSamplerBinding shadow_maps[3] = {
                {g->csm[0], g->point_clamp}, {g->csm[1], g->point_clamp}, {g->atlas, g->point_clamp}};
            if (d->kind == GFX_MAT_FLOOR) {
                SDL_GPUTextureSamplerBinding tsb[4] = {
                    {g->scene.reflection_strength > 0.0f ? g->refl_color : g->dummy_tex, g->linear_clamp},
                    shadow_maps[0], shadow_maps[1], shadow_maps[2]};
                SDL_BindGPUFragmentSamplers(pass, 0, tsb, 4);
            } else if (d->kind == GFX_MAT_TOON) {
                SDL_BindGPUFragmentSamplers(pass, 0, shadow_maps, 3);
            } else
#endif
            if (d->kind == GFX_MAT_TEXT) {
                SDL_GPUTextureSamplerBinding tsb = {g->ui_atlas ? g->ui_atlas : g->dummy_tex, g->ui_sampler};
                SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
                g->stats.tex_binds++;
            }
        }
        DrawVSUniforms vu = {d->model, draw_normal(d), d->extra};
        DrawFSUniforms fu = {d->tint, d->params, {HMM_V4(0, 0, 0, 0), HMM_V4(0, 0, 0, 0)}, HMM_V4(d->hide, 0, 0, 0)};
#ifdef MG_RENDER_FULL
        for (int k = 0; k < GFX_LIGHTS_PER_DRAW; k++) {
            fu.lights[k / 4].Elements[k % 4] = (float)d->lights[k];
        }
#endif
        SDL_PushGPUVertexUniformData(g->cmd, 0, &vu, sizeof(vu));
        SDL_PushGPUFragmentUniformData(g->cmd, 0, &fu, sizeof(fu));
        const GfxMeshRec *m = &g->meshes[d->mesh];
        const uint32_t ic = d->range_count ? d->range_count : m->index_count;
        SDL_DrawGPUIndexedPrimitives(pass, ic, 1, m->first_index + d->range_first, m->vertex_offset, 0);
        count++;
        g->stats.tris += ic / 3;
        g->stats.instances++;
        g->stats.uniform_bytes += (uint32_t)(sizeof(vu) + sizeof(fu));
    }
    return count;
}

#ifdef MG_RENDER_FULL
bool gfx_add_light(GfxContext *g, const GfxLight *light)
{
    if (!g->in_frame || g->light_count >= GFX_MAX_LIGHTS) {
        return false;
    }
    g->lights[g->light_count++] = *light;
    return true;
}

void gfx_set_lights_per_draw(GfxContext *g, int n)
{
    g->lights_per_draw = n < 1 ? 1 : (n > GFX_LIGHTS_PER_DRAW ? GFX_LIGHTS_PER_DRAW : n);
}

static void pack_lights(GfxContext *g)
{
    LightsFSUniforms *u = &g->light_ubo;
    u->count = HMM_V4((float)g->light_count, 0, 0, 0);
    for (uint32_t i = 0; i < g->light_count; i++) {
        const GfxLight *l = &g->lights[i];
        u->pos_range[i] = HMM_V4V(l->pos, l->range);
        u->color_type[i] = HMM_V4V(HMM_MulV3F(l->color, l->intensity), l->type == GFX_LIGHT_SPOT ? 1.0f : 0.0f);
        u->dir_cone[i] = HMM_V4V(l->dir, l->cos_outer);
        u->extra[i] = HMM_V4(l->cos_inner, -1.0f, 0, 0);
    }
}

/* World bounds of a draw: model-space box through the model matrix. */
static void draw_bounds(const GfxContext *g, const DrawItem *d, HMM_Vec3 *mn, HMM_Vec3 *mx)
{
    const GfxMeshRec *m = &g->meshes[d->mesh];
    HMM_Vec3 c = HMM_MulV3F(HMM_AddV3(m->bmin, m->bmax), 0.5f);
    HMM_Vec3 e = HMM_MulV3F(HMM_SubV3(m->bmax, m->bmin), 0.5f);
    HMM_Vec3 wc = HMM_MulM4V4(d->model, HMM_V4V(c, 1.0f)).XYZ;
    HMM_Vec3 we;
    for (int i = 0; i < 3; i++) {
        we.Elements[i] = 0.0f;
        for (int k = 0; k < 3; k++) {
            we.Elements[i] += mg_absf(d->model.Elements[k][i]) * e.Elements[k];
        }
    }
    *mn = HMM_SubV3(wc, we);
    *mx = HMM_AddV3(wc, we);
}

/* Up to lights_per_draw lights whose sphere touches the draw's bounds,
 * nearest (relative to range) first. Only lit materials need them. */
static void select_lights(GfxContext *g)
{
    for (uint32_t i = 0; i < g->draw_count; i++) {
        DrawItem *d = &g->draws[i];
        memset(d->lights, -1, sizeof(d->lights));
        if (g->light_count == 0 || (d->kind != GFX_MAT_TOON && d->kind != GFX_MAT_FLOOR)) {
            continue;
        }
        HMM_Vec3 mn, mx;
        draw_bounds(g, d, &mn, &mx);
        float best[GFX_LIGHTS_PER_DRAW];
        int n = 0;
        for (uint32_t li = 0; li < g->light_count; li++) {
            const GfxLight *l = &g->lights[li];
            float d2 = 0.0f;
            for (int k = 0; k < 3; k++) {
                float v = l->pos.Elements[k];
                float q = v < mn.Elements[k] ? mn.Elements[k] - v : (v > mx.Elements[k] ? v - mx.Elements[k] : 0.0f);
                d2 += q * q;
            }
            if (d2 > l->range * l->range) {
                continue;
            }
            /* rank by distance to the draw's centre: the light dropped when
             * the list is full is the one that reaches least of it */
            HMM_Vec3 dc = HMM_SubV3(l->pos, HMM_MulV3F(HMM_AddV3(mn, mx), 0.5f));
            float score = HMM_DotV3(dc, dc) / (l->range * l->range);
            int slot = n < g->lights_per_draw ? n++ : -1;
            if (slot < 0) { /* full: replace the worst if this one is better */
                int worst = 0;
                for (int k = 1; k < n; k++) {
                    worst = best[k] > best[worst] ? k : worst;
                }
                if (score >= best[worst]) {
                    continue;
                }
                slot = worst;
            }
            best[slot] = score;
            d->lights[slot] = (int8_t)li;
        }
    }
}

static void release_shadow_maps(GfxContext *g)
{
    SDL_GPUTexture **ts[] = {&g->csm[0], &g->csm[1], &g->atlas};
    for (size_t i = 0; i < MG_ARRAY_COUNT(ts); i++) {
        if (*ts[i]) {
            SDL_ReleaseGPUTexture(g->dev, *ts[i]);
            *ts[i] = NULL;
        }
    }
}

bool gfx_set_shadows(GfxContext *g, const GfxShadowConfig *cfg)
{
    bool realloc = !g->csm[0] || cfg->csm_size != g->shadow_cfg.csm_size || cfg->atlas_w != g->shadow_cfg.atlas_w ||
                   cfg->atlas_h != g->shadow_cfg.atlas_h;
    g->shadow_cfg = *cfg;
    if (!realloc) {
        return true;
    }
    SDL_WaitForGPUIdle(g->dev);
    release_shadow_maps(g);
    const SDL_GPUTextureUsageFlags u = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    for (int i = 0; i < 2; i++) {
        g->csm[i] = make_texture(g, g->shadow_fmt, (uint32_t)cfg->csm_size, (uint32_t)cfg->csm_size, u,
                                 SDL_GPU_SAMPLECOUNT_1);
        g->csm_valid[i] = false;
    }
    g->atlas = make_texture(g, g->shadow_fmt, (uint32_t)cfg->atlas_w, (uint32_t)cfg->atlas_h, u, SDL_GPU_SAMPLECOUNT_1);
    g->atlas_valid = false;
    memset(g->slot_light, 0, sizeof(g->slot_light));
    int bpp = g->shadow_fmt == SDL_GPU_TEXTUREFORMAT_D16_UNORM ? 2 : 4;
    mg_log_info("gfx: shadow maps %dx%d x2 + atlas %dx%d, %d MB", cfg->csm_size, cfg->csm_size, cfg->atlas_w,
                cfg->atlas_h, (2 * cfg->csm_size * cfg->csm_size + cfg->atlas_w * cfg->atlas_h) * bpp >> 20);
    return g->csm[0] && g->csm[1] && g->atlas;
}

static bool casts_shadow(const DrawItem *d)
{
    return (d->kind == GFX_MAT_TOON || d->kind == GFX_MAT_UNLIT) && d->hide <= 0.5f &&
           !(d->flags & (GFX_DRAW_NO_SHADOW | GFX_DRAW_PREVIEW | GFX_DRAW_NO_MAIN));
}

/* Depth of every caster from `vp` into the current pass/viewport. */
static uint32_t draw_casters(GfxContext *g, SDL_GPURenderPass *pass, const HMM_Mat4 *vp, bool dynamic_only)
{
    SDL_GPUBufferBinding vbb = {g->vb, 0};
    SDL_GPUBufferBinding ibb = {g->ib, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vbb, 1);
    SDL_BindGPUIndexBuffer(pass, &ibb, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_BindGPUGraphicsPipeline(pass, g->shadow_pipe);
    SDL_PushGPUVertexUniformData(g->cmd, 1, vp, sizeof(HMM_Mat4));
    uint32_t n = 0;
    for (uint32_t i = 0; i < g->draw_count; i++) {
        const DrawItem *d = &g->draws[i];
        if (!casts_shadow(d) || (dynamic_only && !(d->flags & GFX_DRAW_DYNAMIC))) {
            continue;
        }
        DrawVSUniforms vu = {d->model, draw_normal(d), d->extra};
        SDL_PushGPUVertexUniformData(g->cmd, 0, &vu, sizeof(vu));
        const GfxMeshRec *m = &g->meshes[d->mesh];
        SDL_DrawGPUIndexedPrimitives(pass, m->index_count, 1, m->first_index, m->vertex_offset, 0);
        n++;
    }
    return n;
}

static void depth_pass(GfxContext *g, SDL_GPUTexture *tex, const HMM_Mat4 *vp)
{
    SDL_GPUDepthStencilTargetInfo dti;
    SDL_zero(dti);
    dti.texture = tex;
    dti.clear_depth = 1.0f;
    dti.load_op = SDL_GPU_LOADOP_CLEAR;
    dti.store_op = SDL_GPU_STOREOP_STORE;
    dti.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dti.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(g->cmd, NULL, 0, &dti);
    draw_casters(g, rp, vp, false);
    SDL_EndGPURenderPass(rp);
}

/* Sun/moon cascades: 0 every frame, 1 every 4th frame (it covers far
 * ground where a few frames of lag do not show). */
static void render_cascades(GfxContext *g)
{
    LightsFSUniforms *u = &g->light_ubo;
    const GfxShadowConfig *c = &g->shadow_cfg;
    bool sun_up = HMM_LenV3(g->scene.sun_color) > 0.01f;
    const float focus = g->scene.shadow_focus;
    const float split0 = focus > 0.0f ? focus + 8.0f : CSM_SPLIT0, split1 = focus > 0.0f ? focus + 30.0f : CSM_SPLIT1;
    u->csm_params = HMM_V4(split0, split1, 1.0f / (float)c->csm_size, c->enabled && sun_up ? 1.0f : 0.0f);
    ShadowCamera cam = shadow_camera(&g->scene);
    u->csm_fwd = HMM_V4V(cam.fwd, c->show_cascades ? 1.0f : 0.0f);
    if (!c->enabled || !sun_up) {
        return;
    }
    const float splits[3] = {0.05f, split0, split1};
    for (int i = 0; i < 2; i++) {
        bool due = i == 0 || (g->frame_no % 4) == 0 || !g->csm_valid[i];
        if (!due || (c->freeze && g->csm_valid[i])) {
            continue;
        }
        g->csm_vp[i] = shadow_fit_cascade(&cam, splits[i], splits[i + 1] * (i == 0 ? 1.1f : 1.0f), g->scene.sun_dir,
                                          c->csm_size, 60.0f);
        depth_pass(g, g->csm[i], &g->csm_vp[i]);
        g->csm_valid[i] = true;
        g->stats.shadow_views++;
    }
    u->csm_vp[0] = g->csm_vp[0];
    u->csm_vp[1] = g->csm_vp[1];
}

/* Atlas layout: spot tiles (atlas_h / 4 square) fill the top half, point
 * faces (atlas_h / 8) the bottom half. Slots 0-7 spots, 8+ six per point. */
#define SPOT_SLOTS 8
static void slot_rect(const GfxContext *g, int slot, float r[4])
{
    const GfxShadowConfig *c = &g->shadow_cfg;
    int size, x, y;
    if (slot < SPOT_SLOTS) {
        size = c->atlas_h / 4;
        int per_row = c->atlas_w / size;
        x = (slot % per_row) * size;
        y = (slot / per_row) * size;
    } else {
        size = c->atlas_h / 8;
        int j = slot - SPOT_SLOTS, per_row = c->atlas_w / size;
        x = (j % per_row) * size;
        y = c->atlas_h / 2 + (j / per_row) * size;
    }
    r[0] = (float)x;
    r[1] = (float)y;
    r[2] = (float)size;
    r[3] = (float)size;
}

static bool sphere_touches_box(HMM_Vec3 c, float r, HMM_Vec3 mn, HMM_Vec3 mx)
{
    float d2 = 0.0f;
    for (int k = 0; k < 3; k++) {
        float v = c.Elements[k];
        float q = v < mn.Elements[k] ? mn.Elements[k] - v : (v > mx.Elements[k] ? v - mx.Elements[k] : 0.0f);
        d2 += q * q;
    }
    return d2 <= r * r;
}

static bool dynamic_inside(const GfxContext *g, const GfxLight *l)
{
    for (uint32_t i = 0; i < g->draw_count; i++) {
        const DrawItem *d = &g->draws[i];
        if ((d->flags & GFX_DRAW_DYNAMIC) && casts_shadow(d)) {
            HMM_Vec3 mn, mx;
            draw_bounds(g, d, &mn, &mx);
            if (sphere_touches_box(l->pos, l->range, mn, mx)) {
                return true;
            }
        }
    }
    return false;
}

/* Pick the shadowed spot/point lights (nearest first, tier caps), keep each
 * in the slot it had, and re-render only the tiles that changed: a new
 * light, or a moving caster inside (<= 15 Hz) or just gone. Static tiles
 * stay cached, so with nobody near a shadowed light this costs nothing. */
static void render_atlas(GfxContext *g)
{
    LightsFSUniforms *u = &g->light_ubo;
    const GfxShadowConfig *c = &g->shadow_cfg;
    u->atlas_params = HMM_V4(1.0f / (float)c->atlas_w, 1.0f / (float)c->atlas_h, 0, 0);
    g->stats.shadowed_lights = 0;
    if (!c->enabled) {
        return;
    }
    /* candidates by distance */
    int order[GFX_MAX_LIGHTS], n = 0;
    float dist[GFX_MAX_LIGHTS];
    for (uint32_t i = 0; i < g->light_count; i++) {
        if (g->lights[i].cast_shadow && g->lights[i].intensity > 0.01f) {
            HMM_Vec3 d = HMM_SubV3(g->lights[i].pos, g->scene.eye);
            dist[i] = HMM_DotV3(d, d);
            int k = n++;
            while (k > 0 && dist[order[k - 1]] > dist[i]) {
                order[k] = order[k - 1];
                k--;
            }
            order[k] = (int)i;
        }
    }
    int max_points = c->max_points < 2 ? c->max_points : 2;
    int spots = 0, points = 0;
    int chosen[GFX_MAX_LIGHTS], nc = 0;
    for (int k = 0; k < n; k++) {
        const GfxLight *l = &g->lights[order[k]];
        if (l->type == GFX_LIGHT_SPOT ? spots < c->max_spots && spots < SPOT_SLOTS : points < max_points) {
            chosen[nc++] = order[k];
            spots += l->type == GFX_LIGHT_SPOT;
            points += l->type == GFX_LIGHT_POINT;
        }
    }
    /* stable slots: keep what is already there, then fill free ones */
    int slot_of[GFX_MAX_LIGHTS];
    bool taken[GFX_SHADOW_SLOTS] = {false};
    for (int k = 0; k < nc; k++) {
        const GfxLight *l = &g->lights[chosen[k]];
        slot_of[k] = -1;
        int lo = l->type == GFX_LIGHT_SPOT ? 0 : SPOT_SLOTS, hi = l->type == GFX_LIGHT_SPOT ? SPOT_SLOTS : GFX_SHADOW_SLOTS;
        for (int s = lo; s < hi; s += l->type == GFX_LIGHT_SPOT ? 1 : 6) {
            if (g->slot_light[s] == l->id && !taken[s]) {
                slot_of[k] = s;
                taken[s] = true;
                break;
            }
        }
    }
    for (int k = 0; k < nc; k++) {
        const GfxLight *l = &g->lights[chosen[k]];
        int lo = l->type == GFX_LIGHT_SPOT ? 0 : SPOT_SLOTS, hi = l->type == GFX_LIGHT_SPOT ? SPOT_SLOTS : GFX_SHADOW_SLOTS;
        int step = l->type == GFX_LIGHT_SPOT ? 1 : 6;
        for (int s = lo; slot_of[k] < 0 && s + step <= hi; s += step) {
            if (!taken[s]) {
                slot_of[k] = s;
                taken[s] = true;
            }
        }
    }
    /* matrices + which tiles to redraw */
    int dirty[GFX_MAX_LIGHTS], nd = 0;
    for (int k = 0; k < nc; k++) {
        int i = chosen[k], s = slot_of[k];
        if (s < 0) {
            continue;
        }
        const GfxLight *l = &g->lights[i];
        int faces = l->type == GFX_LIGHT_SPOT ? 1 : 6;
        for (int f = 0; f < faces; f++) {
            float r[4];
            slot_rect(g, s + f, r);
            u->shadow_vp[s + f] = l->type == GFX_LIGHT_SPOT ? shadow_spot_vp(l) : shadow_point_face_vp(l->pos, l->range, f);
            u->shadow_rect[s + f] = HMM_V4(r[0] / (float)c->atlas_w, r[1] / (float)c->atlas_h, r[2] / (float)c->atlas_w,
                                           r[3] / (float)c->atlas_h);
        }
        u->extra[i].Y = (float)s;
        u->extra[i].Z = l->type == GFX_LIGHT_POINT ? 1.0f : 0.0f;
        g->stats.shadowed_lights++;
        bool inside = dynamic_inside(g, l);
        bool due = !g->atlas_valid || g->slot_light[s] != l->id || (inside && (g->frame_no % 4) == 0) ||
                   (g->slot_dynamic[s] && !inside);
        if (due && (!c->freeze || g->slot_light[s] != l->id || !g->atlas_valid)) {
            dirty[nd++] = k;
            g->slot_light[s] = l->id;
            g->slot_dynamic[s] = inside;
        }
    }
    if (nd == 0) {
        return;
    }
    SDL_GPUDepthStencilTargetInfo dti;
    SDL_zero(dti);
    dti.texture = g->atlas;
    dti.clear_depth = 1.0f;
    dti.load_op = g->atlas_valid ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR;
    dti.store_op = SDL_GPU_STOREOP_STORE;
    dti.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dti.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(g->cmd, NULL, 0, &dti);
    for (int d = 0; d < nd; d++) {
        int k = dirty[d], s = slot_of[k];
        const GfxLight *l = &g->lights[chosen[k]];
        int faces = l->type == GFX_LIGHT_SPOT ? 1 : 6;
        for (int f = 0; f < faces; f++) {
            float r[4];
            slot_rect(g, s + f, r);
            SDL_GPUViewport vp = {r[0], r[1], r[2], r[3], 0.0f, 1.0f};
            SDL_Rect sc = {(int)r[0], (int)r[1], (int)r[2], (int)r[3]};
            SDL_SetGPUViewport(rp, &vp);
            SDL_SetGPUScissor(rp, &sc);
            SDL_BindGPUGraphicsPipeline(rp, g->tile_clear_pipe);
            SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
            draw_casters(g, rp, &u->shadow_vp[s + f], false);
            g->stats.shadow_views++;
        }
    }
    SDL_EndGPURenderPass(rp);
    g->atlas_valid = true;
}
#endif /* MG_RENDER_FULL */


static void draw_preview_image(GfxContext *g, SDL_GPURenderPass *pass)
{
    const float *r = g->preview_rect;
    float px_per_pt = (float)g->ui_px_w / g->ui_w_pt;
    SDL_BindGPUGraphicsPipeline(pass, g->panel_pipe);
    SDL_GPUTextureSamplerBinding tsb = {g->preview_color, g->linear_clamp};
    SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
    HMM_Vec4 ndc = HMM_V4(r[0] / g->ui_w_pt * 2.0f - 1.0f, 1.0f - r[1] / g->ui_h_pt * 2.0f,
                          (r[0] + r[2]) / g->ui_w_pt * 2.0f - 1.0f, 1.0f - (r[1] + r[2]) / g->ui_h_pt * 2.0f);
    struct {
        HMM_Vec4 rect, params, tint, mode;
    } fu = {HMM_V4(r[0], r[1], r[2], r[2]), HMM_V4(r[3], px_per_pt, 0, 0), HMM_V4(0, 0, 0, 0), HMM_V4(1, 0, 0, 0)};
    SDL_PushGPUVertexUniformData(g->cmd, 0, &ndc, sizeof(ndc));
    SDL_PushGPUFragmentUniformData(g->cmd, 0, &fu, sizeof(fu));
    SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
}

/* Second camera: draws flagged GFX_DRAW_PREVIEW into a square texture. */
/* The picture-in-picture draws, each with its own viewport, depth range and camera */
static uint32_t draw_insets(GfxContext *g, SDL_GPURenderPass *pass, bool before_world)
{
    uint32_t count = 0;
    const float k = g->ui_w_pt > 0.0f ? (float)g->target_w / g->ui_w_pt : 1.0f;
    for (int i = 0; i < g->inset_count; i++) {
        const GfxInset *in = &g->insets[i];
        if (in->before_world != before_world || in->head_cell >= 0) {
            continue;
        }
        /* a viewport must stay on the target: while a panel slides in, crop it and shift the projection */
        const float x = in->x * k, y = in->y * k, w = in->w * k, h = in->h * k;
        const float cx = fmaxf(x, 0.0f), cy = fmaxf(y, 0.0f);
        const float cw = fminf(x + w, (float)g->target_w) - cx, ch = fminf(y + h, (float)g->target_h) - cy;
        if (cw <= 0.0f || ch <= 0.0f) {
            continue;
        }
        HMM_Mat4 crop = HMM_M4D(1.0f);
        crop.Elements[0][0] = w / cw;
        crop.Elements[1][1] = h / ch;
        crop.Elements[3][0] = (2.0f * (x - cx) + w) / cw - 1.0f;
        crop.Elements[3][1] = 1.0f - (2.0f * (y - cy) + h) / ch;
        const HMM_Mat4 cropped = HMM_MulM4(crop, in->view_proj);
        SDL_GPUViewport vp = {cx, cy, cw, ch, in->min_depth, in->max_depth};
        SDL_SetGPUViewport(pass, &vp);
        SDL_Rect sc = {(int)cx, (int)cy, (int)(cw + 0.5f), (int)(ch + 0.5f)};
        SDL_SetGPUScissor(pass, &sc);
        push_frame_uniforms(g, &cropped, in->eye, cw, ch, 0.0f);
        count += draw_list(g, pass, TARGET_MAIN, false, i + 1);
    }
    if (g->inset_count > 0) {
        SDL_GPUViewport full = {0.0f, 0.0f, (float)g->target_w, (float)g->target_h, 0.0f, 1.0f};
        SDL_SetGPUViewport(pass, &full);
        SDL_Rect sc = {0, 0, (int)g->target_w, (int)g->target_h};
        SDL_SetGPUScissor(pass, &sc);
    }
    return count;
}

void gfx_inset_begin(GfxContext *g, float x, float y, float w, float h, const HMM_Mat4 *view_proj, HMM_Vec3 eye, float min_depth, float max_depth, bool before_world)
{
    if (g->inset_count >= GFX_MAX_INSETS) {
        return;
    }
    GfxInset *in = &g->insets[g->inset_count++];
    *in = (GfxInset){x, y, w, h, *view_proj, eye, min_depth, max_depth, before_world, -1};
    g->current_inset = g->inset_count;
}

void gfx_head_cell_begin(GfxContext *g, int cell, const HMM_Mat4 *view_proj, HMM_Vec3 eye)
{
    gfx_inset_begin(g, 0.0f, 0.0f, 0.0f, 0.0f, view_proj, eye, 0.0f, 1.0f, false);
    if (g->current_inset) {
        g->insets[g->current_inset - 1].head_cell = cell;
    }
}

void gfx_inset_end(GfxContext *g) { g->current_inset = 0; }

static void render_preview(GfxContext *g)
{
    if (!g->preview_color) {
        const SDL_GPUTextureUsageFlags rt = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        g->preview_color = make_texture(g, g->color_fmt, GFX_PREVIEW_PX, GFX_PREVIEW_PX, rt, SDL_GPU_SAMPLECOUNT_1);
        if (g->msaa != SDL_GPU_SAMPLECOUNT_1) {
            g->preview_msaa = make_texture(g, g->color_fmt, GFX_PREVIEW_PX, GFX_PREVIEW_PX,
                                           SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, g->msaa);
        }
        g->preview_depth = make_texture(g, g->depth_fmt, GFX_PREVIEW_PX, GFX_PREVIEW_PX,
                                        SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, g->msaa);
    }
    if (!g->preview_color || !g->preview_depth || (g->msaa != SDL_GPU_SAMPLECOUNT_1 && !g->preview_msaa)) {
        return;
    }
    SDL_GPUColorTargetInfo cti;
    SDL_zero(cti);
    cti.clear_color = (SDL_FColor){g->preview_bg.X, g->preview_bg.Y, g->preview_bg.Z, 1.0f};
    cti.load_op = SDL_GPU_LOADOP_CLEAR;
    if (g->preview_msaa) {
        cti.texture = g->preview_msaa;
        cti.store_op = SDL_GPU_STOREOP_RESOLVE;
        cti.resolve_texture = g->preview_color;
    } else {
        cti.texture = g->preview_color;
        cti.store_op = SDL_GPU_STOREOP_STORE;
    }
    SDL_GPUDepthStencilTargetInfo dti;
    SDL_zero(dti);
    dti.texture = g->preview_depth;
    dti.clear_depth = 1.0f;
    dti.load_op = SDL_GPU_LOADOP_CLEAR;
    dti.store_op = SDL_GPU_STOREOP_DONT_CARE;
    dti.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dti.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(g->cmd, &cti, 1, &dti);
    const GfxSceneParams *c = &g->preview_scene;
    HMM_Mat4 vp = HMM_MulM4(c->proj, c->view);
    push_frame_uniforms(g, &vp, c->eye, (float)GFX_PREVIEW_PX, (float)GFX_PREVIEW_PX, 0.0f);
    draw_list(g, rp, TARGET_MAIN, true, 0);
    SDL_EndGPURenderPass(rp);
}

/* Head icons waiting this frame: one small pass, then each is copied to its atlas cell. Returns the draws. */
static uint32_t render_heads(GfxContext *g)
{
    int jobs = 0;
    for (int i = 0; i < g->inset_count; i++) {
        jobs += g->insets[i].head_cell >= 0 ? 1 : 0;
    }
    if (jobs == 0) {
        return 0;
    }
    const uint32_t tw = GFX_HEAD_PX * GFX_HEADS_PER_FRAME, th = GFX_HEAD_PX;
    const SDL_GPUTextureUsageFlags rt = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (!g->head_atlas) {
        g->head_atlas = make_texture(g, g->color_fmt, GFX_HEAD_PX * 8, GFX_HEAD_PX * 8, rt, SDL_GPU_SAMPLECOUNT_1);
        if (g->head_atlas) { /* never sampled before it is clear */
            SDL_GPUColorTargetInfo clear;
            SDL_zero(clear);
            clear.texture = g->head_atlas;
            clear.load_op = SDL_GPU_LOADOP_CLEAR;
            clear.store_op = SDL_GPU_STOREOP_STORE;
            SDL_EndGPURenderPass(SDL_BeginGPURenderPass(g->cmd, &clear, 1, NULL));
        }
    }
    if (!g->head_color) {
        g->head_color = make_texture(g, g->color_fmt, tw, th, rt, SDL_GPU_SAMPLECOUNT_1);
        if (g->msaa != SDL_GPU_SAMPLECOUNT_1) {
            g->head_msaa = make_texture(g, g->color_fmt, tw, th, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, g->msaa);
        }
        g->head_depth = make_texture(g, g->depth_fmt, tw, th, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, g->msaa);
    }
    if (!g->head_atlas || !g->head_color || !g->head_depth || (g->msaa != SDL_GPU_SAMPLECOUNT_1 && !g->head_msaa)) {
        return 0;
    }
    SDL_GPUColorTargetInfo cti;
    SDL_zero(cti);
    cti.load_op = SDL_GPU_LOADOP_CLEAR; /* transparent */
    if (g->head_msaa) {
        cti.texture = g->head_msaa;
        cti.store_op = SDL_GPU_STOREOP_RESOLVE;
        cti.resolve_texture = g->head_color;
    } else {
        cti.texture = g->head_color;
        cti.store_op = SDL_GPU_STOREOP_STORE;
    }
    SDL_GPUDepthStencilTargetInfo dti;
    SDL_zero(dti);
    dti.texture = g->head_depth;
    dti.clear_depth = 1.0f;
    dti.load_op = SDL_GPU_LOADOP_CLEAR;
    dti.store_op = SDL_GPU_STOREOP_DONT_CARE;
    dti.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dti.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(g->cmd, &cti, 1, &dti);
    uint32_t draws = 0;
    int slot = 0;
    for (int i = 0; i < g->inset_count && slot < GFX_HEADS_PER_FRAME; i++) {
        const GfxInset *in = &g->insets[i];
        if (in->head_cell < 0) {
            continue;
        }
        SDL_GPUViewport vp = {(float)slot * GFX_HEAD_PX, 0.0f, GFX_HEAD_PX, GFX_HEAD_PX, 0.0f, 1.0f};
        SDL_SetGPUViewport(rp, &vp);
        SDL_Rect sc = {slot * GFX_HEAD_PX, 0, GFX_HEAD_PX, GFX_HEAD_PX};
        SDL_SetGPUScissor(rp, &sc);
        push_frame_uniforms(g, &in->view_proj, in->eye, GFX_HEAD_PX, GFX_HEAD_PX, 0.0f);
        draws += draw_list(g, rp, TARGET_MAIN, false, i + 1);
        slot++;
    }
    SDL_EndGPURenderPass(rp);
    slot = 0;
    for (int i = 0; i < g->inset_count && slot < GFX_HEADS_PER_FRAME; i++) {
        const int cell = g->insets[i].head_cell;
        if (cell < 0) {
            continue;
        }
        SDL_GPUBlitInfo b;
        SDL_zero(b);
        b.source.texture = g->head_color;
        b.source.x = (Uint32)slot * GFX_HEAD_PX;
        b.source.w = b.source.h = GFX_HEAD_PX;
        b.destination.texture = g->head_atlas;
        b.destination.x = (Uint32)(cell % 8) * GFX_HEAD_PX;
        b.destination.y = (Uint32)(cell / 8) * GFX_HEAD_PX;
        b.destination.w = b.destination.h = GFX_HEAD_PX;
        b.load_op = SDL_GPU_LOADOP_LOAD;
        b.filter = SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(g->cmd, &b);
        slot++;
    }
    g->stats.passes++;
    g->stats.head_passes++;
    return draws;
}



static void blit_scene_to_swap(GfxContext *g, uint32_t rw, uint32_t rh)
{
    SDL_GPUBlitInfo blit;
    SDL_zero(blit);
    blit.source.texture = g->scene_tex;
    blit.source.w = rw;
    blit.source.h = rh;
    blit.destination.texture = g->swap;
    blit.destination.w = g->swap_w;
    blit.destination.h = g->swap_h;
    blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blit.filter = SDL_GPU_FILTER_LINEAR;
    SDL_BlitGPUTexture(g->cmd, &blit);
}

/* The finished frame (3D + UI, in scene_tex at the render size) -> PNG:
 * our own texture, so the swapchain's storage orientation (it differs
 * between iOS devices) does not matter. Waits for the GPU: stalls this frame. */
static void save_screenshot(GfxContext *g, uint32_t w, uint32_t h)
{
    SDL_GPUTransferBufferCreateInfo tci = {SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, w * h * 4, 0};
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(g->dev, &tci);
    bool ok = false;
    if (tb) {
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(g->cmd);
        SDL_GPUTextureRegion src = {g->scene_tex, 0, 0, 0, 0, 0, w, h, 1};
        SDL_GPUTextureTransferInfo dst = {tb, 0, w, h};
        SDL_DownloadFromGPUTexture(cp, &src, &dst);
        SDL_EndGPUCopyPass(cp);
        SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(g->cmd);
        g->cmd = NULL;
        if (fence && SDL_WaitForGPUFences(g->dev, true, &fence, 1)) {
            const uint8_t *px = (const uint8_t *)SDL_MapGPUTransferBuffer(g->dev, tb, false);
            bool bgra = g->color_fmt == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ||
                        g->color_fmt == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB;
            ok = px && png_write_rgb(g->shot_path, px, w, h, w * 4, bgra);
            SDL_UnmapGPUTransferBuffer(g->dev, tb);
        }
        if (fence) {
            SDL_ReleaseGPUFence(g->dev, fence);
        }
    }
    if (g->cmd) {
        SDL_SubmitGPUCommandBuffer(g->cmd);
        g->cmd = NULL;
    }
    if (tb) {
        SDL_ReleaseGPUTransferBuffer(g->dev, tb);
    }
    if (ok) {
        mg_log_info("gfx: screenshot %ux%u -> %s", w, h, g->shot_path);
    } else {
        mg_log_warn("gfx: screenshot failed: %s", SDL_GetError());
    }
    g->shot_path[0] = 0;
}

static void frame_end(GfxContext *g)
{
    if (!g->in_frame) {
        return;
    }
    g->in_frame = false;
    if (g->msaa_wanted >= 0) {
        apply_msaa(g);
    }
    g->stats.draws = 0;
    g->stats.ui_draws = 0;
    g->stats.head_passes = 0;
    g->stats.reflect_draws = 0;
    g->stats.ui_quads = g->ui_count;
    g->stats.tris = g->stats.instances = g->stats.pipe_binds = g->stats.tex_binds = g->stats.uniform_bytes = g->stats.passes = 0;
    g->stats.gpu_main_ms = (float)SDL_GetAtomicInt(&g->gpu_main_us) * 0.001f;
    g->stats.gpu_ui_ms = (float)SDL_GetAtomicInt(&g->gpu_ui_us) * 0.001f;
#ifdef MG_RENDER_FULL
    g->stats.lights = g->light_count;
#endif
    uint32_t rw = (uint32_t)((float)g->swap_w * g->render_scale + 0.5f);
    uint32_t rh = (uint32_t)((float)g->swap_h * g->render_scale + 0.5f);
    rw = rw > 0 ? rw : 1;
    rh = rh > 0 ? rh : 1;
    const bool shot = g->shot_path[0] != 0; /* this frame's 3D + UI go to scene_tex first */
    /* this frame's pass layout: an offscreen scene only when the frame needs one (a lower render scale, the
     * preview, a screenshot); the textures, once made, stay allocated (no target churn). No blur since UI-1. */
    const bool scaled = rw != g->swap_w || rh != g->swap_h || g->preview_on || shot || g->dissolve_capture || g->dissolve_alpha > 0.0f;
    g->keep_scaled = g->keep_scaled || scaled;
    g->stats.width_px = rw;
    g->stats.height_px = rh;
    g->stats.msaa = sample_count(g->msaa);

    if (!ensure_targets(g, rw, rh, g->keep_scaled)) { /* allocation follows keep_scaled, the passes follow `scaled` */
        SDL_SubmitGPUCommandBuffer(g->cmd);
        g->cmd = NULL;
        return;
    }

    /* UI vertices for this frame */
    if (g->ui_count > 0) {
        PROF_SCOPE("render.ui");
        uint32_t bytes = g->ui_count * (uint32_t)sizeof(GfxUiInst);
        const uint32_t r = g->ring % GFX_BUFFER_RING;
        void *map = SDL_MapGPUTransferBuffer(g->dev, g->ui_tb_ring[r], false);
        memcpy(map, g->ui_insts, bytes);
        SDL_UnmapGPUTransferBuffer(g->dev, g->ui_tb_ring[r]);
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(g->cmd);
        SDL_GPUTransferBufferLocation src = {g->ui_tb_ring[r], 0};
        SDL_GPUBufferRegion region = {g->ui_vb_ring[r], 0, bytes};
        SDL_UploadToGPUBuffer(cp, &src, &region, false);
        SDL_EndGPUCopyPass(cp);
    }

    {
        PROF_SCOPE("render.sort");
        sort_draws(g);
    }
#ifdef MG_RENDER_FULL
    pack_lights(g);
    select_lights(g);
    g->frame_no++;
    g->stats.shadow_views = 0;
    double sh0 = mg_time_now();
    render_cascades(g);
    render_atlas(g);
    g->stats.shadow_cpu_ms = (float)((mg_time_now() - sh0) * 1000.0);
#endif
    double main0 = mg_time_now();
    const GfxSceneParams *s = &g->scene;
    HMM_Mat4 view_proj = HMM_MulM4(s->proj, s->view);
    HMM_Mat4 inv_vp = HMM_InvGeneralM4(view_proj);
    SDL_FColor horizon = {s->sky_horizon.X, s->sky_horizon.Y, s->sky_horizon.Z, 1.0f};

#ifdef MG_RENDER_FULL
    /* reflection pass */
    if (s->reflection_strength > 0.0f) {
        ReflectionView rv;
        reflection_build(s, &rv);
        SDL_GPUColorTargetInfo cti;
        SDL_zero(cti);
        cti.texture = g->refl_color;
        cti.clear_color = horizon;
        cti.load_op = SDL_GPU_LOADOP_CLEAR;
        cti.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo dti;
        SDL_zero(dti);
        dti.texture = g->refl_depth;
        dti.clear_depth = 1.0f;
        dti.load_op = SDL_GPU_LOADOP_CLEAR;
        dti.store_op = SDL_GPU_STOREOP_DONT_CARE;
        dti.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        dti.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(g->cmd, &cti, 1, &dti);
        float refl_w = (float)(g->target_w / 2 > 0 ? g->target_w / 2 : 1);
        float refl_h = (float)(g->target_h / 2 > 0 ? g->target_h / 2 : 1);
        draw_sky(g, rp, g->pipes[TARGET_REFL].sky, &rv.sky_inv_vp, rv.eye);
        push_frame_uniforms(g, &rv.view_proj, rv.eye, refl_w, refl_h, 0.0f);
        g->stats.reflect_draws = draw_list(g, rp, TARGET_REFL, false, 0);
        SDL_EndGPURenderPass(rp);
    }
#endif

    /* with timing on, the 3D passes get their own command buffer and fence */
    SDL_GPUCommandBuffer *swap_cmd = NULL;
    if (g->gpu_timing && scaled && !shot &&
        SDL_GetAtomicInt(&g->timing_head) - SDL_GetAtomicInt(&g->timing_tail) < GPU_TIMING_RING) {
        swap_cmd = g->cmd;
        g->cmd = SDL_AcquireGPUCommandBuffer(g->dev);
    }
    build_instances(g);
    upload_instances(g);
    if (g->preview_on) {
        render_preview(g);
        g->stats.passes++;
    }
    const uint32_t head_draws = render_heads(g);

    /* main pass */
    SDL_GPUColorTargetInfo cti;
    SDL_zero(cti);
    cti.clear_color = horizon;
    cti.load_op = SDL_GPU_LOADOP_CLEAR;
    SDL_GPUTexture *final_color = scaled ? g->scene_tex : g->swap;
    if (g->msaa != SDL_GPU_SAMPLECOUNT_1) {
        cti.texture = g->msaa_color;
        cti.store_op = SDL_GPU_STOREOP_RESOLVE;
        cti.resolve_texture = final_color;
    } else {
        cti.texture = final_color;
        cti.store_op = SDL_GPU_STOREOP_STORE;
    }
    SDL_GPUDepthStencilTargetInfo dti;
    SDL_zero(dti);
    dti.texture = g->depth;
    dti.clear_depth = 1.0f;
    dti.load_op = SDL_GPU_LOADOP_CLEAR;
    dti.store_op = SDL_GPU_STOREOP_DONT_CARE;
    dti.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dti.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(g->cmd, &cti, 1, &dti);
    g->stats.passes++;
    draw_sky(g, pass, g->pipes[TARGET_MAIN].sky, &inv_vp, s->eye);
#ifdef MG_RENDER_FULL
    push_frame_uniforms(g, &view_proj, s->eye, (float)g->target_w, (float)g->target_h, s->reflection_strength);
#else
    push_frame_uniforms(g, &view_proj, s->eye, (float)g->target_w, (float)g->target_h, 0.0f);
#endif
    {
        PROF_SCOPE("render.encode");
        const uint32_t pre_world = draw_insets(g, pass, true);
#ifndef MG_RENDER_FULL
        push_frame_uniforms(g, &view_proj, s->eye, (float)g->target_w, (float)g->target_h, 0.0f);
#endif
        g->stats.draws = head_draws + pre_world + draw_list(g, pass, TARGET_MAIN, false, 0);
        g->stats.draws += draw_insets(g, pass, false);
    }

    if (scaled) {
        /* upscale the 3D image to the swapchain, then draw UI at native size */
        SDL_EndGPURenderPass(pass);
        if (g->dissolve_capture) { /* LIB-1: keep this frame's 3D image for the cross-dissolve */
            g->dissolve_capture = false;
            if (!g->dissolve_tex || g->dissolve_w != rw || g->dissolve_h != rh) {
                if (g->dissolve_tex) {
                    SDL_ReleaseGPUTexture(g->dev, g->dissolve_tex);
                }
                g->dissolve_tex = make_texture(g, g->color_fmt, rw, rh, SDL_GPU_TEXTUREUSAGE_SAMPLER, SDL_GPU_SAMPLECOUNT_1);
                g->dissolve_w = rw, g->dissolve_h = rh;
            }
            if (g->dissolve_tex) {
                SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(g->cmd);
                SDL_GPUTextureLocation src = {g->scene_tex, 0, 0, 0, 0, 0}, dst = {g->dissolve_tex, 0, 0, 0, 0, 0};
                SDL_CopyGPUTextureToTexture(cp, &src, &dst, rw, rh, 1, false);
                SDL_EndGPUCopyPass(cp);
            }
        }
        SDL_GPUFence *main_fence = NULL;
        double main_submit = 0.0;
        if (swap_cmd) {
            main_submit = mg_time_now();
            main_fence = SDL_SubmitGPUCommandBufferAndAcquireFence(g->cmd);
            g->cmd = swap_cmd;
        }
        g->timing[SDL_GetAtomicInt(&g->timing_head) % GPU_TIMING_RING] = (GpuTimingSlot){main_fence, NULL, main_submit};
        SDL_GPUColorTargetInfo uti;
        SDL_zero(uti);
        uti.texture = shot ? g->scene_tex : g->swap;
        uti.load_op = shot ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_DONT_CARE; /* every pixel is rewritten below */
        uti.store_op = SDL_GPU_STOREOP_STORE;
        pass = SDL_BeginGPURenderPass(g->cmd, &uti, 1, NULL);
        g->stats.passes++;
        if (!shot) {
            SDL_BindGPUGraphicsPipeline(pass, g->present_pipe);
            const bool dissolve = g->dissolve_alpha > 0.0f && g->dissolve_tex;
            SDL_GPUTextureSamplerBinding present_src[2] = {{g->scene_tex, g->linear_clamp}, {dissolve ? g->dissolve_tex : g->dummy_tex, g->linear_clamp}};
            SDL_BindGPUFragmentSamplers(pass, 0, present_src, 2);
            const HMM_Vec4 mix = HMM_V4(dissolve ? g->dissolve_alpha : 0.0f, 0.0f, 0.0f, 0.0f);
            SDL_PushGPUFragmentUniformData(g->cmd, 0, &mix, sizeof(mix));
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
            g->stats.pipe_binds++;
            g->stats.tex_binds++;
        }
        if (!g->passes_logged) {
            g->passes_logged = true;
            mg_log_info("gfx: passes: main colour CLEAR/STORE, depth CLEAR/DONT_CARE; UI colour %s/STORE, no depth", shot ? "LOAD" : "DONT_CARE");
        }
    }

    g->ui_px_w = shot ? rw : g->swap_w;
    g->ui_px_h = shot ? rh : g->swap_h;
    if (g->ui_count > 0 && g->ui_atlas && g->ui_w_pt > 0.0f) { /* the whole UI: one instanced draw, clipping in the shader */
        struct { HMM_Vec4 screen, tilt, safe; } u = {HMM_V4(2.0f / g->ui_w_pt, 2.0f / g->ui_h_pt, 0.0f, 0.0f), HMM_V4(g->ui_tilt[0], g->ui_tilt[1], g->ui_tilt[2], 0.0f),
                                                    HMM_V4(g->ui_safe[0], g->ui_safe[1], g->ui_safe[2], g->ui_safe[3])};
        SDL_BindGPUGraphicsPipeline(pass, scaled ? g->ui_pipe_direct : g->ui_pipe);
        SDL_PushGPUVertexUniformData(g->cmd, 0, &u, sizeof(u));
        SDL_GPUBufferBinding vbb = {g->ui_vb_ring[g->ring % GFX_BUFFER_RING], 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vbb, 1);
        SDL_GPUTextureSamplerBinding tsb[2] = {{g->ui_atlas, g->ui_sampler}, {g->head_atlas ? g->head_atlas : g->dummy_tex, g->linear_clamp}};
        SDL_BindGPUFragmentSamplers(pass, 0, tsb, 2);
        g->stats.pipe_binds++;
        g->stats.tex_binds++;
        g->stats.uniform_bytes += (uint32_t)sizeof(u);
        SDL_Rect full = {0, 0, (int)g->ui_px_w, (int)g->ui_px_h};
        SDL_SetGPUScissor(pass, &full);
        SDL_DrawGPUPrimitives(pass, 6, g->ui_count, 0, 0);
        g->stats.draws++;
        g->stats.ui_draws++;
        g->stats.tris += g->ui_count * 2;
    }
    if (scaled && g->preview_on && g->preview_color && g->ui_w_pt > 0.0f) {
        /* over the panel it sits in (no UI is drawn on top of the preview) */
        SDL_Rect full = {0, 0, (int)g->ui_px_w, (int)g->ui_px_h};
        SDL_SetGPUScissor(pass, &full);
        draw_preview_image(g, pass);
    }
    SDL_EndGPURenderPass(pass);
    g->stats.main_cpu_ms = (float)((mg_time_now() - main0) * 1000.0);
    if (shot) {
        blit_scene_to_swap(g, rw, rh);
        save_screenshot(g, rw, rh);
        return;
    }
    if (swap_cmd) {
        int head = SDL_GetAtomicInt(&g->timing_head);
        g->timing[head % GPU_TIMING_RING].ui_fence = SDL_SubmitGPUCommandBufferAndAcquireFence(g->cmd);
        SDL_SetAtomicInt(&g->timing_head, head + 1);
        SDL_SignalSemaphore(g->timing_sem);
    } else {
        SDL_SubmitGPUCommandBuffer(g->cmd);
    }
    g->cmd = NULL;
}

void gfx_frame_end(GfxContext *g)
{
    const bool shot = g->shot_path[0] != 0; /* readback, PNG and its target texture may allocate */
    if (shot) {
        prof_allocs_exempt(true);
    }
    frame_end(g);
    g->ring++;
    if (shot) {
        prof_allocs_exempt(false);
    }
}

void gfx_request_screenshot(GfxContext *g, const char *path)
{
    SDL_snprintf(g->shot_path, sizeof(g->shot_path), "%s", path);
}

void gfx_get_stats(const GfxContext *g, GfxStats *out) { *out = g->stats; }

void gfx_get_memory(const GfxContext *g, GfxMemory *o)
{
    SDL_zerop(o);
    const uint32_t samples = (uint32_t)sample_count(g->msaa);
    const uint64_t color = SDL_CalculateGPUTextureFormatSize(g->color_fmt, g->target_w, g->target_h, 1);
    o->msaa_color = g->msaa_color ? color * samples : 0;
    o->depth = g->depth ? (uint64_t)SDL_CalculateGPUTextureFormatSize(g->depth_fmt, g->target_w, g->target_h, 1) * samples : 0;
    o->scene = g->scene_tex ? color : 0;
    o->blur = 0; /* no blur since UI-1 */
    o->preview = g->preview_color ? (uint64_t)GFX_PREVIEW_PX * GFX_PREVIEW_PX * 4u * (1u + (g->preview_msaa ? samples : 0u)) : 0;
    o->swapchain = (uint64_t)g->swap_w * g->swap_h * 4u * 3u;
    o->ui_atlas = g->ui_atlas_bytes;
    o->mesh_vb_reserved = GFX_VB_BYTES;
    o->mesh_ib_reserved = GFX_IB_BYTES;
    o->mesh_vb_used = g->vb_used;
    o->mesh_ib_used = g->ib_used;
    o->ui_buffers = GFX_MAX_UI * sizeof(GfxUiInst) * 2u * GFX_BUFFER_RING;
    o->instance_buffers = GFX_MAX_INSTANCES * (uint64_t)sizeof(GfxInstance) * 2u * GFX_BUFFER_RING;
}
