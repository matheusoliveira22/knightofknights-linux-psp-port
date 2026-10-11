#include "dx.h"
#include "../fmt/bmp.h"
#include "../plat/plat.h"
#include "../core/vm.h"
#include <math.h>
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

/* ================================================================== surfaces
 * Like the original's DirectDraw surfaces, pixels are 16-bit 565, stored in the PSP's native order
 * (r in the low bits) in tiles of at most 512x512 (the GE texture limit). Blits to the back buffer
 * draw the tile pixels directly as a texture (colour key via the GE colour test); Direct3D texture
 * use gets a converted A1B5G5R5 copy where the key becomes the alpha bit. */

typedef struct {
    int x0, y0, w, h;        /* area of the surface covered by this tile */
    int tw, th;              /* texture size (power of two) */
    uint16_t *px;            /* tw*th RGB565 pixels */
    uint16_t *tex16;         /* upload buffer (A1B5G5R5) */
    uint16_t *shadow;        /* px as of the last conversion into tex16 */
    int reconvert;           /* colour key changed: convert every dirty row */
    int tex, dirty;          /* converted texture for IM drawing */
    int dy0, dy1;            /* its dirty rows (tile-local, [dy0, dy1)) */
    int btex, bdirty;        /* zero-copy texture for blits */
} tile_t;

typedef struct {
    int w, h;
    int ntx, nty;
    tile_t *tiles;
    int key_on;
    uint16_t key;            /* RGB565 */
    int ext_tex;             /* captured back buffer: external texture */
    float ext_u, ext_v;
    int refs;                /* COM reference count (texture interfaces share the surface's) */
} surf_t;

#define SURF_BASE 0x6e000000u
#define MAX_SURF 512
#define TILE 512
static surf_t *surfs[MAX_SURF];

static surf_t *S(uint32_t h)
{
    uint32_t i = h - SURF_BASE;
    return i < MAX_SURF ? surfs[i] : NULL;
}

static int pow2(int v) { int p = 1; while (p < v) p <<= 1; return p; }

/* native order: r bits 0-4, g 5-10, b 11-15 */
static uint16_t q565(uint32_t rgb) { return (uint16_t)((rgb >> 19 & 31) | (rgb >> 10 & 63) << 5 | (rgb >> 3 & 31) << 11); }
static uint32_t rgb_of565(uint16_t c)
{
    return (uint32_t)((c & 31) * 255 / 31) << 16 | (uint32_t)((c >> 5 & 63) * 255 / 63) << 8 | (c >> 11) * 255 / 31;
}
/* DirectDraw RGB565 (r high) <-> native */
static uint16_t swap_rb(uint16_t c) { return (uint16_t)((c >> 11) | (c & 0x07e0) | (c & 31) << 11); }

static inline uint16_t *pix(surf_t *s, int x, int y)
{
    tile_t *t = &s->tiles[(y / TILE) * s->ntx + x / TILE];
    return &t->px[(y - t->y0) * t->tw + (x - t->x0)];
}

static void touch_rect(surf_t *s, int l, int t, int r, int b)
{
    s->ext_tex = 0;
    if (l < 0) l = 0;
    if (t < 0) t = 0;
    if (r > s->w) r = s->w;
    if (b > s->h) b = s->h;
    if (l >= r || t >= b) return;
    for (int ty = t / TILE; ty <= (b - 1) / TILE; ty++)
        for (int tx = l / TILE; tx <= (r - 1) / TILE; tx++) {
            tile_t *tl = &s->tiles[ty * s->ntx + tx];
            int y0 = t - tl->y0, y1 = b - tl->y0;
            if (y0 < 0) y0 = 0;
            if (y1 > tl->h) y1 = tl->h;
            if (!tl->dirty || y0 < tl->dy0) tl->dy0 = y0;
            if (!tl->dirty || y1 > tl->dy1) tl->dy1 = y1;
            tl->dirty = tl->bdirty = 1;
        }
}

static inline void touch_at(surf_t *s, int x, int y) { touch_rect(s, x, y, x + 1, y + 1); }

/* CPU writes into a surface mark only the rows they actually changed, so a HUD redrawn with the same
 * text every frame does not re-convert and re-upload its texture. A surface showing a captured back
 * buffer (ext_tex) goes back to its own pixels on any write, so the whole written area is dirty then. */
typedef struct { surf_t *s; int ext, y0, y1; } wr_t;
static void gpu_guard(surf_t *s)
{
    for (int i = 0; i < s->ntx * s->nty; i++) plat_tex_before_write(s->tiles[i].btex);
}
static wr_t wr_begin(surf_t *s) { gpu_guard(s); wr_t w = { s, s->ext_tex != 0, 1 << 30, -1 }; return w; }
static inline void wr_row(wr_t *w, int y) { if (y < w->y0) w->y0 = y; if (y >= w->y1) w->y1 = y + 1; }
static void wr_end(wr_t *w, int l, int t, int r, int b)
{
    if (w->ext) touch_rect(w->s, l, t, r, b);
    else if (w->y0 < w->y1) touch_rect(w->s, l, w->y0, r, w->y1);
}
static void touch_all(surf_t *s) { touch_rect(s, 0, 0, s->w, s->h); }

