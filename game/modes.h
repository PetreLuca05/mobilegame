/* Multiplayer modes shown under Main menu -> Multiplayer. Adding a mode is
 * one row in game/modes.c (+ its strings and an entry screen/state). */
#ifndef MG_MODES_H
#define MG_MODES_H

#include "core/mg_types.h"

typedef struct ModeDef {
    const char *id;
    const char *name_key, *desc_key; /* assets/strings.json */
    int min_players, max_players;
    bool enabled;    /* false: greyed with a "Soon" tag */
    int entry_state; /* FeState opened when chosen */
} ModeDef;

int mode_count(void);
const ModeDef *mode_get(int i);
/* index by id, -1 if unknown */
int mode_find(const char *id);

#endif
