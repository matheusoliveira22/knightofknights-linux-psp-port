/* PSP backend: sceGu rendering, sceCtrl input, sceAudio output, files on the memory stick.
 *
 * The game's virtual 640x480 screen is shown 4:3-pillarboxed (362x272) by default; the 3D view
 * uses the same rectangle so 2D backgrounds and 3D line up exactly like the original.
 * Triangles are clipped against the near plane on the CPU: the GE has no real clipping and
 * drops primitives that cross z = near, which would eat floor polygons close to the camera.
 */
#include "../plat.h"
#include "../../core/prof.h"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspctrl.h>
#include <pspaudio.h>
#include <psppower.h>
#include <psputils.h>
#include <pspdebug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <malloc.h>
#include <ctype.h>
#include <math.h>

PSP_MODULE_INFO("KnightOfKnights", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

#define BUF_W 512
#define SCR_W 480
#define SCR_H 272

static unsigned int __attribute__((aligned(64))) dlist[2][512 * 1024];   /* 2 x 2 MB: vertices live here too */
static int cur_list;
static void *fbp0, *fbp1, *zbp, *draw_buf;
static int running = 1;
static char data_dir[256] = "ms0:/PSP/GAME/KnightOfKnights";
static int vp_x = 59, vp_y = 0, vp_w = 362, vp_h = 272;  /* game area on screen: 4:3, or all of it when wide */
static int wide_on, wide_off_forced = -1;
static float virt_w = SCREEN_W;    /* width of the 2D coordinate space (SCREEN_H tall) */

void plat_set_widescreen(int on)
{
    if (wide_off_forced < 0) { const char *e = getenv("KOK_WIDE"); wide_off_forced = e && e[0] == '0'; }
    on = on && !wide_off_forced;
    if (on == wide_on) return;
    wide_on = on;
    if (on) { vp_x = 0; vp_w = SCR_W; virt_w = (float)SCREEN_H * SCR_W / SCR_H; }
    else { vp_x = 59; vp_w = 362; virt_w = SCREEN_W; }
}

float plat_virtual_width(void) { return virt_w; }
static float proj[16];
static ScePspFMatrix4 modelview;
static uint32_t fog_rgb;
static float fog_start, fog_end;
static int fog_on;

/* ------------------------------------------------------------------ misc */

void plat_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char path[300];
    snprintf(path, sizeof path, "%s/log.txt", data_dir);   /* next to the EBOOT */
    FILE *f = fopen(path, "a");
    if (f) {
        vfprintf(f, fmt, ap);
        fputc('\n', f);
        fclose(f);
    }
    va_end(ap);
}

static int stat_draws, stat_verts, stat_texup, stat_dropped, stat_cpuverts;   /* cpuverts: through the CPU clip path */

/* Vertex memory from the current display list, or NULL when the frame's list is (nearly) full:
 * the GE has no bounds check, and running past the list corrupts memory on real hardware. */
static void *list_alloc(int bytes)
{
    if (sceGuCheckList() + bytes + 16 * 1024 > (int)sizeof dlist[0]) { stat_dropped++; return NULL; }
    return sceGuGetMemory(bytes);
}
/* x86 masks floating-point exceptions and the game relies on it (0/0 and x/0 in effect and fade
 * maths). On the Allegrex they trap and kill the game on a real PSP (PPSSPP does not model this),
 * and with FCR31.FS clear every denormal raises an unmaskable unimplemented-operation exception
 * that the kernel emulates very slowly (fading effects decay into denormals). The FPU control
 * register is per thread. (pspSdkDisableFPUExceptions would clear FS as well.) */
static void fpu_setup(void)
{
    uint32_t r;
    __asm__ volatile("cfc1 %0, $31" : "=r"(r));
    r &= 0x00800000u;   /* keep the condition bit; no enables, causes or flags; round to nearest */
    r |= 0x01000000u;   /* FS: flush denormals to zero */
    __asm__ volatile("ctc1 %0, $31" : : "r"(r));
}

static int exit_cb(int a, int b, void *c) { (void)a; (void)b; (void)c; running = 0; return 0; }

