/* BOOK-1 pack loader: the checks of the reference reader (mgpack_read.c, removed with the book content), once, then in-place reads (book_pack.h). */
#include "book_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/assets.h"
#include "core/log.h"

/* the record sizes packbin.py writes (the comments of gastro.mgpack.h): a layout change must fail the build */
_Static_assert(sizeof(mgpk_header_t) == 16 + 32 * 12, "mgpk header");
_Static_assert(sizeof(mgpk_room_t) == 28, "mgpk room");
_Static_assert(sizeof(mgpk_topic_t) == 40, "mgpk topic");
_Static_assert(sizeof(mgpk_atom_t) == 20, "mgpk atom");
_Static_assert(sizeof(mgpk_fact_t) == 20, "mgpk fact");
_Static_assert(sizeof(mgpk_item_t) == 40, "mgpk item");
_Static_assert(sizeof(mgpk_opt_t) == 20, "mgpk opt");
_Static_assert(sizeof(mgpk_script_t) == 36, "mgpk script");
_Static_assert(sizeof(mgpk_step_t) == 12, "mgpk step");
_Static_assert(sizeof(mgpk_prop_t) == 28, "mgpk prop");
_Static_assert(sizeof(mgpk_furniture_t) == 36, "mgpk furniture");
_Static_assert(sizeof(mgpk_figlabel_t) == 20, "mgpk figlabel");
_Static_assert(sizeof(mgpk_cast_t) == 36, "mgpk cast");
_Static_assert(sizeof(mgpk_arc_t) == 16, "mgpk arc");
_Static_assert(sizeof(mgpk_mission_t) == 32, "mgpk mission");
_Static_assert(sizeof(mgpk_quest_t) == 20, "mgpk quest");
_Static_assert(sizeof(mgpk_mstep_t) == 60, "mgpk mstep");
_Static_assert(sizeof(mgpk_line_t) == 8, "mgpk line");
_Static_assert(sizeof(mgpk_person_t) == 44, "mgpk person");
_Static_assert(MGPK_TABLE_COUNT <= 32, "mgpk table slots");

#define BAD_LOGGED 20 /* like the reader: the first 20 in full, then only the count */

typedef struct Chk {
    BookPack *p;
    const char *label;
} Chk;

#define BAD(c, ...)                                                                                                                                  \
    do {                                                                                                                                             \
        if ((c)->p->problems < BAD_LOGGED) {                                                                                                         \
            char bad_msg_[160];                                                                                                                      \
            snprintf(bad_msg_, sizeof(bad_msg_), __VA_ARGS__);                                                                                       \
            mg_log_error("book: %s: %s", (c)->label, bad_msg_);                                                                                      \
        }                                                                                                                                            \
        (c)->p->problems++;                                                                                                                          \
    } while (0)

#define IDX(c, v, n, what, row)                                                                                                                      \
    do {                                                                                                                                             \
        if ((uint32_t)(v) >= (uint32_t)(n)) {                                                                                                        \
            BAD(c, "%s %u: index %u >= %u", what, (unsigned)(row), (unsigned)(v), (unsigned)(n));                                                   \
        }                                                                                                                                            \
    } while (0)
#define IDX_OPT(c, v, n, what, row)                                                                                                                  \
    do {                                                                                                                                             \
        if ((uint32_t)(v) != MGPK_NONE) {                                                                                                            \
            IDX(c, v, n, what, row);                                                                                                                 \
        }                                                                                                                                            \
    } while (0)
#define IDX16(c, v, n, what, row)                                                                                                                    \
    do {                                                                                                                                             \
        if ((v) != 0xFFFF) {                                                                                                                         \
            IDX(c, v, n, what, row);                                                                                                                 \
        }                                                                                                                                            \
    } while (0)

static const void *table(Chk *c, unsigned t, uint32_t rec, uint32_t *count)
{
    const mgpk_table_t *d = &c->p->hdr->table[t];
    *count = d->count;
    if (d->size != rec) {
        BAD(c, "table %u: record size %u, reader expects %u", t, d->size, rec);
        *count = 0;
        return NULL;
    }
    if (d->offset % 8) {
        BAD(c, "table %u: offset %u not 8-byte aligned", t, d->offset);
    }
    if ((uint64_t)d->offset + (uint64_t)d->count * d->size > c->p->size) {
        BAD(c, "table %u: past the end of the file", t);
        *count = 0;
        return NULL;
    }
    return c->p->blob + d->offset;
}