uint32_t dd_create_surface(int w, int h)
{
    for (int i = 1; i < MAX_SURF; i++) {
        if (surfs[i]) continue;
        surf_t *s = calloc(1, sizeof *s);
        s->w = w; s->h = h;
        s->refs = 1;
        s->ntx = (w + TILE - 1) / TILE;
        s->nty = (h + TILE - 1) / TILE;
        s->tiles = calloc((size_t)(s->ntx * s->nty), sizeof(tile_t));
        for (int ty = 0; ty < s->nty; ty++)
            for (int tx = 0; tx < s->ntx; tx++) {
                tile_t *t = &s->tiles[ty * s->ntx + tx];
                t->x0 = tx * TILE; t->y0 = ty * TILE;
                t->w = w - t->x0 < TILE ? w - t->x0 : TILE;
                t->h = h - t->y0 < TILE ? h - t->y0 : TILE;
                t->tw = pow2(t->w); t->th = pow2(t->h);
                t->px = memalign(64, (size_t)t->tw * t->th * 2);   /* GE texture base alignment */
                if (t->px) memset(t->px, 0, (size_t)t->tw * t->th * 2);
                if (!t->px) vm_fatal("out of memory (surface %dx%d)", w, h);
                t->dirty = t->bdirty = 1;
                t->dy0 = 0; t->dy1 = t->h;
            }
        surfs[i] = s;
        return SURF_BASE + i;
    }
    vm_fatal("dd: surface table full");
    return 0;
}

uint32_t dd_load_bmp(const char *path, int colorkey)
{
    bmp_t b;
    if (!bmp_load(path, &b)) { plat_log("dd: missing bitmap '%s'", path); return 0; }
    uint32_t h = dd_create_surface(b.w, b.h);
    surf_t *s = S(h);
    for (int y = 0; y < b.h; y++)
        for (int x = 0; x < b.w; x++) {
            const uint8_t *c = b.rgb + ((size_t)y * b.w + x) * 3;
            *pix(s, x, y) = q565((uint32_t)c[0] << 16 | c[1] << 8 | c[2]);
        }
    if (colorkey) dd_set_colorkey(h, 1, dd_pixel(h, 0, 0));
    bmp_free(&b);
    return h;
}

void dd_addref(uint32_t h) { surf_t *s = S(h); if (s) s->refs++; }

void dd_release(uint32_t h)
{
    surf_t *s = S(h);
    if (!s || --s->refs > 0) return;
    gpu_guard(s);
    for (int i = 0; i < s->ntx * s->nty; i++) {
        plat_tex_destroy(s->tiles[i].tex);
        plat_tex_destroy(s->tiles[i].btex);
        free(s->tiles[i].px);
        free(s->tiles[i].tex16);
        free(s->tiles[i].shadow);
    }
    free(s->tiles);
    free(s);
    surfs[h - SURF_BASE] = NULL;
}

int dd_width(uint32_t h) { surf_t *s = S(h); return s ? s->w : h == DD_BACK || h == DD_PRIMARY ? SCREEN_W : 0; }
int dd_height(uint32_t h) { surf_t *s = S(h); return s ? s->h : h == DD_BACK || h == DD_PRIMARY ? SCREEN_H : 0; }

void dd_set_colorkey(uint32_t h, int on, uint32_t rgb)
{
    surf_t *s = S(h);
    if (!s) return;
    s->key_on = on;
    s->key = q565(rgb);
    for (int i = 0; i < s->ntx * s->nty; i++) s->tiles[i].reconvert = 1;
    touch_all(s);
}

uint32_t dd_pixel(uint32_t h, int x, int y)
{
    surf_t *s = S(h);
    if (!s || x < 0 || y < 0 || x >= s->w || y >= s->h) return 0;
    return rgb_of565(*pix(s, x, y));
}

uint16_t dd_get565(uint32_t h, int x, int y)
{
    surf_t *s = S(h);
    return s && x >= 0 && y >= 0 && x < s->w && y < s->h ? swap_rb(*pix(s, x, y)) : 0;
}

void dd_put565(uint32_t h, int x, int y, uint16_t c)
{
    surf_t *s = S(h);
    if (!s || x < 0 || y < 0 || x >= s->w || y >= s->h) return;
    gpu_guard(s);
    *pix(s, x, y) = swap_rb(c);
    touch_at(s, x, y);
}

uint64_t prof_us[16];   /* 0 upload, 1 cpu blt, 2 state handler (logic + draw calls), 3 scene render,
                        * 4 audio callback (mixing + music decode), 5 logic ticks, 6 tick count,
                        * 7 vblank wait (PSP), 8 collision pass (FUN_00429a00) */
