#include "assets.h"

#include <string.h>

const MgAsset *mg_asset_find(const char *name)
{
    for (const MgAsset *a = mg_assets; a->name; a++) {
        if (strcmp(a->name, name) == 0) {
            return a;
        }
    }
    return NULL;
}