static int cb_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    int cbid = sceKernelCreateCallback("exit", exit_cb, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

uint64_t plat_time_us(void) { return sceKernelGetSystemTimeWide(); }
void plat_sleep_ms(int ms) { sceKernelDelayThread(ms * 1000); }

/* ------------------------------------------------------------------ files */

/* Win32-like path normalisation: '\\' -> '/', trailing spaces/dots of components dropped.
 * The memory stick filesystem is case-insensitive already. */
static void norm_path(const char *rel, char *out, size_t n)
{
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", rel);
    for (char *p = tmp; *p; p++) if (*p == '\\') *p = '/';
    char *o = out;
    o += snprintf(out, n, "%s", data_dir);
    char *save = NULL;
    for (char *c = strtok_r(tmp, "/", &save); c; c = strtok_r(NULL, "/", &save)) {
        size_t l = strlen(c);
        while (l && (c[l - 1] == ' ' || c[l - 1] == '.')) c[--l] = 0;
        if (!l || !strcmp(c, ".")) continue;
        o += snprintf(o, n - (size_t)(o - out), "/%s", c);
    }
}

const char *plat_data_path(const char *rel)
{
    static char buf[512];
    norm_path(rel, buf, sizeof buf);
    return buf;
}

static void present(void);

uint8_t *plat_read_file(const char *path, size_t *size)
{
    pf[PF_FILE_READ]++;
    present();   /* show a finished frame before a (possibly long) load */
    char full[512];
    norm_path(path, full, sizeof full);
    FILE *f = fopen(full, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    pf[PF_FILE_KB] += (uint32_t)(n >> 10);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(n + 1);
    if (!d || fread(d, 1, n, f) != (size_t)n) { fclose(f); free(d); return NULL; }
    d[n] = 0;
    fclose(f);
    if (size) *size = n;
    return d;
}

int plat_write_file(const char *path, const void *data, size_t size)
{
    present();
    char full[512];
    norm_path(path, full, sizeof full);
    FILE *f = fopen(full, "wb");
    if (!f) return 0;
    int ok = fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

/* ------------------------------------------------------------------ input */

static uint8_t keys[256];
static int frame_no;
static struct { int frame, vk, down; } test_keys[256];
static int ntest_keys;
static uint8_t held[256];
static struct { int frame; unsigned bits; int down; } test_pad[128];   /* "pad" lines: PSP buttons */
static int ntest_pad;
static unsigned held_pad;
static int test_noaudio, test_perf;

/* PSP pad -> the keys the game reads for player 1 (FUN_00439240): arrows, Z X C V S. Menu and play
 * buttons are separate; the game reports what is on screen (plat_set_input_context):
 *   play:  Square X (right-hand attack), Triangle Z (left-hand attack), Cross C (jump),
 *          Circle V (use item), L Z+X (combo / magic), R S (ring / pick up), START Enter (pause)
 *   menus: Cross or START confirm (Z, which the original menus accept); Circle is back (VK_BACK,
 *          handled by game_menu_cancel in sys.c, the original has no cancel)
 *   ring:  Cross Z (select), Circle or R S (close), Square C (drop the item), START Enter (pause)
 * SELECT is F1 (minimap on/off) everywhere. Escape (quits the original) is not mapped. */
static int input_ctx = PLAT_INPUT_MENU;
void plat_set_input_context(int ctx) { input_ctx = ctx; }

static void poll_pad(void)
{
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(&pad, 1);
    memset(keys, 0, sizeof keys);
    for (int i = 0; i < ntest_pad; i++)
        if (test_pad[i].frame == frame_no) held_pad = test_pad[i].down ? held_pad | test_pad[i].bits : held_pad & ~test_pad[i].bits;
    pad.Buttons |= held_pad;
    int lx = pad.Lx - 128, ly = pad.Ly - 128;
    if ((pad.Buttons & PSP_CTRL_UP) || ly < -64) keys[0x26] = 0x80;
    if ((pad.Buttons & PSP_CTRL_DOWN) || ly > 64) keys[0x28] = 0x80;
    if ((pad.Buttons & PSP_CTRL_LEFT) || lx < -64) keys[0x25] = 0x80;
    if ((pad.Buttons & PSP_CTRL_RIGHT) || lx > 64) keys[0x27] = 0x80;
    /* a button still held when the context changes (closing the ring with Circle, confirming a menu
     * with Cross) is ignored until released, so it does not act again with its new meaning */
    static int last_ctx = -1;
    static unsigned latched;
    const unsigned actions = PSP_CTRL_CROSS | PSP_CTRL_CIRCLE | PSP_CTRL_SQUARE | PSP_CTRL_TRIANGLE |
                             PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_START;
    unsigned b = pad.Buttons;
    if (input_ctx != last_ctx) { latched = b & actions; last_ctx = input_ctx; }
    latched &= b;
    b &= ~latched;
    if (b & PSP_CTRL_SELECT) keys[0x70] = 0x80;   /* F1 */
    switch (input_ctx) {
    case PLAT_INPUT_MENU:
        if (b & (PSP_CTRL_CROSS | PSP_CTRL_START)) keys['Z'] = 0x80;
        if (b & PSP_CTRL_CIRCLE) keys[0x08] = 0x80;
        break;
    case PLAT_INPUT_RING:
        if (b & PSP_CTRL_CROSS) keys['Z'] = 0x80;
        if (b & (PSP_CTRL_CIRCLE | PSP_CTRL_RTRIGGER)) keys['S'] = 0x80;
        if (b & PSP_CTRL_SQUARE) keys['C'] = 0x80;
        if (b & PSP_CTRL_START) keys[0x0d] = 0x80;
        break;
    default:
        if (b & PSP_CTRL_SQUARE) keys['X'] = 0x80;
        if (b & PSP_CTRL_TRIANGLE) keys['Z'] = 0x80;
        if (b & PSP_CTRL_CROSS) keys['C'] = 0x80;
        if (b & PSP_CTRL_CIRCLE) keys['V'] = 0x80;
        if (b & PSP_CTRL_RTRIGGER) keys['S'] = 0x80;
        if (b & PSP_CTRL_LTRIGGER) keys['Z'] = keys['X'] = 0x80;
        if (b & PSP_CTRL_START) keys[0x0d] = 0x80;
        break;
    }
    for (int i = 0; i < ntest_keys; i++)
        if (test_keys[i].frame == frame_no) held[test_keys[i].vk] = (uint8_t)test_keys[i].down;
    for (int i = 0; i < 256; i++) if (held[i]) keys[i] = 0x80;
}

void plat_keyboard_state(uint8_t vk[256]) { memcpy(vk, keys, 256); }

int plat_joystick(uint32_t *x, uint32_t *y, uint32_t *b)
{
    /* the pad is already presented as the keyboard: report no joystick (JOYERR_UNPLUGGED) */
    (void)x; (void)y; (void)b;
    return 0;
}

int plat_pump(void)
{
    poll_pad();
    return running;
}

/* ------------------------------------------------------------------ textures */

typedef struct { void *pix; int w, h, psm, used, wrapped, key_on; uint32_t key8888; int swz; unsigned list; } tex_t;
#define MAX_TEX 1024
static tex_t texs[MAX_TEX];

static int in_list;
static int present_pending, pending_frame;   /* a finished list not yet synced/shown (present()) */
static unsigned list_serial;   /* display lists started so far; tex_t.list = the last one that drew it */

/* The GE reads textures and vertex buffers when it runs a list: while the CPU builds that frame
 * and, since the swap is deferred, while the next frame's logic runs. Memory a list may still read
 * is therefore never freed or rewritten in place: it is retired and freed once the GE finished the
 * last list started (the earlier ones were synced before it began).
 * The game also rewrites some surfaces between draws of one frame (the HUD text routines share a
 * 256x256 texture, cleared and redrawn before each of their sprites), so a texture already used by
 * the current list moves to a fresh buffer before an update. */
static struct { void **p; int n, cap; } retired[2];

static void retire(void *p)
{
    if (!p) return;
    int b = list_serial & 1;
    if (retired[b].n == retired[b].cap) {
        int cap = retired[b].cap ? retired[b].cap * 2 : 64;
        void **q = realloc(retired[b].p, (size_t)cap * sizeof *q);
        if (!q) { sceGuSync(0, 0); free(p); return; }
        retired[b].p = q; retired[b].cap = cap;
    }
    retired[b].p[retired[b].n++] = p;
}

static void free_retired(unsigned serial)
{
    int b = serial & 1;
    pf[PF_GPU_RETIRED] += retired[b].n;
    for (int i = 0; i < retired[b].n; i++) free(retired[b].p[i]);
    retired[b].n = 0;
}

static void tex_detach(tex_t *t)
{
    if (t->list != list_serial || t->wrapped || t->psm != GU_PSM_5551) return;
    size_t n = (size_t)t->w * t->h * 2;
    void *p = memalign(16, n);
    if (!p) { sceGuSync(0, 0); return; }
    memcpy(p, t->pix, n);
    sceKernelDcacheWritebackRange(p, n);
    retire(t->pix);
    t->pix = p;
}

static void to5551(uint16_t *o, const uint8_t *rgba, int n)
{
    for (int i = 0; i < n; i++) {
        const uint8_t *p = rgba + i * 4;
        o[i] = (uint16_t)((p[0] >> 3) | (p[1] >> 3) << 5 | (p[2] >> 3) << 10 | (p[3] >= 128) << 15);
    }
}

/* GE swizzled layout (16-byte x 8-row blocks): much better texture cache behaviour. Used for the
 * RGBA textures (meshes, font atlas); w * 2 bytes must be a multiple of 16 and h of 8. */
static void swizzle16(uint16_t *out, const uint16_t *in, int w, int h)
{
    int wb = w * 2, rows = wb / 16, pitch = (wb - 16) / 4;
    const uint8_t *ysrc = (const uint8_t *)in;
    uint32_t *dst = (uint32_t *)out;
    for (int by = 0; by < h / 8; by++, ysrc += wb * 8) {
        const uint8_t *xsrc = ysrc;
        for (int bx = 0; bx < rows; bx++, xsrc += 16) {
            const uint32_t *src = (const uint32_t *)xsrc;
            for (int j = 0; j < 8; j++, src += pitch) {
                *dst++ = *src++; *dst++ = *src++; *dst++ = *src++; *dst++ = *src++;
            }
        }
    }
}

static void fill5551(tex_t *t, const uint8_t *rgba)
{
    if (!t->swz) { to5551(t->pix, rgba, t->w * t->h); return; }
    uint16_t *lin = malloc((size_t)t->w * t->h * 2);
    if (!lin) { t->swz = 0; to5551(t->pix, rgba, t->w * t->h); return; }
    to5551(lin, rgba, t->w * t->h);
    swizzle16(t->pix, lin, t->w, t->h);
    free(lin);
}

int plat_tex_create(int w, int h, const uint8_t *rgba)
{
    for (int i = 1; i < MAX_TEX; i++) {
        if (texs[i].used) continue;
        tex_t *t = &texs[i];
        t->w = w; t->h = h; t->psm = GU_PSM_5551; t->used = 1;
        t->wrapped = t->key_on = 0;
        t->swz = w % 8 == 0 && h % 8 == 0;
        t->pix = memalign(16, (size_t)w * h * 2);
        if (!t->pix) { t->used = 0; return 0; }
        if (rgba) fill5551(t, rgba);
        else memset(t->pix, 0, (size_t)w * h * 2);
        sceKernelDcacheWritebackRange(t->pix, (size_t)w * h * 2);
        if (in_list) sceGuTexFlush();   /* the memory may have held another texture */
        return i;
    }
    return 0;
}

void plat_tex_update(int id, const uint8_t *rgba)
{
    if (id <= 0 || id >= MAX_TEX || !texs[id].used) return;
    tex_t *t = &texs[id];
    fill5551(t, rgba);
    sceKernelDcacheWritebackRange(t->pix, (size_t)t->w * t->h * 2);
    sceGuTexFlush();
}

int plat_tex_create16(int w, int h, const uint16_t *px)
{
    for (int i = 1; i < MAX_TEX; i++) {
        if (texs[i].used) continue;
        tex_t *t = &texs[i];
        t->w = w; t->h = h; t->psm = GU_PSM_5551; t->used = 1;
        t->wrapped = t->key_on = t->swz = 0;   /* updated by rows: linear */
        t->pix = memalign(16, (size_t)w * h * 2);
        if (!t->pix) return 0;
        memcpy(t->pix, px, (size_t)w * h * 2);
        sceKernelDcacheWritebackRange(t->pix, (size_t)w * h * 2);
        if (in_list) sceGuTexFlush();   /* the memory may have held another texture */
        return i;
    }
    return 0;
}

void plat_tex_update16(int id, const uint16_t *px)
{
    if (id <= 0 || id >= MAX_TEX || !texs[id].used) return;
    tex_t *t = &texs[id];
    tex_detach(t);
    memcpy(t->pix, px, (size_t)t->w * t->h * 2);
    sceKernelDcacheWritebackRange(t->pix, (size_t)t->w * t->h * 2);
    sceGuTexFlush();
    stat_texup++;
}

int plat_tex_wrap565(int w, int h, uint16_t *px)
{
    for (int i = 1; i < MAX_TEX; i++) {
        if (texs[i].used) continue;
        texs[i] = (tex_t){ px, w, h, GU_PSM_5650, 1, 1, 0, 0, 0 };
        sceKernelDcacheWritebackRange(px, (size_t)w * h * 2);
        return i;
    }
    return 0;
}

/* A wrapped texture is the dx surface's own memory: the submitted list may still be reading it while
 * the next frame's logic redraws the surface (menus recompose their screen surface every tick), so
 * wait for the GE first. Writes after a draw within the list being built cannot be ordered. */
void plat_tex_before_write(int id)
{
    if (id <= 0 || id >= MAX_TEX || !texs[id].used || !texs[id].wrapped) return;
    if (!in_list && present_pending && texs[id].list == list_serial) {
        sceGuSync(0, 0);
        texs[id].list = 0;
    }
}

void plat_tex_sync565(int id, int key_on, uint16_t key)
{
    if (id <= 0 || id >= MAX_TEX || !texs[id].used) return;
    tex_t *t = &texs[id];
    sceKernelDcacheWritebackRange(t->pix, (size_t)t->w * t->h * 2);
    sceGuTexFlush();
    stat_texup++;
    t->key_on = key_on;
    /* the GE expands 565 texels by bit replication before the colour test */
    uint32_t r = key & 31, g = key >> 5 & 63, b = key >> 11;
    t->key8888 = (b << 3 | b >> 2) << 16 | (g << 2 | g >> 4) << 8 | (r << 3 | r >> 2);
}

void plat_tex_update16_rows(int id, const uint16_t *px, int y0, int y1)
{
    if (id <= 0 || id >= MAX_TEX || !texs[id].used) return;
    tex_t *t = &texs[id];
    tex_detach(t);
    uint16_t *dst = (uint16_t *)t->pix + (size_t)y0 * t->w;
    memcpy(dst, px + (size_t)y0 * t->w, (size_t)(y1 - y0) * t->w * 2);
    sceKernelDcacheWritebackRange(dst, (size_t)(y1 - y0) * t->w * 2);
    sceGuTexFlush();
    stat_texup++;
}

void plat_tex_destroy(int id)
{
    if (id <= 0 || id >= MAX_TEX || !texs[id].used) return;
    tex_t *t = &texs[id];
    if (!t->wrapped && t->psm == GU_PSM_5551) {
        if (t->list == list_serial) retire(t->pix);
        else free(t->pix);
    }
    t->used = 0;
}

/* GE state already in the current display list, so draws only send what changes (sceGuTexImage
 * also flushes the texture cache). Reset at the start of every list; -1 = unknown. */
static struct {
    int tex_on, psm, w, h, linear, blend, fog, cull, ztest, zmask, mats;
    const void *pix;
    uint32_t fog_rgb;
    float fog_start, fog_end;
} gs;

static void gs_reset(void)
{
    memset(&gs, 0xff, sizeof gs);   /* every int -1 */
    gs.pix = NULL;
    gs.mats = 0;
    /* constant for the whole list */
    sceGuTexLevelMode(GU_TEXTURE_CONST, 0.0f);   /* no mipmaps: always sample level 0 */
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
}

#define GS_TOGGLE(field, cap, on) do { int on_ = !!(on); \
    if (gs.field != on_) { gs.field = on_; if (on_) sceGuEnable(cap); else sceGuDisable(cap); } } while (0)

static void bind_tex(int id, int linear)
{
    if (id <= 0 || id >= MAX_TEX || !texs[id].used) { GS_TOGGLE(tex_on, GU_TEXTURE_2D, 0); return; }
    tex_t *t = &texs[id];
    t->list = list_serial;
    GS_TOGGLE(tex_on, GU_TEXTURE_2D, 1);
    int mode = t->psm | t->swz << 8;
    if (gs.psm != mode) { gs.psm = mode; sceGuTexMode(t->psm, 0, 0, t->swz); }
    if (gs.pix != t->pix || gs.w != t->w || gs.h != t->h) {
        gs.pix = t->pix; gs.w = t->w; gs.h = t->h;
        sceGuTexImage(0, t->w, t->h, t->w, t->pix);
    }
    if (gs.linear != linear) {
        gs.linear = linear;
        sceGuTexFilter(linear ? GU_LINEAR : GU_NEAREST, linear ? GU_LINEAR : GU_NEAREST);
    }
}

static void set_blend(int blend);
static void gs_blend(int blend)
{
    if (gs.blend == blend) return;
    gs.blend = blend;
    set_blend(blend);
}

static void gs_fog(int on, float start, float end, uint32_t rgb)
{
    if (on && (gs.fog != 1 || gs.fog_start != start || gs.fog_end != end || gs.fog_rgb != rgb)) {
        gs.fog_start = start; gs.fog_end = end; gs.fog_rgb = rgb;
        sceGuFog(start, end, 0xff000000u | (rgb & 0xff) << 16 | (rgb & 0xff00) | (rgb >> 16 & 0xff));
    }
    GS_TOGGLE(fog, GU_FOG, on);
}

static void gs_depth(int ztest, int zwrite)
{
    GS_TOGGLE(ztest, GU_DEPTH_TEST, ztest);
    int mask = zwrite ? GU_FALSE : GU_TRUE;
    if (gs.zmask != mask) { gs.zmask = mask; sceGuDepthMask(mask); }
}

/* ------------------------------------------------------------------ frame */

static void test_load(void);

static void *vram_alloc(unsigned int size)
{
    static unsigned int off;
    void *r = (void *)off;
    off += size;
    return r;
}

int plat_init(int argc, char **argv)
{
    if (argc > 0 && argv[0]) {
        snprintf(data_dir, sizeof data_dir, "%s", argv[0]);
        char *s = strrchr(data_dir, '/');
        if (s) *s = 0;
    }
    fpu_setup();
    int th = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);
    scePowerSetClockFrequency(333, 333, 166);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    fbp0 = vram_alloc(BUF_W * SCR_H * 2);
    fbp1 = vram_alloc(BUF_W * SCR_H * 2);
    zbp = vram_alloc(BUF_W * SCR_H * 2);
    sceGuInit();
    sceGuStart(GU_DIRECT, dlist[0]);
    sceGuDrawBuffer(GU_PSM_5650, fbp0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, fbp1, BUF_W);
    sceGuDepthBuffer(zbp, BUF_W);
    sceGuOffset(2048 - SCR_W / 2, 2048 - SCR_H / 2);
    sceGuViewport(2048, 2048, SCR_W, -SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuFrontFace(GU_CW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuAlphaFunc(GU_GREATER, 0, 0xff);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
    draw_buf = fbp0;
    test_load();
    return 1;
}

void plat_shutdown(void)
{
    sceGuTerm();
    sceKernelExitGame();
}

void plat_frame_begin(uint32_t clear_rgb)
{
    (void)clear_rgb;
    present();
    list_serial++;
    sceGuStart(GU_DIRECT, dlist[cur_list]);
    in_list = 1;
    gs_reset();
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuClearColor(0xff000000);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    sceGuScissor(vp_x, vp_y, vp_w, vp_h);   /* this pspsdk takes width/height */
}

void plat_clear(int color, uint32_t rgb, int depth)
{
    int bits = 0;
    if (color) {
        sceGuClearColor(0xff000000u | (rgb & 0xff) << 16 | (rgb & 0xff00) | (rgb >> 16 & 0xff));
        bits |= GU_COLOR_BUFFER_BIT;
    }
    if (depth) { sceGuClearDepth(0); bits |= GU_DEPTH_BUFFER_BIT; }
    if (bits) sceGuClear(bits);
}

/* Test harness: <data>/test.txt with lines "shot f1,f2,...", "keys frame:+VK,frame:-VK,...",
 * "pad frame:+0xBITS,frame:-0xBITS,..." (PSP buttons, e.g. 0x4000 Cross, 0x2000 Circle), "exit" (quit after the last shot), "noaudio", "perf" and "env NAME=value". Shots go to <data>/shot_<frame>.ppm (the whole 480x272 screen). */
static int shot_frames[64], nshots, test_exit;

static void test_load(void)
{
    char *d = (char *)plat_read_file("test.txt", NULL);
    if (!d) return;
    for (char *save = NULL, *l = strtok_r(d, "\r\n", &save); l; l = strtok_r(NULL, "\r\n", &save)) {
        if (!strncmp(l, "shot ", 5)) {
            for (char *q = l + 5; *q && nshots < 64; ) {
                shot_frames[nshots++] = (int)strtol(q, &q, 10);
                if (*q == ',') q++;
                else break;
            }
        } else if (!strncmp(l, "keys ", 5)) {
            for (char *q = l + 5; *q && ntest_keys < 256; ) {
                int f = (int)strtol(q, &q, 10);
                if (*q++ != ':') break;
                int down = *q++ == '+';
                int vk = q[0] == '0' && q[1] == 'x' ? (int)strtol(q, &q, 16) : toupper((unsigned char)*q++);
                test_keys[ntest_keys].frame = f;
                test_keys[ntest_keys].vk = vk & 255;
                test_keys[ntest_keys++].down = down;
                if (*q == ',') q++;
            }
        } else if (!strncmp(l, "pad ", 4)) {
            for (char *q = l + 4; *q && ntest_pad < 128; ) {
                int f = (int)strtol(q, &q, 10);
                if (*q++ != ':') break;
                test_pad[ntest_pad].down = *q++ == '+';
                test_pad[ntest_pad].bits = (unsigned)strtoul(q, &q, 16);
                test_pad[ntest_pad++].frame = f;
                if (*q == ',') q++;
            }
        } else if (!strcmp(l, "exit")) test_exit = 1;
        else if (!strcmp(l, "noaudio")) test_noaudio = 1;
        else if (!strcmp(l, "perf")) {   /* once a second: frame time, GE stats, heap; slow frames itemised */
            test_perf = 1;
            if (!getenv("KOK_SPIKE")) setenv("KOK_SPIKE", "40000", 1);
        }
        else if (!strncmp(l, "env ", 4)) {   /* env NAME=value */
            char *eq = strchr(l + 4, '=');
            if (eq) { *eq = 0; setenv(l + 4, eq + 1, 1); }
        }
    }
    free(d);
}

static void test_shot(int frame)
{
    char name[64];
    snprintf(name, sizeof name, "shot_%d.ppm", frame);
    uint8_t *o = malloc(SCR_W * SCR_H * 3 + 32);
    int hl = sprintf((char *)o, "P6\n%d %d\n255\n", SCR_W, SCR_H);
    const uint16_t *fb = (const uint16_t *)((uintptr_t)draw_buf | 0x44000000u);   /* uncached VRAM */
    for (int y = 0; y < SCR_H; y++)
        for (int x = 0; x < SCR_W; x++) {
            uint16_t c = fb[y * BUF_W + x];
            uint8_t *p = o + hl + (y * SCR_W + x) * 3;
            p[0] = (uint8_t)((c & 31) * 255 / 31);
            p[1] = (uint8_t)((c >> 5 & 63) * 255 / 63);
            p[2] = (uint8_t)((c >> 11) * 255 / 31);
        }
    plat_write_file(name, o, (size_t)(hl + SCR_W * SCR_H * 3));
    free(o);
}

/* The frame's list is only kicked at plat_frame_end; waiting for the GE, the vblank and the buffer
 * swap happen when the next frame starts drawing (or before file I/O, so a frame drawn just before a
 * synchronous load is still shown). The next frame's logic ticks thus run while the GE renders. */
static unsigned last_sync_us;

static void present(void)
{
    if (!present_pending) return;
    present_pending = 0;
    uint64_t t0 = plat_time_us();
    sceGuSync(0, 0);
    last_sync_us = (unsigned)(plat_time_us() - t0);
    free_retired(list_serial);   /* the list just finished */
    for (int i = 0; i < nshots; i++)
        if (shot_frames[i] == pending_frame) {
            test_shot(pending_frame);
            if (test_exit && i == nshots - 1) running = 0;
        }
    extern uint64_t prof_us[16];
    uint64_t tv = plat_time_us();
    sceDisplayWaitVblankStart();
    prof_us[7] += plat_time_us() - tv;
    draw_buf = sceGuSwapBuffers();
}

void plat_frame_end(void)
{
    in_list = 0;
    int used = sceGuFinish();
    uint64_t t1 = plat_time_us();
    {   /* perf log once a second: frame time, GE work, peak display list use */
        static uint64_t last, prev_end;
        static unsigned worst;
        static int peak;
        if (used > peak) peak = used;
        if (prev_end && t1 - prev_end > worst) worst = (unsigned)(t1 - prev_end);
        prev_end = t1;
        if (test_perf && frame_no % 60 == 0) {
            extern uint64_t prof_us[16];
            struct mallinfo mi = mallinfo();
            plat_log("heap: arena=%u KB used=%u KB", (unsigned)mi.arena >> 10, (unsigned)mi.uordblks >> 10);
            plat_log("frame %d: %u us/frame draws=%d verts=%d cpuverts=%d texup=%d list=%d B peak=%d B dropped=%d sync=%u us | upload %u blt %u",
                     frame_no, (unsigned)((t1 - last) / 60), stat_draws, stat_verts, stat_cpuverts, stat_texup, used, peak, stat_dropped,
                     last_sync_us, (unsigned)(prof_us[0] / 60), (unsigned)(prof_us[1] / 60));
            plat_log("  cpu/frame: state %u (logic %u in %u ticks [collision %u], scene %u, vblank wait %u) audio %u | worst frame %u us",
                     (unsigned)(prof_us[2] / 60), (unsigned)(prof_us[5] / 60), (unsigned)prof_us[6], (unsigned)(prof_us[8] / 60), (unsigned)(prof_us[3] / 60), (unsigned)(prof_us[7] / 60),
                     (unsigned)(prof_us[4] / 60), worst);
            memset(prof_us, 0, sizeof prof_us);
            last = t1;
            peak = 0;
            worst = 0;
        }
    }
    stat_draws = stat_verts = stat_texup = stat_cpuverts = 0;
    pending_frame = frame_no++;
    present_pending = 1;
    cur_list ^= 1;
}

/* ------------------------------------------------------------------ 3D */

typedef struct { float u, v; uint32_t c; float x, y, z; } gvert_t;   /* GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF */

static uint32_t abgr(uint32_t argb) { return (argb & 0xff00ff00u) | (argb & 0xff) << 16 | (argb >> 16 & 0xff); }

static float near_z = 1.0f, far_z = 1e30f;

void plat_set_projection(float sx, float sy, float n, float f)
{
    /* same matrix as the GL backend (view space z forward); vertices arrive in view space */
    memset(proj, 0, sizeof proj);
    proj[0] = sx * SCREEN_W / virt_w;   /* widescreen: same vertical field, wider horizontal */
    proj[5] = sy;
    proj[10] = (f + n) / (f - n);
    proj[11] = 1.0f;
    proj[14] = -2.0f * f * n / (f - n);
    near_z = n;
    far_z = f;
    gs.mats = 0;
}

void plat_set_modelview(const float m[16]) { memcpy(&modelview, m, sizeof modelview); }

void plat_set_fog(int enable, float start, float end, uint32_t rgb)
{
    fog_on = enable;
    fog_start = start;
    fog_end = end;
    fog_rgb = rgb;
}

static void set_blend(int blend)
{
    switch (blend) {
    case BLEND_NONE: sceGuDisable(GU_BLEND); return;
    case BLEND_ALPHA: sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0); break;
    case BLEND_ADD: sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xffffffff, 0xffffffff); break;
    case BLEND_MUL: sceGuBlendFunc(GU_ADD, GU_FIX, GU_SRC_COLOR, 0, 0); break;
    case BLEND_ADD_ALPHA: sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xffffffff); break;
    }
    sceGuEnable(GU_BLEND);
}

