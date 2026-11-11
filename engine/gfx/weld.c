#include "weld.h"

#include <string.h>

uint32_t mesh_weld(const GfxVertex *v, uint32_t n, GfxVertex *out, uint32_t *idx, uint32_t *table, uint32_t cap)
{
    memset(table, 0xFF, sizeof(uint32_t) * cap);
    uint32_t nv = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *b = (const uint8_t *)&v[i];
        uint32_t h = 2166136261u; /* FNV-1a over the vertex's 28 bytes */
        for (size_t k = 0; k < sizeof(GfxVertex); k++) {
            h = (h ^ b[k]) * 16777619u;
        }
        for (h &= cap - 1;; h = (h + 1) & (cap - 1)) {
            if (table[h] == UINT32_MAX) {
                table[h] = nv;
                out[nv] = v[i];
                idx[i] = nv++;
                break;
            }
            if (memcmp(&out[table[h]], &v[i], sizeof(GfxVertex)) == 0) {
                idx[i] = table[h];
                break;
            }
        }
    }
    return nv;
}
