/* STUB (BOOK-ENGINE integration): creation of the stand-in game/script_player_stub.c and its test hook. Delete with
 * the stub when game/script_player.c lands. */
#ifndef MG_SCRIPT_PLAYER_STUB_H
#define MG_SCRIPT_PLAYER_STUB_H

#include "core/arena.h"
#include "script_player.h"

ScriptPlayer *script_player_stub_create(Arena *arena);
/* --autotest missions: the next script played leaves this fact unset (-1: sets them all). */
void script_player_stub_skip_fact(ScriptPlayer *sp, int fact);

#endif
