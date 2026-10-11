#include <malloc.h>
/* Hand-written replacements for the original's Win32 / DirectX setup layer (0x401000-0x402f80)
 * and its file I/O (options, character saves, fonts). Signatures match gen/protos.h. */
#include "gen/vmc.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char *H(uint32_t va) { return (const char *)VA(va); }

/* ------------------------------------------------------------------ DirectDraw surfaces */

/* FUN_00401000: load a BMP into a surface record; colour key = top-left pixel when param_3 */
uint32_t f_401000(uint32_t path, uint32_t rec, int32_t colorkey, int32_t sysmem)
{
    (void)sysmem;
    uint32_t s = dd_load_bmp(H(path), colorkey);
    MU32(rec) = s;
    return s != 0;
}

uint32_t f_4011a0(uint32_t w, uint32_t h, uint32_t sys) { (void)sys; return dd_create_surface((int)w, (int)h); }
uint32_t f_401220(uint32_t a, int32_t b, int32_t c, int32_t d) { (void)a; (void)b; (void)c; (void)d; return 1; }
int32_t f_4014e0(uint32_t mask) { (void)mask; return 0; }
uint32_t f_4019a0(void) { return 32u << 20; } /* available video memory */

/* FUN_00440f00: 256x256 work texture surface (fades, captured frames, text cache) */
uint32_t f_440f00(void) { return dd_create_surface(256, 256); }

/* FUN_00440d90: load a BMP texture surface (colour key from top-left pixel when param_3) */
uint32_t f_440d90(uint32_t path, uint32_t out, int32_t colorkey)
{
    uint32_t s = dd_load_bmp(H(path), colorkey);
    MU32(out) = s;
    return s != 0;
}

/* FUN_00440cf0: load a BMP as IDirect3DTexture2 into a {texture, handle} slot pair */
uint32_t f_440cf0(uint32_t path, uint32_t dev, uint32_t slot, int32_t colorkey)
{
    (void)dev;
    uint32_t s = dd_load_bmp(H(path), colorkey);
    if (!s) return 0;
    MU32(slot) = s;
    MU32(slot + 4) = s;
    return 1;
}

/* ------------------------------------------------------------------ scene / device setup */

uint32_t f_401710(void)
{
    im_set_render_state(0x29, 1);  /* COLORKEYENABLE */
    im_set_render_state(0x1b, 1);  /* ALPHABLENDENABLE */
    im_set_render_state(4, 1);     /* TEXTUREPERSPECTIVE */
    im_set_render_state(0x1d, 0);  /* SPECULARENABLE */
    return 1;
}

uint32_t f_401680(void)
{
    f_401710();
    MU32(0x4a388c) = rm_frame_create(0);
    rm_scene_set_background(MU32(0x4a388c), 0xff000000u);
    MU32(0x4a3890) = rm_frame_create(MU32(0x4a388c));
    MU32(0x4a3894) = rm_frame_create(MU32(0x4a388c));
    MU32(0x4a3898) = rm_frame_create(MU32(0x4a388c));
    return 1;
}

uint32_t f_401780(void)
{
    MU32(0x4a3888) = 0x6b000001u; /* viewport */
    rm_viewport_init(MU32(0x4a3898), SCREEN_W, SCREEN_H);
    rm_viewport_set_back(100.0f);
    rm_viewport_set_front(0.0f);  /* rejected by D3DRM: the front plane stays at 1.0 */
    rm_viewport_set_field(0.75f);
    return 1;
}

uint32_t f_401650(void) { return f_401680() && f_401780(); }

/* FUN_00401ec0: reset of the DirectDraw globals, surface records and sprite flags */
void f_401ec0(void)
{
    M32(0x48bbe8) = 0x10;
    for (uint32_t a = 0x4a3858; a <= 0x4a3898; a += 4) if (a != 0x4a3868 && a != 0x4a386c && a != 0x4a3870 && a != 0x4a3874) MU32(a) = 0;
    M32(0x48bbe0) = 0;
    M32(0x48bbec) = 0;
    M32(0x4a193c) = 0;
    M32(0x4a18ec) = 100;
    for (uint32_t p = 0x49ee60; p < 0x49fec8; p += 0xa8) MU32(p) = 0;
    for (uint32_t p = 0x49b7fc; p < 0x49eeac; p += 0x8c) MU8(p) = 0;
    f_43dbd0();
}