static int upload(surf_t *s, tile_t *t)
{
    if (t->tex && !t->dirty) return t->tex;
    uint64_t t0 = plat_time_us();
    int full = !t->tex16 || !t->tex || t->reconvert;
    if (!t->tex16) t->tex16 = calloc((size_t)t->tw * t->th, 2);
    if (!t->shadow) t->shadow = malloc((size_t)t->tw * t->th * 2);
    int y0 = full ? 0 : t->dy0, y1 = full ? t->th : t->dy1;
    /* convert and send only the dirty rows that really differ from the last conversion: the text
     * routines clear and redraw the same HUD strings every frame */
    int cy0 = y1, cy1 = y0;
    int key_on = s->key_on;
    uint16_t key = s->key;
    size_t rb = (size_t)t->tw * 2;
    for (int y = y0; y < y1; y++) {
        const uint16_t *in = t->px + (size_t)y * t->tw;
        uint16_t *sh = t->shadow + (size_t)y * t->tw;
        if (t->shadow) {
            if (!full && !memcmp(in, sh, rb)) continue;
            memcpy(sh, in, rb);
        }
        uint16_t *out = t->tex16 + (size_t)y * t->tw;
        for (int x = 0; x < t->tw; x++) {
            uint16_t c = in[x];
            uint16_t a = key_on && c == key ? 0 : 0x8000;
            out[x] = (uint16_t)(a | (c & 31) | ((c >> 6) & 31) << 5 | (c >> 11) << 10);
        }
        if (y < cy0) cy0 = y;
        cy1 = y + 1;
    }
    t->reconvert = 0;
    if (!t->tex) t->tex = plat_tex_create16(t->tw, t->th, t->tex16);
    else if (cy1 > cy0) plat_tex_update16_rows(t->tex, t->tex16, cy0, cy1);
    t->dirty = 0;
    prof_us[0] += plat_time_us() - t0;
    return t->tex;
}

static int blit_tex(surf_t *s, tile_t *t)
{
    if (!t->btex) {
        t->btex = plat_tex_wrap565(t->tw, t->th, t->px);
        t->bdirty = 1;
    }
    if (t->bdirty) plat_tex_sync565(t->btex, s->key_on, s->key);
    t->bdirty = 0;
    return t->btex;
}

void dd_touch(uint32_t h) { surf_t *s = S(h); if (s) touch_all(s); }

void dd_capture_back(uint32_t h)
{
    surf_t *s = S(h);
    if (!s) return;
    s->ext_tex = plat_capture(&s->ext_u, &s->ext_v);
}

/* texture for IM drawing (surfaces used as Direct3D textures are at most 512x512: one tile) */
int dd_texture(uint32_t h, float *umax, float *vmax)
{
    surf_t *s = S(h);
    if (!s) return 0;
    if (s->ext_tex) {
        if (umax) *umax = s->ext_u;
        if (vmax) *vmax = s->ext_v;
        return s->ext_tex;
    }
    tile_t *t = &s->tiles[0];
    if (umax) *umax = (float)t->w / t->tw;
    if (vmax) *vmax = (float)t->h / t->th;
    return upload(s, t);
}

static float hud_k = 1.0f;
void im_set_hud_scale(float k) { hud_k = k; }

/* 2D layout of an element with bounding box (x0,y0)-(x1,y1) for the current frame: the HUD scale
 * (PSP) and the widescreen extra width. The anchor comes from the left/centre/right third of the
 * screen the element's centre lies in; side columns scale from the top or bottom edge (the
 * hit/damage counter under each gauge moves down with it), the centre column from the top, middle
 * or bottom third. The element scales about its anchor and moves with it: left anchors stay at the
 * left edge, right ones go to the right edge of a wide screen, centre ones stay centred. Elements
 * covering half the screen or more (fades, backgrounds) stretch across the full width instead.
 * x' = ax + (x - ax) * sx + off, y' = ay + (y - ay) * sy. */
typedef struct { float ax, ay, sx, sy, off; } hudmap_t;
static int hud_map(float x0, float y0, float x1, float y1, hudmap_t *m)
{
    float extra = plat_virtual_width() - SCREEN_W;
    if (hud_k == 1.0f && extra < 0.5f) return 0;
    if (x1 - x0 >= SCREEN_W / 2 || y1 - y0 >= SCREEN_H / 2) {
        if (extra < 0.5f) return 0;
        m->ax = m->ay = m->off = 0; m->sx = (SCREEN_W + extra) / SCREEN_W; m->sy = 1;
        return 1;
    }
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    int col = cx < SCREEN_W / 3.0f ? 0 : cx > SCREEN_W * 2 / 3.0f ? 2 : 1;
    m->ax = col * (SCREEN_W / 2.0f);
    m->off = col * extra / 2;
    if (col != 1) m->ay = cy < SCREEN_H / 2.0f ? 0 : SCREEN_H;
    else m->ay = cy < SCREEN_H / 3.0f ? 0 : cy > SCREEN_H * 2 / 3.0f ? SCREEN_H : SCREEN_H / 2;
    m->sx = m->sy = hud_k;
    return 1;
}
static inline float hud_x(const hudmap_t *m, float x) { return m->ax + (x - m->ax) * m->sx + m->off; }
static inline float hud_y(const hudmap_t *m, float y) { return m->ay + (y - m->ay) * m->sy; }

