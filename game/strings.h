/* UI text from assets/strings.json (no user-facing strings in C).
 * str("key") returns the key itself when it is missing, so gaps show. */
#ifndef MG_STRINGS_H
#define MG_STRINGS_H

#include "core/arena.h"
#include "core/mg_types.h"

bool strings_init(Arena *perm);
const char *str(const char *key);
/* Lookups since start that fell back to English (no Romanian entry) / found no entry at all (the raw key showed);
 * `last` gets the latest such key. --autotest ui fails on either (Romanian is the game's only language). */
int strings_untranslated(const char **last);
int strings_missing(const char **last);

#endif