/* a string reference: inside the table and at the start of a string; returns its length */
static uint32_t str_ok(Chk *c, uint32_t off, const char *what, uint32_t row)
{
    const BookPack *p = c->p;
    if (off >= p->str_size || (off > 0 && p->str[off - 1] != '\0')) {
        BAD(c, "%s %u: bad string offset %u", what, row, off);
        return 0;
    }
    return (uint32_t)strlen(p->str + off);
}

/* the length of a string the checks above already judged (0 for MGPK_NONE or an unsound offset): measuring only */
static uint32_t str_len(const BookPack *p, uint32_t off)
{
    return off >= p->str_size || (off > 0 && p->str[off - 1] != '\0') ? 0u : (uint32_t)strlen(p->str + off);
}

static uint32_t str_opt(Chk *c, uint32_t off, const char *what, uint32_t row) { return off == MGPK_NONE ? 0 : str_ok(c, off, what, row); }

static void check_content(Chk *c)
{
    BookPack *p = c->p;
    const uint32_t nroom = p->room_count, ntopic = p->topic_count, natom = p->atom_count, nfact = p->fact_count, nitem = p->item_count;
    const uint32_t nopt = p->opt_count, nref = p->ref_count, nscript = p->script_count, nstep = p->step_count, nprop = p->prop_count;
    const uint32_t nslot = p->slot_count, nfig = p->figlabel_count, nfurn = p->furniture_count;
    const mgpk_room_t *room = p->rooms;
    const mgpk_fact_t *fact = p->facts;
    const mgpk_atom_t *atom = p->atoms;
    const mgpk_item_t *item = p->items;
    const mgpk_opt_t *opt = p->opts;
    const uint32_t *ref = p->refs;
    const mgpk_furniture_t *furn = p->furniture;
    BookPackStats *st = &p->stats;

    if (p->hdr->fact_count != nfact || p->hdr->item_count != nitem) {
        BAD(c, "header counts differ from the tables");
    }
    for (uint32_t i = 0; i < nroom; i++) {
        str_ok(c, room[i].id, "room", i);
        str_ok(c, room[i].name, "room", i);
    }
    for (uint32_t i = 0; i < ntopic; i++) {
        const mgpk_topic_t *t = &p->topics[i];
        str_ok(c, t->id, "topic", i);
        str_ok(c, t->title, "topic", i);
        IDX(c, t->room, nroom, "topic room", i);
        if ((uint64_t)t->first_fact + t->fact_count > nfact) {
            BAD(c, "topic %u: facts out of range", i);
        }
        if ((uint64_t)t->first_item + t->item_count > nitem) {
            BAD(c, "topic %u: items out of range", i);
        }
        if ((uint64_t)t->first_script + t->script_count > nscript) {
            BAD(c, "topic %u: scripts out of range", i);
        }
        for (uint32_t k = 0; k < t->fact_count && t->first_fact + k < nfact; k++) {
            if (fact[t->first_fact + k].topic != i) {
                BAD(c, "topic %u: fact %u belongs to topic %u", i, t->first_fact + k, fact[t->first_fact + k].topic);
            }
        }
    }
    for (uint32_t i = 0; i < natom; i++) {
        str_ok(c, atom[i].id, "atom", i);
        str_ok(c, atom[i].text, "atom", i);
        str_opt(c, atom[i].fig, "atom fig", i);
        IDX(c, atom[i].topic, ntopic, "atom topic", i);
    }
    for (uint32_t i = 0; i < nfact; i++) {
        str_ok(c, fact[i].id, "fact", i);
        str_ok(c, fact[i].claim, "fact", i);
        IDX(c, fact[i].atom, natom, "fact atom", i);
        IDX(c, fact[i].topic, ntopic, "fact topic", i);
    }
    for (uint32_t i = 0; i < nitem; i++) {
        const mgpk_item_t *it = &item[i];
        str_ok(c, it->id, "item", i);
        str_ok(c, it->stem, "item", i);
        str_ok(c, it->explanation, "item", i);
        str_opt(c, it->review_note, "item", i);
        IDX(c, it->topic, ntopic, "item topic", i);
        if (it->type > 7) {
            BAD(c, "item %u: type %u", i, it->type);
        }
        if ((uint64_t)it->first_opt + it->n_opts > nopt || it->n_opts == 0) {
            BAD(c, "item %u: options out of range", i);
        }
        if ((uint64_t)it->first_fact_ref + it->n_facts > nref || it->n_facts == 0) {
            BAD(c, "item %u: fact refs out of range", i);
        } else {
            for (uint32_t k = 0; k < it->n_facts; k++) {
                IDX(c, ref[it->first_fact_ref + k], nfact, "item fact", i);
            }
        }
    }
    for (uint32_t i = 0; i < nopt; i++) {
        str_ok(c, opt[i].text, "opt", i);
        str_opt(c, opt[i].aux, "opt aux", i);
        str_opt(c, opt[i].why, "opt why", i);
        IDX_OPT(c, opt[i].fact, nfact, "opt fact", i);
    }
    /* scripts: bounds, and the string bytes each one pulls in (title + teach text + its items) */
    uint64_t big_bytes = 0;
    for (uint32_t i = 0; i < nscript; i++) {
        const mgpk_script_t *s = &p->scripts[i];
        uint64_t bytes = str_ok(c, s->title, "script", i);
        str_ok(c, s->id, "script", i);
        str_opt(c, s->npc, "script npc", i);
        IDX(c, s->topic, ntopic, "script topic", i);
        IDX(c, s->room, nroom, "script room", i);
        IDX_OPT(c, s->prop, nprop, "script prop", i);
        IDX_OPT(c, s->slot, nslot, "script slot", i);
        if (s->kind > 11) {
            BAD(c, "script %u: kind %u", i, s->kind);
        }
        if (s->part < 1 || s->part > s->parts) {
            BAD(c, "script %u: part %u of %u", i, s->part, s->parts);
        }
        st->split_parts += s->parts > 1;
        if (s->required) {
            st->required++;
            if (s->step_count > 15) {
                BAD(c, "script %u: required script with %u steps (max 15)", i, s->step_count);
            }
        }
        if ((uint64_t)s->first_step + s->step_count > nstep || s->step_count == 0) {
            BAD(c, "script %u: steps out of range", i);
            continue;
        }
        for (uint32_t k = 0; k < s->step_count; k++) {
            const uint32_t si = s->first_step + k;
            const mgpk_step_t *sp = &p->steps[si];
            if (sp->stage > 2) {
                BAD(c, "step %u: stage %u", si, sp->stage);
            }
            if (sp->item == MGPK_NONE) {
                if ((uint64_t)sp->first_atom_ref + sp->n_atoms > nref || sp->n_atoms == 0) {
                    BAD(c, "step %u: atom refs out of range", si);
                    continue;
                }
                for (uint32_t a = 0; a < sp->n_atoms; a++) {
                    const uint32_t ai = ref[sp->first_atom_ref + a];
                    IDX(c, ai, natom, "step atom", si);
                    if (ai < natom) {
                        bytes += str_len(p, atom[ai].text);
                    }
                }
            } else {
                IDX(c, sp->item, nitem, "step item", si);
                if (sp->item >= nitem) {
                    continue;
                }
                const mgpk_item_t *it = &item[sp->item];
                bytes += str_len(p, it->stem) + str_len(p, it->explanation) + str_len(p, it->review_note);
                for (uint32_t o = 0; o < it->n_opts && it->first_opt + o < nopt; o++) {
                    const mgpk_opt_t *op = &opt[it->first_opt + o];
                    bytes += str_len(p, op->text) + str_len(p, op->aux) + str_len(p, op->why);
                }
            }
        }
        if (bytes > big_bytes) {
            big_bytes = bytes;
            st->largest_script = i;
            st->largest_script_steps = s->step_count;
        }
    }
    st->largest_script_string_bytes = (uint32_t)big_bytes;
    for (uint32_t i = 0; i < nprop; i++) {
        const mgpk_prop_t *pr = &p->props[i];
        str_ok(c, pr->id, "prop", i);
        str_ok(c, pr->kind, "prop", i);
        str_ok(c, pr->label, "prop", i);
        IDX(c, pr->room, nroom, "prop room", i);
        if (pr->furniture != 0xFFFF) {
            IDX(c, pr->furniture, nfurn, "prop furniture", i);
            if (pr->furniture < nfurn && furn[pr->furniture].room != pr->room) {
                BAD(c, "prop %u: its furniture is in another room", i);
            }
        }
    }
    for (uint32_t i = 0; i < nfurn; i++) {
        str_ok(c, furn[i].id, "furniture", i);
        str_ok(c, furn[i].kind, "furniture", i);
        if (furn[i].room != 0xFFFF) {
            IDX(c, furn[i].room, nroom, "furniture room", i);
        }
        if (furn[i].mount > MGPK_MOUNT_DOOR) {
            BAD(c, "furniture %u: mount %u", i, furn[i].mount);
        }
        st->door_leaves += furn[i].mount == MGPK_MOUNT_DOOR;
    }
    for (uint32_t i = 0; i < nslot; i++) {
        const mgpk_slot_t *sl = &p->slots[i];
        str_ok(c, sl->id, "slot", i);
        str_ok(c, sl->kind, "slot", i);
        IDX(c, sl->room, nroom, "slot room", i);
        if (sl->furniture != 0xFFFF) {
            IDX(c, sl->furniture, nfurn, "slot furniture", i);
        }
    }
    for (uint32_t i = 0; i < nfig; i++) {
        const mgpk_figlabel_t *fg = &p->figlabels[i];
        str_ok(c, fg->fig, "figlabel", i);
        str_ok(c, fg->text, "figlabel", i);
        if (fg->x1 > fg->w || fg->y1 > fg->h || fg->x0 > fg->x1 || fg->y0 > fg->y1) {
            BAD(c, "figlabel %u: box outside the crop", i);
        }
    }
}