static void hud_scale_verts(pvert_t *b, int n)
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    hudmap_t m;
    for (int i = 0; i < n; i++) {
        if (b[i].x < x0) x0 = b[i].x;
        if (b[i].x > x1) x1 = b[i].x;
        if (b[i].y < y0) y0 = b[i].y;
        if (b[i].y > y1) y1 = b[i].y;
    }
    if (!hud_map(x0, y0, x1, y1, &m)) return;
    for (int i = 0; i < n; i++) { b[i].x = hud_x(&m, b[i].x); b[i].y = hud_y(&m, b[i].y); }
}

static void quad2d(int tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                   uint32_t color, int blend, int flags)
{
    pvert_t v[6] = {
        { x0, y0, 0, u0, v0, color }, { x1, y0, 0, u1, v0, color }, { x1, y1, 0, u1, v1, color },
        { x0, y0, 0, u0, v0, color }, { x1, y1, 0, u1, v1, color }, { x0, y1, 0, u0, v1, color } };
    plat_draw_tris_2d(tex, v, 6, blend, flags);
}

void dd_blt(uint32_t dst, const rect_t *dr, uint32_t src, const rect_t *sr, uint32_t flags)
{
    surf_t *ss = S(src);
    if (!ss) return;
    rect_t s = sr ? *sr : (rect_t){ 0, 0, ss->w, ss->h };
    rect_t d = dr ? *dr : (rect_t){ 0, 0, dd_width(dst), dd_height(dst) };
    int keyed = (flags & DDBLT_KEYSRC) && ss->key_on;
    int sw = s.right - s.left, sh = s.bottom - s.top;
    if (sw <= 0 || sh <= 0) return;
    if (dst == DD_BACK) {
        hudmap_t m = { 0, 0, 1, 1, 0 };
        hud_map(d.left, d.top, d.right, d.bottom, &m);
        float kx = (float)(d.right - d.left) / sw, ky = (float)(d.bottom - d.top) / sh;
        for (int i = 0; i < ss->ntx * ss->nty; i++) {
            tile_t *t = &ss->tiles[i];
            int l = s.left > t->x0 ? s.left : t->x0, r = s.right < t->x0 + t->w ? s.right : t->x0 + t->w;
            int tp = s.top > t->y0 ? s.top : t->y0, b = s.bottom < t->y0 + t->h ? s.bottom : t->y0 + t->h;
            if (l >= r || tp >= b) continue;
            int tex = blit_tex(ss, t);
            quad2d(tex, hud_x(&m, d.left + (l - s.left) * kx), hud_y(&m, d.top + (tp - s.top) * ky),
                   hud_x(&m, d.left + (r - s.left) * kx), hud_y(&m, d.top + (b - s.top) * ky),
                   (float)(l - t->x0) / t->tw, (float)(tp - t->y0) / t->th,
                   (float)(r - t->x0) / t->tw, (float)(b - t->y0) / t->th,
                   0xffffffffu, BLEND_NONE, keyed ? 0 : DRAW2D_OPAQUE);
        }
        return;
    }
    surf_t *ds = S(dst);
    if (!ds) return;
    int dw = d.right - d.left, dh = d.bottom - d.top;
    if (dw <= 0 || dh <= 0) return;
    uint64_t t0 = plat_time_us();
    wr_t w = wr_begin(ds);
    for (int y = 0; y < dh; y++) {
        int dy = d.top + y, sy = s.top + y * sh / dh;
        if (dy < 0 || dy >= ds->h || sy < 0 || sy >= ss->h) continue;
        if (sw == dw) {   /* unscaled: copy runs that stay inside one source and one destination tile */
            int x = 0;
            if (d.left < 0) x = -d.left;
            if (s.left + x < 0) x = -s.left;
            int xe = dw;
            if (d.left + xe > ds->w) xe = ds->w - d.left;
            if (s.left + xe > ss->w) xe = ss->w - s.left;
            while (x < xe) {
                int sx = s.left + x, dx = d.left + x;
                int n = xe - x;
                if (TILE - sx % TILE < n) n = TILE - sx % TILE;
                if (TILE - dx % TILE < n) n = TILE - dx % TILE;
                const uint16_t *sp = pix(ss, sx, sy);
                uint16_t *dp = pix(ds, dx, dy);
                if (keyed) {
                    uint16_t k = ss->key;
                    for (int i = 0; i < n; i++)
                        if (sp[i] != k && dp[i] != sp[i]) { dp[i] = sp[i]; wr_row(&w, dy); }
                } else if (memcmp(dp, sp, (size_t)n * 2)) {
                    memcpy(dp, sp, (size_t)n * 2);
                    wr_row(&w, dy);
                }
                x += n;
            }
            continue;
        }
        for (int x = 0; x < dw; x++) {
            int dx = d.left + x, sx = s.left + x * sw / dw;
            if (dx < 0 || dx >= ds->w || sx < 0 || sx >= ss->w) continue;
            uint16_t c = *pix(ss, sx, sy);
            if (keyed && c == ss->key) continue;
            uint16_t *dp = pix(ds, dx, dy);
            if (*dp != c) { *dp = c; wr_row(&w, dy); }
        }
    }
    wr_end(&w, d.left, d.top, d.right, d.bottom);
    prof_us[1] += plat_time_us() - t0;
}

