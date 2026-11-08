/* Minimal PNG writer (8-bit RGB, deflate "stored" blocks: no compression,
 * no dependencies). For debug screenshots only. */
#ifndef MG_PNG_WRITE_H
#define MG_PNG_WRITE_H

#include <stdbool.h>
#include <stdint.h>

/* `rgba`: w x h pixels, 4 bytes each, rows `pitch` bytes apart; `bgra`
 * swaps red and blue. Alpha is dropped. */
bool png_write_rgb(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t pitch, bool bgra);

#endif
