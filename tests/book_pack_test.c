/* BOOK-1 ctest: game/book_pack.c on the embedded pack (the generated copy the build embeds), on the committed dummy pack
 * (the exact numbers content/gastro/mgpack_read.c prints for it) and on corrupted copies in memory, which must be refused.
 *   book_pack_test GENERATED.mgpack content/gastro_demo/gastro.mgpack
 * Brings its own mg_log_* / mg_asset_find so it links without SDL. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "book_pack.h"
#include "core/assets.h"
#include "core/log.h"

static int g_quiet; /* corrupted copies log their problems: keep the test output short */
static int g_failed;

static void vlog(const char *lvl, const char *fmt, va_list ap)
{
    if (g_quiet) {
        return;
    }
    fprintf(stderr, "%s: ", lvl);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}
void mg_log_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog("info", fmt, ap);
    va_end(ap);
}
void mg_log_warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog("warn", fmt, ap);
    va_end(ap);
}
void mg_log_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog("error", fmt, ap);
    va_end(ap);
}
const MgAsset *mg_asset_find(const char *name)
{
    (void)name;
    return NULL;
}

#define CHECK(cond)                                                                                                                                  \
    do {                                                                                                                                             \
        if (!(cond)) {                                                                                                                               \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                                          \
            g_failed++;                                                                                                                              \
        }                                                                                                                                            \
    } while (0)
#define EQ(a, b)                                                                                                                                     \
    do {                                                                                                                                             \
        unsigned long long a_ = (unsigned long long)(a), b_ = (unsigned long long)(b);                                                               \
        if (a_ != b_) {                                                                                                                              \
            fprintf(stderr, "FAIL %s:%d: %s = %llu, want %llu\n", __FILE__, __LINE__, #a, a_, b_);                                                   \
            g_failed++;                                                                                                                              \
        }                                                                                                                                            \
    } while (0)

/* the file in an 8-byte aligned buffer (malloc aligns to at least 8 on every target we build) */
static uint8_t *load(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n + 8);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "%s: read failed\n", path);
        exit(2);
    }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

/* mgpack_read content/gastro_demo/gastro.mgpack (key=value lines) */
static void expect_dummy_counts(const BookPack *p)
{
    EQ(p->stats.bytes, 102864);
    EQ(p->hdr->version, 4);
    EQ(p->stats.fnv1a, 0xb8ee2920u);
    EQ(p->room_count, 85);
    EQ(p->topic_count, 6);
    EQ(p->atom_count, 24);
    EQ(p->fact_count, 24);
    EQ(p->item_count, 12);
    EQ(p->opt_count, 35);
    EQ(p->ref_count, 48);
    EQ(p->script_count, 12);
    EQ(p->stats.required, 12);
    EQ(p->stats.split_parts, 0);
    EQ(p->step_count, 24);
    EQ(p->prop_count, 201);
    EQ(p->slot_count, 33);
    EQ(p->figlabel_count, 2);
    EQ(p->str_size, 47461);
    EQ(p->stats.strings, 2115);
    EQ(p->stats.longest_string_bytes, 79);
    EQ(p->cast_count, 6);
    EQ(p->mission_count, 3);
    EQ(p->quest_count, 6);
    EQ(p->mstep_count, 12);
    EQ(p->line_count, 68);
    EQ(p->stats.dialogue_bytes, 2131);
    EQ(p->stats.longest_dialogue_line_bytes, 50);
    EQ(p->person_count, 12);
    EQ(p->stats.people_walking, 3);
    EQ(p->furniture_count - p->stats.door_leaves, 1019); /* the reader's furniture= excludes the door leaves */
    EQ(p->stats.door_leaves, 105);
    EQ(p->stats.largest_script_steps, 2);
    EQ(p->stats.largest_script_string_bytes, 362);
    CHECK(strcmp(book_pack_str(p, p->scripts[p->stats.largest_script].id), "s-demo_t5-0") == 0);
}

