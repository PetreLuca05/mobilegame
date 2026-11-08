/* SDL3 GPU implementation of gfx.h.
 *
 * Passes per frame:
 *   copy pass        UI vertices for this frame
 *   reflection pass  half-res mirrored scene (sky + GFX_DRAW_REFLECT draws)
 *   main pass        sky, opaque by material, floor (samples reflection),
 *                    sprites back-to-front, UI; 4x MSAA resolved to swapchain
 */
#include <SDL3/SDL.h>
#include <string.h>

#include "core/assets.h"
#include "core/log.h"
#include "gfx.h"
#include "material.h"
#include "reflection.h"

#define GFX_MAX_MESHES 256
#define GFX_MAX_DRAWS 2048
#define GFX_VB_BYTES (8u << 20)
#define GFX_IB_BYTES (4u << 20)
#define GFX_MAX_UI_QUADS 8192
#define GFX_MAX_SHADERS 32
#define GFX_MAX_PANELS 4
#define GFX_MAX_UI_BATCHES 64
#define GFX_PREVIEW_PX 512

typedef struct GfxMeshRec {
    uint32_t first_index;
    uint32_t index_count;
    int32_t vertex_offset;
} GfxMeshRec;

typedef struct DrawItem {
    HMM_Mat4 model;
    HMM_Mat4 normal;
    HMM_Vec4 tint;
    HMM_Vec4 params;
    float sort;
    GfxMeshId mesh;
    uint8_t kind;
    uint8_t flags;
} DrawItem;

typedef struct DrawVSUniforms {
    HMM_Mat4 model;
    HMM_Mat4 normal;
} DrawVSUniforms;

typedef struct DrawFSUniforms {
    HMM_Vec4 tint;
    HMM_Vec4 params;
} DrawFSUniforms;

typedef struct FrameFSUniforms {
    HMM_Vec4 sun_dir;
    HMM_Vec4 sun_color;
    HMM_Vec4 ambient;
    HMM_Vec4 eye;
    HMM_Vec4 misc;
} FrameFSUniforms;

typedef struct SkyFSUniforms {
    HMM_Mat4 inv_view_proj;
    HMM_Vec4 eye;
    HMM_Vec4 horizon;
    HMM_Vec4 zenith;
} SkyFSUniforms;

enum { TARGET_MAIN, TARGET_REFL, TARGET_COUNT };
enum { LAYOUT_NONE, LAYOUT_MESH, LAYOUT_UI };

typedef struct PipelineSet {
    SDL_GPUGraphicsPipeline *mat[GFX_MAT_COUNT];
    SDL_GPUGraphicsPipeline *sky;
} PipelineSet;

typedef struct BlurPanel {
    float x, y, w, h, radius; /* pt */
    HMM_Vec4 tint;
} BlurPanel;

/* Run of UI quads sharing one clip rectangle. */
typedef struct UiBatch {
    uint32_t first, count;
    bool clipped;
    float clip[4]; /* x, y, w, h in pt */
} UiBatch;

typedef struct ShaderCacheEntry {
    const char *name;
    SDL_GPUShader *shader;
} ShaderCacheEntry;

struct GfxContext {
    SDL_GPUDevice *dev;
    SDL_Window *win;
    SDL_GPUShaderFormat shader_fmt;
    const char *backend;
    SDL_GPUTextureFormat color_fmt;
    SDL_GPUTextureFormat depth_fmt;
    SDL_GPUTextureFormat refl_fmt;
    SDL_GPUSampleCount msaa;