static void game_viewport(void)
{
    sceGuOffset(2048 - (vp_x + vp_w / 2), 2048 - (vp_y + vp_h / 2));
    sceGuViewport(2048, 2048, vp_w, vp_h);
}

/* transform to view space, clip against z = near, emit into the display list */
static int clip_tri(const gvert_t in[3], gvert_t out[4])
{
    int n = 0;
    for (int i = 0; i < 3; i++) {
        const gvert_t *a = &in[i], *b = &in[(i + 1) % 3];
        int ain = a->z >= near_z, bin = b->z >= near_z;
        if (ain) out[n++] = *a;
        if (ain != bin) {
            float t = (near_z - a->z) / (b->z - a->z);
            gvert_t *o = &out[n++];
            o->x = a->x + (b->x - a->x) * t;
            o->y = a->y + (b->y - a->y) * t;
            o->z = near_z;
            o->u = a->u + (b->u - a->u) * t;
            o->v = a->v + (b->v - a->v) * t;
            o->c = a->c;   /* flat colour within a batch */
        }
    }
    return n;
}

static int cull_on = 1;
void plat_set_cull(int on) { cull_on = on; }

/* projection and the z-flipping view matrix: once per display list (or projection change) */
static void load_view_proj(void)
{
    if (gs.mats) return;
    gs.mats = 1;
    /* vertices are in view space with +z forward; the GE computes fog from view-space -z, so the
     * view matrix flips z and the projection takes the flip back */
    ScePspFMatrix4 pf, vw;
    memcpy(&pf, proj, sizeof pf);
    pf.z.x = -pf.z.x; pf.z.y = -pf.z.y; pf.z.z = -pf.z.z; pf.z.w = -pf.z.w;
    memset(&vw, 0, sizeof vw);
    vw.x.x = 1; vw.y.y = 1; vw.z.z = -1; vw.w.w = 1;
    sceGuSetMatrix(GU_PROJECTION, &pf);
    sceGuSetMatrix(GU_VIEW, &vw);
}

