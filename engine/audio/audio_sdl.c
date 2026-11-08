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
