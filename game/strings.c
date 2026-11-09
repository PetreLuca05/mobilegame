#include "strings.h"

#include <string.h>

#include "cJSON.h"
#include "core/assets.h"
#include "core/log.h"

#define MAX_STRINGS 320

typedef struct StringEntry {
    const char *key, *value;
} StringEntry;

/* filled once at startup from the embedded JSON, read-only afterwards */
typedef struct StringTable {
    StringEntry e[MAX_STRINGS];
    int count;
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
        if (cJSON_IsString(it) && t->count < MAX_STRINGS) {
            t->e[t->count].key = copy(perm, it->string);
            t->e[t->count].value = copy(perm, it->valuestring);
            t->count++;
        }
    }
    cJSON_Delete(j);
    return true;
}

/* Romanian first: str() takes the first match, so what it lacks falls back to English */
bool strings_init(Arena *perm)
{
    StringTable *t = g_strings_table();
    load_table(t, perm, "assets/strings_ro.json");
    if (!load_table(t, perm, "assets/strings.json")) {
        mg_log_error("strings: assets/strings.json missing or invalid");
        return false;
    }
    return true;
}

const char *str(const char *key)
{
    const StringTable *t = g_strings_table();
    for (int i = 0; i < t->count; i++) {
        if (strcmp(t->e[i].key, key) == 0) {
            return t->e[i].value;
        }
    }
    return key;
}
