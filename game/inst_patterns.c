/* The institute's detail textures (see inst_patterns.h): generated at build time by tools/gen_patterns.py into the
 * embedded assets/world/inst_patterns.bin ("MGP1", size, layers, levels, then each layer's mip chain), uploaded once. */
#include "inst_patterns.h"

#include <string.h>

#include "core/assets.h"
#include "core/log.h"

/* KINDS of the page, by COL value (KIND_OF) */
int inst_pattern_of(uint32_t hex)
{
    switch (hex) {
    case 0xe8ecf0u: case 0xdfe9f2u: case 0xcfe3eau: case 0xdde8e1u: case 0xd5dbe0u: case 0xd9dfe8u: case 0xe3e8ecu:
    case 0xf0e6d0u: case 0xefe6d2u:
        return INST_PAT_TILE; /* f_exam f_lab f_ward f_or f_ed f_wc f_img corr f_recep f_pharm */
    case 0xe9dcc6u: case 0xe6dccbu: case 0xb98a5au: case 0x8a6238u:
        return INST_PAT_WOOD; /* f_meet f_office wood wood2 */
    case 0xcfc9beu: case 0xd9d4c8u: case 0xc4beb0u:
        return INST_PAT_PAVING; /* f_terrace pave curb */
    case 0xf3ead8u: case 0xe9dcc2u: case 0xd8c9aau:
        return INST_PAT_PLASTER; /* wall wallx trim */
    case 0xb9694au: return INST_PAT_BRICK;
    case 0x8a929au: return INST_PAT_ASPHALT; /* road */
    case 0x7cc95au: case 0x8fd764u: return INST_PAT_GRASS;
    case 0xb9c0c7u: case 0x9aa3abu: return INST_PAT_GRAVEL; /* roof roof2 */
    case 0x55c3e0u: return INST_PAT_WATER;
    case 0x4a9a44u: case 0x3f9a4au: case 0x57b356u: case 0x4c9f45u:
        return INST_PAT_HEDGE; /* hedge leaf leaf2 plant */
    case 0x7fc4e8u: return INST_PAT_GLASS; /* COL.glass (the blankets share it, as on the page) */
    default: return INST_PAT_NONE;
    }
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

bool inst_patterns_upload(GfxContext *g)
{
    if (!g || gfx_has_patterns(g)) {
        return true;
    }
    const MgAsset *a = mg_asset_find("assets/world/inst_patterns.bin");
    if (!a || a->size < 16 || memcmp(a->data, "MGP1", 4) != 0) {
        mg_log_error("inst: assets/world/inst_patterns.bin missing or not MGP1");
        return false;
    }
    const uint8_t *d = a->data;
    const uint32_t size = rd32(d + 4), layers = rd32(d + 8), levels = rd32(d + 12);
    size_t chain = 0;
    for (uint32_t m = 0; m < levels; m++) {
        const size_t s = size >> m ? size >> m : 1;
        chain += s * s;
    }
    if (size != INST_PAT_SIZE || layers != INST_PAT_LAYERS || a->size != 16 + chain * layers) {
        mg_log_error("inst: inst_patterns.bin: %u x %u x %u levels does not fit its %zu bytes", size, layers, levels, (size_t)a->size);
        return false;
    }
    return gfx_set_patterns(g, d + 16, size, layers, levels);
}
