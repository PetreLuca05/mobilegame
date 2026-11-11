/* The renderer's mesh pool bookkeeping: meshes are carved linearly out of one
 * vertex and one index buffer, so the only way to free is a stack: take a
 * mark, upload, reset to the mark (everything uploaded after it goes, every
 * id below it stays valid). Pure arithmetic, no GPU: gpu.c owns the buffers
 * and tests/test_level_mem.c checks this part on its own. */
#ifndef MG_MESH_POOL_H
#define MG_MESH_POOL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct GfxMeshMark {
    uint32_t meshes;   /* mesh count at the mark (the first id that goes on a reset) */
    uint32_t vb_bytes; /* vertex pool bytes used at the mark */
    uint32_t ib_bytes; /* index pool bytes used at the mark */
} GfxMeshMark;

typedef struct GfxMeshPool {
    uint32_t count, max;           /* meshes */
    uint32_t vb_used, vb_cap;      /* bytes */
    uint32_t ib_used, ib_cap;
} GfxMeshPool;

/* Room for one more mesh of these sizes? */
static inline bool mesh_pool_fits(const GfxMeshPool *p, uint32_t vbytes, uint32_t ibytes)
{
    return p->count < p->max && p->vb_used + vbytes <= p->vb_cap && p->ib_used + ibytes <= p->ib_cap;
}

/* Takes the room; the caller has checked mesh_pool_fits. Returns the new id. */
static inline uint32_t mesh_pool_take(GfxMeshPool *p, uint32_t vbytes, uint32_t ibytes)
{
    p->vb_used += vbytes;
    p->ib_used += ibytes;
    return p->count++;
}

static inline GfxMeshMark mesh_pool_mark(const GfxMeshPool *p)
{
    const GfxMeshMark m = {p->count, p->vb_used, p->ib_used};
    return m;
}

/* Drops everything taken after `m`. A mark from above the current top (stale:
 * the pool was already reset below it) changes nothing and returns false. */
static inline bool mesh_pool_reset_to(GfxMeshPool *p, GfxMeshMark m)
{
    if (m.meshes > p->count || m.vb_bytes > p->vb_used || m.ib_bytes > p->ib_used) {
        return false;
    }
    p->count = m.meshes;
    p->vb_used = m.vb_bytes;
    p->ib_used = m.ib_bytes;
    return true;
}

#endif