void dd_fill(uint32_t dst, const rect_t *dr, uint32_t rgb)
{
    rect_t d = dr ? *dr : (rect_t){ 0, 0, dd_width(dst), dd_height(dst) };
    if (dst == DD_BACK) {
        hudmap_t m = { 0, 0, 1, 1, 0 };
        hud_map(d.left, d.top, d.right, d.bottom, &m);
        quad2d(0, hud_x(&m, d.left), hud_y(&m, d.top), hud_x(&m, d.right), hud_y(&m, d.bottom), 0, 0, 0, 0,
               0xff000000u | rgb, BLEND_NONE, DRAW2D_OPAQUE);
        return;
    }
    surf_t *s = S(dst);
    if (!s) return;
    uint16_t c = q565(rgb);
    wr_t w = wr_begin(s);
    for (int y = d.top < 0 ? 0 : d.top; y < d.bottom && y < s->h; y++)
        for (int x = d.left < 0 ? 0 : d.left; x < d.right && x < s->w; x++) {
            uint16_t *p = pix(s, x, y);
            if (*p != c) { *p = c; wr_row(&w, y); }
        }
    wr_end(&w, d.left, d.top, d.right, d.bottom);
}

/* ================================================================== text */

typedef struct { uint16_t code; uint8_t w; uint32_t off; int ax, ay; } glyph_t;
typedef struct {
    int h, n;
    glyph_t *g;
    const uint8_t *bits;
    int atlas, aw, ah, cx, cy;
    uint8_t *atlas_rgba;
} font_t;
static font_t fonts[2];
static uint8_t *font_blob;

static void font_load(void)
{
    size_t n;
    font_blob = plat_read_file("kokfont.bin", &n);
    if (!font_blob || memcmp(font_blob, "KFNT", 4)) { plat_log("dx: kokfont.bin missing"); font_blob = NULL; return; }
    const uint8_t *p = font_blob + 8;
    for (int s = 0; s < 2; s++) {
        font_t *f = &fonts[s];
        memcpy(&f->h, p, 4);
        memcpy(&f->n, p + 4, 4);
        p += 8;
        f->g = calloc(f->n, sizeof(glyph_t));
        uint32_t off = 0;
        for (int i = 0; i < f->n; i++) {
            f->g[i].code = p[i * 3] | p[i * 3 + 1] << 8;
            f->g[i].w = p[i * 3 + 2];
            f->g[i].off = off;
            f->g[i].ax = -1;
            off += f->g[i].w * f->h;
        }
        p += f->n * 3;
        f->bits = p;
        p += off;
        f->aw = 512;   /* GE textures are at most 512x512 */
        f->ah = 512;
    }
}

static font_t *font_for(int px) { return px >= 24 ? &fonts[1] : &fonts[0]; }

static glyph_t *glyph(font_t *f, uint16_t code)
{
    int lo = 0, hi = f->n - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (f->g[m].code == code) return &f->g[m];
        if (f->g[m].code < code) lo = m + 1; else hi = m - 1;
    }
    return NULL;
}

/* Decode one Shift-JIS character; returns bytes consumed. */
static int sjis_next(const unsigned char *s, uint16_t *code)
{
    if ((s[0] >= 0x81 && s[0] <= 0x9f) || (s[0] >= 0xe0 && s[0] <= 0xfc)) {
        if (!s[1]) { *code = '?'; return 1; }
        *code = s[0] << 8 | s[1];
        return 2;
    }
    *code = s[0];
    return 1;
}

static int char_advance(font_t *f, uint16_t code) { return code < 0x100 ? f->h / 2 : f->h; }

