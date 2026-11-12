#include "content_cleanup.h"

#include <SDL3/SDL.h>

#include "core/log.h"
#include "save.h"

/* Deletes every file in dir whose name matches pattern; returns how many. */
static int delete_matching(const char *dir, const char *pattern)
{
    if (!dir || !dir[0]) {
        return 0;
    }
    int count = 0, deleted = 0;
    char **names = SDL_GlobDirectory(dir, pattern, 0, &count);
    if (!names) {
        return 0;
    }
    const size_t dl = SDL_strlen(dir);
    const bool slash = dir[dl - 1] == '/';
    for (int i = 0; i < count; i++) {
        char path[1024];
        SDL_snprintf(path, sizeof(path), "%s%s%s", dir, slash ? "" : "/", names[i]);
        if (SDL_RemovePath(path)) {
            deleted++;
        } else {
            mg_log_warn("content cleanup: could not delete %s: %s", names[i], SDL_GetError());
        }
    }
    SDL_free(names);
    return deleted;
}

int content_cleanup_run(Prefs *prefs, const char *profile)
{
    if (prefs->content_cleaned) {
        return 0;
    }
    const int v = save_version(profile);
    int deleted = 0;
    if (v < 8) { /* 0 = no slot: an install that may still hold files from a build with the pack */
        char pattern[160];
        SDL_snprintf(pattern, sizeof(pattern), "%s%sbook_*", profile && profile[0] ? profile : "", profile && profile[0] ? "-" : "");
        char *pref = SDL_GetPrefPath("docvex", "mobilegame");
        deleted += delete_matching(pref, pattern);
        SDL_free(pref);
#if defined(SDL_PLATFORM_IOS)
        deleted += delete_matching(SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS), "book_*");
#endif
        mg_log_info("content cleanup: save v%d, %d old book file(s) deleted", v, deleted);
    }
    prefs->content_cleaned = true;
    return deleted;
}
