/* ctest: text_wrap (engine/ui/text_layout.c) on a made-up font with round advances: every glyph 10 pt at size 10,
 * 'i' and the space 5 pt. Widths, UTF-8 multi-byte sequences, words longer than a line, empty lines, the caller's cap. */
#include <stdio.h>
#include <string.h>

#include "ui/text.h"

static TextFont g_font;
static int g_failed;

#define CHECK(cond)                                                                                                                                  \
    do {                                                                                                                                             \
        if (!(cond)) {                                                                                                                               \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                                          \
            g_failed++;                                                                                                                              \
        }                                                                                                                                            \
    } while (0)

static void glyph(uint32_t slot, float adv)
{
    g_font.glyphs[slot] = (TextGlyph){.w = 8.0f, .h = 10.0f, .advance = adv, .present = true};
}

static void setup(void)
{
    g_font.em_px = 10.0f;
    g_font.line_px = 12.0f;
    for (uint32_t c = 33; c < 127; c++) {
        glyph(c, 10.0f);
    }
    glyph(' ', 5.0f);
    glyph('i', 5.0f);
    glyph(0x219, 10.0f); /* ș */
    glyph(0x103, 10.0f); /* ă */
    glyph(0x21B, 10.0f); /* ț */
    for (uint32_t s = 14; s <= 24; s++) { /* „ ” – ’ › → − † α β γ */
        glyph(s, 10.0f);
    }
}

/* Wraps s at width w and compares with the expected lines (NULL-terminated) and their widths. */
static void expect(int line, const char *s, float w, const char *const *want, const float *want_w)
{
    TextLine out[32];
    const int n = text_wrap(&g_font, s, strlen(s), 10.0f, w, out, 32);
    int k = 0;
    while (want[k]) {
        k++;
    }
    if (n != k) {
        fprintf(stderr, "FAIL line %d: \"%s\" at %.0f pt: %d lines, want %d\n", line, s, (double)w, n, k);
        for (int i = 0; i < n && i < 32; i++) {
            fprintf(stderr, "   [%d] \"%.*s\" %.1f\n", i, (int)out[i].len, s + out[i].start, (double)out[i].width);
        }
        g_failed++;
        return;
    }
    for (int i = 0; i < n; i++) {
        const bool same = strlen(want[i]) == out[i].len && memcmp(s + out[i].start, want[i], out[i].len) == 0;
        if (!same || (want_w && out[i].width != want_w[i])) {
            fprintf(stderr, "FAIL line %d: \"%s\" line %d = \"%.*s\" %.1f pt, want \"%s\" %.1f\n", line, s, i, (int)out[i].len, s + out[i].start,
                    (double)out[i].width, want[i], want_w ? (double)want_w[i] : -1.0);
            g_failed++;
        }
    }
}
#define EXPECT(s, w, ...)                                                                                                                            \
    do {                                                                                                                                             \
        static const char *const lines_[] = {__VA_ARGS__, NULL};                                                                                     \
        expect(__LINE__, s, w, lines_, NULL);                                                                                                        \
    } while (0)
#define EXPECT_W(s, w, widths, ...)                                                                                                                  \
    do {                                                                                                                                             \
        static const char *const lines_[] = {__VA_ARGS__, NULL};                                                                                     \
        static const float widths_[] = widths;                                                                                                       \
        expect(__LINE__, s, w, lines_, widths_);                                                                                                     \
    } while (0)
#define W(...) {__VA_ARGS__}

