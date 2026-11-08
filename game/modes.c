#include "modes.h"

#include <string.h>

#include "frontend.h"

static const ModeDef k_modes[] = {
    {"free_roam", "mode_free_roam", "mode_free_roam_desc", 2, 4, true, FE_FREE_ROAM},
    {"coop_shift", "mode_coop", "mode_coop_desc", 2, 4, false, FE_MAIN_MENU},
    {"versus", "mode_versus", "mode_versus_desc", 2, 4, false, FE_MAIN_MENU},
};

int mode_count(void) { return (int)(sizeof(k_modes) / sizeof(k_modes[0])); }

const ModeDef *mode_get(int i) { return &k_modes[i >= 0 && i < mode_count() ? i : 0]; }

int mode_find(const char *id)
{
    for (int i = 0; i < mode_count(); i++) {
        if (strcmp(k_modes[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}
