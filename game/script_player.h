/* BOOK-ENGINE S5: the script player (one modal card that plays a script's steps). Owned by the script-player agent;
 * this copy holds only the interface the missions layer uses (theirs wins at integration). */
#ifndef MG_SCRIPT_PLAYER_H
#define MG_SCRIPT_PLAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "book_pack.h"
#include "book_progress.h"
#include "platform/app.h"

struct Ui;
typedef struct ScriptPlayer ScriptPlayer;
bool script_player_open(ScriptPlayer *sp, const BookPack *pack, uint32_t script_index, const char *presenter_name);
bool script_player_active(const ScriptPlayer *sp);
int  script_player_frame(ScriptPlayer *sp, struct Ui *ui, MgApp *app, BookProgress *progress, double now_ms); /* 1 = ended */
bool script_player_finished(const ScriptPlayer *sp);
void script_player_close(ScriptPlayer *sp);

#endif
