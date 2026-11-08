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

#endif