int main(void)
{
    setup();
    TextLine out[8];

    /* empty text: no lines */
    CHECK(text_wrap(&g_font, "", 0, 10.0f, 100.0f, out, 8) == 0);
    /* widths: "hello" 50, the space 5, "i" 5 */
    EXPECT_W("hello world", 100.0f, W(50.0f, 50.0f), "hello", "world");
    EXPECT_W("hello world", 105.0f, W(105.0f), "hello world");
    EXPECT_W("hi there", 1000.0f, W(70.0f), "hi there");
    EXPECT_W("a  b   ", 1000.0f, W(30.0f), "a  b"); /* trailing spaces are not part of the line */
    EXPECT("ab   cd", 30.0f, "ab", "cd");          /* a space run at a break belongs to no line */
    EXPECT("one two three four", 75.0f, "one two", "three", "four");
    /* empty lines and hard breaks */
    EXPECT("a\n\nb", 100.0f, "a", "", "b");
    EXPECT("abc\n", 100.0f, "abc");
    EXPECT("\n", 100.0f, "");
    EXPECT("\n\n", 100.0f, "", "");
    EXPECT("x\n  y", 100.0f, "x", "  y"); /* indentation after a hard break stays */
    EXPECT("a b\r\nc", 100.0f, "a b", "c");
    /* words longer than the line are cut between code points */
    EXPECT_W("abcdefghij", 34.0f, W(30.0f, 30.0f, 25.0f, 10.0f), "abc", "def", "ghi", "j");
    EXPECT("ab cdefghij", 40.0f, "ab", "cdef", "ghij");
    EXPECT("abc", 0.0f, "a", "b", "c"); /* nothing fits: one code point a line, never stuck */
    EXPECT("   abcdef", 30.0f, "   a", "bcd", "ef"); /* indentation after a break counts; the word is cut */
    EXPECT("      ab", 30.0f, "", "ab");           /* the indentation alone fills the line: the word goes below */
    /* UTF-8: a multi-byte letter is one glyph and is never split */
    EXPECT_W("șșșș", 25.0f, W(20.0f, 20.0f), "șș", "șș");
    EXPECT("„αβ”", 20.0f, "„α", "β”");
    EXPECT("ăș ăș", 20.0f, "ăș", "ăș");
    EXPECT("→ γ − †", 25.0f, "→ γ", "− †");
    EXPECT_W("cărți și", 1000.0f, W(65.0f), "cărți și"); /* c ă r ț 40 + i 5, space 5, ș 10 + i 5 */
    /* a broken sequence is one '?' per byte, and never reads past the string */
    {
        const char bad[] = "a\xC3";
        CHECK(text_wrap(&g_font, bad, strlen(bad), 10.0f, 1000.0f, out, 8) == 1 && out[0].len == 2 && out[0].width == 20.0f);
        const char cut[] = "\xE2\x80\x9E"; /* „ given only 2 of its 3 bytes: two '?' */
        CHECK(text_wrap(&g_font, cut, 2, 10.0f, 1000.0f, out, 8) == 1 && out[0].width == 20.0f);
        CHECK(text_width_n(&g_font, cut, 2, 10.0f) == 20.0f && text_width_n(&g_font, cut, 3, 10.0f) == 10.0f);
    }
    /* len limits the text; a NUL ends it early */
    CHECK(text_wrap(&g_font, "hello world", 5, 10.0f, 1000.0f, out, 8) == 1 && out[0].len == 5);
    CHECK(text_wrap(&g_font, "ab\0cd", 5, 10.0f, 1000.0f, out, 8) == 1 && out[0].len == 2);
    /* the cap: the count is the whole text's, nothing is written past cap */
    {
        TextLine small[3];
        memset(small, 0xAB, sizeof(small));
        const int n = text_wrap(&g_font, "aa bb cc dd ee", 14, 10.0f, 25.0f, small, 2);
        CHECK(n == 5);
        CHECK(small[0].len == 2 && small[1].start == 3);
        CHECK(small[2].start == 0xABABABABu); /* untouched */
    }
    /* a long text: every byte lands in exactly one line or one break, in order */
    {
        static char big[25000];
        static TextLine lines[4096];
        size_t n = 0;
        while (n + 12 < sizeof(big)) {
            const char *w = (n / 7) % 5 == 0 ? "ș„α” " : (n / 7) % 3 == 0 ? "lung\n" : "cuvânt ";
            memcpy(big + n, w, strlen(w));
            n += strlen(w);
        }
        big[n] = 0;
        const int count = text_wrap(&g_font, big, n, 10.0f, 300.0f, lines, 4096);
        CHECK(count > 100 && count <= 4096);
        uint32_t at = 0;
        for (int i = 0; i < count && i < 4096; i++) {
            CHECK(lines[i].start >= at && lines[i].width <= 300.0f);
            for (uint32_t b = at; b < lines[i].start; b++) { /* only breaks between lines */
                CHECK(big[b] == ' ' || big[b] == '\n');
            }
            CHECK((big[lines[i].start] & 0xC0) != 0x80); /* never inside a UTF-8 sequence */
            at = lines[i].start + lines[i].len;
        }
    }
    if (g_failed) {
        fprintf(stderr, "text_wrap_test: %d checks FAILED\n", g_failed);
        return 1;
    }
    printf("text_wrap_test: all checks passed\n");
    return 0;
}
