/* One-time cleanup of book files left by builds that carried the Gastro pack (removed 2026-11-12, save v8).
 * Runs once per profile at launch when the save slot is older than v8 or missing: deletes the profile's
 * "book_*" files in the pref folder (progress book_<book>_<uid>.bin, its .tmp, autotest pictures) and, on iOS,
 * the "book_*" pictures in Documents. Then sets prefs.content_cleaned (the caller saves the prefs). */
#ifndef MG_CONTENT_CLEANUP_H
#define MG_CONTENT_CLEANUP_H

#include <stdbool.h>

#include "prefs.h"

/* Returns the number of files deleted (0 when it did not need to run). */
int content_cleanup_run(Prefs *prefs, const char *profile);

#endif
