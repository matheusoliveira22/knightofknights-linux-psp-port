#include "bmp.h"
#include "../plat/plat.h"
#include <stdlib.h>
#include <string.h>

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return p[0] | p[1] << 8; }

int bmp_load(const char *path, bmp_t *b)
{
    size_t n;
    uint8_t *d = plat_read_file(path, &n);
    memset(b, 0, sizeof *b);
    if (!d) return 0;
    if (n < 54 || d[0] != 'B' || d[1] != 'M') { free(d); return 0; }
    uint32_t off = le32(d + 10), hsz = le32(d + 14);
    int32_t w = (int32_t)le32(d + 18), h = (int32_t)le32(d + 22);
    int bpp = le16(d + 28);
    uint32_t comp = le32(d + 30), ncol = le32(d + 46);
    int topdown = h < 0;
    if (h < 0) h = -h;
    if (comp != 0 || (bpp != 8 && bpp != 24) || w <= 0 || h <= 0) { free(d); return 0; }
    if (bpp == 8) {
        if (!ncol) ncol = 256;
        const uint8_t *pal = d + 14 + hsz;
        if (hsz > n || 14 + hsz + (size_t)(ncol < 256 ? ncol : 256) * 4 > n) { free(d); return 0; }
        for (uint32_t i = 0; i < ncol && i < 256; i++)
            b->pal[i] = pal[i * 4 + 2] << 16 | pal[i * 4 + 1] << 8 | pal[i * 4];
    }
    int stride = ((w * bpp / 8) + 3) & ~3;
    if (off + (size_t)stride * h > n) { free(d); return 0; }
    b->w = w;
    b->h = h;
    b->rgb = malloc((size_t)w * h * 3);
    if (bpp == 8) b->idx = malloc((size_t)w * h);
    for (int y = 0; y < h; y++) {
        const uint8_t *row = d + off + (size_t)stride * (topdown ? y : h - 1 - y);
        uint8_t *o = b->rgb + (size_t)y * w * 3;
        for (int x = 0; x < w; x++) {
            if (bpp == 8) {
                uint32_t c = b->pal[row[x]];
                b->idx[y * w + x] = row[x];
                o[x * 3] = c >> 16; o[x * 3 + 1] = c >> 8; o[x * 3 + 2] = c;
            } else {
                o[x * 3] = row[x * 3 + 2]; o[x * 3 + 1] = row[x * 3 + 1]; o[x * 3 + 2] = row[x * 3];
            }
        }
    }
    free(d);
    return 1;
}

void bmp_free(bmp_t *b)
{
    free(b->rgb);
    free(b->idx);
    memset(b, 0, sizeof *b);
}
