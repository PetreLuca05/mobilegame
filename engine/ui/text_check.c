#include "text_check.h"

#include <stdint.h>
#include <string.h>

#include "core/log.h"
#include "text.h"

#define TC_SLOTS 16384 /* open addressing, power of two: the vocabulary + the data words seen in a run */
#define TC_MAX_BAD 32
#define TC_TEXT 96
#define TC_WORD 32
#define TC_MAX_WATCH 64

typedef struct TcBad {
    uint64_t hash; /* of the whole text: one entry per distinct text */
    char text[TC_TEXT], word[TC_WORD];
    const char *screen;
} TcBad;

/* only touched by autotest runs; zero (off) in the shipped game */
typedef struct TextCheck {
    bool on;
    int exempt;
    int used;
    bool full_logged;
    const char *screen;
    uint64_t slot[TC_SLOTS]; /* word hashes, 0 = empty */
    TcBad bad[TC_MAX_BAD];
    int bad_count, bad_dropped;
    const char *watch[TC_MAX_WATCH];
    int watch_count, watch_hits;
    const char *watch_first;
} TextCheck;

static TextCheck *tc(void)
{
    static TextCheck t;
    return &t;
}

/* unit symbols drawn next to numbers: not words of any language */
static const char *const k_units[] = {"fps", "ms", "pt", "px", "kb", "mb", "gb", "hz", "gpu", "cpu", "km"};

static bool is_letter(uint32_t cp)
{
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= 0xC0 && cp < 0x250 && cp != 0xD7 && cp != 0xF7) ||
           (cp >= 0x391 && cp <= 0x3C9);
}

static uint32_t fold(uint32_t cp)
{
    if (cp >= 'A' && cp <= 'Z') {
        return cp + 32;
    }
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) {
        return cp + 32;
    }
    if (((cp >= 0x100 && cp < 0x138) || (cp >= 0x14A && cp < 0x178) || (cp >= 0x218 && cp < 0x220)) && (cp & 1u) == 0) {
        return cp + 1; /* Ă ă, Ş ş, Ţ ţ, Ș ș, Ț ț ... */
    }
    if (cp >= 0x391 && cp <= 0x3A9) {
        return cp + 32;
    }
    return cp;
}

static bool is_digit(uint32_t cp) { return cp >= '0' && cp <= '9'; }

/* One word: [a, b) bytes. Returns its hash, 0 if it is not a word (single letter, touches a digit, a unit). */
typedef struct Word {
    const char *a, *b;
    int letters;
    bool digit_touch, after_percent;
} Word;

static uint64_t word_hash(const Word *w)
{
    uint64_t h = 1469598103934665603ull;
    const char *p = w->a;
    while (p < w->b) {
        uint32_t cp = fold(text_next_cp(&p, w->b));
        h = (h ^ cp) * 1099511628211ull;
    }
    return h ? h : 1;
}

static bool is_unit(const Word *w)
{
    const size_t n = (size_t)(w->b - w->a);
    for (size_t i = 0; i < sizeof(k_units) / sizeof(k_units[0]); i++) {
        if (strlen(k_units[i]) == n) {
            size_t k = 0;
            while (k < n && (w->a[k] | 32) == k_units[i][k]) {
                k++;
            }
            if (k == n) {
                return true;
            }
        }
    }
    return false;
}

/* Calls fn for each word of the first len bytes of s. */
typedef void (*WordFn)(const Word *w, void *ctx);
static void each_word(const char *s, size_t len, WordFn fn, void *ctx)
{
    const char *end = len == (size_t)-1 ? NULL : s + len;
    const char *p = s;
    uint32_t prev = 0;
    Word w = {0};
    bool in = false;
    while ((!end || p < end) && *p) {
        const char *at = p;
        uint32_t cp = text_next_cp(&p, end);
        if (is_letter(cp)) {
            if (!in) {
                w = (Word){at, p, 0, is_digit(prev), prev == '%'};
                in = true;
            }
            w.b = p;
            w.letters++;
        } else if (in) {
            w.digit_touch = w.digit_touch || is_digit(cp);
            fn(&w, ctx);
            in = false;
        }
        prev = cp;
    }
    if (in) {
        fn(&w, ctx);
    }
}

static bool counts(const Word *w) { return w->letters >= 2 && !w->digit_touch && !w->after_percent && !is_unit(w); }

static bool has(uint64_t h)
{
    const TextCheck *t = tc();
    for (uint32_t i = (uint32_t)h & (TC_SLOTS - 1), n = 0; n < TC_SLOTS; i = (i + 1) & (TC_SLOTS - 1), n++) {
        if (t->slot[i] == h) {
            return true;
        }
        if (!t->slot[i]) {
            return false;
        }
    }
    return false;
}