static void check_missions(Chk *c)
{
    BookPack *p = c->p;
    BookPackStats *st = &p->stats;
    const uint32_t nroom = p->room_count, ntopic = p->topic_count, nscript = p->script_count;
    const uint32_t ncast = p->cast_count, narc = p->arc_count, nmis = p->mission_count, nquest = p->quest_count, nms = p->mstep_count;
    const uint32_t nline = p->line_count, nper = p->person_count;
    const mgpk_cast_t *cast = p->cast;
    const mgpk_line_t *line = p->lines;
    const mgpk_quest_t *quest = p->quests;
    const mgpk_mstep_t *ms = p->msteps;
    const mgpk_person_t *per = p->people;
    const mgpk_room_t *room = p->rooms;

    for (uint32_t i = 0; i < nline; i++) {
        const uint32_t n = str_ok(c, line[i].text, "line", i);
        st->dialogue_bytes += n;
        st->longest_dialogue_line_bytes = n > st->longest_dialogue_line_bytes ? n : st->longest_dialogue_line_bytes;
        IDX16(c, line[i].speaker, ncast, "line speaker", i);
        if (line[i].role > 1) {
            BAD(c, "line %u: role %u", i, line[i].role);
        }
    }
    for (uint32_t i = 0; i < ncast; i++) {
        str_ok(c, cast[i].id, "cast", i);
        str_ok(c, cast[i].name, "cast", i);
        str_ok(c, cast[i].role, "cast", i);
        if (cast[i].kind > MGPK_KIND_STUDENT || cast[i].hair > 5 || cast[i].age > 2 || cast[i].skin > 3 || cast[i].sex > 1) {
            BAD(c, "cast %u: look out of range", i);
        }
        IDX16(c, cast[i].person, nper, "cast post", i);
        if (cast[i].person < nper && per[cast[i].person].cast != i) {
            BAD(c, "cast %u: its post is someone else's", i);
        }
    }
    for (uint32_t i = 0; i < narc; i++) {
        const mgpk_arc_t *a = &p->arcs[i];
        IDX(c, a->giver, ncast, "arc giver", i);
        str_ok(c, a->title, "arc", i);
        if ((uint64_t)a->first_line + a->n_intro + a->n_outro > nline) {
            BAD(c, "arc %u: lines past the table", i);
        }
    }
    for (uint32_t i = 0; i < nmis; i++) {
        const mgpk_mission_t *m = &p->missions[i];
        str_ok(c, m->id, "mission", i);
        str_ok(c, m->title, "mission", i);
        IDX(c, m->case_line, nline, "mission case line", i);
        IDX(c, m->giver, ncast, "mission giver", i);
        IDX16(c, m->patient, ncast, "mission patient", i);
        IDX16(c, m->ward, nroom, "mission ward", i);
        if ((uint64_t)m->first_line + m->n_intro + m->n_outro > nline) {
            BAD(c, "mission %u: lines past the table", i);
        }
        if ((uint64_t)m->first_quest + m->n_quests > nquest) {
            BAD(c, "mission %u: quests past the table", i);
        } else {
            for (uint32_t q = 0; q < m->n_quests; q++) {
                if (quest[m->first_quest + q].mission != i) {
                    BAD(c, "mission %u: quest %u is another case's", i, m->first_quest + q);
                }
            }
        }
    }
    for (uint32_t i = 0; i < nquest; i++) {
        const mgpk_quest_t *q = &quest[i];
        IDX(c, q->topic, ntopic, "quest topic", i);
        IDX(c, q->host, ncast, "quest host", i);
        IDX(c, q->mission, nmis, "quest mission", i);
        IDX(c, q->done_line, nline, "quest done line", i);
        if ((uint64_t)q->first_line + q->n_intro > nline) {
            BAD(c, "quest %u: lines past the table", i);
        }
        if ((uint64_t)q->first_step + q->n_steps > nms) {
            BAD(c, "quest %u: steps past the table", i);
        } else {
            for (uint32_t k = 0; k < q->n_steps; k++) {
                if (ms[q->first_step + k].quest != i) {
                    BAD(c, "quest %u: step %u is another quest's", i, q->first_step + k);
                }
            }
        }
    }
    for (uint32_t i = 0; i < nms; i++) {
        const mgpk_mstep_t *m = &ms[i];
        IDX(c, m->script, nscript, "step script", i);
        IDX(c, m->presenter, ncast, "step presenter", i);
        IDX(c, m->room, nroom, "step room", i);
        IDX(c, m->quest, nquest, "step quest", i);
        IDX16(c, m->patient, ncast, "step patient", i);
        IDX(c, m->line, nline, "step line", i);
        IDX(c, m->after_good, nline, "step line", i);
        IDX(c, m->after_mixed, nline, "step line", i);
        if (m->pat_pose > MGPK_POSE_WALK) {
            BAD(c, "step %u: pose %u", i, m->pat_pose);
        }
        if (m->room < nroom && (m->stand_x < room[m->room].x0 || m->stand_x > room[m->room].x1 || m->stand_z < room[m->room].z0 || m->stand_z > room[m->room].z1)) {
            BAD(c, "step %u: presenter stands outside its room", i);
        }
        /* a script in two steps (the reader marks a seen[] array; here the earlier steps are searched: no allocation,
         * a few hundred steps at load) */
        if (m->script < nscript) {
            for (uint32_t j = 0; j < i; j++) {
                if (ms[j].script == m->script) {
                    BAD(c, "script %u in two steps", m->script);
                    break;
                }
            }
        }
    }
    for (uint32_t i = 0; nms && i < nscript; i++) {
        if (!p->scripts[i].required) {
            continue;
        }
        bool seen = false;
        for (uint32_t j = 0; j < nms && !seen; j++) {
            seen = ms[j].script == i;
        }
        if (!seen) {
            BAD(c, "required script %u in no mission step", i);
        }
    }
    for (uint32_t i = 0; i < nper; i++) {
        IDX16(c, per[i].cast, ncast, "person cast", i);
        IDX16(c, per[i].room, nroom, "person room", i);
        if (per[i].kind > MGPK_KIND_STUDENT || per[i].pose > MGPK_POSE_WALK || per[i].mode > 1 || per[i].floor < 0 || per[i].floor > 3) {
            BAD(c, "person %u: out of range", i);
        }
        st->people_walking += per[i].mode != 0;
    }
}

