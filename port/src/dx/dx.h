/* DirectX 6 / GDI emulation for the ported game code: DirectDraw surfaces, Direct3D immediate
 * mode TL-vertex drawing, GDI text, DirectSound buffers and DirectMusic segments.
 *
 * Surfaces are 32-bit handles stored where the game stored IDirectDrawSurface pointers.
 * DD_BACK / DD_PRIMARY stand for the back buffer and front buffer; blits to the back buffer are
 * drawn immediately in call order on top of whatever was rendered before.
 */
#ifndef KOK_DX_H
#define KOK_DX_H
#include <stdint.h>

#define DD_PRIMARY 0x6f000001u
#define DD_BACK    0x6f000002u

/* DDBLT flags the game uses */
#define DDBLT_COLORFILL 0x00000400u
#define DDBLT_DDFX      0x00000800u
#define DDBLT_KEYSRC    0x00008000u
#define DDBLT_WAIT      0x01000000u

typedef struct { int32_t left, top, right, bottom; } rect_t;

void dx_init(void);

/* DirectDraw */
uint32_t dd_load_bmp(const char *path, int colorkey);           /* FUN_00401000 semantics */
uint32_t dd_create_surface(int w, int h);
void dd_release(uint32_t s);
void dd_addref(uint32_t s);
int dd_width(uint32_t s);
int dd_height(uint32_t s);
void dd_blt(uint32_t dst, const rect_t *dr, uint32_t src, const rect_t *sr, uint32_t flags);
void dd_fill(uint32_t dst, const rect_t *dr, uint32_t rgb);
void dd_set_colorkey(uint32_t s, int on, uint32_t rgb);
uint32_t dd_pixel(uint32_t s, int x, int y);                    /* 0xRRGGBB */
int dd_texture(uint32_t s, float *umax, float *vmax);           /* plat texture for IM drawing */
uint16_t dd_get565(uint32_t s, int x, int y);
void dd_put565(uint32_t s, int x, int y, uint16_t c);
void dd_touch(uint32_t s);                                      /* pixels changed: re-upload */
void dd_capture_back(uint32_t s);   /* surface shows a copy of the current back buffer (game area) */

/* GDI text (DrawTextA with DT_WORDBREAK/DT_LEFT/DT_TOP; MS Gothic monospace metrics) */
enum { FONT_32 = 32, FONT_16 = 16 };
void dx_text(uint32_t dst, const rect_t *r, const char *sjis, int font, uint32_t rgb, int opaque, uint32_t bkrgb);
void dx_text_out(uint32_t dst, int x, int y, const char *sjis, int font, uint32_t rgb, int opaque, uint32_t bkrgb);

/* Direct3D immediate mode (IDirect3DDevice2) */
void im_set_render_state(int state, uint32_t value);
uint32_t im_get_render_state(int state);
/* D3DTLVERTEX: x, y, z, rhw, color(ARGB), specular, tu, tv */
typedef struct { float x, y, z, rhw; uint32_t color, specular; float tu, tv; } tlvertex_t;
void im_draw(int primtype, const tlvertex_t *v, int n);
/* 2D draws (im_draw sprites, text onto the back buffer) are scaled by k about the screen corner,
 * edge midpoint or centre nearest to each element, so HUD groups stay aligned and on screen */
void im_set_hud_scale(float k);
/* while on, 2D draws belong to one centred block (the minimap's 7x7 tile grid): no HUD scale and
 * no per-element anchoring, only the shift that keeps the centre of a wide screen */
void im_hud_center(int on);
/* the next im_draw textured with surface `from` samples surface `to` instead (once). The HUD text
 * routines draw into private surfaces this way: the original shares one texture between them,
 * redrawn before each sprite, which would mean several full re-uploads per frame. 0 cancels. */
void im_redirect_next(uint32_t from, uint32_t to);
/* texture handle used by D3DRENDERSTATE_TEXTUREHANDLE: a surface handle */

/* DirectSound: one buffer per slot, Play() restarts it. */
#define DS_MAX 64
int ds_load_wav(int slot, const char *path);
void ds_play(int slot, int loop);
void ds_stop(int slot);
void ds_set_volume(int slot, int db100);    /* DirectSound volume in 1/100 dB (0 = full) */
int ds_playing(int slot);

/* DirectMusic: segments are pre-rendered OGG files (music/<name>.ogg). */
#define DM_MAX 16
int dm_load(int slot, const char *midi_path);
void dm_play(int slot, uint32_t repeats);   /* 0xffffffff = forever */
void dm_stop(void);
void dm_set_enabled(int on);
void dx_audio_pause(int on);   /* silence and hold every voice and the music */

#endif