static void draw_state_3d(int tex, int blend, int zwrite, int ztest)
{
    game_viewport();
    load_view_proj();
    gs_fog(fog_on, fog_start, fog_end, fog_rgb);
    GS_TOGGLE(cull, GU_CULL_FACE, cull_on);
    gs_depth(ztest, zwrite);
    gs_blend(blend);
    bind_tex(tex, 0);
}

static const ScePspFMatrix4 identity4 = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };

void plat_draw_tris(int tex, const pvert_t *v, int n, int blend, int zwrite, int ztest)
{
    if (n <= 0) return;
    stat_cpuverts += n;
    const float *m = &modelview.x.x;
    static gvert_t *tmp;
    static int tmp_cap;
    if (n * 2 > tmp_cap) {
        tmp_cap = n * 2;
        free(tmp);
        tmp = memalign(16, (size_t)tmp_cap * sizeof(gvert_t));
    }
    gvert_t *dst = tmp;
    int out = 0;
    for (int i = 0; i + 2 < n; i += 3) {
        gvert_t t[3], c[4];
        int all_in = 1, all_out = 1, bad = 0;
        for (int k = 0; k < 3; k++) {
            const pvert_t *s = &v[i + k];
            t[k].x = s->x * m[0] + s->y * m[4] + s->z * m[8] + m[12];
            t[k].y = s->x * m[1] + s->y * m[5] + s->z * m[9] + m[13];
            t[k].z = s->x * m[2] + s->y * m[6] + s->z * m[10] + m[14];
            t[k].u = s->u; t[k].v = s->v;
            t[k].c = abgr(s->color);
            if (t[k].z < near_z) all_in = 0; else all_out = 0;
            /* drop garbage geometry (NaN / ~1e29 from a broken effect) before it reaches the GE:
             * real hardware can hang on it where PPSSPP shrugs it off */
            if (!(fabsf(t[k].x) < 1e4f && fabsf(t[k].y) < 1e4f && fabsf(t[k].z) < 1e4f)) bad = 1;
        }
        if (all_out || bad) continue;
        if (all_in) { dst[out++] = t[0]; dst[out++] = t[1]; dst[out++] = t[2]; continue; }
        int cn = clip_tri(t, c);
        if (cn >= 3) { dst[out++] = c[0]; dst[out++] = c[1]; dst[out++] = c[2]; }
        if (cn == 4) { dst[out++] = c[0]; dst[out++] = c[2]; dst[out++] = c[3]; }
    }
    if (!out) return;
    gvert_t *gv = list_alloc(out * (int)sizeof(gvert_t));
    if (!gv) return;
    memcpy(gv, dst, (size_t)out * sizeof(gvert_t));
    draw_state_3d(tex, blend, zwrite, ztest);
    sceGuSetMatrix(GU_MODEL, &identity4);   /* vertices already in view space */
    stat_draws++; stat_verts += out;
    sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                   out, 0, gv);
}