static int atlas_slot(font_t *f, glyph_t *g)
{
    if (g->ax >= 0) return 1;
    if (!f->atlas_rgba) {
        f->atlas_rgba = calloc((size_t)f->aw * f->ah, 4);
        f->atlas = plat_tex_create(f->aw, f->ah, f->atlas_rgba);
    }
    if (f->cx + g->w > f->aw) { f->cx = 0; f->cy += f->h; }
    if (f->cy + f->h > f->ah) return 0; /* full: glyph is skipped */
    g->ax = f->cx; g->ay = f->cy;
    f->cx += g->w;
    for (int y = 0; y < f->h; y++)
        for (int x = 0; x < g->w; x++) {
            uint8_t *o = f->atlas_rgba + ((size_t)(g->ay + y) * f->aw + g->ax + x) * 4;
            o[0] = o[1] = o[2] = 255;
            o[3] = f->bits[g->off + y * g->w + x];
        }
    plat_tex_update(f->atlas, f->atlas_rgba);
    return 1;
}

/* text drawn straight onto the back buffer: laid out like a HUD element over its text box */
static hudmap_t bkmap = { 0, 0, 1, 1, 0 };
#define BKX(v) (bkmap.ax + ((float)(v) - bkmap.ax) * bkmap.sx + bkmap.off)
#define BKY(v) (bkmap.ay + ((float)(v) - bkmap.ay) * bkmap.sy)

static void draw_glyph(uint32_t dst, font_t *f, glyph_t *g, int x, int y, uint32_t rgb)
{
    if (dst == DD_BACK) {
        if (!atlas_slot(f, g)) return;
        quad2d(f->atlas, BKX(x), BKY(y), BKX(x + g->w), BKY(y + f->h),
               (float)g->ax / f->aw, (float)g->ay / f->ah, (float)(g->ax + g->w) / f->aw,
               (float)(g->ay + f->h) / f->ah, 0xff000000u | rgb, BLEND_ALPHA, 0);
        return;
    }
    surf_t *s = S(dst);
    if (!s) return;
    uint16_t c = q565(rgb);
    wr_t w = wr_begin(s);
    for (int yy = 0; yy < f->h; yy++)
        for (int xx = 0; xx < g->w; xx++) {
            int px = x + xx, py = y + yy;
            if (px < 0 || py < 0 || px >= s->w || py >= s->h) continue;
            /* GDI with ANTIALIASED_QUALITY on a 16-bit DC: threshold to keep crisp edges */
            if (f->bits[g->off + yy * g->w + xx] < 96) continue;
            uint16_t *p = pix(s, px, py);
            if (*p != c) { *p = c; wr_row(&w, py); }
        }
    wr_end(&w, x, y, x + g->w, y + f->h);
}

static void text_impl(uint32_t dst, const rect_t *r, int wrap, const char *str, int px, uint32_t rgb,
                      int opaque, uint32_t bk)
{
    font_t *f = font_for(px);
    if (!f->g) return;
    const unsigned char *s = (const unsigned char *)str;
    int x = r->left, y = r->top;
    /* the same anchor a sprite over the text box would get */
    float rr = r->right > r->left ? r->right : r->left + 1, rb = r->bottom > r->top ? r->bottom : r->top + f->h;
    if (dst != DD_BACK || !hud_map(r->left, r->top, rr, rb, &bkmap)) bkmap = (hudmap_t){ 0, 0, 1, 1, 0 };
    while (*s) {
        if (*s == '\r') { s++; continue; }
        if (*s == '\n') { x = r->left; y += f->h; s++; continue; }
        uint16_t code;
        int n = sjis_next(s, &code);
        int adv = char_advance(f, code);
        if (wrap && x + adv > r->right && x > r->left) { x = r->left; y += f->h; }
        if (opaque) {
            rect_t bg = { x, y, x + adv, y + f->h };
            if (dst == DD_BACK) bg = (rect_t){ (int)BKX(x), (int)BKY(y), (int)BKX(x + adv), (int)BKY(y + f->h) };
            dd_fill(dst, &bg, bk);
        }
        glyph_t *g = glyph(f, code);
        if (g && code != ' ') draw_glyph(dst, f, g, x, y, rgb);
        x += adv;
        s += n;
    }
}

void dx_text(uint32_t dst, const rect_t *r, const char *sjis, int font, uint32_t rgb, int opaque, uint32_t bk)
{
    text_impl(dst, r, 1, sjis, font, rgb, opaque, bk);
}

void dx_text_out(uint32_t dst, int x, int y, const char *sjis, int font, uint32_t rgb, int opaque, uint32_t bk)
{
    rect_t r = { x, y, 1 << 20, 1 << 20 };
    text_impl(dst, &r, 0, sjis, font, rgb, opaque, bk);
}

/* ================================================================== D3D immediate mode */

static uint32_t rs[256];

void im_set_render_state(int state, uint32_t v) { if (state >= 0 && state < 256) rs[state] = v; }
uint32_t im_get_render_state(int state) { return state >= 0 && state < 256 ? rs[state] : 0; }

