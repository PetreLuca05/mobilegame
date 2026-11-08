/* Tiny procedural sound: one pre-rendered chime played through an SDL audio
 * stream callback (no allocation after init, no audio assets). Silent if
 * the device cannot be opened. */
#ifndef MG_AUDIO_H
#define MG_AUDIO_H

#include "core/arena.h"
#include "core/mg_types.h"

typedef struct Audio Audio;

/* NULL when there is no output device (callers may pass NULL below). */
Audio *audio_create(Arena *perm);
void audio_destroy(Audio *a);
/* Soft two-note elevator "ding". Safe to call from the frame or tick. */
void audio_ding(Audio *a);
/* Short menu click (sine blip). */
void audio_click(Audio *a);
/* M6 marker feedback: short procedural notes with an attack / release
 * envelope; one voice per cue. */
typedef enum AudioCue {
    AUDIO_CUE_TICK,  /* tapped: 880 -> 1320 Hz */
    AUDIO_CUE_CHIME, /* reached: 660 / 880 / 1320 Hz */
    AUDIO_CUE_DUP,   /* cancelled: low 330 Hz */
    AUDIO_CUE_BUZZ,  /* failed: 220 Hz square-ish */
    AUDIO_CUE_COUNT
} AudioCue;
void audio_cue(Audio *a, AudioCue cue);
/* Settings -> Sound off: every sound call is ignored. */
void audio_set_muted(Audio *a, bool muted);

#endif