static void add_word(const Word *w, void *ctx)
{
    (void)ctx;
    if (!counts(w)) {
        return;
    }
    TextCheck *t = tc();
    const uint64_t h = word_hash(w);
    for (uint32_t i = (uint32_t)h & (TC_SLOTS - 1), n = 0; n < TC_SLOTS; i = (i + 1) & (TC_SLOTS - 1), n++) {
        if (t->slot[i] == h) {
            return;
        }
        if (!t->slot[i]) {
            if (t->used >= TC_SLOTS * 3 / 4) {
                if (!t->full_logged) {
                    mg_log_error("text check: word table full (%d): raise TC_SLOTS", t->used);
                    t->full_logged = true;
                }
                return;
            }
            t->slot[i] = h;
            t->used++;
            return;
        }
    }
}

void text_check_enable(bool on) { tc()->on = on; }
bool text_check_enabled(void) { return tc()->on; }
void text_check_vocab(const char *s) { each_word(s, (size_t)-1, add_word, NULL); }

void text_check_data(const char *s)
{
    if (tc()->on && s) {
        each_word(s, (size_t)-1, add_word, NULL);
    }
}

void text_check_exempt_push(void) { tc()->exempt++; }
void text_check_exempt_pop(void) { tc()->exempt--; }
void text_check_set_screen(const char *name) { tc()->screen = name; }

typedef struct ScanCtx {
    const Word *bad;
    Word first;
} ScanCtx;

static void scan_word(const Word *w, void *ctx)
{
    ScanCtx *c = (ScanCtx *)ctx;
    if (!c->bad && counts(w) && !has(word_hash(w))) {
        c->first = *w;
        c->bad = &c->first;
    }
}

void text_check_watch(const char *s)
{
    TextCheck *t = tc();
    if (s && s[0] && t->watch_count < TC_MAX_WATCH) {
        t->watch[t->watch_count++] = s;
    }
}

void text_check_watch_reset(void)
{
    tc()->watch_hits = 0;
    tc()->watch_first = NULL;
}

int text_check_watch_count(void) { return tc()->watch_count; }

int text_check_watch_hits(const char **first)
{
    if (first) {
        *first = tc()->watch_first;
    }
    return tc()->watch_hits;
}

static void watch_scan(TextCheck *t, const char *s, size_t len)
{
    size_t n = 0;
    while ((len == (size_t)-1 || n < len) && s[n]) {
        n++;
    }
    for (int i = 0; i < t->watch_count; i++) {
        const size_t wn = strlen(t->watch[i]);
        if (n >= wn && memcmp(s, t->watch[i], wn) == 0 && (n == wn || s[wn] == ' ')) {
            if (t->watch_hits++ == 0) {
                t->watch_first = t->watch[i];
            }
            return;
        }
    }
}

void text_check_scan(const char *s, size_t len)
{
    TextCheck *t = tc();
    if (!t->on || !s) {
        return;
    }
    if (t->watch_count > 0) {
        watch_scan(t, s, len);
    }
    if (t->exempt > 0) {
        return;
    }
    ScanCtx c = {0};
    each_word(s, len, scan_word, &c);
    if (!c.bad) {
        return;
    }
    size_t n = 0;
    while ((len == (size_t)-1 || n < len) && s[n]) {
        n++;
    }
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ (uint8_t)s[i]) * 1099511628211ull;
    }
    for (int i = 0; i < t->bad_count; i++) {
        if (t->bad[i].hash == h) {
            return;
        }
    }
    if (t->bad_count >= TC_MAX_BAD) {
        t->bad_dropped++;
        return;
    }
    TcBad *b = &t->bad[t->bad_count++];
    b->hash = h;
    const size_t tn = n < TC_TEXT - 1 ? n : TC_TEXT - 1;
    memcpy(b->text, s, tn);
    b->text[tn] = 0;
    const size_t wn = (size_t)(c.first.b - c.first.a) < TC_WORD - 1 ? (size_t)(c.first.b - c.first.a) : TC_WORD - 1;
    memcpy(b->word, c.first.a, wn);
    b->word[wn] = 0;
    b->screen = t->screen ? t->screen : "?";
    mg_log_error("text check: hard-coded text on %s: \"%s\" (\"%s\" is in no string)", b->screen, b->text, b->word);
}

int text_check_count(void) { return tc()->bad_count + tc()->bad_dropped; }

bool text_check_get(int i, const char **text, const char **word, const char **screen)
{
    const TextCheck *t = tc();
    if (i < 0 || i >= t->bad_count) {
        return false;
    }
    *text = t->bad[i].text;
    *word = t->bad[i].word;
    *screen = t->bad[i].screen;
    return true;
}
