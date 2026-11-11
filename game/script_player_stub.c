/* STUB (BOOK-ENGINE integration): a thin stand-in for game/script_player.c (the script-player agent's). It shows the
 * script's title and the presenter's name on a card for 0.6 s, then sets every fact the script covers (as a player who
 * answered everything right) and ends. Compiled only while game/script_player.c does not exist (CMakeLists); delete
 * this file, script_player_stub.h and the MG_SCRIPT_PLAYER_STUB lines in game.c when the real one lands. */
#include "script_player_stub.h"

#include <SDL3/SDL.h>

#include "missions.h"
#include "ui/ui_widgets.h"

struct ScriptPlayer {
    const BookPack *pack;
    uint32_t script;
    char presenter[64];
    bool active, finished;
    double t0;
    int skip_fact;
};

ScriptPlayer *script_player_stub_create(Arena *arena)
{
    ScriptPlayer *sp = ARENA_PUSH(arena, ScriptPlayer, 1);
    if (sp) {
        SDL_zerop(sp);
        sp->skip_fact = -1;
    }
    return sp;
}

void script_player_stub_skip_fact(ScriptPlayer *sp, int fact)
{
    if (sp) {
        sp->skip_fact = fact;
    }
}

bool script_player_open(ScriptPlayer *sp, const BookPack *pack, uint32_t script_index, const char *presenter_name)
{
    if (!sp || !pack || script_index >= pack->script_count) {
        return false;
    }
    sp->pack = pack, sp->script = script_index, sp->active = true, sp->finished = false, sp->t0 = -1.0;
    SDL_snprintf(sp->presenter, sizeof(sp->presenter), "%s", presenter_name ? presenter_name : "");
    return true;
}

bool script_player_active(const ScriptPlayer *sp) { return sp && sp->active; }
bool script_player_finished(const ScriptPlayer *sp) { return sp && sp->finished; }

void script_player_close(ScriptPlayer *sp)
{
    if (sp) {
        sp->active = false;
    }
}

int script_player_frame(ScriptPlayer *sp, struct Ui *ui, MgApp *app, BookProgress *progress, double now_ms)
{
    if (!sp || !sp->active) {
        return 1;
    }
    if (sp->t0 < 0.0) {
        sp->t0 = now_ms;
    }
    const mgpk_script_t *sc = book_script(sp->pack, sp->script);
    { /* the card: scrim, title, presenter */
        const float w = SDL_min(app->width_pt - 80.0f, 520.0f), h = 150.0f;
        float x, y;
        ui_modal(ui, "sp_stub", w, h, &x, &y);
        ui_text_c(ui, TEXT_FACE_DISPLAY, x + w * 0.5f, y + 50.0f, 22.0f, ui_rgba(k_ui.ink), sc ? book_pack_str(sp->pack, sc->title) : "");
        ui_text_c(ui, TEXT_FACE_BOLD, x + w * 0.5f, y + 96.0f, k_ui.t_body, ui_rgba(k_ui.teal_text), sp->presenter);
        ui_modal_end(ui);
    }
    if (now_ms - sp->t0 < 600.0) {
        return 0;
    }
    uint32_t facts[256];
    const int n = SDL_min(missions_script_facts(sp->pack, (int)sp->script, facts, 256), 256);
    for (int i = 0; i < n; i++) {
        if ((int)facts[i] != sp->skip_fact) {
            book_progress_set_fact(progress, (int)facts[i], now_ms);
        }
    }
    sp->skip_fact = -1;
    sp->finished = true;
    sp->active = false;
    return 1;
}
