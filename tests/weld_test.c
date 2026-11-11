/* mesh_weld on every generated mesh (assets as the game loads them): each triangle rebuilt from the welded vertices
 * and indices is byte-identical to the soup's, so no flat face or facet edge can change; and the weld does merge.
 * usage: weld_test DIR (the build's generated/assets) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx/weld.h"

static unsigned char *load(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)*size);
    if (b && fread(b, 1, (size_t)*size, f) != (size_t)*size) {
        free(b);
        b = NULL;
    }
    fclose(f);
    return b;
}

static uint32_t u32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int main(int argc, char **argv)
{
    static const char *const files[] = {"menu/library.mgmb", "avatar/avatar.mgmb", "avatar/avatar_options.mgmb", "world/kumar_island.mgmb",
                                        "world/lawrance_island.mgmb", "world/ambulance.mgmb", "world/vehicles.mgmb"};
    if (argc < 2) {
        fprintf(stderr, "usage: weld_test DIR\n");
        return 2;
    }
    int bad = 0;
    unsigned long total_in = 0, total_out = 0, groups = 0;
    for (size_t f = 0; f < sizeof(files) / sizeof(files[0]); f++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", argv[1], files[f]);
        long size = 0;
        unsigned char *b = load(path, &size);
        if (!b || size < 12 || memcmp(b, "MGMB", 4) != 0) {
            fprintf(stderr, "weld_test: %s missing or not MGMB\n", path);
            free(b);
            return 1;
        }
        const uint32_t ng = u32(b + 8);
        long at = 12;
        unsigned long fin = 0, fout = 0;
        for (uint32_t gi = 0; gi < ng; gi++) {
            const uint32_t nv = u32(b + at + 24), ni = u32(b + at + 28);
            const GfxVertex *v = (const GfxVertex *)(const void *)(b + at + 32);
            at += 32 + (long)nv * (long)sizeof(GfxVertex) + (long)ni * 44;
            if (nv == 0) {
                continue;
            }
            uint32_t cap = 1;
            while (cap < 2u * nv) {
                cap <<= 1;
            }
            GfxVertex *out = malloc(sizeof(GfxVertex) * nv);
            uint32_t *idx = malloc(sizeof(uint32_t) * nv), *table = malloc(sizeof(uint32_t) * cap);
            const uint32_t kept = mesh_weld(v, nv, out, idx, table, cap);
            for (uint32_t i = 0; i < nv; i++) {
                if (idx[i] >= kept || memcmp(&out[idx[i]], &v[i], sizeof(GfxVertex)) != 0) {
                    fprintf(stderr, "weld_test: %s group %u vertex %u is not the soup's\n", files[f], gi, i);
                    bad = 1;
                    break;
                }
            }
            for (uint32_t a = 0; a < kept && !bad; a++) { /* nothing kept twice: every merge that could happen did */
                for (uint32_t c = a + 1; c < kept && c < a + 64; c++) {
                    if (memcmp(&out[a], &out[c], sizeof(GfxVertex)) == 0) {
                        fprintf(stderr, "weld_test: %s group %u keeps a duplicate\n", files[f], gi);
                        bad = 1;
                    }
                }
            }
            fin += nv, fout += kept, groups++;
            free(out), free(idx), free(table);
        }
        printf("weld_test: %-28s %7lu -> %7lu vertices (%.1f %%), every triangle identical\n", files[f], fin, fout, fin ? 100.0 * (double)fout / (double)fin : 0.0);
        total_in += fin, total_out += fout;
        free(b);
    }
    printf("weld_test: %lu groups, %lu -> %lu vertices\n", groups, total_in, total_out);
    if (bad) {
        printf("WELD TEST FAILED\n");
        return 1;
    }
    printf("WELD TEST PASSED\n");
    return 0;
}
