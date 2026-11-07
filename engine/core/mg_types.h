/* Common includes for engine and game code. */
#ifndef MG_TYPES_H
#define MG_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "HandmadeMath.h"

#define MG_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

static inline float mg_clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float mg_minf(float a, float b) { return a < b ? a : b; }
static inline float mg_maxf(float a, float b) { return a > b ? a : b; }
static inline float mg_absf(float a) { return a < 0.0f ? -a : a; }
static inline float mg_saturate(float v) { return mg_clampf(v, 0.0f, 1.0f); }
static inline float mg_lerpf(float a, float b, float t) { return a + (b - a) * t; }

/* 0xRRGGBB -> linear-in-gamma (we render in gamma space) float colour */
static inline HMM_Vec4 mg_rgb(uint32_t hex)
{
    return HMM_V4((float)((hex >> 16) & 0xFF) / 255.0f, (float)((hex >> 8) & 0xFF) / 255.0f,
                  (float)(hex & 0xFF) / 255.0f, 1.0f);
}

#endif
