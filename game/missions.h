/* BOOK-ENGINE S4: the missions layer. The book is reached only through NPCs: arcs (a chapter, opened and closed by its
 * giver) > cases (missions table: giver, patient, ward, intro / outro, a one-line summary) > quests (one topic) > steps
 * (msteps: a script played with its presenter at a stand point, maybe with a patient in a bed / seat).
 *
 * Progress stays per fact: a step is done when every fact its script covers (the facts of the items its predict / check
 * steps hold) is set in BookProgress; a quest, a case and an arc are done when all their steps are. The only other state
 * is the accepted + closed bit pair per case (book_progress_accept_case / _close_case). The gates, and nothing else:
 * a case closes (its giver's debrief) only when its steps are done; a chapter exam opens only when its rooms are
 * complete (missions_exam_open). Any case can be accepted at any time, any step of an accepted case played in any order;
 * the objective suggests the book order.
 *
 * Flow (S4): tracked case's giver -> intro + case line -> accept -> next undone step's presenter -> (quest intro on the
 * quest's first step) + the step line -> start -> the script player with the presenter's name -> the presenter's reply
 * (+ the quest's done line) -> next. All done -> the giver -> outro (+ the arc outro after its last case) -> reward.
 * Staging: the objective step's presenter stands at its stand point, a case step's patient lies / sits at the step's
 * place; a case step starts only with its patient present.
 *
 * No allocation after missions_init (one per-step byte array from the caller's arena); lines are read in place. */
#ifndef MG_MISSIONS_H
#define MG_MISSIONS_H

#include <stdbool.h>
#include <stdint.h>

#include "book_pack.h"
#include "book_people.h"
#include "book_progress.h"
#include "core/arena.h"
#include "core/mg_types.h"
#include "net/net_transport.h"
#include "script_player.h"

#define MISSION_DLG_LINES 48
#define MISSION_NONE (-1)
#define MSTEP_KIND_CASE 1 /* script kind "case": the step has a patient */

typedef enum MissionObjKind {
    MOBJ_NONE,           /* nothing tracked (every case closed) */
    MOBJ_OFFER,          /* the tracked case is not accepted: its giver */
    MOBJ_STEP,           /* the next undone step of the tracked case: its presenter */
    MOBJ_DEBRIEF,        /* every step done: back to the giver */
} MissionObjKind;

typedef enum MissionDlgKind {
    MDLG_NONE,
    MDLG_OFFER,   /* (arc intro) + case intro + case line; choices accept / later */
    MDLG_REMIND,  /* the giver of an accepted, unfinished case: the case line */
    MDLG_STEP,    /* (quest intro) + the step line; choices start / later */
    MDLG_REPLY,   /* after the script: the presenter's reply (+ the quest's done line) */
    MDLG_DEBRIEF, /* case outro (+ arc outro); at the end: closed, reward */
} MissionDlgKind;

enum { /* Missions.events: what happened since missions_take_events */
    ME_ACCEPTED = 1u,
    ME_STEP_STARTED = 2u,
    ME_STEP_ENDED = 4u,
    ME_CASE_CLOSED = 8u,
    ME_REWARD = 16u,      /* T3: the case's reward (the arc's too when ME_ARC_DONE) */
    ME_OBJECTIVE = 32u,   /* the objective moved to someone else */
    ME_COMMIT = 64u,      /* progress changed: save the file and push */
    ME_NO_PATIENT = 128u, /* a case step was refused: its patient is not there */
    ME_ARC_DONE = 256u,
    ME_DIALOGUE = 512u,   /* a dialogue opened */
};

typedef struct MissionObjective {
    MissionObjKind kind;
    int mission, mstep, cast; /* rows; cast = who to talk to */
    int storey, room;         /* room = pack room row, -1 a corridor */
    HMM_Vec3 pos;             /* where they stand (feet, y = 0: the caller puts it on the storey's floor) */
    int steps_done, steps_total;
} MissionObjective;

typedef struct MissionDialogue {
    MissionDlgKind kind;
    int mission, mstep, cast; /* cast: the one talked to */
    uint32_t lines[MISSION_DLG_LINES]; /* rows of the lines table */
    int count, at;
    bool choices;             /* the last line asks: yes / later */
    bool quest_done;          /* REPLY: the quest finished with this step */
    bool good;                /* REPLY: the step is done (after_good) or not (after_mixed) */
} MissionDialogue;

