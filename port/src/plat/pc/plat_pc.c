/* PC backend: SDL2 window/input/audio + OpenGL 1.x fixed-function rendering. */
#include "../plat.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

static SDL_Window *win;
static SDL_GLContext glc;
static SDL_AudioDeviceID audio_dev;
static plat_audio_cb audio_cb;
static SDL_GameController *pad;
static char data_dir[512] = ".";
static uint8_t keys[256];
static int vp_x, vp_y, vp_w, vp_h;   /* game area in window pixels: 4:3 box, or the whole window when wide */
static int wide_on, wide_off_forced;
static float virt_w = SCREEN_W;     /* width of the 2D coordinate space (SCREEN_H tall) */
static float proj[16];

void plat_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ---------- files: case-insensitive resolution like the Win32 filesystem ---------- */

static int resolve_ci(const char *rel, char *out, size_t outsz)
{
    char path[1024];
    snprintf(out, outsz, "%s", data_dir);
    snprintf(path, sizeof path, "%s", rel);
    for (char *p = path; *p; p++) if (*p == '\\') *p = '/';
    char *save = NULL;
    for (char *comp = strtok_r(path, "/", &save); comp; comp = strtok_r(NULL, "/", &save)) {
        /* Win32 ignores trailing spaces and dots in path components */
        for (size_t l = strlen(comp); l > 0 && (comp[l - 1] == ' ' || comp[l - 1] == '.'); l--) comp[l - 1] = 0;
        if (!*comp || !strcmp(comp, ".")) continue;
        char cand[1024];
        snprintf(cand, sizeof cand, "%s/%s", out, comp);
        struct stat st;
        if (stat(cand, &st) == 0) { snprintf(out, outsz, "%s", cand); continue; }
        DIR *d = opendir(out);
        int found = 0;
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                if (!strcasecmp(e->d_name, comp)) {
                    snprintf(cand, sizeof cand, "%s/%s", out, e->d_name);
                    found = 1;
                    break;
                }
            }
            closedir(d);
        }
        if (!found) { snprintf(cand, sizeof cand, "%s/%s", out, comp); }
        snprintf(out, outsz, "%s", cand);
        if (!found) return 0;
    }
    return 1;
}

const char *plat_data_path(const char *rel)
{
    static char buf[1024];
    resolve_ci(rel, buf, sizeof buf);
    return buf;
}

uint8_t *plat_read_file(const char *path, size_t *size)
{
    char full[1024];
    if (!resolve_ci(path, full, sizeof full)) return NULL;
    FILE *f = fopen(full, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(n + 1);
    if (fread(d, 1, n, f) != (size_t)n) { fclose(f); free(d); return NULL; }
    d[n] = 0;
    fclose(f);
    if (size) *size = n;
    return d;
}

int plat_write_file(const char *path, const void *data, size_t size)
{
    char full[1024];
    resolve_ci(path, full, sizeof full);
    FILE *f = fopen(full, "wb");
    if (!f) return 0;
    int ok = fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

/* ---------- lifecycle ---------- */

static void audio_trampoline(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    if (audio_cb) audio_cb((int16_t *)stream, len / 4);
    else memset(stream, 0, len);
}

static void update_viewport(void)
{
    int w, h;
    SDL_GL_GetDrawableSize(win, &w, &h);
    if (w * 3 > h * 4) { vp_h = h; vp_w = wide_on ? w : h * 4 / 3; }
    else { vp_w = w; vp_h = w * 3 / 4; }   /* narrower than 4:3: letterbox */
    vp_x = (w - vp_w) / 2;
    vp_y = (h - vp_h) / 2;
    virt_w = (float)SCREEN_H * vp_w / vp_h;
    if (virt_w < SCREEN_W) virt_w = SCREEN_W;
}

void plat_set_widescreen(int on)
{
    on = on && !wide_off_forced;
    if (on == wide_on) return;
    wide_on = on;
    update_viewport();
}

float plat_virtual_width(void) { return virt_w; }

int plat_init(int argc, char **argv)
{
    const char *env = getenv("KOK_DATA");
    if (argc > 1 && argv[1][0] != '-') snprintf(data_dir, sizeof data_dir, "%s", argv[1]);
    else if (env) snprintf(data_dir, sizeof data_dir, "%s", env);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) {
        plat_log("SDL_Init: %s", SDL_GetError());
        return 0;
    }
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    const char *wide = getenv("KOK_WIDE");
    wide_off_forced = wide && wide[0] == '0';
    win = SDL_CreateWindow("Knight of Knights", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           wide_off_forced ? 960 : 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!win) { plat_log("SDL_CreateWindow: %s", SDL_GetError()); return 0; }
    glc = SDL_GL_CreateContext(win);
    if (!glc) { plat_log("GL context: %s", SDL_GetError()); return 0; }
    SDL_GL_SetSwapInterval(1);
    update_viewport();
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i) && (pad = SDL_GameControllerOpen(i))) break;
    return 1;
}