/* Static batches (map and character meshes): the vertices are converted to the GE format once and
 * transformed by the GE, unless the batch's bounding sphere crosses the near plane (the GE drops
 * such triangles, so those still go through the CPU clipper). Batches outside the view frustum
 * are skipped. */
typedef struct { int n; gvert_t v[]; } gstatic_t;

void plat_static_free(void *cache) { retire(cache); }

void plat_draw_tris_static(int tex, const pvert_t *v, int n, void **cache, const float sphere[4],
                           int blend, int zwrite, int ztest)
{
    if (n <= 0) return;
    const float *m = &modelview.x.x;
    /* sphere centre in view space; radius scaled by the largest axis scale of the modelview */
    float cx = sphere[0] * m[0] + sphere[1] * m[4] + sphere[2] * m[8] + m[12];
    float cy = sphere[0] * m[1] + sphere[1] * m[5] + sphere[2] * m[9] + m[13];
    float cz = sphere[0] * m[2] + sphere[1] * m[6] + sphere[2] * m[10] + m[14];
    float s0 = m[0] * m[0] + m[1] * m[1] + m[2] * m[2], s1 = m[4] * m[4] + m[5] * m[5] + m[6] * m[6];
    float s2 = m[8] * m[8] + m[9] * m[9] + m[10] * m[10];
    float sm = s0 > s1 ? s0 : s1;
    if (s2 > sm) sm = s2;
    float r = sphere[3] * sqrtf(sm);
    if (!(fabsf(cx) < 1e4f && fabsf(cy) < 1e4f && fabsf(cz) < 1e4f && r < 1e5f)) {
        plat_draw_tris(tex, v, n, blend, zwrite, ztest);   /* let the CPU path filter it */
        return;
    }
    /* frustum: behind the near plane, beyond the far plane, or outside the side planes
     * (x_ndc = sx*x/z: plane sx*x - z = 0, normal length sqrt(sx^2 + 1)) */
    float sx = proj[0], sy = proj[5];
    if (cz + r < near_z) return;
    if (cz - r > far_z) return;
    if ((sx * cx - cz) > r * sqrtf(sx * sx + 1) || (-sx * cx - cz) > r * sqrtf(sx * sx + 1)) return;
    if ((sy * cy - cz) > r * sqrtf(sy * sy + 1) || (-sy * cy - cz) > r * sqrtf(sy * sy + 1)) return;
    if (cz - r < near_z) { plat_draw_tris(tex, v, n, blend, zwrite, ztest); return; }
    gstatic_t *g = *cache;
    if (!g || g->n != n) {
        retire(g);
        g = memalign(64, sizeof *g + (size_t)n * sizeof(gvert_t));
        pf[PF_GPU_ALLOC]++;
        if (!g) { plat_draw_tris(tex, v, n, blend, zwrite, ztest); return; }
        g->n = n;
        for (int i = 0; i < n; i++)
            g->v[i] = (gvert_t){ v[i].u, v[i].v, abgr(v[i].color), v[i].x, v[i].y, v[i].z };
        sceKernelDcacheWritebackRange(g, sizeof *g + (size_t)n * sizeof(gvert_t));
        *cache = g;
    }
    draw_state_3d(tex, blend, zwrite, ztest);
    sceGuSetMatrix(GU_MODEL, &modelview);
    stat_draws++; stat_verts += n;
    sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                   n, 0, g->v);
}

