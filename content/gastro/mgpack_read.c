/* Reference reader for the BOOK-1 content pack (gastro.mgpack, format MGPK v2, little-endian).
 *   cc -std=c11 -O1 -Wall -Wextra -Werror -include gastro.mgpack.h mgpack_read.c -o mgpack_read && ./mgpack_read gastro.mgpack
 * Loads the file into one buffer, checks the header, every table bound and record size, every string offset and every
 * cross-reference, then prints counts and measured sizes as key=value lines. Exit 0 = the pack is sound.
 * check.py compiles and runs it against the pack and compares its counts with the JSON sources. No allocation after load,
 * no parsing at run time: the engine reader can be this file minus the checks. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t *g_buf;
static size_t g_size;
static const char *g_str;
static uint32_t g_str_size;
static unsigned g_bad;

#define BAD(...) do { if (g_bad < 20) { fprintf(stderr, "mgpack: " __VA_ARGS__); fputc('\n', stderr); } g_bad++; } while (0)

static const mgpk_header_t *g_hdr;

static const void *table(unsigned t, uint32_t rec, uint32_t *count)
{
    const mgpk_table_t *d = &g_hdr->table[t];
    *count = d->count;
    if (d->size != rec) { BAD("table %u: record size %u, reader expects %u", t, d->size, rec); *count = 0; return NULL; }
    if (d->offset % 8) BAD("table %u: offset %u not 8-byte aligned", t, d->offset);
    if ((uint64_t)d->offset + (uint64_t)d->count * d->size > g_size) { BAD("table %u: past the end of the file", t); *count = 0; return NULL; }
    return g_buf + d->offset;
}

/* a string reference: inside the table and at the start of a string */
static uint32_t str_ok(uint32_t off, const char *what, uint32_t row)
{
    if (off >= g_str_size || (off > 0 && g_str[off - 1] != '\0')) { BAD("%s %u: bad string offset %u", what, row, off); return 0; }
    return (uint32_t)strlen(g_str + off);
}

static uint32_t str_opt(uint32_t off, const char *what, uint32_t row)
{
    return off == MGPK_NONE ? 0 : str_ok(off, what, row);
}