typedef struct Missions {
    const BookPack *pack;
    BookProgress *progress;
    BookPeople *people;
    ScriptPlayer *player;
    bool on;
    int tracked;              /* mission row, MISSION_NONE */
    MissionObjective obj;
    MissionDialogue dlg;
    int playing;              /* mstep in the script player, MISSION_NONE */
    uint8_t *step_done;       /* per mstep, from the progress (rebuilt when it changes) */
    double progress_seen;     /* progress.updated_ms the cache was built from (< 0: rebuild) */
    int staged_presenter, staged_patient; /* cast rows staged for the objective, -1 */
    uint32_t events;
    double now_ms;            /* wall clock of the last missions_update (progress stamps) */
    /* counts for the tests and the report */
    uint32_t talks, dialogues, steps_started, steps_ended, cases_accepted, cases_closed, rewards, refused;
} Missions;

/* Ready when the pack has missions; registers the talk callback with `people` (may be NULL: no staging, talks come
 * through missions_talk). Tracks the first case that is not closed (an accepted one first). */
bool missions_init(Missions *m, const BookPack *pack, BookProgress *progress, BookPeople *people, ScriptPlayer *player, Arena *arena);
/* Unstages what the objective staged; the struct is off afterwards. */
void missions_shutdown(Missions *m);
/* Once per frame while playing: the done cache (when the progress changed), the objective, the staging. */
void missions_update(Missions *m, double now_ms);
/* The player talks to a cast member (the people layer's talk callback calls this). May open a dialogue. */
void missions_talk(Missions *m, int cast);
/* Dialogue buttons: Continue (false when the dialogue ended), the two choices on the last line. */
bool missions_dialogue_next(Missions *m);
void missions_dialogue_choose(Missions *m, bool yes);
void missions_dialogue_close(Missions *m); /* back / later: nothing changes */
bool missions_dialogue_open(const Missions *m);
/* The script player's frame while a step plays; on its end the presenter's reply opens. True while it plays. */
bool missions_script_frame(Missions *m, struct Ui *ui, MgApp *app);
bool missions_playing(const Missions *m);
uint32_t missions_take_events(Missions *m);

/* Tracking (journal: Track) */
void missions_track(Missions *m, int mission);
/* Done tests (by row; out of range = false) */
bool missions_step_done(const Missions *m, int mstep);
bool missions_quest_done(const Missions *m, int quest);
bool missions_case_done(const Missions *m, int mission);
bool missions_chapter_done(const Missions *m, int chapter); /* every case of the chapter closed */
void missions_case_count(const Missions *m, int mission, int *done, int *total);
void missions_quest_count(const Missions *m, int quest, int *done, int *total);
/* The first undone step of a case in book order, MISSION_NONE when all are done. */
int missions_next_step(const Missions *m, int mission);
bool missions_case_accepted(const Missions *m, int mission);
bool missions_case_closed(const Missions *m, int mission);
/* The arc row of a chapter, MISSION_NONE if none. */
int missions_arc_of_chapter(const BookPack *p, int chapter);
/* S7: a room (topic) is complete when every fact of the topic is set; a chapter's rooms are its topics' rooms. The
 * chapter exam opens only then (the second gate). */
bool missions_topic_complete(const Missions *m, int topic);
bool missions_exam_open(const Missions *m, int chapter);
/* Facts a step's script covers (the items' fact refs), up to `cap` written; returns the count (tests, the stub). */
int missions_step_facts(const BookPack *p, int mstep, uint32_t *out, int cap);
int missions_script_facts(const BookPack *p, int script, uint32_t *out, int cap);

/* Text helpers */
const char *missions_line(const Missions *m, uint32_t line_row);
int missions_line_speaker(const Missions *m, uint32_t line_row); /* cast row, -1 none */
const char *missions_cast_name(const BookPack *p, int cast);
const char *missions_cast_role(const BookPack *p, int cast);
/* The cast member's look as an account skin (portraits through the head icon atlas; S4b mapping, coarse). */
NetSkin missions_cast_skin(const BookPack *p, int cast);
/* Where a cast member's post is (the people table), false if they have none. */
bool missions_cast_post(const BookPack *p, int cast, HMM_Vec3 *pos, int *storey, int *room);

#endif