bool book_pack_open(BookPack *p, const void *blob, size_t size, const char *label)
{
    memset(p, 0, sizeof(*p));
    Chk chk = {p, label ? label : "pack"}, *c = &chk;
    p->blob = blob;
    p->size = size;
    const uint16_t one = 1;
    if (*(const uint8_t *)&one != 1) {
        BAD(c, "the pack is little-endian; this host is not");
        return false;
    }
    if (!blob || ((uintptr_t)blob & 7u) != 0) {
        BAD(c, "missing, or not 8-byte aligned in memory");
        return false;
    }
    if (size < sizeof(mgpk_header_t)) {
        BAD(c, "file too small");
        return false;
    }
    if (size > UINT32_MAX) {
        BAD(c, "file too large");
        return false;
    }
    p->hdr = (const mgpk_header_t *)blob;
    if (p->hdr->magic != MGPK_MAGIC) {
        BAD(c, "bad magic");
        return false;
    }
    if (p->hdr->version != MGPK_VERSION) {
        BAD(c, "version %u, reader %u", p->hdr->version, MGPK_VERSION);
        return false;
    }
    uint32_t nstr;
    p->str = table(c, MGPK_STRINGS, 1, &nstr);
    p->str_size = nstr;
    if (!p->str || nstr == 0 || p->str[0] != '\0' || p->str[nstr - 1] != '\0') {
        BAD(c, "bad string table");
        return false;
    }
    p->rooms = table(c, MGPK_ROOMS, sizeof(mgpk_room_t), &p->room_count);
    p->topics = table(c, MGPK_TOPICS, sizeof(mgpk_topic_t), &p->topic_count);
    p->atoms = table(c, MGPK_ATOMS, sizeof(mgpk_atom_t), &p->atom_count);
    p->facts = table(c, MGPK_FACTS, sizeof(mgpk_fact_t), &p->fact_count);
    p->items = table(c, MGPK_ITEMS, sizeof(mgpk_item_t), &p->item_count);
    p->opts = table(c, MGPK_OPTS, sizeof(mgpk_opt_t), &p->opt_count);
    p->refs = table(c, MGPK_REFS, sizeof(uint32_t), &p->ref_count);
    p->scripts = table(c, MGPK_SCRIPTS, sizeof(mgpk_script_t), &p->script_count);
    p->steps = table(c, MGPK_STEPS, sizeof(mgpk_step_t), &p->step_count);
    p->props = table(c, MGPK_PROPS, sizeof(mgpk_prop_t), &p->prop_count);
    p->slots = table(c, MGPK_SLOTS, sizeof(mgpk_slot_t), &p->slot_count);
    p->figlabels = table(c, MGPK_FIGLABELS, sizeof(mgpk_figlabel_t), &p->figlabel_count);
    p->furniture = table(c, MGPK_FURNITURE, sizeof(mgpk_furniture_t), &p->furniture_count);
    if (p->problems) {
        return false;
    }
    check_content(c);
    p->cast = table(c, MGPK_CAST, sizeof(mgpk_cast_t), &p->cast_count);
    p->arcs = table(c, MGPK_ARCS, sizeof(mgpk_arc_t), &p->arc_count);
    p->missions = table(c, MGPK_MISSIONS, sizeof(mgpk_mission_t), &p->mission_count);
    p->quests = table(c, MGPK_QUESTS, sizeof(mgpk_quest_t), &p->quest_count);
    p->msteps = table(c, MGPK_MSTEPS, sizeof(mgpk_mstep_t), &p->mstep_count);
    p->lines = table(c, MGPK_LINES, sizeof(mgpk_line_t), &p->line_count);
    p->people = table(c, MGPK_PEOPLE, sizeof(mgpk_person_t), &p->person_count);
    check_missions(c);

    BookPackStats *st = &p->stats;
    uint32_t run = 0;
    for (uint32_t i = 1; i < nstr; i++) { /* number of strings, longest string */
        if (p->str[i] == '\0') {
            st->strings++;
            st->longest_string_bytes = run > st->longest_string_bytes ? run : st->longest_string_bytes;
            run = 0;
        } else {
            run++;
        }
    }
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; i++) {
        h = (h ^ p->blob[i]) * 16777619u;
    }
    st->fnv1a = h;
    st->bytes = (uint32_t)size;
    if (p->problems > BAD_LOGGED) {
        mg_log_error("book: %s: %u more problems not shown", c->label, p->problems - BAD_LOGGED);
    }
    p->ok = p->problems == 0;
    return p->ok;
}