/* ------------------------------------------------------------------ 2D */

void plat_draw_tris_2d(int tex, const pvert_t *v, int n, int blend, int flags)
{
    int linear = flags & DRAW2D_LINEAR;
    if (n <= 0) return;
    for (int i = 0; i < n; i++)   /* garbage coordinates can hang the GE on hardware */
        if (!(fabsf(v[i].x) < 1e5f && fabsf(v[i].y) < 1e5f)) return;
    gvert_t *dst = list_alloc(n * (int)sizeof(gvert_t));
    if (!dst) return;
    const float sx = (float)vp_w / virt_w, sy = (float)vp_h / SCREEN_H;
    for (int i = 0; i < n; i++) {
        dst[i].x = vp_x + v[i].x * sx;
        dst[i].y = vp_y + v[i].y * sy;
        dst[i].z = 0;
        dst[i].u = v[i].u;
        dst[i].v = v[i].v;
        dst[i].c = abgr(v[i].color);
        if (tex > 0 && tex < MAX_TEX && texs[tex].used) {   /* 2D vertices take texel coordinates */
            dst[i].u *= texs[tex].w;
            dst[i].v *= texs[tex].h;
        }
    }
    GS_TOGGLE(fog, GU_FOG, 0);
    GS_TOGGLE(cull, GU_CULL_FACE, 0);
    gs_depth(0, 0);
    gs_blend(blend);
    bind_tex(tex, linear);
    if (flags & DRAW2D_OPAQUE) sceGuDisable(GU_ALPHA_TEST);
    int ckey = !(flags & DRAW2D_OPAQUE) && tex > 0 && tex < MAX_TEX && texs[tex].used && texs[tex].key_on;
    if (ckey) {
        sceGuColorFunc(GU_NOTEQUAL, texs[tex].key8888, 0xffffff);
        sceGuEnable(GU_COLOR_TEST);
    }
    sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
                   n, 0, dst);
    stat_draws++; stat_verts += n;
    if (ckey) sceGuDisable(GU_COLOR_TEST);
    sceGuEnable(GU_ALPHA_TEST);
}