static void expect_sound(const BookPack *p)
{
    CHECK(p->ok);
    EQ(p->problems, 0);
    CHECK(p->room_count > 0 && p->script_count > 0 && p->mstep_count > 0);
    /* accessors: in place, bounded */
    CHECK(book_room(p, 0) == &p->rooms[0]);
    CHECK(book_room(p, p->room_count) == NULL);
    CHECK(book_mstep(p, p->mstep_count) == NULL);
    CHECK(book_ref(p, p->ref_count) == MGPK_NONE);
    CHECK(strcmp(book_pack_str(p, MGPK_NONE), "") == 0);
    CHECK(strcmp(book_pack_str(p, p->str_size), "") == 0);
    CHECK(book_pack_str(p, book_room(p, 0)->name)[0] != '\0');
    CHECK((const uint8_t *)book_script(p, 0) >= p->blob && (const uint8_t *)book_script(p, 0) < p->blob + p->size);
    /* every required script is in exactly one mission step (the check must have held) */
    for (uint32_t i = 0; i < p->script_count; i++) {
        int n = 0;
        for (uint32_t j = 0; j < p->mstep_count; j++) {
            n += p->msteps[j].script == i;
        }
        CHECK(n <= 1 && (!p->scripts[i].required || n == 1));
    }
}

typedef void (*Corrupt)(uint8_t *b, size_t *size);
static mgpk_header_t *H(uint8_t *b) { return (mgpk_header_t *)b; }
static void *T(uint8_t *b, unsigned t) { return b + H(b)->table[t].offset; }
static void c_magic(uint8_t *b, size_t *n) { (void)n, H(b)->magic ^= 1u; }
static void c_version(uint8_t *b, size_t *n) { (void)n, H(b)->version = 3; }
static void c_short_file(uint8_t *b, size_t *n) { (void)b, *n = sizeof(mgpk_header_t) - 4; }
static void c_truncated(uint8_t *b, size_t *n) { (void)b, *n -= 64; }
static void c_record_size(uint8_t *b, size_t *n) { (void)n, H(b)->table[MGPK_ROOMS].size = 27; }
static void c_short_table(uint8_t *b, size_t *n) { (void)n, H(b)->table[MGPK_FACTS].count += 100000; }
static void c_unaligned_table(uint8_t *b, size_t *n) { (void)n, H(b)->table[MGPK_LINES].offset += 4; }
static void c_header_count(uint8_t *b, size_t *n) { (void)n, H(b)->item_count++; }
static void c_string_mid(uint8_t *b, size_t *n) { (void)n, ((mgpk_room_t *)T(b, MGPK_ROOMS))[0].name += 1; }
static void c_string_past(uint8_t *b, size_t *n) { (void)n, ((mgpk_fact_t *)T(b, MGPK_FACTS))[3].claim = H(b)->table[MGPK_STRINGS].count; }
static void c_strings_unterminated(uint8_t *b, size_t *n)
{
    (void)n;
    b[H(b)->table[MGPK_STRINGS].offset + H(b)->table[MGPK_STRINGS].count - 1] = 'x';
}
static void c_topic_room(uint8_t *b, size_t *n) { (void)n, ((mgpk_topic_t *)T(b, MGPK_TOPICS))[0].room = 9999; }
static void c_item_ref(uint8_t *b, size_t *n) { (void)n, ((uint32_t *)T(b, MGPK_REFS))[((mgpk_item_t *)T(b, MGPK_ITEMS))[0].first_fact_ref] = 1u << 30; }
static void c_script_steps(uint8_t *b, size_t *n) { (void)n, ((mgpk_script_t *)T(b, MGPK_SCRIPTS))[1].step_count = 0; }
static void c_mission_quest(uint8_t *b, size_t *n) { (void)n, ((mgpk_quest_t *)T(b, MGPK_QUESTS))[0].mission = 0xFFFE; }
static void c_script_twice(uint8_t *b, size_t *n) /* two steps play script 0, and the script step 1 played is in none */
{
    (void)n;
    mgpk_mstep_t *ms = T(b, MGPK_MSTEPS);
    ms[1].script = ms[0].script;
}
static void c_presenter_outside(uint8_t *b, size_t *n) { (void)n, ((mgpk_mstep_t *)T(b, MGPK_MSTEPS))[0].stand_x = 1e6f; }
static void c_person_floor(uint8_t *b, size_t *n) { (void)n, ((mgpk_person_t *)T(b, MGPK_PEOPLE))[0].floor = 7; }