void plat_shutdown(void)
{
    if (audio_dev) SDL_CloseAudioDevice(audio_dev);
    if (glc) SDL_GL_DeleteContext(glc);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
}

uint64_t plat_time_us(void)
{
    static uint64_t t0;
    uint64_t now = SDL_GetPerformanceCounter() * 1000000ull / SDL_GetPerformanceFrequency();
    if (!t0) t0 = now;
    return now - t0;
}

void plat_sleep_ms(int ms) { SDL_Delay(ms); }

/* ---------- input ---------- */

static int sdl_to_vk(SDL_Keycode k)
{
    if (k >= SDLK_a && k <= SDLK_z) return 'A' + (k - SDLK_a);
    if (k >= SDLK_0 && k <= SDLK_9) return '0' + (k - SDLK_0);
    if (k >= SDLK_F1 && k <= SDLK_F12) return 0x70 + (k - SDLK_F1);
    if (k >= SDLK_KP_1 && k <= SDLK_KP_9) return 0x61 + (k - SDLK_KP_1);
    switch (k) {
    case SDLK_KP_0: return 0x60;
    case SDLK_LEFT: return 0x25; case SDLK_UP: return 0x26;
    case SDLK_RIGHT: return 0x27; case SDLK_DOWN: return 0x28;
    case SDLK_RETURN: case SDLK_KP_ENTER: return 0x0d;
    case SDLK_ESCAPE: return 0x1b; case SDLK_SPACE: return 0x20;
    case SDLK_BACKSPACE: return 0x08; case SDLK_TAB: return 0x09;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return 0x10;
    case SDLK_LCTRL: case SDLK_RCTRL: return 0x11;
    case SDLK_LALT: case SDLK_RALT: return 0x12;
    case SDLK_COMMA: return 0xbc; case SDLK_PERIOD: return 0xbe;
    case SDLK_SLASH: return 0xbf; case SDLK_SEMICOLON: return 0xba;
    case SDLK_MINUS: return 0xbd; case SDLK_EQUALS: return 0xbb;
    case SDLK_INSERT: return 0x2d; case SDLK_DELETE: return 0x2e;
    case SDLK_HOME: return 0x24; case SDLK_END: return 0x23;
    case SDLK_PAGEUP: return 0x21; case SDLK_PAGEDOWN: return 0x22;
    }
    return 0;
}

int plat_pump(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: return 0;
        case SDL_KEYDOWN: case SDL_KEYUP: {
            int vk = sdl_to_vk(e.key.keysym.sym);
            if (vk) keys[vk] = e.type == SDL_KEYDOWN ? 0x80 : 0;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT)) {
                Uint32 fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
            break;
        }
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) update_viewport();
            break;
        case SDL_CONTROLLERDEVICEADDED:
            if (!pad) pad = SDL_GameControllerOpen(e.cdevice.which);
            break;
        }
    }
    return 1;
}