/* FUN_00401e50: DirectDraw / Direct3D init. The port's back buffer is RGB565 like the original. */
uint32_t f_401e50(void)
{
    f_401ec0();
    MU32(0x4a3858) = DD_PRIMARY;
    MU32(0x4a385c) = DD_BACK;
    MU32(0x4a3864) = 0x6b000002u;  /* DirectDraw */
    MU32(0x4a3878) = 0x6b000003u;  /* Direct3D2 */
    MU32(0x4a387c) = 0x6b000004u;  /* IDirect3DDevice2 */
    MU32(0x4a3880) = 0x6b000005u;  /* Direct3DRM2 */
    MU32(0x4a3884) = 0x6b000006u;  /* RM device */
    /* pixel format shifts used by the 16-bit conversion code (FUN_004014e0) */
    M32(0x4a18c8) = 0;  M32(0x4a18cc) = 5;  M32(0x4a18d0) = 11;
    M32(0x4a18d4) = 5;  M32(0x4a18d8) = 6;  M32(0x4a18dc) = 5;
    MU32(0x488c60) = dd_create_surface(256, 256);
    MU32(0x488c64) = dd_create_surface(256, 256);
    return 1;
}

uint32_t f_401f70(void) { return 1; }
uint32_t f_4021c0(void) { return 1; }
uint32_t f_402390(int32_t a, uint32_t b, uint32_t c, int32_t d, uint32_t e, uint32_t f)
{
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
    return 1;
}
uint8_t f_4023f0(void) { return 1; }
uint32_t f_4024a0(void) { return 1; }  /* device release on exit */

/* ------------------------------------------------------------------ DirectMusic */

uint32_t f_401b20(void) { return 1; }
uint32_t f_401b50(void) { return 1; }
uint32_t f_401b80(void) { return 1; }

uint32_t f_401a20(void)
{
    MU32(0x49b670) = 0x6d000100u;  /* performance */
    MU32(0x49b674) = 0x6d000101u;  /* composer */
    MU32(0x49b678) = 0x6d000102u;  /* loader */
    return 1;
}

uint32_t f_401bb0(uint32_t slot, uint32_t path)
{
    int i = (int)(slot - 0x49b67c) / 4;
    dm_load(i, H(path));
    MU32(slot) = 0x6d000000u + (uint32_t)i;
    return 1;
}

void f_401cb0(void) { dm_stop(); }

/* FUN_00401d50: Composer::AutoTransition to segment `seg` repeated `repeats` times */
void f_401d50(int32_t seg, uint32_t repeats)
{
    if (M32(0x488f44) && MU32(0x49b670)) dm_play(seg, repeats);
}

/* FUN_00401dc0: Performance::PlaySegment */
void f_401dc0(int32_t seg, uint32_t repeats)
{
    if (M32(0x488f44) && MU32(0x49b670)) dm_play(seg, repeats);
}

void f_401e20(void)
{
    if (M32(0x488f44) && MU32(0x49b670)) dm_stop();
}

/* ------------------------------------------------------------------ DirectSound */

uint32_t f_402630(void) { MU32(0x49b6a8) = 0x6c000100u; return 1; }
void f_4026d0(void) { }

uint32_t f_402740(uint32_t path, uint32_t slot)
{
    int i = (int)(slot - 0x49b6b0) / 4;
    if (!ds_load_wav(i, H(path))) return 0;
    MU32(slot) = 0x6c000000u + (uint32_t)i;
    return 1;
}