    PipelineSet pipes[TARGET_COUNT];
    SDL_GPUGraphicsPipeline *ui_pipe;        /* UI inside the MSAA main pass */
    SDL_GPUGraphicsPipeline *ui_pipe_direct; /* UI pass on the swapchain (render scale < 1) */
    SDL_GPUGraphicsPipeline *blur_pipe;      /* one Gaussian direction, quarter res */
    SDL_GPUGraphicsPipeline *panel_pipe;     /* frosted UI panel on the swapchain */
    BlurPanel panels[GFX_MAX_PANELS];
    uint32_t panel_count;
    bool keep_scaled; /* once a panel needed the offscreen scene, keep it (no target churn) */
    UiBatch ui_batches[GFX_MAX_UI_BATCHES];
    uint32_t ui_batch_count;
    /* settings preview: second camera into an offscreen square */
    bool preview_on;
    GfxSceneParams preview_scene;
    float preview_rect[4]; /* x, y, size, corner radius (pt) */
    HMM_Vec4 preview_bg;
    SDL_GPUTexture *preview_color, *preview_msaa, *preview_depth;
    ShaderCacheEntry shaders[GFX_MAX_SHADERS];
    uint32_t shader_count;

    SDL_GPUBuffer *vb;
    SDL_GPUBuffer *ib;
    uint32_t vb_used;
    uint32_t ib_used;
    GfxMeshRec meshes[GFX_MAX_MESHES];
    uint32_t mesh_count;

    SDL_GPUBuffer *ui_vb;
    SDL_GPUBuffer *ui_ib;
    SDL_GPUTransferBuffer *ui_tb;
    GfxUiVertex *ui_verts;
    uint32_t ui_quads;
    float ui_w_pt, ui_h_pt;
    SDL_GPUTexture *ui_atlas;
    SDL_GPUSampler *ui_sampler;
    SDL_GPUSampler *linear_clamp;

    SDL_GPUTexture *msaa_color;
    SDL_GPUTexture *depth;
    SDL_GPUTexture *refl_color;
    SDL_GPUTexture *refl_depth;
    SDL_GPUTexture *dummy_tex;
    SDL_GPUTexture *scene_tex; /* resolved 3D image when rendering below native size */
    SDL_GPUTexture *blur_a, *blur_b; /* quarter-res blurred copy of the scene for panels */
    uint32_t blur_w, blur_h;
    uint32_t target_w, target_h;
    float render_scale;

    SDL_GPUCommandBuffer *cmd;
    SDL_GPUTexture *swap;
    uint32_t swap_w, swap_h;
    bool in_frame;
    bool paused;
    GfxSceneParams scene;

    DrawItem *draws;
    uint16_t *order;
    uint32_t draw_count;
    GfxStats stats;
};

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
} PipeDesc;

