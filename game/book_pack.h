/* BOOK-1: the book content pack (content/gastro.mgpack, format MGPK v4), read in place.
 * book_pack_open runs the checks of the reference reader content/gastro/mgpack_read.c once (header, every record
 * size, every table bound, every index and string offset, each mission-step script used once, each required script
 * in a mission step) and then hands out const pointers into the blob: no copy, no allocation, no parsing later.
 * A pack with problems is refused (ok = false): the game runs on without book content.
 * The record layout is content/gastro/gastro.mgpack.h (the same header ships with the real and the dummy pack). */
#ifndef MG_BOOK_PACK_H
#define MG_BOOK_PACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../content/gastro/gastro.mgpack.h"

/* The numbers the reference reader prints (key=value lines of mgpack_read), measured at load. */
typedef struct BookPackStats {
    uint32_t bytes, fnv1a;
    uint32_t required, split_parts;          /* required scripts, scripts stored in more than one part */
    uint32_t strings, longest_string_bytes;  /* strings in the table (after the leading empty one) */
    uint32_t dialogue_bytes, longest_dialogue_line_bytes;
    uint32_t people_walking, door_leaves;    /* door_leaves are counted in furniture_count; the reader prints them apart */
    uint32_t largest_script, largest_script_steps, largest_script_string_bytes;
} BookPackStats;

typedef struct BookPack {
    bool ok;            /* opened with no problems: every pointer below is valid */
    uint32_t problems;  /* what the checks found (each is logged, the first 20 in full) */
    const uint8_t *blob;
    size_t size;
    const mgpk_header_t *hdr;
    const char *str;    /* the string table: str + offset */
    uint32_t str_size;
    const mgpk_room_t *rooms;
    const mgpk_topic_t *topics;
    const mgpk_atom_t *atoms;
    const mgpk_fact_t *facts;
    const mgpk_item_t *items;
    const mgpk_opt_t *opts;
    const uint32_t *refs;
    const mgpk_script_t *scripts;
    const mgpk_step_t *steps;
    const mgpk_prop_t *props;
    const mgpk_slot_t *slots;
    const mgpk_figlabel_t *figlabels;
    const mgpk_furniture_t *furniture;
    const mgpk_cast_t *cast;
    const mgpk_arc_t *arcs;
    const mgpk_mission_t *missions;
    const mgpk_quest_t *quests;
    const mgpk_mstep_t *msteps;
    const mgpk_line_t *lines;
    const mgpk_person_t *people;
    uint32_t room_count, topic_count, atom_count, fact_count, item_count, opt_count, ref_count, script_count, step_count;
    uint32_t prop_count, slot_count, figlabel_count, furniture_count;
    uint32_t cast_count, arc_count, mission_count, quest_count, mstep_count, line_count, person_count;
    BookPackStats stats;
} BookPack;

/* Checks the blob and fills `p`. The blob must stay alive and be 8-byte aligned (the embedded pack is).
 * `label` names the pack in the log. Returns p->ok. */
bool book_pack_open(BookPack *p, const void *blob, size_t size, const char *label);
/* The embedded content/gastro.mgpack (the real pack, or the dummy one when MG_CONTENT_DEMO). Logs one summary line. */
bool book_pack_load(BookPack *p);
/* true when this build embeds the public dummy pack (MG_CONTENT_DEMO): the game shows its „conținut demo” chip */
bool book_pack_is_demo(void);

/* A string by offset: "" for MGPK_NONE, an offset out of range or a pack that is not open. */
const char *book_pack_str(const BookPack *p, uint32_t off);

/* Records by row; NULL when the row is out of range or the pack is not open. */
const mgpk_room_t *book_room(const BookPack *p, uint32_t i);
const mgpk_topic_t *book_topic(const BookPack *p, uint32_t i);
const mgpk_atom_t *book_atom(const BookPack *p, uint32_t i);
const mgpk_fact_t *book_fact(const BookPack *p, uint32_t i);
const mgpk_item_t *book_item(const BookPack *p, uint32_t i);
const mgpk_opt_t *book_opt(const BookPack *p, uint32_t i);
const mgpk_script_t *book_script(const BookPack *p, uint32_t i);
const mgpk_step_t *book_step(const BookPack *p, uint32_t i);
const mgpk_prop_t *book_prop(const BookPack *p, uint32_t i);
const mgpk_slot_t *book_slot(const BookPack *p, uint32_t i);
const mgpk_figlabel_t *book_figlabel(const BookPack *p, uint32_t i);
const mgpk_furniture_t *book_furniture(const BookPack *p, uint32_t i);
const mgpk_cast_t *book_cast(const BookPack *p, uint32_t i);
const mgpk_arc_t *book_arc(const BookPack *p, uint32_t i);
const mgpk_mission_t *book_mission(const BookPack *p, uint32_t i);
const mgpk_quest_t *book_quest(const BookPack *p, uint32_t i);
const mgpk_mstep_t *book_mstep(const BookPack *p, uint32_t i);
const mgpk_line_t *book_line(const BookPack *p, uint32_t i);
const mgpk_person_t *book_person(const BookPack *p, uint32_t i);
/* refs[i] (a fact or an atom row, as the item / step says); MGPK_NONE out of range */
uint32_t book_ref(const BookPack *p, uint32_t i);

#endif