#define IDX(v, n, what, row) do { if ((uint32_t)(v) >= (uint32_t)(n)) BAD("%s %u: index %u >= %u", what, (unsigned)(row), (unsigned)(v), (unsigned)(n)); } while (0)
#define IDX_OPT(v, n, what, row) do { if ((uint32_t)(v) != MGPK_NONE) IDX(v, n, what, row); } while (0)

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: mgpack_read FILE.mgpack\n"); return 2; }
    const uint16_t one = 1;
    if (*(const uint8_t *)&one != 1) { fprintf(stderr, "mgpack: the pack is little-endian; this host is not\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "mgpack: read failed\n"); return 2; }
    fclose(f);
    g_buf = buf;
    g_size = (size_t)n;
    if (g_size < sizeof(mgpk_header_t)) { fprintf(stderr, "mgpack: file too small\n"); return 1; }
    g_hdr = (const mgpk_header_t *)buf;
    if (g_hdr->magic != MGPK_MAGIC) { fprintf(stderr, "mgpack: bad magic\n"); return 1; }
    if (g_hdr->version != MGPK_VERSION) { fprintf(stderr, "mgpack: version %u, reader %u\n", g_hdr->version, MGPK_VERSION); return 1; }

    uint32_t nstr, nroom, ntopic, natom, nfact, nitem, nopt, nref, nscript, nstep, nprop, nslot, nfig;
    g_str = table(MGPK_STRINGS, 1, &nstr);
    g_str_size = nstr;
    if (!g_str || nstr == 0 || g_str[0] != '\0' || g_str[nstr - 1] != '\0') { fprintf(stderr, "mgpack: bad string table\n"); return 1; }
    const mgpk_room_t *room = table(MGPK_ROOMS, sizeof(mgpk_room_t), &nroom);
    const mgpk_topic_t *topic = table(MGPK_TOPICS, sizeof(mgpk_topic_t), &ntopic);
    const mgpk_atom_t *atom = table(MGPK_ATOMS, sizeof(mgpk_atom_t), &natom);
    const mgpk_fact_t *fact = table(MGPK_FACTS, sizeof(mgpk_fact_t), &nfact);
    const mgpk_item_t *item = table(MGPK_ITEMS, sizeof(mgpk_item_t), &nitem);
    const mgpk_opt_t *opt = table(MGPK_OPTS, sizeof(mgpk_opt_t), &nopt);
    const uint32_t *ref = table(MGPK_REFS, sizeof(uint32_t), &nref);
    const mgpk_script_t *script = table(MGPK_SCRIPTS, sizeof(mgpk_script_t), &nscript);
    const mgpk_step_t *step = table(MGPK_STEPS, sizeof(mgpk_step_t), &nstep);
    const mgpk_prop_t *prop = table(MGPK_PROPS, sizeof(mgpk_prop_t), &nprop);
    const mgpk_slot_t *slot = table(MGPK_SLOTS, sizeof(mgpk_slot_t), &nslot);
    const mgpk_figlabel_t *fig = table(MGPK_FIGLABELS, sizeof(mgpk_figlabel_t), &nfig);
    if (g_bad) return 1;
    if (g_hdr->fact_count != nfact || g_hdr->item_count != nitem) BAD("header counts differ from the tables");

    for (uint32_t i = 0; i < nroom; i++) { str_ok(room[i].id, "room", i); str_ok(room[i].name, "room", i); }
    for (uint32_t i = 0; i < ntopic; i++) {
        const mgpk_topic_t *t = &topic[i];
        str_ok(t->id, "topic", i); str_ok(t->title, "topic", i);
        IDX(t->room, nroom, "topic room", i);
        if ((uint64_t)t->first_fact + t->fact_count > nfact) BAD("topic %u: facts out of range", i);
        if ((uint64_t)t->first_item + t->item_count > nitem) BAD("topic %u: items out of range", i);
        if ((uint64_t)t->first_script + t->script_count > nscript) BAD("topic %u: scripts out of range", i);
        for (uint32_t k = 0; k < t->fact_count && t->first_fact + k < nfact; k++)
            if (fact[t->first_fact + k].topic != i) BAD("topic %u: fact %u belongs to topic %u", i, t->first_fact + k, fact[t->first_fact + k].topic);
    }
    for (uint32_t i = 0; i < natom; i++) {
        str_ok(atom[i].id, "atom", i); str_ok(atom[i].text, "atom", i); str_opt(atom[i].fig, "atom fig", i);
        IDX(atom[i].topic, ntopic, "atom topic", i);
    }
    for (uint32_t i = 0; i < nfact; i++) {
        str_ok(fact[i].id, "fact", i); str_ok(fact[i].claim, "fact", i);
        IDX(fact[i].atom, natom, "fact atom", i); IDX(fact[i].topic, ntopic, "fact topic", i);
    }
    for (uint32_t i = 0; i < nitem; i++) {
        const mgpk_item_t *it = &item[i];
        str_ok(it->id, "item", i); str_ok(it->stem, "item", i); str_ok(it->explanation, "item", i); str_opt(it->review_note, "item", i);
        IDX(it->topic, ntopic, "item topic", i);
        if (it->type > 7) BAD("item %u: type %u", i, it->type);
        if ((uint64_t)it->first_opt + it->n_opts > nopt || it->n_opts == 0) BAD("item %u: options out of range", i);
        if ((uint64_t)it->first_fact_ref + it->n_facts > nref || it->n_facts == 0) BAD("item %u: fact refs out of range", i);
        else for (uint32_t k = 0; k < it->n_facts; k++) IDX(ref[it->first_fact_ref + k], nfact, "item fact", i);
    }
    for (uint32_t i = 0; i < nopt; i++) {
        str_ok(opt[i].text, "opt", i); str_opt(opt[i].aux, "opt aux", i); str_opt(opt[i].why, "opt why", i);
        IDX_OPT(opt[i].fact, nfact, "opt fact", i);
    }
    /* scripts: bounds, and the string bytes each one pulls in (title + teach text + its items) */
    uint64_t big_bytes = 0; uint32_t big = 0, big_steps = 0, parts = 0, required = 0;
    for (uint32_t i = 0; i < nscript; i++) {
        const mgpk_script_t *s = &script[i];
        uint64_t bytes = str_ok(s->title, "script", i);
        str_ok(s->id, "script", i); str_opt(s->npc, "script npc", i);
        IDX(s->topic, ntopic, "script topic", i); IDX(s->room, nroom, "script room", i);
        IDX_OPT(s->prop, nprop, "script prop", i); IDX_OPT(s->slot, nslot, "script slot", i);
        if (s->kind > 11) BAD("script %u: kind %u", i, s->kind);
        if (s->part < 1 || s->part > s->parts) BAD("script %u: part %u of %u", i, s->part, s->parts);
        if (s->parts > 1) parts++;
        if (s->required) {
            required++;
            if (s->step_count > 15) BAD("script %u: required script with %u steps (max 15)", i, s->step_count);
        }
        if ((uint64_t)s->first_step + s->step_count > nstep || s->step_count == 0) { BAD("script %u: steps out of range", i); continue; }
        for (uint32_t k = 0; k < s->step_count; k++) {
            const mgpk_step_t *st = &step[s->first_step + k];
            if (st->stage > 2) BAD("step %u: stage %u", s->first_step + k, st->stage);
            if (st->item == MGPK_NONE) {
                if ((uint64_t)st->first_atom_ref + st->n_atoms > nref || st->n_atoms == 0) { BAD("step %u: atom refs out of range", s->first_step + k); continue; }
                for (uint32_t a = 0; a < st->n_atoms; a++) {
                    uint32_t ai = ref[st->first_atom_ref + a];
                    IDX(ai, natom, "step atom", s->first_step + k);
                    if (ai < natom) bytes += strlen(g_str + atom[ai].text);
                }
            } else {
                IDX(st->item, nitem, "step item", s->first_step + k);
                if (st->item >= nitem) continue;
                const mgpk_item_t *it = &item[st->item];
                bytes += strlen(g_str + it->stem) + strlen(g_str + it->explanation);
                if (it->review_note != MGPK_NONE) bytes += strlen(g_str + it->review_note);
                for (uint32_t o = 0; o < it->n_opts && it->first_opt + o < nopt; o++) {
                    const mgpk_opt_t *op = &opt[it->first_opt + o];
                    bytes += strlen(g_str + op->text);
                    if (op->aux != MGPK_NONE) bytes += strlen(g_str + op->aux);
                    if (op->why != MGPK_NONE) bytes += strlen(g_str + op->why);
                }
            }
        }
        if (bytes > big_bytes) { big_bytes = bytes; big = i; big_steps = s->step_count; }
    }
    for (uint32_t i = 0; i < nprop; i++) { str_ok(prop[i].id, "prop", i); str_ok(prop[i].kind, "prop", i); str_ok(prop[i].label, "prop", i); IDX(prop[i].room, nroom, "prop room", i); }
    for (uint32_t i = 0; i < nslot; i++) { str_ok(slot[i].id, "slot", i); str_ok(slot[i].kind, "slot", i); IDX(slot[i].room, nroom, "slot room", i); }
    for (uint32_t i = 0; i < nfig; i++) {
        str_ok(fig[i].fig, "figlabel", i); str_ok(fig[i].text, "figlabel", i);
        if (fig[i].x1 > fig[i].w || fig[i].y1 > fig[i].h || fig[i].x0 > fig[i].x1 || fig[i].y0 > fig[i].y1) BAD("figlabel %u: box outside the crop", i);
    }
    /* longest string, number of strings */
    uint32_t longest = 0, nstrings = 0, run = 0;
    for (uint32_t i = 1; i < nstr; i++) {
        if (g_str[i] == '\0') { nstrings++; if (run > longest) longest = run; run = 0; } else run++;
    }
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < g_size; i++) h = (h ^ g_buf[i]) * 16777619u;
    printf("bytes=%zu\nversion=%u\nfnv1a=%08x\n", g_size, g_hdr->version, h);
    printf("rooms=%u\ntopics=%u\natoms=%u\nfacts=%u\nitems=%u\nopts=%u\nrefs=%u\nscripts=%u\nrequired=%u\nsplit_parts=%u\nsteps=%u\nprops=%u\nslots=%u\nfiglabels=%u\n",
           nroom, ntopic, natom, nfact, nitem, nopt, nref, nscript, required, parts, nstep, nprop, nslot, nfig);
    printf("string_table_bytes=%u\nstrings=%u\nlongest_string_bytes=%u\n", nstr, nstrings, longest);
    printf("largest_script=%s\nlargest_script_steps=%u\nlargest_script_string_bytes=%llu\n", g_str + script[big].id, big_steps, (unsigned long long)big_bytes);
    printf("problems=%u\n", g_bad);
    free(buf);
    return g_bad ? 1 : 0;
}