int plat_capture(float *umax, float *vmax)
{
    /* copy the game area of the current draw buffer into a VRAM texture */
    static int id;
    static void *cap;
    if (!id) {
        cap = (void *)((uintptr_t)vram_alloc(512 * SCR_H * 2) | 0x04000000u);   /* rows >= 272 never sampled */
        for (int i = 1; i < MAX_TEX; i++)
            if (!texs[i].used) { id = i; break; }
        texs[id] = (tex_t){ cap, 512, 512, GU_PSM_5650, 1, 0, 0, 0, 0 };
    }
    sceGuCopyImage(GU_PSM_5650, vp_x, vp_y, vp_w, vp_h, BUF_W, (void *)((uintptr_t)draw_buf | 0x04000000u),
                   0, 0, 512, cap);
    sceGuTexSync();
    sceGuTexFlush();
    *umax = (float)vp_w / 512;
    *vmax = (float)vp_h / 512;
    return id;
}

/* ------------------------------------------------------------------ audio */

static plat_audio_cb audio_cb;
static int audio_ch = -1;
static SceUID audio_mutex;

static int audio_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    static int16_t buf[2][1024 * 2] __attribute__((aligned(64)));
    int b = 0;
    fpu_setup();   /* the Vorbis decoder runs here */
    while (running) {
        sceKernelWaitSema(audio_mutex, 1, NULL);
        extern uint64_t prof_us[16];
        uint64_t t0 = plat_time_us();
        if (audio_cb) audio_cb(buf[b], 1024);
        else memset(buf[b], 0, sizeof buf[b]);
        prof_us[4] += plat_time_us() - t0;
        sceKernelSignalSema(audio_mutex, 1);
        sceAudioOutputPannedBlocking(audio_ch, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, buf[b]);
        b ^= 1;
    }
    return 0;
}