void plat_keyboard_state(uint8_t vk[256]) { memcpy(vk, keys, 256); }

int plat_joystick(uint32_t *x, uint32_t *y, uint32_t *buttons)
{
    if (!pad) return 0;
    int ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    int ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) ax = -32768;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) ax = 32767;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) ay = -32768;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) ay = 32767;
    *x = (uint32_t)(ax + 32768);
    *y = (uint32_t)(ay + 32768);
    static const SDL_GameControllerButton map[] = {
        SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
        SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START };
    uint32_t b = 0;
    for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++)
        if (SDL_GameControllerGetButton(pad, map[i])) b |= 1u << i;
    *buttons = b;
    return 1;
}

/* ---------- textures ---------- */

int plat_tex_create(int w, int h, const uint8_t *rgba)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    /* remember size for updates */
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, 1.0f);
    return (int)t;
}

void plat_tex_update(int id, const uint8_t *rgba)
{
    GLint w, h;
    glBindTexture(GL_TEXTURE_2D, id);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

static uint8_t *rgba_from16(int w, int h, const uint16_t *px)
{
    uint8_t *o = malloc((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) {
        uint16_t c = px[i];
        o[i * 4] = (uint8_t)((c & 31) * 255 / 31);
        o[i * 4 + 1] = (uint8_t)((c >> 5 & 31) * 255 / 31);
        o[i * 4 + 2] = (uint8_t)((c >> 10 & 31) * 255 / 31);
        o[i * 4 + 3] = c & 0x8000 ? 255 : 0;
    }
    return o;
}

int plat_tex_create16(int w, int h, const uint16_t *px)
{
    uint8_t *o = rgba_from16(w, h, px);
    int id = plat_tex_create(w, h, o);
    free(o);
    return id;
}

void plat_tex_update16(int id, const uint16_t *px)
{
    GLint w, h;
    glBindTexture(GL_TEXTURE_2D, id);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    uint8_t *o = rgba_from16(w, h, px);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, o);
    free(o);
}

#define MAX_WRAP 4096
static struct { uint16_t *px; int w, h; } wraps[MAX_WRAP];

int plat_tex_wrap565(int w, int h, uint16_t *px)
{
    int id = plat_tex_create(w, h, NULL);
    if (id > 0 && id < MAX_WRAP) { wraps[id].px = px; wraps[id].w = w; wraps[id].h = h; }
    plat_tex_sync565(id, 0, 0);
    return id;
}

void plat_tex_before_write(int id) { (void)id; }   /* GL orders texture updates after draws */

void plat_tex_sync565(int id, int key_on, uint16_t key)
{
    if (id <= 0 || id >= MAX_WRAP || !wraps[id].px) return;
    int w = wraps[id].w, h = wraps[id].h;
    uint8_t *o = malloc((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) {
        uint16_t c = wraps[id].px[i];
        o[i * 4] = (uint8_t)((c & 31) * 255 / 31);
        o[i * 4 + 1] = (uint8_t)((c >> 5 & 63) * 255 / 63);
        o[i * 4 + 2] = (uint8_t)((c >> 11) * 255 / 31);
        o[i * 4 + 3] = key_on && c == key ? 0 : 255;
    }
    glBindTexture(GL_TEXTURE_2D, id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, o);
    free(o);
}

void plat_tex_update16_rows(int id, const uint16_t *px, int y0, int y1)
{
    GLint w;
    glBindTexture(GL_TEXTURE_2D, id);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    uint8_t *o = rgba_from16(w, y1 - y0, px + (size_t)y0 * w);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y0, w, y1 - y0, GL_RGBA, GL_UNSIGNED_BYTE, o);
    free(o);
}

void plat_tex_destroy(int id)
{
    if (id > 0 && id < MAX_WRAP) wraps[id].px = NULL;
    GLuint t = id;
    if (t) glDeleteTextures(1, &t);
}

/* ---------- rendering ---------- */

void plat_frame_begin(uint32_t clear_rgb)
{
    int w, h;
    SDL_GL_GetDrawableSize(win, &w, &h);
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(vp_x, vp_y, vp_w, vp_h);
    glScissor(vp_x, vp_y, vp_w, vp_h);
    glEnable(GL_SCISSOR_TEST);
    glClearColor(((clear_rgb >> 16) & 255) / 255.f, ((clear_rgb >> 8) & 255) / 255.f, (clear_rgb & 255) / 255.f, 1);
    glDepthMask(GL_TRUE);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.0f);
    glFrontFace(GL_CW);
    glCullFace(GL_BACK);
}

