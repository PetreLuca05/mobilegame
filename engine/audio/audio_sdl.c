#include "audio.h"

#include <math.h>

#include <SDL3/SDL.h>

#include "core/log.h"

#define AUDIO_RATE 48000
#define DING_SECONDS 1.1f

struct Audio {
    SDL_AudioStream *stream;
    float *ding;
    int ding_len;
    SDL_AtomicInt cursor; /* next sample to play, ding_len = idle */
    float click[1440];    /* 30 ms */
    SDL_AtomicInt click_cursor;
    float *cue[AUDIO_CUE_COUNT];
    int cue_len[AUDIO_CUE_COUNT];
    SDL_AtomicInt cue_cursor[AUDIO_CUE_COUNT];
    bool muted;
};

/* Two sine notes (E6 then C6) with soft attack, exponential decay and a
 * quiet octave partial for a bell-like tone. */
static void render_ding(float *out, int n)
{
    const float notes[2] = {1318.5f, 1046.5f};
    const float starts[2] = {0.0f, 0.32f};
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)AUDIO_RATE;
        float s = 0.0f;
        for (int k = 0; k < 2; k++) {
            float u = t - starts[k];
            if (u < 0.0f) {
                continue;
            }
            float env = fminf(u / 0.006f, 1.0f) * expf(-u * 4.2f);
            float w = 2.0f * (float)M_PI * notes[k] * u;
            s += env * (sinf(w) + 0.18f * sinf(2.0f * w));
        }
        out[i] = 0.22f * s;
    }
}

/* One note: 10 ms attack, `release` at the end, optional odd partials. */
static void note(float *out, float start_s, float dur_s, float hz, float gain, float release_s, bool square)
{
    const int n0 = (int)(start_s * AUDIO_RATE), n = (int)(dur_s * AUDIO_RATE);
    for (int i = 0; i < n; i++) {
        float t = (float)i / (float)AUDIO_RATE;
        float env = fminf(t / 0.010f, 1.0f) * fminf((dur_s - t) / release_s, 1.0f);
        float w = 2.0f * (float)M_PI * hz * t;
        float v = square ? sinf(w) + sinf(3.0f * w) / 3.0f + sinf(5.0f * w) / 5.0f : sinf(w) + 0.15f * sinf(2.0f * w);
        out[n0 + i] += gain * env * v;
    }
}

static float db(float d) { return powf(10.0f, d / 20.0f); }

static bool render_cues(Audio *a, Arena *perm)
{
    static const float secs[AUDIO_CUE_COUNT] = {0.10f, 0.21f, 0.05f, 0.18f};
    for (int c = 0; c < AUDIO_CUE_COUNT; c++) {
        a->cue_len[c] = (int)(secs[c] * AUDIO_RATE);
        a->cue[c] = ARENA_PUSH(perm, float, a->cue_len[c] + 16);
        if (!a->cue[c]) {
            return false;
        }
        SDL_memset(a->cue[c], 0, sizeof(float) * (size_t)(a->cue_len[c] + 16));
        SDL_SetAtomicInt(&a->cue_cursor[c], a->cue_len[c]);
    }
    const float vol = 0.5f; /* master for cues; the dB figures are relative to it */
    note(a->cue[AUDIO_CUE_TICK], 0.0f, 0.04f, 880.0f, vol * db(-6), 0.03f, false);
    note(a->cue[AUDIO_CUE_TICK], 0.04f, 0.06f, 1320.0f, vol * db(-6), 0.05f, false);
    for (int i = 0; i < 3; i++) {
        static const float hz[3] = {660.0f, 880.0f, 1320.0f};
        note(a->cue[AUDIO_CUE_CHIME], 0.07f * (float)i, 0.07f, hz[i], vol * db(-8), 0.05f, false);
    }
    note(a->cue[AUDIO_CUE_DUP], 0.0f, 0.05f, 330.0f, vol * db(-12), 0.03f, false);
    note(a->cue[AUDIO_CUE_BUZZ], 0.0f, 0.18f, 220.0f, vol * db(-10), 0.05f, true);
    return true;
}

static void SDLCALL feed(void *user, SDL_AudioStream *stream, int additional, int total)
{
    Audio *a = (Audio *)user;
    (void)total;
    float buf[512];
    int want = additional / (int)sizeof(float);
    while (want > 0) {
        int n = want < 512 ? want : 512;
        int cur = SDL_GetAtomicInt(&a->cursor), cc = SDL_GetAtomicInt(&a->click_cursor);
        const int click_len = (int)(sizeof(a->click) / sizeof(a->click[0]));
        for (int i = 0; i < n; i++) {
            buf[i] = (cur < a->ding_len ? a->ding[cur++] : 0.0f) + (cc < click_len ? a->click[cc++] : 0.0f);
        }
        for (int c = 0; c < AUDIO_CUE_COUNT; c++) {
            int p = SDL_GetAtomicInt(&a->cue_cursor[c]);
            for (int i = 0; i < n && p < a->cue_len[c]; i++) {
                buf[i] += a->cue[c][p++];
            }
            SDL_SetAtomicInt(&a->cue_cursor[c], p);
        }
        SDL_SetAtomicInt(&a->cursor, cur);
        SDL_SetAtomicInt(&a->click_cursor, cc);
        SDL_PutAudioStreamData(stream, buf, n * (int)sizeof(float));
        want -= n;
    }
}

Audio *audio_create(Arena *perm)
{
    Audio *a = ARENA_PUSH(perm, Audio, 1);
    if (!a) {
        return NULL;
    }
    a->ding_len = (int)(DING_SECONDS * AUDIO_RATE);
    a->ding = ARENA_PUSH(perm, float, a->ding_len);
    if (!a->ding) {
        return NULL;
    }
    render_ding(a->ding, a->ding_len);
    SDL_SetAtomicInt(&a->cursor, a->ding_len);
    const int click_len = (int)(sizeof(a->click) / sizeof(a->click[0]));
    for (int i = 0; i < click_len; i++) { /* 1.8 kHz blip, fast decay */
        float t = (float)i / (float)AUDIO_RATE;
        a->click[i] = 0.12f * sinf(2.0f * (float)M_PI * 1800.0f * t) * expf(-t * 160.0f);
    }
    SDL_SetAtomicInt(&a->click_cursor, click_len);
    if (!render_cues(a, perm)) {
        return NULL;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        mg_log_warn("audio: init failed: %s", SDL_GetError());
        return NULL;
    }
    SDL_AudioSpec spec = {SDL_AUDIO_F32, 1, AUDIO_RATE};
    a->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, a);
    if (!a->stream) {
        mg_log_warn("audio: no output device: %s", SDL_GetError());
        return NULL;
    }
    SDL_ResumeAudioStreamDevice(a->stream);
    return a;
}

void audio_destroy(Audio *a)
{
    if (a && a->stream) {
        SDL_DestroyAudioStream(a->stream);
        a->stream = NULL;
    }
}

void audio_set_muted(Audio *a, bool muted)
{
    if (a) {
        a->muted = muted;
    }
}

void audio_cue(Audio *a, AudioCue cue)
{
    if (a && !a->muted) {
        SDL_SetAtomicInt(&a->cue_cursor[cue], 0);
    }
}

void audio_ding(Audio *a)
{
    if (a && !a->muted) {
        SDL_SetAtomicInt(&a->cursor, 0);
    }
}

void audio_click(Audio *a)
{
    if (a && !a->muted) {
        SDL_SetAtomicInt(&a->click_cursor, 0);
    }
}