enum { RS_TEXTUREHANDLE = 1, RS_TEXTUREMAG = 17, RS_TEXTUREMIN = 18, RS_SRCBLEND = 19, RS_DESTBLEND = 20,
       RS_ALPHABLENDENABLE = 27, RS_COLORKEYENABLE = 41 };

static int blend_mode(void)
{
    if (!rs[RS_ALPHABLENDENABLE]) return BLEND_NONE;
    uint32_t s = rs[RS_SRCBLEND], d = rs[RS_DESTBLEND];
    if (s == 5 && d == 6) return BLEND_ALPHA;
    if (s == 2 && d == 2) return BLEND_ADD;
    if (s == 1 && d == 3) return BLEND_MUL;
    if (s == 5 && d == 2) return BLEND_ADD_ALPHA;
    if (s == 2 && d == 1) return BLEND_NONE;
    return BLEND_ALPHA;
}

void im_draw(int prim, const tlvertex_t *v, int n)
{
    static pvert_t buf[1024];
    float um = 1, vm = 1;
    int tex = 0;
    uint32_t th = rs[RS_TEXTUREHANDLE];
    surf_t *s = S(th);
    float voff = 0;
    if (s) {
        tex = dd_texture(th, &um, &vm);
        if (vm < 0) voff = -vm;
    }
    int out = 0;
#define PUT(i) do { const tlvertex_t *t = &v[i]; pvert_t *o = &buf[out++]; \
        o->x = t->x; o->y = t->y; o->z = 0; o->u = t->tu * um; o->v = voff + t->tv * vm; o->color = t->color; } while (0)
    if (prim == 4) {            /* D3DPT_TRIANGLELIST */
        for (int i = 0; i + 2 < n && out < 1020; i += 3) { PUT(i); PUT(i + 1); PUT(i + 2); }
    } else if (prim == 5) {     /* D3DPT_TRIANGLESTRIP */
        for (int i = 0; i + 2 < n && out < 1020; i++) {
            if (i & 1) { PUT(i + 1); PUT(i); PUT(i + 2); } else { PUT(i); PUT(i + 1); PUT(i + 2); }
        }
    } else if (prim == 6) {     /* D3DPT_TRIANGLEFAN */
        for (int i = 1; i + 1 < n && out < 1020; i++) { PUT(0); PUT(i); PUT(i + 1); }
    }
#undef PUT
    hud_scale_verts(buf, out);
    int blend = blend_mode();
    /* no D3D alpha test: vertex alpha only matters to blend modes that read it (see im_draw_lit) */
    if (blend != BLEND_ALPHA && blend != BLEND_ADD_ALPHA)
        for (int i = 0; i < out; i++) buf[i].color |= 0xff000000u;
    plat_draw_tris_2d(tex, buf, out, blend,
                      (rs[RS_TEXTUREMAG] == 2 ? DRAW2D_LINEAR : 0) | (rs[RS_COLORKEYENABLE] ? 0 : DRAW2D_OPAQUE));
}

/* ================================================================== audio */

typedef struct {
    int16_t *pcm;      /* stereo 44.1 kHz */
    int frames;
    int pos, playing, loop;
    int vol;           /* 0..256 */
} voice_t;
static voice_t voices[DS_MAX];

static struct {
    char paths[DM_MAX][64];
    int enabled;
    int cur;
    stb_vorbis *vorb;
    FILE *file;
    uint32_t repeats;
    int16_t carry[4096 * 2];
    int src_rate, src_ch;
} music = { .enabled = 1, .cur = -1 };

static volatile int audio_paused;
void dx_audio_pause(int on) { audio_paused = on; }

