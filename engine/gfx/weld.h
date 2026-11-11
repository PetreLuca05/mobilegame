/* Welding a triangle soup: identical vertices (all 28 bytes: position, normal, colour) become one, so a flat quad's
 * two triangles share their corners. Nothing that differs in any byte is merged, so the drawn triangles are exactly
 * the soup's. Pure (no GPU), for mesh_upload_welded and tests/weld_test.c. */
#ifndef MG_WELD_H
#define MG_WELD_H

#include <stdint.h>

#include "gfx/gfx.h"

/* out: room for n vertices; idx: n indices; table: cap entries, cap a power of two >= 2 n. Returns the vertices kept. */
uint32_t mesh_weld(const GfxVertex *v, uint32_t n, GfxVertex *out, uint32_t *idx, uint32_t *table, uint32_t cap);

#endif