static void corrupted(const uint8_t *good, size_t size, const char *name, Corrupt fn)
{
    uint8_t *b = malloc(size + 8);
    memcpy(b, good, size);
    size_t n = size;
    fn(b, &n);
    BookPack p;
    g_quiet = 1;
    const bool ok = book_pack_open(&p, b, n, name);
    g_quiet = 0;
    if (ok || p.problems == 0) {
        fprintf(stderr, "FAIL corrupted copy \"%s\" was accepted (problems %u)\n", name, p.problems);
        g_failed++;
    } else {
        printf("refused %-22s problems=%u\n", name, p.problems);
    }
    free(b);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: book_pack_test GENERATED.mgpack DUMMY.mgpack\n");
        return 2;
    }
    size_t gen_size, demo_size;
    uint8_t *gen = load(argv[1], &gen_size), *demo = load(argv[2], &demo_size);

    BookPack p;
    CHECK(book_pack_open(&p, gen, gen_size, "generated"));
    expect_sound(&p);
#ifdef MG_CONTENT_DEMO
    expect_dummy_counts(&p); /* this build embeds the dummy pack */
#endif
    printf("generated: %s pack, bytes=%u fnv1a=%08x rooms=%u topics=%u items=%u scripts=%u required=%u msteps=%u problems=%u\n",
           book_pack_is_demo() ? "demo" : "real", p.stats.bytes, p.stats.fnv1a, p.room_count, p.topic_count, p.item_count, p.script_count,
           p.stats.required, p.mstep_count, p.problems);

    CHECK(book_pack_open(&p, demo, demo_size, "dummy"));
    expect_sound(&p);
    expect_dummy_counts(&p);

    /* refused: the blob not 8-byte aligned in memory */
    uint8_t *odd = malloc(demo_size + 8);
    memcpy(odd + 1, demo, demo_size);
    g_quiet = 1;
    CHECK(!book_pack_open(&p, odd + 1, demo_size, "unaligned") && p.problems > 0);
    g_quiet = 0;
    free(odd);
    /* a refused pack hands out nothing */
    CHECK(book_room(&p, 0) == NULL && strcmp(book_pack_str(&p, 0), "") == 0);

    static const struct {
        const char *name;
        Corrupt fn;
    } bad[] = {
        {"bad magic", c_magic},
        {"version 3", c_version},
        {"shorter than a header", c_short_file},
        {"truncated", c_truncated},
        {"record size", c_record_size},
        {"short table", c_short_table},
        {"unaligned table", c_unaligned_table},
        {"header item count", c_header_count},
        {"string mid-string", c_string_mid},
        {"string past the table", c_string_past},
        {"strings unterminated", c_strings_unterminated},
        {"topic room index", c_topic_room},
        {"item fact ref", c_item_ref},
        {"script without steps", c_script_steps},
        {"quest of no mission", c_mission_quest},
        {"script in two steps", c_script_twice},
        {"presenter outside", c_presenter_outside},
        {"person floor", c_person_floor},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        corrupted(demo, demo_size, bad[i].name, bad[i].fn);
    }
    free(gen);
    free(demo);
    if (g_failed) {
        fprintf(stderr, "book_pack_test: %d checks FAILED\n", g_failed);
        return 1;
    }
    printf("book_pack_test: all checks passed\n");
    return 0;
}