static const void *g_figs;        /* its figures (file) */
static size_t g_figs_size;


/* a whole file, once at start (malloc'd and kept for the run: 16-byte aligned, the pack is read in place); NULL if missing */
static void *load_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    void *d = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        const long n = ftell(f);
        if (n > 0 && fseek(f, 0, SEEK_SET) == 0 && (d = malloc((size_t)n)) != NULL && fread(d, 1, (size_t)n, f) != (size_t)n) {
            free(d);
            d = NULL;
        }
        *size = d ? (size_t)n : 0;
    }
    fclose(f);
    return d;
}

const void *book_pack_figures(size_t *size)
{
    if (g_figs) {
        *size = g_figs_size;
        return g_figs;
    }
    const MgAsset *a = mg_asset_find("content/figures.mgfig");
    *size = a ? a->size : 0;
    return a ? a->data : NULL;
}

bool book_pack_load(BookPack *p, const char *content_dir)
{
    const void *data = NULL;
    size_t size = 0;
    char label[512];
    if (content_dir && content_dir[0]) { /* startup only: the file stays loaded for the run  */
        char path[512];
        snprintf(path, sizeof(path), "%s/fixture.mgpack", content_dir);
        snprintf(label, sizeof(label), "%s", path);
        data = load_file(path, &size);
        snprintf(path, sizeof(path), "%s/figures.mgfig", content_dir);
        g_figs = load_file(path, &g_figs_size);
    } else {
        const MgAsset *a = mg_asset_find("content/gastro.mgpack");
        data = a ? a->data : NULL;
        size = a ? a->size : 0;
        snprintf(label, sizeof(label), "content/gastro.mgpack");
    }
    if (!data) {
        memset(p, 0, sizeof(*p));
        mg_log_info("book: no content pack (%s): no book content", content_dir && content_dir[0] ? label : "none embedded");
        return false;
    }
    if (!book_pack_open(p, data, size, label)) {
        mg_log_error("book: %s refused (%u problems): the game runs without book content", label, p->problems);
        return false;
    }
    mg_log_info("book: %s v%u %u bytes fnv1a %08x: rooms %u topics %u atoms %u facts %u items %u opts %u scripts %u (%u required) steps %u "
                "props %u slots %u furniture %u cast %u missions %u quests %u msteps %u lines %u people %u, problems 0",
                label, p->hdr->version, p->stats.bytes, p->stats.fnv1a, p->room_count, p->topic_count, p->atom_count, p->fact_count, p->item_count,
                p->opt_count, p->script_count, p->stats.required, p->step_count, p->prop_count, p->slot_count, p->furniture_count, p->cast_count,
                p->mission_count, p->quest_count, p->mstep_count, p->line_count, p->person_count);
    return true;
}

