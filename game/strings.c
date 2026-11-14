#include "strings.h"

#include <string.h>

#include "cJSON.h"
#include "core/assets.h"
#include "core/log.h"
#include "ui/text_check.h"

#define MAX_STRINGS 1024 /* both tables together (Romanian first, then English) */

typedef struct StringEntry {
    const char *key, *value;
    bool fallback; /* from strings.json (English) only: the game's language (Romanian) has no entry */
} StringEntry;

/* filled once at startup from the embedded JSON, read-only afterwards */
typedef struct StringTable {
    StringEntry e[MAX_STRINGS];
    int count;
    bool loading_fallback;
    /* what reached the screen without a Romanian text (ui autotest): counts and the last key of each */
    int untranslated, missing;
    const char *last_untranslated, *last_missing;
} StringTable;

static StringTable *g_strings_table(void)
{
    static StringTable table; /* const data after strings_init, like an asset */
    return &table;
}

static const char *copy(Arena *a, const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = (char *)arena_alloc(a, n, 1);
    if (d) {
        memcpy(d, s, n);
    }
    return d;
}

static bool load_table(StringTable *t, Arena *perm, const char *path)
{
    const MgAsset *a = mg_asset_find(path);
    cJSON *j = a ? cJSON_ParseWithLength((const char *)a->data, a->size) : NULL;
    if (!j) {
        return false;
    }
    const cJSON *it;
    cJSON_ArrayForEach(it, j)
    {
        if (!cJSON_IsString(it)) {
            continue;
        }
        if (t->count >= MAX_STRINGS) { /* never silently: a dropped key shows on screen as its raw name */
            mg_log_error("strings: table full (%d), %s and the rest of %s dropped: raise MAX_STRINGS", MAX_STRINGS, it->string, path);
            break;
        }
        t->e[t->count].key = copy(perm, it->string);
        t->e[t->count].value = copy(perm, it->valuestring);
        t->e[t->count].fallback = t->loading_fallback;
        t->count++;
    }
    cJSON_Delete(j);
    return true;
}

/* Romanian first: str() takes the first match, so what it lacks falls back to English */
bool strings_init(Arena *perm)
{
    StringTable *t = g_strings_table();
    load_table(t, perm, "assets/strings_ro.json");
    t->loading_fallback = true;
    if (!load_table(t, perm, "assets/strings.json")) {
        mg_log_error("strings: assets/strings.json missing or invalid");
        return false;
    }
    return true;
}

const char *str(const char *key)
{
    StringTable *t = g_strings_table();
    for (int i = 0; i < t->count; i++) {
        if (strcmp(t->e[i].key, key) == 0) {
            if (t->e[i].fallback) { /* Romanian is the game's only language: an English-only key is a gap */
                t->untranslated++;
                t->last_untranslated = t->e[i].key;
            }
            return t->e[i].value;
        }
    }
    t->missing++; /* shows as its raw key */
    t->last_missing = key;
    return key;
}

int strings_untranslated(const char **last)
{
    const StringTable *t = g_strings_table();
    if (last) {
        *last = t->last_untranslated;
    }
    return t->untranslated;
}

int strings_missing(const char **last)
{
    const StringTable *t = g_strings_table();
    if (last) {
        *last = t->last_missing;
    }
    return t->missing;
}

void strings_feed_text_check(void)
{
    const StringTable *t = g_strings_table();
    for (int i = 0; i < t->count; i++) {
        if (!t->e[i].fallback) { /* Romanian only: an English word on screen is what the check is for */
            text_check_vocab(t->e[i].value);
        }
    }
}

int strings_watch_prefix(const char *prefix)
{
    const StringTable *t = g_strings_table();
    const size_t pn = strlen(prefix);
    int n = 0;
    for (int i = 0; i < t->count; i++) {
        if (strncmp(t->e[i].key, prefix, pn) != 0) {
            continue;
        }
        int first = i; /* what str() returns: the first entry with this key (Romanian before English) */
        for (int k = 0; k < i; k++) {
            if (strcmp(t->e[k].key, t->e[i].key) == 0) {
                first = k;
                break;
            }
        }
        if (first == i) {
            text_check_watch(t->e[i].value);
            n++;
        }
    }
    return n;
}
