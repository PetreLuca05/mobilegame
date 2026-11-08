/* UI text from assets/strings.json (no user-facing strings in C).
 * str("key") returns the key itself when it is missing, so gaps show. */
#ifndef MG_STRINGS_H
#define MG_STRINGS_H

#include "core/arena.h"
#include "core/mg_types.h"

bool strings_init(Arena *perm);
const char *str(const char *key);

#endif
