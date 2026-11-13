/* Text check (autotests only): every word the UI draws must come from the game's string table (its Romanian
 * values, fed in with text_check_vocab) or from data that is legitimately dynamic (names of players, NPCs,
 * islands, levels, rooms: text_check_data). Anything else is hard-coded text and is recorded with the screen it was
 * drawn on; the autotest fails listing them.
 *
 * A word = a run of letters (ASCII, Latin-1 / Extended-A/B, Greek), compared without case. Not words: a run that
 * touches a digit (844x390, R1, Player0042: numbers, sizes, ids), a single letter (keypad keys, floor letters),
 * the few unit symbols in text_check.c (fps, ms, pt, ...), and anything after '%' in a vocabulary string (format).
 *
 * Off unless text_check_enable(true): then text_draw_n, text_mesh and ui_label_caps scan what they draw.
 * Fixed tables, no allocation. Exempt draws (debug-only screens) go between text_check_exempt_push / _pop. */
#ifndef MG_TEXT_CHECK_H
#define MG_TEXT_CHECK_H

#include <stdbool.h>
#include <stddef.h>

void text_check_enable(bool on);
bool text_check_enabled(void);
/* The string table's Romanian values: their words are the vocabulary (call before or after enabling). */
void text_check_vocab(const char *s);
/* Dynamic data about to be drawn (a name, a map's room name, pack text): its words are accepted from now on.
 * A no-op while the check is off, so it can sit in a draw path. */
void text_check_data(const char *s);
/* Draws between these are not checked (debug-only screens). Nests. */
void text_check_exempt_push(void);
void text_check_exempt_pop(void);
/* What the next violations are recorded against (the screen's name; a static string). */
void text_check_set_screen(const char *name);
/* Checks the first len bytes of s (or up to a NUL with len = (size_t)-1). */
void text_check_scan(const char *s, size_t len);
/* Distinct offending draws so far (by text); fills the i-th one's text, its first bad word and its screen. */
int text_check_count(void);
bool text_check_get(int i, const char **text, const char **word, const char **screen);

#endif
