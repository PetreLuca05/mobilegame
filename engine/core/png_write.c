#include "png_write.h"

#include <SDL3/SDL.h>

static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

static void put32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)v;
}

/* length | type | data | crc(type + data) */
static bool chunk(SDL_IOStream *io, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t head[8], tail[4];
    put32(head, len);
    SDL_memcpy(head + 4, type, 4);
    uint32_t crc = crc_update(0xFFFFFFFFu, head + 4, 4);
    crc = crc_update(crc, data, len) ^ 0xFFFFFFFFu;
    put32(tail, crc);
    return SDL_WriteIO(io, head, 8) == 8 && (len == 0 || SDL_WriteIO(io, data, len) == len) &&
           SDL_WriteIO(io, tail, 4) == 4;
}

bool png_write_rgb(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t pitch, bool bgra)
{
    const uint32_t row = 1 + w * 3; /* filter byte + RGB */
    const uint64_t raw_len = (uint64_t)row * h;
    const uint64_t blocks = (raw_len + 65534) / 65535;
    const uint64_t z_len = 2 + raw_len + blocks * 5 + 4;
    if (z_len > 0x7FFFFFFFu) {
        return false;
    }
    uint8_t *z = (uint8_t *)SDL_malloc((size_t)z_len);
    uint8_t *raw = (uint8_t *)SDL_malloc((size_t)raw_len);
    if (!z || !raw) {
        SDL_free(z);
        SDL_free(raw);
        return false;
    }
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *o = raw + (size_t)y * row;
        o[0] = 0;
        for (uint32_t x = 0; x < w; x++) {
            const uint8_t *in = rgba + (size_t)y * pitch + x * 4;
            o[1 + x * 3 + 0] = in[bgra ? 2 : 0];
            o[1 + x * 3 + 1] = in[1];
            o[1 + x * 3 + 2] = in[bgra ? 0 : 2];
        }
    }
    /* zlib: header, stored blocks, adler32 */
    size_t zp = 0;
    z[zp++] = 0x78;
    z[zp++] = 0x01;
    uint32_t a = 1, b = 0;
    for (uint64_t off = 0; off < raw_len;) {
        uint32_t n = (uint32_t)(raw_len - off > 65535 ? 65535 : raw_len - off);
        z[zp++] = off + n == raw_len ? 1 : 0;
        z[zp++] = (uint8_t)n;
        z[zp++] = (uint8_t)(n >> 8);
        z[zp++] = (uint8_t)~n;
        z[zp++] = (uint8_t)(~n >> 8);
        SDL_memcpy(z + zp, raw + off, n);
        for (uint32_t i = 0; i < n; i++) {
            a = (a + raw[off + i]) % 65521u;
            b = (b + a) % 65521u;
        }
        zp += n;
        off += n;
    }
    put32(z + zp, (b << 16) | a);
    zp += 4;
    SDL_free(raw);

    uint8_t ihdr[13];
    put32(ihdr, w);
    put32(ihdr + 4, h);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 2;  /* RGB */
    ihdr[10] = 0; /* deflate */
    ihdr[11] = 0;
    ihdr[12] = 0;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    bool ok = io && SDL_WriteIO(io, sig, 8) == 8 && chunk(io, "IHDR", ihdr, 13) &&
              chunk(io, "IDAT", z, (uint32_t)zp) && chunk(io, "IEND", NULL, 0);
    if (io) {
        ok = SDL_CloseIO(io) && ok;
    }
    SDL_free(z);
    return ok;
}