static int frame_no;

/* KOK_SHOT=<prefix>:f1,f2,... dumps the game area as <prefix>_<frame>.ppm at those frames;
 * KOK_SHOT_EXIT=1 quits after the last one (automated checks). */
static void maybe_screenshot(void)
{
    const char *spec = getenv("KOK_SHOT");
    if (!spec) return;
    char buf[512];
    snprintf(buf, sizeof buf, "%s", spec);
    char *colon = strrchr(buf, ':');
    if (!colon) return;
    *colon = 0;
    int last = 0, hit = 0;
    for (char *t = strtok(colon + 1, ","); t; t = strtok(NULL, ",")) {
        int f = atoi(t);
        if (f > last) last = f;
        if (f == frame_no) hit = 1;
    }
    if (hit) {
        char path[600];
        snprintf(path, sizeof path, "%s_%d.ppm", buf, frame_no);
        uint8_t *px = malloc((size_t)vp_w * vp_h * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(vp_x, vp_y, vp_w, vp_h, GL_RGB, GL_UNSIGNED_BYTE, px);
        FILE *f = fopen(path, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", vp_w, vp_h);
            for (int y = vp_h - 1; y >= 0; y--) fwrite(px + (size_t)y * vp_w * 3, 1, (size_t)vp_w * 3, f);
            fclose(f);
        }
        free(px);
    }
    if (frame_no >= last && getenv("KOK_SHOT_EXIT")) exit(0);
}

/* KOK_KEYS="frame:+VK,frame:-VK,..." scripted key presses (VK as letter/digit or hex 0xNN) */
static void scripted_keys(void)
{
    const char *spec = getenv("KOK_KEYS");
    if (!spec) return;
    char buf[2048];
    snprintf(buf, sizeof buf, "%s", spec);
    for (char *t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
        char *c = strchr(t, ':');
        if (!c || atoi(t) != frame_no) continue;
        int down = c[1] == '+';
        const char *k = c + 2;
        int vk = strlen(k) == 1 ? (unsigned char)k[0] : (int)strtol(k, NULL, 0);
        keys[vk & 0xff] = down ? 0x80 : 0;
    }
}

void plat_frame_end(void)
{
    maybe_screenshot();
    SDL_GL_SwapWindow(win);
    frame_no++;
    scripted_keys();
}

void plat_clear(int color, uint32_t rgb, int depth)
{
    GLbitfield bits = 0;
    if (color) {
        glClearColor(((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, 1);
        bits |= GL_COLOR_BUFFER_BIT;
    }
    if (depth) { glDepthMask(GL_TRUE); bits |= GL_DEPTH_BUFFER_BIT; }
    if (bits) glClear(bits);
}

void plat_set_projection(float sx, float sy, float n, float f)
{
    memset(proj, 0, sizeof proj);
    proj[0] = sx * SCREEN_W / virt_w;   /* widescreen: same vertical field, wider horizontal */
    proj[5] = sy;
    proj[10] = (f + n) / (f - n);
    proj[11] = 1.0f;
    proj[14] = -2.0f * f * n / (f - n);
}

void plat_set_modelview(const float m[16])
{
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(m);
}

void plat_set_fog(int enable, float start, float end, uint32_t rgb)
{
    if (!enable) { glDisable(GL_FOG); return; }
    GLfloat c[4] = { ((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, 1 };
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, start);
    glFogf(GL_FOG_END, end);
    glFogfv(GL_FOG_COLOR, c);
}

static void set_blend(int blend)
{
    switch (blend) {
    case BLEND_NONE: glDisable(GL_BLEND); return;
    case BLEND_ALPHA: glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
    case BLEND_ADD: glBlendFunc(GL_ONE, GL_ONE); break;
    case BLEND_MUL: glBlendFunc(GL_ZERO, GL_SRC_COLOR); break;
    case BLEND_ADD_ALPHA: glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;
    }
    glEnable(GL_BLEND);
}

static void submit(int tex, const pvert_t *v, int n)
{
    static uint8_t *cols;
    static int ncols;
    if (n > ncols) { ncols = n * 2; cols = realloc(cols, ncols * 4); }
    for (int i = 0; i < n; i++) {
        uint32_t c = v[i].color;
        cols[i * 4] = c >> 16; cols[i * 4 + 1] = c >> 8; cols[i * 4 + 2] = c; cols[i * 4 + 3] = c >> 24;
    }
    if (tex) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex); }
    else glDisable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(pvert_t), &v->x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(pvert_t), &v->u);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, cols);
    glDrawArrays(GL_TRIANGLES, 0, n);
}