static void mixer(int16_t *out, int frames)
{
    static int32_t acc[8192 * 2];
    if (audio_paused) { memset(out, 0, (size_t)frames * 2 * sizeof *out); return; }
    if (frames > 8192) frames = 8192;
    memset(acc, 0, frames * 2 * sizeof(int32_t));
    for (int i = 0; i < DS_MAX; i++) {
        voice_t *v = &voices[i];
        if (!v->playing || !v->pcm) continue;
        for (int f = 0; f < frames; f++) {
            if (v->pos >= v->frames) {
                if (v->loop) v->pos = 0;
                else { v->playing = 0; break; }
            }
            acc[f * 2] += v->pcm[v->pos * 2] * v->vol >> 8;
            acc[f * 2 + 1] += v->pcm[v->pos * 2 + 1] * v->vol >> 8;
            v->pos++;
        }
    }
    if (music.vorb && music.enabled) {
        int got = 0;
        while (got < frames) {
            int want = frames - got;
            static short tmp[8192 * 2];
            int n = stb_vorbis_get_samples_short_interleaved(music.vorb, 2, tmp, want * 2);
            if (n <= 0) {
                if (music.repeats == 0) { break; }
                if (music.repeats != 0xffffffffu) music.repeats--;
                stb_vorbis_seek_start(music.vorb);
                continue;
            }
            for (int k = 0; k < n; k++) {
                acc[(got + k) * 2] += tmp[k * 2];
                acc[(got + k) * 2 + 1] += tmp[k * 2 + 1];
            }
            got += n;
        }
    }
    for (int i = 0; i < frames * 2; i++) {
        int32_t s = acc[i];
        out[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return p[0] | p[1] << 8; }

int ds_load_wav(int slot, const char *path)
{
    size_t n;
    uint8_t *d = plat_read_file(path, &n);
    if (!d || slot < 0 || slot >= DS_MAX) { plat_log("ds: missing '%s'", path); free(d); return 0; }
    int ch = 1, rate = 22050, bits = 16;
    const uint8_t *data = NULL;
    uint32_t dlen = 0;
    for (size_t p = 12; p + 8 <= n;) {
        uint32_t id = le32(d + p), len = le32(d + p + 4);
        if (id == 0x20746d66) { ch = le16(d + p + 10); rate = le32(d + p + 12); bits = le16(d + p + 22); }
        else if (id == 0x61746164) { data = d + p + 8; dlen = len; if (p + 8 + dlen > n) dlen = n - p - 8; }
        p += 8 + len + (len & 1);
    }
    if (!data) { free(d); return 0; }
    int bps = bits / 8 * ch;
    if (bps <= 0 || rate <= 0) { plat_log("ds: bad format '%s'", path); free(d); return 0; }
    int src_frames = dlen / bps;
    int frames = (int)((int64_t)src_frames * 44100 / rate);
    int16_t *pcm = malloc((size_t)frames * 4 + 4);
    for (int i = 0; i < frames; i++) {
        int si = (int)((int64_t)i * rate / 44100);
        int16_t l, r;
        const uint8_t *s = data + (size_t)si * bps;
        if (bits == 8) { l = (int16_t)((s[0] - 128) << 8); r = ch > 1 ? (int16_t)((s[1] - 128) << 8) : l; }
        else { l = (int16_t)le16(s); r = ch > 1 ? (int16_t)le16(s + 2) : l; }
        pcm[i * 2] = l;
        pcm[i * 2 + 1] = r;
    }
    free(d);
    plat_audio_lock();
    free(voices[slot].pcm);
    voices[slot].pcm = pcm;
    voices[slot].frames = frames;
    voices[slot].playing = 0;
    voices[slot].vol = 256;
    plat_audio_unlock();
    return 1;
}

void ds_play(int slot, int loop)
{
    if (slot < 0 || slot >= DS_MAX) return;
    plat_audio_lock();
    voices[slot].pos = 0;
    voices[slot].loop = loop;
    voices[slot].playing = 1;
    plat_audio_unlock();
}

void ds_stop(int slot)
{
    if (slot < 0 || slot >= DS_MAX) return;
    plat_audio_lock();
    voices[slot].playing = 0;
    plat_audio_unlock();
}

void ds_set_volume(int slot, int db100)
{
    if (slot < 0 || slot >= DS_MAX) return;
    voices[slot].vol = db100 >= 0 ? 256 : (int)(256 * powf(10.0f, db100 / 2000.0f));
}

int ds_playing(int slot) { return slot >= 0 && slot < DS_MAX && voices[slot].playing; }

int dm_load(int slot, const char *midi_path)
{
    if (slot < 0 || slot >= DM_MAX) return 0;
    const char *base = strrchr(midi_path, '/');
    base = base ? base + 1 : midi_path;
    char name[48];
    snprintf(name, sizeof name, "%s", base);
    char *dot = strrchr(name, '.');
    if (dot) *dot = 0;
    snprintf(music.paths[slot], sizeof music.paths[slot], "music/%s.ogg", name);
    return 1;
}

void dm_stop(void)
{
    plat_audio_lock();
    if (music.vorb) stb_vorbis_close(music.vorb);
    music.vorb = NULL;
    if (music.file) fclose(music.file);
    music.file = NULL;
    music.cur = -1;
    plat_audio_unlock();
}

void dm_play(int slot, uint32_t repeats)
{
    if (slot < 0 || slot >= DM_MAX || !music.paths[slot][0]) return;
    dm_stop();
    FILE *f = fopen(plat_data_path(music.paths[slot]), "rb");
    if (!f) { plat_log("dm: missing '%s'", music.paths[slot]); return; }
    int err;
    stb_vorbis *v = stb_vorbis_open_file(f, 0, &err, NULL);
    if (!v) { fclose(f); return; }
    plat_audio_lock();
    music.vorb = v;
    music.file = f;
    music.cur = slot;
    music.repeats = repeats;
    plat_audio_unlock();
}

void dm_set_enabled(int on) { music.enabled = on; }

void dx_init(void)
{
    font_load();
    plat_audio_start(mixer);
    im_set_render_state(RS_COLORKEYENABLE, 1);
}