/* FUN_004029e0: stop, rewind and play sound buffer `i` */
uint32_t f_4029e0(int32_t i)
{
    if (M32(0x488f40) == 1) {
        ds_stop(i);
        ds_play(i, 0);
    }
    return 0;
}

/* ------------------------------------------------------------------ frame-rate overlay */

uint32_t f_401800(void)
{
    uint8_t k[256];
    plat_keyboard_state(k);
    int key = M32(0x450ff0) & 0xff;
    if ((k[key] & 0x80) == 0) {
        if (M32(0x478c5c) == 0) return 0;
    } else if (M32(0x478c5c) == 0) {
        M32(0x478c5c) = 1;
        MU32(0x478c60) = ms_time();
        M32(0x478b48) = 1;
        M32(0x478c58) = 0;
        M32(0x450fec) = 100;
        return 0;
    } else {
        if (ms_time() - MU32(0x478c60) > 999) {
            if (M32(0x478c58) < M32(0x478b48)) M32(0x478c58) = M32(0x478b48);
            if (M32(0x478b48) < M32(0x450fec)) M32(0x450fec) = M32(0x478b48);
            MU32(0x478c60) = ms_time();
            M32(0x478b48) = 0;
        }
        M32(0x478b48)++;
    }
    if ((k[key] & 0x80) == 0 && M32(0x478c5c) != 0 && ms_time() - MU32(0x478c60) > 499) {
        plat_log("Max = %d/sec  Min = %d/sec fps", M32(0x478c58), M32(0x450fec));
        MU32(0x478c60) = 0;
        M32(0x478b48) = 0;
        M32(0x478c58) = 0;
        M32(0x450fec) = 100;
        M32(0x478c5c) = 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ fonts (FUN_004324b0) */

void f_4324b0(void)
{
    MU32(0x4a8518) = FONT_32;
    MU32(0x4a851c) = (uint32_t)(int32_t)((float)(M32(0x488c54) << 4) * MF(0x44d954)); /* 16 */
}

void f_432530(void) { }  /* DeleteObject(fonts) */

/* ------------------------------------------------------------------ options: dat/opt.dat */

uint32_t f_43dee0(void)
{
    plat_write_file(H(0x4706a4), VA(0x488f28), 0x24);
    return 1;
}

uint32_t f_43ddb0(void)
{
    memset(VA(0x488f28), 0, 0x24);
    size_t n = 0;
    uint8_t *d = plat_read_file(H(0x4706a4), &n);
    if (!d) {
        static const int32_t def[9] = { 1, 1, 0, 1, 1, 1, 1, 1, 0x1e };
        memcpy(VA(0x488f28), def, sizeof def);
        f_43dee0();
    } else {
        memcpy(VA(0x488f28), d, n < 0x24 ? n : 0x24);
        free(d);
    }
    /* the port always renders to a virtual 640x480 screen (option 1 of the original) */
    M32(0x488f2c) = 1;
    M32(0x488c54) = 0x280;
    M32(0x488c58) = 0x1e0;
    return 1;
}

/* ------------------------------------------------------------------ character saves */

static const uint32_t save_names[7] = { 0x47077c, 0x470764, 0x47074c, 0x470734, 0x47071c, 0x470704, 0x4706ec };

static const char *save_path(int32_t slot)
{
    int c = slot ? M32(0x4a3828) : M32(0x4a3824);
    return c >= 0 && c < 7 ? H(save_names[c]) : NULL;
}

/* FUN_0043e120: load save/character/<name>.dat (bytes stored inverted); new record if missing */
uint32_t f_43e120(int32_t slot)
{
    const char *path = save_path(slot);
    size_t n = 0;
    uint8_t *d = path ? plat_read_file(path, &n) : NULL;
    if (!d) { f_43df70(slot); return 1; }
    uint8_t buf[0xd00] = { 0 };
    memcpy(buf, d, n < sizeof buf ? n : sizeof buf);
    free(d);
    for (int i = 0; i < 0xd00; i++) buf[i] = (uint8_t)~buf[i];
    memcpy(VA(0x49fec8 + (uint32_t)slot * 0xd00), buf, 0xd00);
    return 1;
}

/* FUN_0043e290: write save/character/<name>.dat */
uint32_t f_43e290(int32_t slot)
{
    uint8_t buf[0xd00];
    memcpy(buf, VA(0x49fec8 + (uint32_t)slot * 0xd00), 0xd00);
    for (int i = 0; i < 0xd00; i++) buf[i] = (uint8_t)~buf[i];
    const char *path = save_path(slot);
    if (path) plat_write_file(path, buf, sizeof buf);
    return 1;
}

/* ------------------------------------------------------------------ window procedure bits */

uint8_t f_402f80(uint32_t a, uint32_t b, uint32_t c) { (void)a; (void)b; (void)c; return 1; }
int32_t f_402e70(uint32_t a, uint32_t b, uint32_t c, int32_t d) { (void)a; (void)b; (void)c; (void)d; return 0; }
int32_t f_402ea0(uint32_t a, uint32_t b, uint32_t c, int32_t d) { (void)a; (void)b; (void)c; (void)d; return 0; }

/* WM_KEYDOWN handling of the original WndProc (FUN_00402ea0): ESC quits, F1 toggles 0x4a383c,
 * F5 sets 0x4a3844. Returns 0 to quit. */
/* Pause (a port addition): Enter or P (PSP: START) during battle, outside fades. */
static int paused;

static int wndproc_keys(void)
{
    static uint8_t prev[256];
    uint8_t k[256];
    plat_keyboard_state(k);
    int quit = (k[0x1b] & 0x80) && !(prev[0x1b] & 0x80);
    if (((k[0x0d] & 0x80) && !(prev[0x0d] & 0x80)) || ((k['P'] & 0x80) && !(prev['P'] & 0x80)))
        if (paused || (M32(0x488c50) == 1 && M32(0x488c68) == 0)) {
            paused = !paused;
            dx_audio_pause(paused);
        }
    if ((k[0x70] & 0x80) && !(prev[0x70] & 0x80)) M32(0x4a383c) = M32(0x4a383c) == 0;
    if ((k[0x74] & 0x80) && !(prev[0x74] & 0x80)) M32(0x4a3844) = 1;
    memcpy(prev, k, sizeof prev);
    return !quit;
}

/* ------------------------------------------------------------------ WinMain (FUN_00402b50) */

uint32_t f_402b50(uint32_t inst, int32_t show) { (void)inst; (void)show; return 0; }

int game_boot(void)
{
    f_439200();
    ms_srand((uint32_t)time(NULL));
    if ((f_43ddb0() & 0xff) == 0) return 0;
    if ((f_401e50() & 0xff) == 0) return 0;
    if ((f_401650() & 0xff) == 0) return 0;
    if ((f_402630() & 0xff) == 0) return 0;
    f_43e910();
    f_401a20();
    f_43ebd0();
    f_4324b0();
    M32(0x488c50) = 0;
    f_430c20();
    MD(0x488c40) = (double)ms_time();
    return 1;
}

/* while paused: no logic ticks (the tick clock is held at now) and no HUD timers; the frozen scene
 * and sprites are redrawn (as FUN_0043f250 does) under a dimmed "PAUSE" overlay */
static void draw_paused(void)
{
    MD(0x488c40) = (double)ms_time();
    COM_Viewport_Clear(MU32(0x4a3888));
    COM_DDSurf_Blt(MU32(0x4a385c), 0, 0, 0, 0x1000400, 0x4a18ec);
    COM_Viewport_Render(MU32(0x4a3888), MU32(0x4a388c));
    COM_Device2_Update(MU32(0x4a3884));
    f_4406e0();
    f_438520();
    static const int st[] = { 1, 19, 20, 27, 41 };
    uint32_t keep[5];
    for (int i = 0; i < 5; i++) keep[i] = im_get_render_state(st[i]);
    im_set_render_state(1, 0);       /* no texture */
    im_set_render_state(19, 5);      /* SRCALPHA / INVSRCALPHA */
    im_set_render_state(20, 6);
    im_set_render_state(27, 1);
    tlvertex_t q[4] = { { 0, 0, 0, 1, 0x90000000u, 0, 0, 0 }, { SCREEN_W, 0, 0, 1, 0x90000000u, 0, 0, 0 },
                        { 0, SCREEN_H, 0, 1, 0x90000000u, 0, 0, 0 }, { SCREEN_W, SCREEN_H, 0, 1, 0x90000000u, 0, 0, 0 } };
    im_draw(5, q, 4);
    for (int i = 0; i < 5; i++) im_set_render_state(st[i], keep[i]);
    rect_t r = { SCREEN_W / 2 - 40, SCREEN_H / 2 - 16, SCREEN_W / 2 + 40, SCREEN_H / 2 + 16 };
    dx_text(DD_BACK, &r, "PAUSE", FONT_32, 0xffffff, 0, 0);
    COM_DDSurf_Blt(MU32(0x4a3858), 0, MU32(0x4a385c), 0, 0x1000000, 0);
}

int game_frame(void)
{
    if (!wndproc_keys()) return 0;
    /* battle (states 1, 2) and event scenes (4) fill a wide screen; the menus are 4:3 artwork */
    int32_t st = M32(0x488c50);
    plat_set_widescreen(st == 1 || st == 2 || st == 4);
    im_set_hud_scale(st == 1 || st == 2 ? PLAT_HUD_SCALE : 1.0f);
    if (paused && M32(0x488c50) != 1) { paused = 0; dx_audio_pause(0); }
    if (paused) {
        draw_paused();
        plat_sleep_ms(15);
        return 1;
    }
    if (getenv("KOK_TRACE")) {
        static int32_t ls = -1, lt = -1;
        static int32_t lf = -1;
        static int32_t lroom = -1, lmove = -1;
        if (M32(0x480340 + 0x74) != lmove && M32(0x480340 + 0x6c) == 0xe) plat_log("move id=%d", M32(0x480340 + 0x74));
        lmove = M32(0x480340 + 0x6c) == 0xe ? M32(0x480340 + 0x74) : -1;
        if (M32(0x488bbc) != lroom) plat_log("room=%d last=%d", M32(0x488bbc), M32(0x488bb4));
        lroom = M32(0x488bbc);
        if (M32(0x488c50) != ls || M32(0x4a3848) != lt || M32(0x488c68) != lf)
            plat_log("state=%d title_sub=%d map=%d fade=%d prog=%f spd=%f heap=%u KB", M32(0x488c50), M32(0x4a3848),
                     M32(0x4a3830), M32(0x488c68), MF(0x488c6c), MF(0x488c70), (unsigned)(mallinfo().uordblks >> 10));
        static int32_t lhp[8];
        for (int a = 0; a < 8; a++) {
            uint32_t b = 0x480340 + a * 0x144;
            if (M8(b + 0x140) && M32(b + 0xd0) != lhp[a])
                plat_log("actor%d hp=%d st=%d pos=%.2f %.2f", a, M32(b + 0xd0), M32(b + 0x6c), MF(b + 0xc), MF(b + 0x14));
            lhp[a] = M32(b + 0xd0);
        }
        lf = M32(0x488c68);
        ls = M32(0x488c50); lt = M32(0x4a3848);
    }
    /* The state handlers run fixed-step logic ticks to catch up with timeGetTime (base at 0x488c40).
     * Bound the backlog so a slow frame (loading, slow hardware) can't snowball into ever-longer
     * catch-up runs: at most ~70 ms of logic per rendered frame, then the game slows down instead. */
    {
        double now = (double)ms_time();
        if (now - MD(0x488c40) > 70.0) MD(0x488c40) = now - 70.0;
    }
    uint32_t fn = MU32(0x453238 + (uint32_t)M32(0x488c50) * 4);
    extern uint64_t prof_us[16];
    extern uint64_t prof_tick_t0;
    uint64_t t0 = prof_tick_t0 = plat_time_us();
    uint32_t r = CALLVA(fn)();
    prof_us[2] += plat_time_us() - t0;
    if ((r & 0xff) == 0) {
        plat_log("state %d returned failure", M32(0x488c50));
        return 0;
    }
    plat_sleep_ms(1);
    return 1;
}

/* per-tick collision pass (FUN_00429a00), timed for the perf log */
void prof_collide(void)
{
    extern uint64_t prof_us[16];
    uint64_t t0 = plat_time_us();
    f_429a00();
    prof_us[8] += plat_time_us() - t0;
}

/* RM user visuals call back into game code with D3DRM's (uv, arg, reason, device, viewport) */
void game_uservisual(uint32_t cb, uint32_t arg, const mat4 *world, const mat4 *view)
{
    im_set_uv_transform(world, view);
    CALLVA(cb)(0, arg, 1 /* D3DRMUSERVISUAL_RENDER */, MU32(0x4a3884), MU32(0x4a3888));
}

/* ------------------------------------------------------------------ registered combos */

/* FUN_004378d0: load save/combo/combo0N.dat (8 records of 0x988 bytes at 0x481270) */
uint32_t f_4378d0(void)
{
    char path[64];
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t rec = 0x481270 + i * 0x988;
        snprintf(path, sizeof path, H(0x46d71c), i);
        memset(VA(rec), 0, 0x988);
        size_t n = 0;
        uint8_t *d = plat_read_file(path, &n);
        if (!d) { f_437860(i); continue; }
        memcpy(VA(rec), d, n < 0x988 ? n : 0x988);
        free(d);
    }
    return 0;
}

/* FUN_00437980: write save/combo/combo0N.dat */
void f_437980(uint32_t i)
{
    char path[64];
    snprintf(path, sizeof path, H(0x46d71c), i);
    plat_write_file(path, VA(0x481270 + i * 0x988), 0x988);
}

/* KOK_BOT=1 (testing): in battle, walk player 1 towards the nearest living enemy and attack.
 * KOK_BOT=explore ignores the enemies (and keeps its HP full) while a room has an open exit, walking
 * from exit to exit, the least used one first.
 * Arrows are camera relative (the battle camera looks down +z). */
void game_bot(uint8_t *k)
{
    static int on = -1, t, explore;
    if (on < 0) { const char *e = getenv("KOK_BOT"); on = e != NULL; explore = e && !strcmp(e, "explore"); }
    if (!on) return;
    {   /* KOK_MAP=n: the stage picked at stage select loads map n instead (2 = the second stage) */
        const char *m = getenv("KOK_MAP");
        if (m && M32(0x488c50) == 0 && M32(0x4a3830) == 1) M32(0x4a3830) = atoi(m);
    }
    if (M32(0x488c50) == 0) {
        /* title -> mode select (1P) -> stage select: steer the map cursor (0x4a850c/e, 4 px a tick)
         * into the Stage 1 box (x 189..209, y 216..246 of 640x480, see the hit test in f_431310) and confirm */
        static int mt;
        mt++;
        k[0x25] = k[0x26] = k[0x27] = k[0x28] = k['Z'] = 0;
        if (M32(0x4a3848) == 3 && M32(0x4a84fc) == 3) {
            int cx = M16(0x4a850c) * 640 / M32(0x488c54), cy = M16(0x4a850e) * 480 / M32(0x488c58);
            if (cx > 205) k[0x25] = 0x80; else if (cx < 193) k[0x27] = 0x80;
            if (cy > 240) k[0x26] = 0x80; else if (cy < 222) k[0x28] = 0x80;
            if (!k[0x25] && !k[0x26] && !k[0x27] && !k[0x28] && mt % 30 < 5) k['Z'] = 0x80;
        } else if (mt % 60 < 5) {
            k['Z'] = 0x80;
        }
        return;
    }
    if (M32(0x488c50) != 1 || !M8(0x480340 + 0x140)) return;
    if (explore) M32(0x480340 + 0xd0) = M32(0x480340 + 0xcc);   /* explorer: the enemies can't stop it */
    vec3 p, e, best = { 0, 0, 0 };
    float bd = 1e9f;
    rm_frame_get_position(MU32(0x480340), MU32(0x4a388c), &p);
    int exits_open = 0;
    for (int i = 0; i < 5; i++) if (MU32(0x4a1bf0 + i * 4)) exits_open = 1;
    for (int a = 2; a < 32 && !(explore && exits_open); a++) {   /* explorer fights only in locked rooms */
        uint32_t b = 0x480340 + a * 0x144;
        if (b + 0x144 > 0x48139c) break;
        if (!M8(b + 0x140) || M32(b + 0xd0) <= 0 || !MU32(b)) continue;
        rm_frame_get_position(MU32(b), MU32(0x4a388c), &e);
        float d = (e.x - p.x) * (e.x - p.x) + (e.z - p.z) * (e.z - p.z);
        if (d < bd) { bd = d; best = e; }
    }
    k[0x25] = k[0x26] = k[0x27] = k[0x28] = k['X'] = k['Z'] = 0;
    if (bd > 1e8f) {
        /* room clear: head for an exit trigger (boxes at 0x4a1ca4.., enabled flags 0x4a1bf0), cycling
         * through them per room visit so the bot explores; the room centre ends the area */
        static int32_t last_room = -1, pick;
        static uint16_t used[256][5];   /* exits taken per room: prefer the least used one */
        int room = M32(0x488bbc) & 255;
        int n = 0, ex[5];
        for (int i = 0; i < 5; i++) if (MU32(0x4a1bf0 + i * 4)) ex[n++] = i;
        if (M32(0x488bbc) != last_room) {
            last_room = M32(0x488bbc);
            pick = -1;
            for (int j = 0; j < n; j++) if (pick < 0 || used[room][ex[j]] < used[room][pick]) pick = ex[j];
            if (pick >= 0) used[room][pick]++;
        }
        if (M32(0x488bb4) == M32(0x488bbc) || !n || pick < 0) { best.x = 0; best.z = 0; }
        else {
            int i = pick;
            best.x = (MF(0x4a1ca4 + i * 4) + MF(0x4a1cb8 + i * 4)) / 2;
            best.z = (MF(0x4a1cf4 + i * 4) + MF(0x4a1d08 + i * 4)) / 2;
        }
        float dx = best.x - p.x, dz = best.z - p.z;
        if (dx > 0.2f) k[0x27] = 0x80;
        if (dx < -0.2f) k[0x25] = 0x80;
        if (dz > 0.2f) k[0x26] = 0x80;
        if (dz < -0.2f) k[0x28] = 0x80;
        return;
    }
    float dx = best.x - p.x, dz = best.z - p.z;
    t++;
    if (bd > 1.6f * 1.6f) {
        if (dx > 0.3f) k[0x27] = 0x80;
        if (dx < -0.3f) k[0x25] = 0x80;
        if (dz > 0.3f) k[0x26] = 0x80;
        if (dz < -0.3f) k[0x28] = 0x80;
    } else if (t % 400 >= 120) {          /* charge patterns: hold Z, X, then both, release */
        int ph = (t % 400 - 120) / 70, in = (t % 400 - 120) % 70;
        if (in < 55) {
            if (ph == 0 || ph == 2 || ph == 3) k['Z'] = 0x80;
            if (ph == 1 || ph == 2 || ph == 3) k['X'] = 0x80;
        }
    } else if (t % 16 < 3) {
        k[(t / 16) % 3 == 2 ? 'Z' : 'X'] = 0x80;
    } else if (t % 16 == 8) {   /* re-face the target between swings */
        if (dx > 0.3f) k[0x27] = 0x80; else if (dx < -0.3f) k[0x25] = 0x80;
        if (dz > 0.3f) k[0x26] = 0x80; else if (dz < -0.3f) k[0x28] = 0x80;
    }
}
