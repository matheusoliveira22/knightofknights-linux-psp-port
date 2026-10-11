/* Windows BMP loader (8-bit palettized and 24-bit, uncompressed). */
#ifndef KOK_BMP_H
#define KOK_BMP_H
#include <stdint.h>

typedef struct {
    int w, h;
    uint8_t *rgb;   /* w*h*3, top-down */
    uint8_t *idx;   /* w*h palette indices, top-down (8-bit files only, else NULL) */
    uint32_t pal[256]; /* 0x00RRGGBB */
} bmp_t;

int bmp_load(const char *path, bmp_t *out);
void bmp_free(bmp_t *b);

#endif