static int cull_on = 1;
void plat_set_cull(int on) { cull_on = on; }

void plat_draw_tris(int tex, const pvert_t *v, int n, int blend, int zwrite, int ztest)
{
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj);
    if (cull_on) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (ztest) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); } else glDisable(GL_DEPTH_TEST);
    glDepthMask(zwrite ? GL_TRUE : GL_FALSE);
    set_blend(blend);
    submit(tex, v, n);
}

void plat_draw_tris_static(int tex, const pvert_t *v, int n, void **cache, const float sphere[4],
                           int blend, int zwrite, int ztest)
{
    (void)cache; (void)sphere;
    plat_draw_tris(tex, v, n, blend, zwrite, ztest);
}

void plat_static_free(void *cache) { (void)cache; }

void plat_draw_tris_2d(int tex, const pvert_t *v, int n, int blend, int flags)
{
    int linear = flags & DRAW2D_LINEAR;
    if (flags & DRAW2D_OPAQUE) glDisable(GL_ALPHA_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, virt_w, SCREEN_H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glDepthMask(GL_FALSE);
    set_blend(blend);
    if (tex) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    }
    submit(tex, v, n);
    glEnable(GL_ALPHA_TEST);
    if (tex && linear) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
}

int plat_capture(float *umax, float *vmax)
{
    static GLuint tex;
    static int tw, th;
    int w = vp_w, h = vp_h, pw = 1, ph = 1;
    while (pw < w) pw <<= 1;
    while (ph < h) ph <<= 1;
    if (!tex || pw != tw || ph != th) {
        if (!tex) glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pw, ph, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        tw = pw; th = ph;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp_x, vp_y, w, h);
    /* GL rows are bottom-up: callers flip v */
    *umax = (float)w / pw;
    *vmax = -(float)h / ph;
    return (int)tex;
}

/* ---------- audio ---------- */

int plat_audio_start(plat_audio_cb cb)
{
    SDL_AudioSpec want = { 0 }, have;
    audio_cb = cb;
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_trampoline;
    audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!audio_dev) { plat_log("audio: %s", SDL_GetError()); return 0; }
    SDL_PauseAudioDevice(audio_dev, 0);
    return 1;
}

void plat_audio_lock(void) { if (audio_dev) SDL_LockAudioDevice(audio_dev); }
void plat_audio_unlock(void) { if (audio_dev) SDL_UnlockAudioDevice(audio_dev); }
