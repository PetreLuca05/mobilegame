#include "spatial.h"

#include <string.h>

static int cell_x(const SpatialGrid *s, float x)
{
    int c = (int)((x - s->x0) / SPATIAL_CELL);
    return c < 0 ? 0 : (c >= s->cols ? s->cols - 1 : c);
}

static int cell_z(const SpatialGrid *s, float z)
{
    int c = (int)((z - s->z0) / SPATIAL_CELL);
    return c < 0 ? 0 : (c >= s->rows ? s->rows - 1 : c);
}

bool spatial_init(SpatialGrid *s, Arena *perm, float x0, float z0, float x1, float z1, int layers, int capacity)
{
    s->x0 = x0;
    s->z0 = z0;
    s->cols = (int)((x1 - x0) / SPATIAL_CELL) + 1;
    s->rows = (int)((z1 - z0) / SPATIAL_CELL) + 1;
    s->layers = layers;
    s->capacity = capacity;
    s->count = 0;
    s->start = ARENA_PUSH(perm, uint32_t, (size_t)(s->cols * s->rows * layers) + 1);
    s->items = ARENA_PUSH(perm, uint16_t, (size_t)capacity);
    return s->start && s->items;
}

void spatial_build(SpatialGrid *s, const float *x, const float *z, const int8_t *layer, size_t stride, int n, Arena *scratch)
{
    const int cells = s->cols * s->rows * s->layers;
    n = n > s->capacity ? s->capacity : n;
    const size_t mark = arena_mark(scratch);
    uint32_t *key = ARENA_PUSH(scratch, uint32_t, (size_t)n);
    uint32_t *fill = ARENA_PUSH(scratch, uint32_t, (size_t)cells);
    memset(s->start, 0, sizeof(uint32_t) * (size_t)(cells + 1));
    for (int i = 0; i < n; i++) {
        const float px = *(const float *)(const void *)((const char *)x + (size_t)i * stride);
        const float pz = *(const float *)(const void *)((const char *)z + (size_t)i * stride);
        int l = *(const int8_t *)((const char *)layer + (size_t)i * stride) + 1; /* -1 yard */
        l = l < 0 ? 0 : (l >= s->layers ? s->layers - 1 : l);
        key[i] = (uint32_t)((l * s->rows + cell_z(s, pz)) * s->cols + cell_x(s, px));
        s->start[key[i] + 1]++;
    }
    for (int c = 0; c < cells; c++) {
        fill[c] = s->start[c];
        s->start[c + 1] += s->start[c];
    }
    /* inside a cell, ascending entity index: the same result on any thread count */
    for (int i = 0; i < n; i++) {
        s->items[fill[key[i]]++] = (uint16_t)i;
    }
    s->count = n;
    arena_reset_to(scratch, mark);
}

int spatial_query(const SpatialGrid *s, int layer, float x, float z, float radius, uint16_t *out, int max)
{
    const int l = layer + 1;
    if (l < 0 || l >= s->layers) {
        return 0;
    }
    int found = 0;
    const int cx0 = cell_x(s, x - radius), cx1 = cell_x(s, x + radius);
    const int cz0 = cell_z(s, z - radius), cz1 = cell_z(s, z + radius);
    for (int cz = cz0; cz <= cz1; cz++) {
        const int row = (l * s->rows + cz) * s->cols;
        for (uint32_t i = s->start[row + cx0]; i < s->start[row + cx1 + 1] && found < max; i++) {
            out[found++] = s->items[i];
        }
    }
    return found;
}
