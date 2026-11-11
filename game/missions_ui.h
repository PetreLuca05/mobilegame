/* BOOK-ENGINE S4 UI (Jelly Pop): the objective pill (HUD, top left under the account chip), the dialogue card (portrait,
 * name, role, lines with Continue, two choices on a question) and the journal page (opened from the pause card: arc
 * tabs, case cards with their stages, Track / Take me there). Strings from assets/strings*.json, pack text as printed. */
#ifndef MG_MISSIONS_UI_H
#define MG_MISSIONS_UI_H

#include "missions.h"
#include "ui/head_icons.h"
#include "ui/ui_textarea.h"

#define MISSION_UI_WRAP 24

typedef enum MissionUiAction {
    MUI_NONE,
    MUI_OPEN_JOURNAL, /* the pill was tapped */
    MUI_CLOSE_JOURNAL,/* the journal's back: return to the pause card */
    MUI_TAKE_ME,      /* Take me there: auto-nav to the objective (the case was tracked first) */
} MissionUiAction;

typedef struct MissionUi {
    bool journal;           /* the journal page is up */
    int arc_tab;            /* arc row shown */
    UiScroll scroll, tabs_scroll;
    UiTextArea dlg_area;    /* the dialogue line (scrolls when long) */
    TextLine dlg_lines[64];
    TextLine wrap[MISSION_UI_WRAP];
    float pill[4];          /* the pill's rect last frame (x, y, w, h; w = 0 none): marker icons stay out of it */
    float card[4];          /* the dialogue card's rect */
    int pill_steps_shown;   /* the steps counter (bumps when it changes) */
} MissionUi;

void missions_ui_init(MissionUi *u);
/* The objective pill at (x, y), at most max_w wide. Tap = MUI_OPEN_JOURNAL. */
MissionUiAction missions_pill(MissionUi *u, Missions *m, Ui *ui, float x, float y, float max_w);
/* The dialogue card while one is open (claims the whole screen's touches). */
void missions_dialogue_card(MissionUi *u, Missions *m, Ui *ui, MgApp *app, HeadIcons *heads);
/* The journal page (full screen, safe insets respected). */
MissionUiAction missions_journal(MissionUi *u, Missions *m, Ui *ui, MgApp *app, HeadIcons *heads);
/* The chapter colour of a case (k_ui.chapter by its arc row). */
uint32_t missions_case_color(const Missions *m, int mission);
/* "Parter" / "Etajul N" / "Terasa" into buf. */
const char *missions_storey_name(int storey, char *buf, int cap);

#endif