const char *book_pack_str(const BookPack *p, uint32_t off)
{
    /* every offset the records hold was checked at load; anything else (a caller's own number) is "" */
    if (!p->ok || off >= p->str_size || (off > 0 && p->str[off - 1] != '\0')) {
        return "";
    }
    return p->str + off;
}

#define ACCESSOR(name, type, field, count)                                                                                                           \
    const type *name(const BookPack *p, uint32_t i) { return p->ok && i < p->count ? &p->field[i] : NULL; }
ACCESSOR(book_room, mgpk_room_t, rooms, room_count)
ACCESSOR(book_topic, mgpk_topic_t, topics, topic_count)
ACCESSOR(book_atom, mgpk_atom_t, atoms, atom_count)
ACCESSOR(book_fact, mgpk_fact_t, facts, fact_count)
ACCESSOR(book_item, mgpk_item_t, items, item_count)
ACCESSOR(book_opt, mgpk_opt_t, opts, opt_count)
ACCESSOR(book_script, mgpk_script_t, scripts, script_count)
ACCESSOR(book_step, mgpk_step_t, steps, step_count)
ACCESSOR(book_prop, mgpk_prop_t, props, prop_count)
ACCESSOR(book_slot, mgpk_slot_t, slots, slot_count)
ACCESSOR(book_figlabel, mgpk_figlabel_t, figlabels, figlabel_count)
ACCESSOR(book_furniture, mgpk_furniture_t, furniture, furniture_count)
ACCESSOR(book_cast, mgpk_cast_t, cast, cast_count)
ACCESSOR(book_arc, mgpk_arc_t, arcs, arc_count)
ACCESSOR(book_mission, mgpk_mission_t, missions, mission_count)
ACCESSOR(book_quest, mgpk_quest_t, quests, quest_count)
ACCESSOR(book_mstep, mgpk_mstep_t, msteps, mstep_count)
ACCESSOR(book_line, mgpk_line_t, lines, line_count)
ACCESSOR(book_person, mgpk_person_t, people, person_count)

uint32_t book_ref(const BookPack *p, uint32_t i) { return p->ok && i < p->ref_count ? p->refs[i] : MGPK_NONE; }
