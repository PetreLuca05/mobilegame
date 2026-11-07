/* Files embedded into the binary at build time (cmake/embed_files.cmake):
 * compiled shaders, the SDF font atlas and the TLS CA bundle. */
#ifndef MG_ASSETS_H
#define MG_ASSETS_H

#include <stddef.h>

typedef struct MgAsset {
    const char *name; /* path relative to the repo root, e.g. "shaders/compiled/spv/ui.vert.spv" */
    const unsigned char *data;
    size_t size; /* data is additionally NUL-terminated */
} MgAsset;

extern const MgAsset mg_assets[];

const MgAsset *mg_asset_find(const char *name);

#endif
