/* Platform layer: everything that differs between the PC (SDL2 + OpenGL) and PSP (sceGu) builds.
 *
 * Coordinate conventions follow Direct3D: left-handed view space looking down +z, row-vector
 * matrices (v' = v * M), stored row-major. 2D coordinates are in the game's virtual 640x480 screen.
 */
#ifndef KOK_PLAT_H
#define KOK_PLAT_H
#include <stdint.h>
#include <stddef.h>

#define SCREEN_W 640
#define SCREEN_H 480

/* The PSP shows the 640x480 screen at 362x272: the battle HUD (2D sprites and text) and the ring
 * menu are drawn this much larger there */
/* Widescreen: while on, the game area fills the whole screen (window) and 2D coordinates span
 * plat_virtual_width() x SCREEN_H (853 x 480 at 16:9); the 3D view keeps its vertical field of view.
 * Off: the 640x480 area is pillarboxed at 4:3. Disabled entirely with KOK_WIDE=0. */
void plat_set_widescreen(int on);
float plat_virtual_width(void);

/* Input context for platforms whose buttons change meaning between menus and play (PSP): the game
 * reports what is on screen every frame. */
enum { PLAT_INPUT_MENU, PLAT_INPUT_GAME, PLAT_INPUT_RING };
void plat_set_input_context(int ctx);

#ifdef PSP
#define PLAT_SINGLE_PLAYER 1     /* one pad: the 2P modes are greyed out and skipped */
#define PLAT_HUD_SCALE 1.3f
#else
#define PLAT_SINGLE_PLAYER 0
#define PLAT_HUD_SCALE 1.0f
#endif

int plat_init(int argc, char **argv);
void plat_shutdown(void);
int plat_pump(void);                 /* handle OS events; 0 when the app should quit */
uint64_t plat_time_us(void);
void plat_sleep_ms(int ms);
void plat_log(const char *fmt, ...);

/* Files: paths are relative to the game data directory, '/' or '\\' separated. */
uint8_t *plat_read_file(const char *path, size_t *size);   /* malloc'd, NULL if missing */
int plat_write_file(const char *path, const void *data, size_t size);
const char *plat_data_path(const char *rel);               /* full host path (static buffer) */

/* Input: Win32 virtual-key state, bit 0x80 set while held (GetKeyboardState semantics). */
void plat_keyboard_state(uint8_t vk[256]);
/* Joystick (joyGetPosEx semantics): axes 0..65535 centred at 32767, buttons bitmask. */
int plat_joystick(uint32_t *x, uint32_t *y, uint32_t *buttons);

/* Textures: RGBA8888 pixels (byte order R,G,B,A), any size; returns id > 0. */
int plat_tex_create(int w, int h, const uint8_t *rgba);
void plat_tex_update(int id, const uint8_t *rgba);
void plat_tex_destroy(int id);
/* 16-bit textures in A1B5G5R5 layout (bit 15 = opaque; r in bits 0-4), the PSP's GU_PSM_5551 */
int plat_tex_create16(int w, int h, const uint16_t *px);
void plat_tex_update16(int id, const uint16_t *px);
void plat_tex_update16_rows(int id, const uint16_t *px, int y0, int y1);   /* px: whole texture */
/* Zero-copy 16-bit textures over caller-owned pixels in the PSP's native 565 order (r in bits 0-4,
 * g 5-10, b 11-15). plat_tex_sync565 after the pixels change; key_on makes texels equal to key
 * transparent in 2D draws without DRAW2D_OPAQUE (PSP: GE colour test). */
int plat_tex_wrap565(int w, int h, uint16_t *px);
void plat_tex_sync565(int id, int key_on, uint16_t key);
/* the CPU is about to change (or free) the pixels a wrapped texture points at: waits for the GPU if
 * a submitted frame still reads them */
void plat_tex_before_write(int id);

enum { BLEND_NONE, BLEND_ALPHA, BLEND_ADD, BLEND_MUL, BLEND_ADD_ALPHA };

typedef struct { float x, y, z, u, v; uint32_t color; } pvert_t; /* color 0xAARRGGBB */

void plat_frame_begin(uint32_t clear_rgb);
void plat_clear(int color, uint32_t rgb, int depth);   /* clear inside the game screen area */
void plat_frame_end(void);

/* 3D */
void plat_set_projection(float sx, float sy, float znear, float zfar); /* x_ndc = sx*x/z, y_ndc = sy*y/z */
void plat_set_modelview(const float m[16]);
void plat_set_fog(int enable, float start, float end, uint32_t rgb);
void plat_draw_tris(int tex, const pvert_t *v, int nverts, int blend, int zwrite, int ztest);
void plat_set_cull(int on);   /* back-face culling of plat_draw_tris (default on) */
/* Vertex data drawn unchanged many times (mesh batches): *cache holds the backend's own copy, made
 * on first use and freed with plat_static_free; sphere = bounding sphere (x, y, z, r) of v. */
void plat_draw_tris_static(int tex, const pvert_t *v, int nverts, void **cache, const float sphere[4],
                           int blend, int zwrite, int ztest);
void plat_static_free(void *cache);

/* Copy the current frame (game area) into a texture; returns its id (reused) and uv extents. */
int plat_capture(float *umax, float *vmax);

/* 2D in virtual 640x480 space, drawn over everything (no depth). x,y in pixels, z ignored.
 * flags: DRAW2D_LINEAR (bilinear filtering), DRAW2D_OPAQUE (ignore texture alpha). */
enum { DRAW2D_LINEAR = 1, DRAW2D_OPAQUE = 2 };
void plat_draw_tris_2d(int tex, const pvert_t *v, int nverts, int blend, int flags);

/* Audio: the mixer callback fills interleaved stereo s16 frames at 44100 Hz. */
typedef void (*plat_audio_cb)(int16_t *out, int frames);
int plat_audio_start(plat_audio_cb cb);
void plat_audio_lock(void);
void plat_audio_unlock(void);

#endif