int plat_audio_start(plat_audio_cb cb)
{
    if (test_noaudio) return 0;
    audio_cb = cb;
    audio_mutex = sceKernelCreateSema("audio", 0, 1, 1, NULL);
    audio_ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 1024, PSP_AUDIO_FORMAT_STEREO);
    if (audio_ch < 0) return 0;
    int th = sceKernelCreateThread("audio", audio_thread, 0x12, 0x10000, PSP_THREAD_ATTR_USER, 0);
    if (th < 0) return 0;
    sceKernelStartThread(th, 0, 0);
    return 1;
}

void plat_audio_lock(void) { if (audio_mutex) sceKernelWaitSema(audio_mutex, 1, NULL); }
void plat_audio_unlock(void) { if (audio_mutex) sceKernelSignalSema(audio_mutex, 1); }

/* Fatal VM error: show it on screen (instead of a hard crash) until the user quits with HOME. */
void plat_fatal_screen(const char *msg)
{
    if (in_list) { sceGuFinish(); in_list = 0; }
    sceGuSync(0, 0);
    pspDebugScreenInit();
    pspDebugScreenPrintf("Knight of Knights - internal error\n\n%s\n\n", msg);
    pspDebugScreenPrintf("Details are in %s/log.txt\nPress HOME to quit.\n", data_dir);
    for (;;) sceKernelDelayThread(100000);
}