static SDL_GPUGraphicsPipeline *make_pipeline(GfxContext *g, const PipeDesc *d)
{
    if (!d->vs || !d->fs) {
        return NULL;
    }
    SDL_GPUVertexBufferDescription vbd;
    SDL_zero(vbd);
    SDL_GPUVertexAttribute attrs[3];
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
    } else if (d->layout == LAYOUT_UI) {
        vbd.slot = 0;
        vbd.pitch = sizeof(GfxUiVertex);
        vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        attrs[0] = (SDL_GPUVertexAttribute){0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(GfxUiVertex, x)};
        attrs[1] = (SDL_GPUVertexAttribute){1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(GfxUiVertex, u)};
        attrs[2] = (SDL_GPUVertexAttribute){2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(GfxUiVertex, r)};
        nattr = 3;
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
    ci.vertex_input_state.vertex_buffer_descriptions = nattr ? &vbd : NULL;
    ci.vertex_input_state.num_vertex_buffers = nattr ? 1 : 0;
    ci.vertex_input_state.vertex_attributes = nattr ? attrs : NULL;
    ci.vertex_input_state.num_vertex_attributes = nattr;
    ci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode = d->cull;
    ci.rasterizer_state.front_face = d->front;
    ci.multisample_state.sample_count = d->samples;
    ci.depth_stencil_state.enable_depth_test = d->depth_test;
    ci.depth_stencil_state.enable_depth_write = d->depth_write;
    ci.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    ci.target_info.color_target_descriptions = &ctd;
    ci.target_info.num_color_targets = 1;
    ci.target_info.depth_stencil_format = d->no_depth ? SDL_GPU_TEXTUREFORMAT_INVALID : g->depth_fmt;
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
        const bool refl = (t == TARGET_REFL);
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
            };
            g->pipes[t].mat[k] = make_pipeline(g, &d);
            if (!g->pipes[t].mat[k]) {
                return false;
            }
        }
        PipeDesc sky = {
            .vs = load_shader(g, "sky.vert", 0, 0),
            .fs = load_shader(g, "sky.frag", 1, 0),
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
        .fs = load_shader(g, "ui.frag", 0, 1),
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
    PipeDesc blur = {
        .vs = load_shader(g, "fullscreen.vert", 0, 0),
        .fs = load_shader(g, "blur.frag", 1, 1),
        .layout = LAYOUT_NONE,
        .cull = SDL_GPU_CULLMODE_NONE,
        .front = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
        .color_fmt = g->color_fmt,
        .samples = SDL_GPU_SAMPLECOUNT_1,
        .no_depth = true,
    };
    g->blur_pipe = make_pipeline(g, &blur);
    PipeDesc panel = blur;
    panel.vs = load_shader(g, "panel.vert", 1, 0);
    panel.fs = load_shader(g, "panel.frag", 1, 1);
    panel.blend = true;
    g->panel_pipe = make_pipeline(g, &panel);
    release_shaders(g);
    return g->ui_pipe != NULL && g->ui_pipe_direct != NULL && g->blur_pipe != NULL && g->panel_pipe != NULL;
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
    SDL_GPUTexture **ts[] = {&g->msaa_color, &g->depth,  &g->refl_color, &g->refl_depth,
                             &g->scene_tex,  &g->blur_a, &g->blur_b};
    for (size_t i = 0; i < MG_ARRAY_COUNT(ts); i++) {
        if (*ts[i]) {
            SDL_ReleaseGPUTexture(g->dev, *ts[i]);
            *ts[i] = NULL;
        }
    }
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
    uint32_t rw = w / 2 > 0 ? w / 2 : 1;
    uint32_t rh = h / 2 > 0 ? h / 2 : 1;
    g->refl_color = make_texture(g, g->refl_fmt, rw, rh,
                                 SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                 SDL_GPU_SAMPLECOUNT_1);
    g->refl_depth = make_texture(g, g->depth_fmt, rw, rh, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
                                 SDL_GPU_SAMPLECOUNT_1);
    if (scaled) {
        g->scene_tex = make_texture(g, g->color_fmt, w, h,
                                    SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                    SDL_GPU_SAMPLECOUNT_1);
        g->blur_w = w / 4 > 0 ? w / 4 : 1;
        g->blur_h = h / 4 > 0 ? h / 4 : 1;
        for (int i = 0; i < 2; i++) {
            (i ? &g->blur_b : &g->blur_a)[0] =
                make_texture(g, g->color_fmt, g->blur_w, g->blur_h,
                             SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, SDL_GPU_SAMPLECOUNT_1);
        }
    }
    return g->depth && g->refl_color && g->refl_depth && (g->msaa == SDL_GPU_SAMPLECOUNT_1 || g->msaa_color) &&
           (!scaled || g->scene_tex);
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

bool gfx_init(GfxContext **out, struct SDL_Window *window, Arena *perm)
{
    GfxContext *g = ARENA_PUSH(perm, GfxContext, 1);
    g->draws = ARENA_PUSH(perm, DrawItem, GFX_MAX_DRAWS);
    g->order = ARENA_PUSH(perm, uint16_t, GFX_MAX_DRAWS);
    g->ui_verts = ARENA_PUSH(perm, GfxUiVertex, GFX_MAX_UI_QUADS * 4);
    if (!g || !g->draws || !g->order || !g->ui_verts) {
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
    g->msaa = SDL_GPU_SAMPLECOUNT_1;
    if (SDL_GPUTextureSupportsSampleCount(g->dev, g->color_fmt, SDL_GPU_SAMPLECOUNT_4) &&
        SDL_GPUTextureSupportsSampleCount(g->dev, g->depth_fmt, SDL_GPU_SAMPLECOUNT_4)) {
        g->msaa = SDL_GPU_SAMPLECOUNT_4;
    }
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

    SDL_GPUBufferCreateInfo uvci = {SDL_GPU_BUFFERUSAGE_VERTEX, GFX_MAX_UI_QUADS * 4 * sizeof(GfxUiVertex), 0};
    SDL_GPUBufferCreateInfo uici = {SDL_GPU_BUFFERUSAGE_INDEX, GFX_MAX_UI_QUADS * 6 * sizeof(uint16_t), 0};
    SDL_GPUTransferBufferCreateInfo utci = {SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                            GFX_MAX_UI_QUADS * 4 * sizeof(GfxUiVertex), 0};
    g->ui_vb = SDL_CreateGPUBuffer(g->dev, &uvci);
    g->ui_ib = SDL_CreateGPUBuffer(g->dev, &uici);
    g->ui_tb = SDL_CreateGPUTransferBuffer(g->dev, &utci);
    if (!g->vb || !g->ib || !g->ui_vb || !g->ui_ib || !g->ui_tb) {
        mg_log_error("gfx: buffer creation failed: %s", SDL_GetError());
        return false;
    }

    /* static quad index buffer for the UI */
    size_t mark = arena_mark(perm);
    uint16_t *qi = ARENA_PUSH(perm, uint16_t, GFX_MAX_UI_QUADS * 6);
    for (uint32_t q = 0; q < GFX_MAX_UI_QUADS; q++) {
        uint16_t b = (uint16_t)(q * 4);
        uint16_t *p = &qi[q * 6];
        p[0] = b;
        p[1] = (uint16_t)(b + 1);
        p[2] = (uint16_t)(b + 2);
        p[3] = b;
        p[4] = (uint16_t)(b + 2);
        p[5] = (uint16_t)(b + 3);
    }
    upload_bytes(g, g->ui_ib, 0, qi, GFX_MAX_UI_QUADS * 6 * sizeof(uint16_t));
    arena_reset_to(perm, mark);

    SDL_GPUSamplerCreateInfo sci;
    SDL_zero(sci);
    sci.min_filter = SDL_GPU_FILTER_LINEAR;
    sci.mag_filter = SDL_GPU_FILTER_LINEAR;
    sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    g->linear_clamp = SDL_CreateGPUSampler(g->dev, &sci);
    g->ui_sampler = g->linear_clamp;

    const uint32_t white = 0xFFFFFFFFu;
    g->dummy_tex = make_texture(g, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 1, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                SDL_GPU_SAMPLECOUNT_1);
    if (!g->linear_clamp || !g->dummy_tex) {
        return false;
    }
    upload_texture(g, g->dummy_tex, &white, 1, 1, 4);
    g->stats.backend = g->backend;
    g->render_scale = 1.0f;
    *out = g;
    return true;
}

void gfx_shutdown(GfxContext *g)
{
    if (!g || !g->dev) {
        return;
    }
    SDL_WaitForGPUIdle(g->dev);
    release_targets(g);
    SDL_GPUTexture *pv[] = {g->preview_color, g->preview_msaa, g->preview_depth};
    for (size_t i = 0; i < MG_ARRAY_COUNT(pv); i++) {
        if (pv[i]) {
            SDL_ReleaseGPUTexture(g->dev, pv[i]);
        }
    }
    for (int t = 0; t < TARGET_COUNT; t++) {
        for (int k = 0; k < GFX_MAT_COUNT; k++) {
            if (g->pipes[t].mat[k]) {
                SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipes[t].mat[k]);
            }
        }
        if (g->pipes[t].sky) {
            SDL_ReleaseGPUGraphicsPipeline(g->dev, g->pipes[t].sky);
        }
    }
    if (g->ui_pipe) {
        SDL_ReleaseGPUGraphicsPipeline(g->dev, g->ui_pipe);
    }
    if (g->blur_pipe) {
        SDL_ReleaseGPUGraphicsPipeline(g->dev, g->blur_pipe);
    }
    if (g->panel_pipe) {
        SDL_ReleaseGPUGraphicsPipeline(g->dev, g->panel_pipe);
    }
    if (g->ui_pipe_direct) {
        SDL_ReleaseGPUGraphicsPipeline(g->dev, g->ui_pipe_direct);
    }
    SDL_ReleaseGPUBuffer(g->dev, g->vb);
    SDL_ReleaseGPUBuffer(g->dev, g->ib);
    SDL_ReleaseGPUBuffer(g->dev, g->ui_vb);
    SDL_ReleaseGPUBuffer(g->dev, g->ui_ib);
    SDL_ReleaseGPUTransferBuffer(g->dev, g->ui_tb);
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
    g->ui_quads = 0;
    g->panel_count = 0;
    g->ui_batch_count = 1;
    g->ui_batches[0] = (UiBatch){0, 0, false, {0, 0, 0, 0}};
    g->preview_on = false;
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
    if (!g->in_frame || mesh == GFX_MESH_NONE || mesh >= g->mesh_count || g->draw_count >= GFX_MAX_DRAWS) {
        return;
    }
    DrawItem *d = &g->draws[g->draw_count++];
    d->model = *model;
    if (mat->kind == GFX_MAT_SPRITE || mat->kind == GFX_MAT_TEXT) {
        d->normal = HMM_M4D(1.0f); /* sprites and glyphs carry UVs in the normal */
    } else {
        d->normal = HMM_TransposeM4(HMM_InvGeneralM4(*model));
    }
    d->tint = mat->tint;
    d->params = mat->params;
    d->mesh = mesh;
    d->kind = (uint8_t)mat->kind;
    d->flags = (uint8_t)flags;
    HMM_Vec3 pos = model->Columns[3].XYZ;
    d->sort = HMM_LenSqrV3(HMM_SubV3(pos, g->scene.eye));
}

GfxUiVertex *gfx_ui_quads(GfxContext *g, uint32_t quad_count)
{
    if (!g->in_frame || g->ui_quads + quad_count > GFX_MAX_UI_QUADS) {
        return NULL;
    }
    GfxUiVertex *v = &g->ui_verts[g->ui_quads * 4];
    g->ui_quads += quad_count;
    g->ui_batches[g->ui_batch_count - 1].count += quad_count;
    return v;
}

static void ui_new_batch(GfxContext *g, bool clipped, float x, float y, float w, float h)
{
    UiBatch *b = &g->ui_batches[g->ui_batch_count - 1];
    if (b->count > 0) {
        if (g->ui_batch_count >= GFX_MAX_UI_BATCHES) {
            return;
        }
        b = &g->ui_batches[g->ui_batch_count++];
    }
    *b = (UiBatch){g->ui_quads, 0, clipped, {x, y, w, h}};
}

void gfx_ui_clip(GfxContext *g, float x, float y, float w, float h) { ui_new_batch(g, true, x, y, w, h); }

void gfx_ui_clip_off(GfxContext *g) { ui_new_batch(g, false, 0, 0, 0, 0); }

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

/* Opaque draws grouped by material, then sprites far-to-near. */
static void sort_draws(GfxContext *g)
{
    uint32_t n = g->draw_count;
    for (uint32_t i = 0; i < n; i++) {
        g->order[i] = (uint16_t)i;
    }
    for (uint32_t i = 1; i < n; i++) {
        uint16_t key = g->order[i];
        const DrawItem *a = &g->draws[key];
        uint32_t j = i;
        while (j > 0) {
            const DrawItem *b = &g->draws[g->order[j - 1]];
            bool before;
            if (a->kind != b->kind) {
                before = a->kind < b->kind;
            } else if (a->kind == GFX_MAT_SPRITE) {
                before = a->sort > b->sort;
            } else {
                before = a->mesh < b->mesh;
            }
            if (!before) {
                break;
            }
            g->order[j] = g->order[j - 1];
            j--;
        }
        g->order[j] = key;
    }
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
    SDL_PushGPUFragmentUniformData(g->cmd, 1, &f, sizeof(f));
}

static void draw_sky(GfxContext *g, SDL_GPURenderPass *pass, SDL_GPUGraphicsPipeline *pipe,
                     const HMM_Mat4 *inv_vp, HMM_Vec3 eye)
{
    SkyFSUniforms s;
    s.inv_view_proj = *inv_vp;
    s.eye = HMM_V4V(eye, 1.0f);
    s.horizon = g->scene.sky_horizon;
    s.zenith = g->scene.sky_zenith;
    SDL_BindGPUGraphicsPipeline(pass, pipe);
    SDL_PushGPUFragmentUniformData(g->cmd, 0, &s, sizeof(s));
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
}

static uint32_t draw_list(GfxContext *g, SDL_GPURenderPass *pass, int target, bool preview)
{
    SDL_GPUBufferBinding vbb = {g->vb, 0};
    SDL_GPUBufferBinding ibb = {g->ib, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vbb, 1);
    SDL_BindGPUIndexBuffer(pass, &ibb, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    int bound = -1;
    uint32_t count = 0;
    for (uint32_t i = 0; i < g->draw_count; i++) {
        const DrawItem *d = &g->draws[g->order[i]];
        if (((d->flags & GFX_DRAW_PREVIEW) != 0) != preview) {
            continue;
        }
        if (target == TARGET_REFL) {
            if (!(d->flags & GFX_DRAW_REFLECT) || d->kind == GFX_MAT_FLOOR) {
                continue;
            }
        } else if (d->flags & GFX_DRAW_NO_MAIN) {
            continue;
        }
        if (bound != d->kind) {
            SDL_BindGPUGraphicsPipeline(pass, g->pipes[target].mat[d->kind]);
            bound = d->kind;
            if (d->kind == GFX_MAT_FLOOR) {
                SDL_GPUTextureSamplerBinding tsb = {
                    g->scene.reflection_strength > 0.0f ? g->refl_color : g->dummy_tex, g->linear_clamp};
                SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
            } else if (d->kind == GFX_MAT_TEXT) {
                SDL_GPUTextureSamplerBinding tsb = {g->ui_atlas ? g->ui_atlas : g->dummy_tex, g->ui_sampler};
                SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
            }
        }
        DrawVSUniforms vu = {d->model, d->normal};
        DrawFSUniforms fu = {d->tint, d->params};
        SDL_PushGPUVertexUniformData(g->cmd, 0, &vu, sizeof(vu));
        SDL_PushGPUFragmentUniformData(g->cmd, 0, &fu, sizeof(fu));
        const GfxMeshRec *m = &g->meshes[d->mesh];
        SDL_DrawGPUIndexedPrimitives(pass, m->index_count, 1, m->first_index, m->vertex_offset, 0);
        count++;
    }
    return count;
}

/* Quarter-res copy of the finished 3D image, blurred H then V (blur_a). */
static void blur_scene(GfxContext *g, uint32_t rw, uint32_t rh)
{
    if (!g->blur_a || !g->blur_b) {
        return;
    }
    SDL_GPUBlitInfo blit;
    SDL_zero(blit);
    blit.source.texture = g->scene_tex;
    blit.source.w = rw;
    blit.source.h = rh;
    blit.destination.texture = g->blur_a;
    blit.destination.w = g->blur_w;
    blit.destination.h = g->blur_h;
    blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blit.filter = SDL_GPU_FILTER_LINEAR;
    SDL_BlitGPUTexture(g->cmd, &blit);
    for (int pass_i = 0; pass_i < 2; pass_i++) {
        SDL_GPUTexture *src = pass_i == 0 ? g->blur_a : g->blur_b;
        SDL_GPUTexture *dst = pass_i == 0 ? g->blur_b : g->blur_a;
        SDL_GPUColorTargetInfo cti;
        SDL_zero(cti);
        cti.texture = dst;
        cti.load_op = SDL_GPU_LOADOP_DONT_CARE;
        cti.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(g->cmd, &cti, 1, NULL);
        SDL_BindGPUGraphicsPipeline(rp, g->blur_pipe);
        HMM_Vec4 dir = pass_i == 0 ? HMM_V4(1.0f / (float)g->blur_w, 0, 0, 0) : HMM_V4(0, 1.0f / (float)g->blur_h, 0, 0);
        SDL_PushGPUFragmentUniformData(g->cmd, 0, &dir, sizeof(dir));
        SDL_GPUTextureSamplerBinding tsb = {src, g->linear_clamp};
        SDL_BindGPUFragmentSamplers(rp, 0, &tsb, 1);
        SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
        SDL_EndGPURenderPass(rp);
    }
}

static void draw_preview_image(GfxContext *g, SDL_GPURenderPass *pass)
{
    const float *r = g->preview_rect;
    float px_per_pt = (float)g->swap_w / g->ui_w_pt;
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
    draw_list(g, rp, TARGET_MAIN, true);
    SDL_EndGPURenderPass(rp);
}

static void draw_panels(GfxContext *g, SDL_GPURenderPass *pass)
{
    if (!g->blur_a) {
        return;
    }
    float px_per_pt = (float)g->swap_w / g->ui_w_pt;
    SDL_BindGPUGraphicsPipeline(pass, g->panel_pipe);
    SDL_GPUTextureSamplerBinding tsb = {g->blur_a, g->linear_clamp};
    SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
    for (uint32_t i = 0; i < g->panel_count; i++) {
        const BlurPanel *p = &g->panels[i];
        HMM_Vec4 ndc = HMM_V4(p->x / g->ui_w_pt * 2.0f - 1.0f, 1.0f - p->y / g->ui_h_pt * 2.0f,
                              (p->x + p->w) / g->ui_w_pt * 2.0f - 1.0f, 1.0f - (p->y + p->h) / g->ui_h_pt * 2.0f);
        struct {
            HMM_Vec4 rect, params, tint, mode;
        } fu = {HMM_V4(p->x, p->y, p->w, p->h),
                HMM_V4(p->radius, px_per_pt, 1.0f / (float)g->swap_w, 1.0f / (float)g->swap_h), p->tint,
                HMM_V4(0, 0, 0, 0)};
        SDL_PushGPUVertexUniformData(g->cmd, 0, &ndc, sizeof(ndc));
        SDL_PushGPUFragmentUniformData(g->cmd, 0, &fu, sizeof(fu));
        SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
    }
}

void gfx_ui_blur_panel(GfxContext *g, float x, float y, float w, float h, float radius, HMM_Vec4 tint)
{
    if (!g->in_frame || g->panel_count >= GFX_MAX_PANELS) {
        return;
    }
    g->panels[g->panel_count++] = (BlurPanel){x, y, w, h, radius, tint};
}

void gfx_frame_end(GfxContext *g)
{
    if (!g->in_frame) {
        return;
    }
    g->in_frame = false;
    g->stats.draws = 0;
    g->stats.reflect_draws = 0;
    g->stats.ui_quads = g->ui_quads;
    uint32_t rw = (uint32_t)((float)g->swap_w * g->render_scale + 0.5f);
    uint32_t rh = (uint32_t)((float)g->swap_h * g->render_scale + 0.5f);
    rw = rw > 0 ? rw : 1;
    rh = rh > 0 ? rh : 1;
    g->keep_scaled = g->keep_scaled || g->panel_count > 0 || g->preview_on;
    const bool scaled = rw != g->swap_w || rh != g->swap_h || g->keep_scaled;
    g->stats.width_px = rw;
    g->stats.height_px = rh;
    g->stats.msaa = g->msaa == SDL_GPU_SAMPLECOUNT_4 ? 4 : 1;

    if (!ensure_targets(g, rw, rh, scaled)) {
        SDL_SubmitGPUCommandBuffer(g->cmd);
        g->cmd = NULL;
        return;
    }

    /* UI vertices for this frame */
    if (g->ui_quads > 0) {
        uint32_t bytes = g->ui_quads * 4 * (uint32_t)sizeof(GfxUiVertex);
        void *map = SDL_MapGPUTransferBuffer(g->dev, g->ui_tb, true);
        memcpy(map, g->ui_verts, bytes);
        SDL_UnmapGPUTransferBuffer(g->dev, g->ui_tb);
        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(g->cmd);
        SDL_GPUTransferBufferLocation src = {g->ui_tb, 0};
        SDL_GPUBufferRegion region = {g->ui_vb, 0, bytes};
        SDL_UploadToGPUBuffer(cp, &src, &region, true);
        SDL_EndGPUCopyPass(cp);
    }

    sort_draws(g);
    const GfxSceneParams *s = &g->scene;
    HMM_Mat4 view_proj = HMM_MulM4(s->proj, s->view);
    HMM_Mat4 inv_vp = HMM_InvGeneralM4(view_proj);
    SDL_FColor horizon = {s->sky_horizon.X, s->sky_horizon.Y, s->sky_horizon.Z, 1.0f};

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
        g->stats.reflect_draws = draw_list(g, rp, TARGET_REFL, false);
        SDL_EndGPURenderPass(rp);
    }

    if (g->preview_on) {
        render_preview(g);
    }

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
    draw_sky(g, pass, g->pipes[TARGET_MAIN].sky, &inv_vp, s->eye);
    push_frame_uniforms(g, &view_proj, s->eye, (float)g->target_w, (float)g->target_h, s->reflection_strength);
    g->stats.draws = draw_list(g, pass, TARGET_MAIN, false);

    if (scaled) {
        /* upscale the 3D image to the swapchain, then draw UI at native size */
        SDL_EndGPURenderPass(pass);
        if (g->panel_count > 0) {
            blur_scene(g, rw, rh);
        }
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
        SDL_GPUColorTargetInfo uti;
        SDL_zero(uti);
        uti.texture = g->swap;
        uti.load_op = SDL_GPU_LOADOP_LOAD;
        uti.store_op = SDL_GPU_STOREOP_STORE;
        pass = SDL_BeginGPURenderPass(g->cmd, &uti, 1, NULL);
    }

    if (scaled && g->panel_count > 0 && g->ui_w_pt > 0.0f) {
        draw_panels(g, pass);
    }
    if (g->ui_quads > 0 && g->ui_atlas && g->ui_w_pt > 0.0f) {
        HMM_Vec4 screen = HMM_V4(2.0f / g->ui_w_pt, 2.0f / g->ui_h_pt, 0.0f, 0.0f);
        SDL_BindGPUGraphicsPipeline(pass, scaled ? g->ui_pipe_direct : g->ui_pipe);
        SDL_PushGPUVertexUniformData(g->cmd, 0, &screen, sizeof(screen));
        SDL_GPUBufferBinding vbb = {g->ui_vb, 0};
        SDL_GPUBufferBinding ibb = {g->ui_ib, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vbb, 1);
        SDL_BindGPUIndexBuffer(pass, &ibb, SDL_GPU_INDEXELEMENTSIZE_16BIT);
        SDL_GPUTextureSamplerBinding tsb = {g->ui_atlas, g->ui_sampler};
        SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
        float k = (float)g->swap_w / g->ui_w_pt; /* pt -> px */
        SDL_Rect full = {0, 0, (int)g->swap_w, (int)g->swap_h};
        for (uint32_t i = 0; i < g->ui_batch_count; i++) {
            const UiBatch *b = &g->ui_batches[i];
            if (b->count == 0) {
                continue;
            }
            SDL_Rect r = full;
            if (b->clipped) {
                r = (SDL_Rect){(int)(b->clip[0] * k), (int)(b->clip[1] * k), (int)(b->clip[2] * k + 0.5f),
                               (int)(b->clip[3] * k + 0.5f)};
                SDL_Rect out;
                r = SDL_GetRectIntersection(&r, &full, &out) ? out : (SDL_Rect){0, 0, 0, 0};
                if (r.w <= 0 || r.h <= 0) {
                    continue;
                }
            }
            SDL_SetGPUScissor(pass, &r);
            SDL_DrawGPUIndexedPrimitives(pass, b->count * 6, 1, b->first * 6, 0, 0);
        }
    }
    if (scaled && g->preview_on && g->preview_color && g->ui_w_pt > 0.0f) {
        /* over the panel it sits in (no UI is drawn on top of the preview) */
        SDL_Rect full = {0, 0, (int)g->swap_w, (int)g->swap_h};
        SDL_SetGPUScissor(pass, &full);
        draw_preview_image(g, pass);
    }
    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(g->cmd);
    g->cmd = NULL;
}

void gfx_get_stats(const GfxContext *g, GfxStats *out) { *out = g->stats; }
