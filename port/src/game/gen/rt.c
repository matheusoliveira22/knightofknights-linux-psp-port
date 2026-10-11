/* Runtime for the translated game code: VM stack, Win32/GDI shims and COM shims onto rm/dx. */
#include "vmc.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* ================================================================== VM stack */

uint32_t vm_sp = VM_STACK_BASE;

uint32_t vm_salloc(uint32_t n)
{
    n = (n + 7u) & ~7u;
    if (vm_sp + n > VM_STACK_BASE + VM_STACK_SIZE) vm_fatal("VM stack overflow");
    uint32_t a = vm_sp;
    memset(vm_stack + (a - VM_STACK_BASE), 0, n);
    vm_sp += n;
    return a;
}

/* ================================================================== strings */

static char *H(uint32_t va) { return (char *)VA(va); }

int w_wsprintfA(uint32_t dst, uint32_t fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const char *f = H(fmt);
    char out[1100], spec[32];
    int o = 0;
    while (*f && o < 1023) {   /* wsprintf output is at most 1024 bytes including the NUL */
        if (*f != '%') { out[o++] = *f++; continue; }
        int k = 0;
        spec[k++] = *f++;
        while (*f && strchr("-+ #0123456789.lh", *f) && k < 28) spec[k++] = *f++;
        char c = *f ? *f++ : 0;
        spec[k++] = c;
        spec[k] = 0;
        char tmp[512];
        if (c == '%') { out[o++] = '%'; continue; }
        if (c == 's') { uint32_t a = va_arg(ap, uint32_t); snprintf(tmp, sizeof tmp, spec, a ? H(a) : "(null)"); }
        else if (c == 'c') snprintf(tmp, sizeof tmp, spec, (int)va_arg(ap, uint32_t));
        else {
            /* drop size modifiers: all wsprintf integer args are 32-bit */
            char sp2[32];
            int j = 0;
            for (int i = 0; spec[i]; i++) if (spec[i] != 'l' && spec[i] != 'h') sp2[j++] = spec[i];
            sp2[j] = 0;
            snprintf(tmp, sizeof tmp, sp2, va_arg(ap, uint32_t));
        }
        for (char *t = tmp; *t && o < 1023; t++) out[o++] = *t;
    }
    out[o] = 0;
    va_end(ap);
    memcpy(H(dst), out, o + 1);
    return o;
}

uint32_t w_lstrcatA(uint32_t d, uint32_t s) { strcat(H(d), H(s)); return d; }
int32_t w_lstrlenA(uint32_t s) { return s ? (int32_t)strlen(H(s)) : 0; }
uint32_t w_lstrcpyA(uint32_t d, uint32_t s) { memmove(H(d), H(s), strlen(H(s)) + 1); return d; }
int32_t w_strcmp(uint32_t a, uint32_t b) { return strcmp(H(a), H(b)); }
int32_t w_strncmp(uint32_t a, uint32_t b, uint32_t n) { return strncmp(H(a), H(b), n); }
uint32_t w_strncpy(uint32_t d, uint32_t s, uint32_t n) { strncpy(H(d), H(s), n); return d; }
uint32_t w_memset(uint32_t d, int c, uint32_t n) { memset(VA(d), c, n); return d; }
uint32_t w_memcpy(uint32_t d, uint32_t s, uint32_t n) { memmove(VA(d), VA(s), n); return d; }
uint32_t w_strchr(uint32_t s, int c) { char *p = strchr(H(s), c); return p ? s + (uint32_t)(p - H(s)) : 0; }
uint32_t w_strstr(uint32_t s, uint32_t t) { char *p = strstr(H(s), H(t)); return p ? s + (uint32_t)(p - H(s)) : 0; }

int32_t w_SetRect(uint32_t r, int32_t l, int32_t t, int32_t rr, int32_t b)
{
    M32(r) = l; M32(r + 4) = t; M32(r + 8) = rr; M32(r + 12) = b;
    return 1;
}

void game_bot(uint8_t *k);

int32_t w_GetKeyboardState(uint32_t buf)
{
    plat_keyboard_state((uint8_t *)VA(buf));
    game_bot((uint8_t *)VA(buf));
    return 1;
}

int32_t w_MessageBoxA(uint32_t hwnd, uint32_t text, uint32_t cap, uint32_t flags)
{
    (void)hwnd; (void)flags;
    plat_log("MessageBox: [%s] %s", cap ? H(cap) : "", text ? H(text) : "");
    return 1;
}

uint32_t w_joyGetPosEx(uint32_t id, uint32_t info)
{
    (void)id;
    uint32_t x = 32767, y = 32767, b = 0;
    int ok = plat_joystick(&x, &y, &b);
    MU32(info + 0x08) = x;   /* dwXpos */
    MU32(info + 0x0c) = y;   /* dwYpos */
    MU32(info + 0x20) = b;   /* dwButtons */
    return ok ? 0 : 167;     /* JOYERR_UNPLUGGED */
}

void w_Sleep(uint32_t ms) { plat_sleep_ms((int)ms); }

float w_D3DRMVectorDotProduct(uint32_t a, uint32_t b)
{
    return MF(a) * MF(b) + MF(a + 4) * MF(b + 4) + MF(a + 8) * MF(b + 8);
}

float w_D3DRMVectorModulus(uint32_t a) { return sqrtf(w_D3DRMVectorDotProduct(a, a)); }

uint32_t w_D3DRMVectorNormalize(uint32_t a)
{
    float l = w_D3DRMVectorModulus(a);
    if (l == 0) { MF(a) = 1; MF(a + 4) = 0; MF(a + 8) = 0; return a; }
    MF(a) /= l; MF(a + 4) /= l; MF(a + 8) /= l;
    return a;
}

/* x87 helper at 0x444fb0 (CRT): see re/notes */
double w_x87_444fb0(double x) { return acos(x); }

/* ================================================================== GDI on surface DCs */

static struct { uint32_t hdc, font, color, bk; int mode; } dcs[8];

static int dc(uint32_t hdc)
{
    for (int i = 0; i < 8; i++) if (dcs[i].hdc == hdc) return i;
    for (int i = 0; i < 8; i++) if (!dcs[i].hdc) { dcs[i].hdc = hdc; dcs[i].font = FONT_32; dcs[i].mode = 2; return i; }
    return 0;
}

static uint32_t bgr2rgb(uint32_t c) { return (c & 0xff) << 16 | (c & 0xff00) | (c >> 16 & 0xff); }

uint32_t w_SelectObject(uint32_t h, uint32_t font) { int i = dc(h); uint32_t o = dcs[i].font; dcs[i].font = font; return o; }
uint32_t w_SetTextColor(uint32_t h, uint32_t c) { int i = dc(h); uint32_t o = dcs[i].color; dcs[i].color = c; return o; }
uint32_t w_SetBkColor(uint32_t h, uint32_t c) { int i = dc(h); uint32_t o = dcs[i].bk; dcs[i].bk = c; return o; }
int32_t w_SetBkMode(uint32_t h, int32_t m) { int i = dc(h); int o = dcs[i].mode; dcs[i].mode = m; return o; }

int32_t w_DrawTextA(uint32_t h, uint32_t str, int32_t n, uint32_t rect, uint32_t flags)
{
    (void)n; (void)flags;
    int i = dc(h);
    rect_t r = { M32(rect), M32(rect + 4), M32(rect + 8), M32(rect + 12) };
    if (h == DD_BACK) dx_frame_begin();
    dx_text(h, &r, H(str), (int)dcs[i].font, bgr2rgb(dcs[i].color), dcs[i].mode == 2, bgr2rgb(dcs[i].bk));
    return 1;
}

int32_t w_TextOutA(uint32_t h, int32_t x, int32_t y, uint32_t str, int32_t n)
{
    int i = dc(h);
    char buf[512];
    if (n < 0) return 0;
    if (n > 511) n = 511;
    const char *src = H(str);
    int len = 0;
    while (len < n && src[len]) len++;
    memcpy(buf, src, len);
    buf[len] = 0;
    if (h == DD_BACK) dx_frame_begin();
    dx_text_out(h, x, y, buf, (int)dcs[i].font, bgr2rgb(dcs[i].color), dcs[i].mode == 2, bgr2rgb(dcs[i].bk));
    return 1;
}

/* ================================================================== frames */

static int in_frame;

void dx_frame_begin(void)
{
    if (in_frame) return;
    in_frame = 1;
    plat_frame_begin(0);
}

void dx_present(void)
{
    if (!in_frame) return;
    in_frame = 0;
    plat_frame_end();
}

static void im_draw_lit(uint32_t prim, uint32_t verts, uint32_t n);

/* ================================================================== COM shims */

#define IS_SURF(h) (((h) & 0xff000000u) == 0x6e000000u)
#define IS_RM(h)   (((h) & 0xff000000u) == 0x70000000u)
#define IS_ARR(h)  (((h) & 0xff000000u) == 0x6a000000u)
#define IS_MAT(h)  (((h) & 0xffffff00u) == 0x69000000u)
static void material_release(uint32_t m);

/* IDirect3DRMFrameArray / VisualArray */
static struct { rmh *v; int n, used; } arrs[16];

static uint32_t arr_new(void)
{
    for (int i = 0; i < 16; i++)
        if (!arrs[i].used) { arrs[i].used = 1; arrs[i].n = 0; return 0x6a000000u + i; }
    vm_fatal("too many RM arrays");
    return 0;
}

static void arr_push(uint32_t a, rmh v)
{
    int i = a & 0xff;
    arrs[i].v = realloc(arrs[i].v, (arrs[i].n + 1) * sizeof(rmh));
    arrs[i].v[arrs[i].n++] = v;
}

uint32_t COM_Unk_Release(uint32_t o)
{
    if (IS_RM(o)) rm_release(o);
    else if (IS_SURF(o)) dd_release(o);
    else if (IS_ARR(o)) arrs[o & 0xff].used = 0;
    else if (IS_MAT(o)) material_release(o);
    return 0;
}

uint32_t COM_Unk_AddRef(uint32_t o) { if (IS_RM(o)) rm_addref(o); else if (IS_SURF(o)) dd_addref(o); return 1; }

uint32_t COM_Unk_QueryInterface(uint32_t o, uint32_t iid, uint32_t out)
{
    (void)iid;
    /* only used to get IDirect3DTexture2 from a surface and FrameN from frames: same handle,
     * holding its own reference like the COM interface it stands for */
    if (IS_RM(o)) rm_addref(o);
    else if (IS_SURF(o)) dd_addref(o);
    MU32(out) = o;
    return 0;
}

uint32_t COM_RM2_CreateFrame(uint32_t rm, uint32_t parent, uint32_t out) { (void)rm; MU32(out) = rm_frame_create(parent); return 0; }
uint32_t COM_RM2_CreateMeshBuilder(uint32_t rm, uint32_t out) { (void)rm; MU32(out) = rm_mb_create(); return 0; }
uint32_t COM_RM2_CreateAnimationSet(uint32_t rm, uint32_t out) { (void)rm; MU32(out) = rm_animset_create(); return 0; }

uint32_t COM_RM2_GetNamedObject(uint32_t rm, uint32_t name, uint32_t out)
{
    (void)rm;
    rmh h = rm_named_object(H(name));
    if (h) rm_addref(h);
    MU32(out) = h;
    return h ? 0 : 0x88760000u + 781; /* D3DRMERR_NOTFOUND */
}

typedef struct { uint32_t cb, arg; } uvis_ctx;

static void uvis_trampoline(void *p, const mat4 *world, const mat4 *view)
{
    uvis_ctx *c = p;
    extern void game_uservisual(uint32_t cb, uint32_t arg, const mat4 *world, const mat4 *view);
    game_uservisual(c->cb, c->arg, world, view);
}

uint32_t COM_RM2_CreateUserVisual(uint32_t rm, uint32_t cb, uint32_t arg, uint32_t out)
{
    (void)rm;
    uvis_ctx *c = malloc(sizeof *c);
    c->cb = cb;
    c->arg = arg;
    MU32(out) = rm_uservisual_create(uvis_trampoline, c);
    return 0;
}

uint32_t COM_Frame2_SetPosition(uint32_t f, uint32_t ref, uint32_t x, uint32_t y, uint32_t z)
{
    rm_frame_set_position(f, ref, BITF(x), BITF(y), BITF(z));
    return 0;
}

uint32_t COM_Frame2_GetPosition(uint32_t f, uint32_t ref, uint32_t out)
{
    vec3 v;
    rm_frame_get_position(f, ref, &v);
    MF(out) = v.x; MF(out + 4) = v.y; MF(out + 8) = v.z;
    return 0;
}

uint32_t COM_Frame2_SetOrientation(uint32_t f, uint32_t ref, uint32_t dx, uint32_t dy, uint32_t dz,
                                   uint32_t ux, uint32_t uy, uint32_t uz)
{
    rm_frame_set_orientation(f, ref, BITF(dx), BITF(dy), BITF(dz), BITF(ux), BITF(uy), BITF(uz));
    return 0;
}

uint32_t COM_Frame2_SetRotation(uint32_t f, uint32_t ref, uint32_t x, uint32_t y, uint32_t z, uint32_t th)
{
    rm_frame_set_rotation(f, ref, BITF(x), BITF(y), BITF(z), BITF(th));
    return 0;
}

uint32_t COM_Frame2_AddScale(uint32_t f, uint32_t how, uint32_t x, uint32_t y, uint32_t z)
{
    rm_frame_add_scale(f, (int)how, BITF(x), BITF(y), BITF(z));
    return 0;
}

uint32_t COM_Frame2_AddRotation(uint32_t f, uint32_t how, uint32_t x, uint32_t y, uint32_t z, uint32_t th)
{
    rm_frame_add_rotation(f, (int)how, BITF(x), BITF(y), BITF(z), BITF(th));
    return 0;
}

uint32_t COM_Frame2_LookAt(uint32_t f, uint32_t t, uint32_t ref, uint32_t c) { rm_frame_lookat(f, t, ref, (int)c); return 0; }
uint32_t COM_Frame2_AddChild(uint32_t f, uint32_t c) { rm_frame_add_child(f, c); return 0; }

static void destroy_tramp(rmh obj, uint32_t cb, uint32_t arg) { CALLVA(cb)(obj, arg); }

uint32_t COM_Frame2_AddDestroyCallback(uint32_t f, uint32_t cb, uint32_t arg)
{
    rm_add_destroy_callback(f, destroy_tramp, cb, arg);
    return 0;
}
uint32_t COM_Frame2_DeleteChild(uint32_t f, uint32_t c) { rm_frame_delete_child(f, c); return 0; }
uint32_t COM_Frame2_AddVisual(uint32_t f, uint32_t v) { rm_frame_add_visual(f, v); return 0; }
uint32_t COM_Frame2_DeleteVisual(uint32_t f, uint32_t v) { rm_frame_delete_visual(f, v); return 0; }

uint32_t COM_Frame2_GetChildren(uint32_t f, uint32_t out)
{
    uint32_t a = arr_new();
    for (int i = 0; i < rm_frame_child_count(f); i++) arr_push(a, rm_frame_child(f, i));
    MU32(out) = a;
    return 0;
}

uint32_t COM_Frame2_GetVisuals(uint32_t f, uint32_t out)
{
    uint32_t a = arr_new();
    for (int i = 0; i < rm_frame_visual_count(f); i++) arr_push(a, rm_frame_visual(f, i));
    MU32(out) = a;
    return 0;
}

uint32_t COM_Frame2_GetParent(uint32_t f, uint32_t out)
{
    rmh p = rm_frame_parent(f);
    if (p) rm_addref(p);
    MU32(out) = p;
    return 0;
}

/* the state handlers call Move(scene) once at the end of every logic tick: time the ticks from the
 * start of the handler (prof_tick_t0, set by game_frame) for the perf log */
uint64_t prof_tick_t0;
uint32_t COM_Frame2_Move(uint32_t f, uint32_t delta)
{
    extern uint64_t prof_us[16];
    rm_frame_move(f, BITF(delta));
    uint64_t t = plat_time_us();
    prof_us[5] += t - prof_tick_t0;
    prof_us[6]++;
    prof_tick_t0 = t;
    return 0;
}

uint32_t COM_Frame2_Transform(uint32_t f, uint32_t dst, uint32_t src)
{
    vec3 s = { MF(src), MF(src + 4), MF(src + 8) }, d;
    rm_frame_transform(f, &d, &s);
    MF(dst) = d.x; MF(dst + 4) = d.y; MF(dst + 8) = d.z;
    return 0;
}

uint32_t COM_Frame2_InverseTransform(uint32_t f, uint32_t dst, uint32_t src)
{
    vec3 s = { MF(src), MF(src + 4), MF(src + 8) }, d;
    rm_frame_inverse_transform(f, &d, &s);
    MF(dst) = d.x; MF(dst + 4) = d.y; MF(dst + 8) = d.z;
    return 0;
}

uint32_t COM_Frame2_SetSceneBackground(uint32_t f, uint32_t c) { rm_scene_set_background(f, c); return 0; }
uint32_t COM_Frame2_SetSceneFogEnable(uint32_t f, uint32_t on) { rm_scene_set_fog_enable(f, (int)on); return 0; }
uint32_t COM_Frame2_SetSceneFogParams(uint32_t f, uint32_t a, uint32_t b, uint32_t d)
{
    rm_scene_set_fog_params(f, BITF(a), BITF(b), BITF(d));
    return 0;
}
uint32_t COM_Frame2_SetSceneFogColor(uint32_t f, uint32_t c) { rm_scene_set_fog_color(f, c); return 0; }

uint32_t COM_Frame2_GetName(uint32_t o, uint32_t size_va, uint32_t buf)
{
    const char *n = rm_get_name(o);
    uint32_t cap = MU32(size_va);
    if (buf && cap) { strncpy(H(buf), n, cap - 1); H(buf)[cap - 1] = 0; }
    MU32(size_va) = (uint32_t)strlen(n) + 1;
    return 0;
}

uint32_t COM_Frame2_SetName(uint32_t o, uint32_t name) { rm_set_name(o, H(name)); return 0; }

uint32_t COM_Array_GetSize(uint32_t a) { return IS_ARR(a) ? (uint32_t)arrs[a & 0xff].n : 0; }

uint32_t COM_Array_GetElement(uint32_t a, uint32_t i, uint32_t out)
{
    if (!IS_ARR(a) || (int)i >= arrs[a & 0xff].n) return 1;
    rmh h = arrs[a & 0xff].v[i];
    rm_addref(h);
    MU32(out) = h;
    return 0;
}

uint32_t COM_MeshBuilder2_Load(uint32_t mb, uint32_t file, uint32_t name, uint32_t flags, uint32_t cb, uint32_t arg)
{
    (void)name; (void)flags; (void)cb; (void)arg;
    return rm_mb_load(mb, H(file)) ? 0 : 1;
}

uint32_t COM_MeshBuilder2_SetPerspective(uint32_t mb, uint32_t on) { rm_mb_set_perspective(mb, (int)on); return 0; }
uint32_t COM_MeshBuilder2_SetQuality(uint32_t mb, uint32_t q) { rm_mb_set_quality(mb, q); return 0; }
uint32_t COM_MeshBuilder2_Scale(uint32_t mb, uint32_t x, uint32_t y, uint32_t z) { rm_mb_scale(mb, BITF(x), BITF(y), BITF(z)); return 0; }
uint32_t COM_MeshBuilder2_SetColor(uint32_t mb, uint32_t c) { rm_mb_set_color(mb, c); return 0; }
uint32_t COM_MeshBuilder2_SetColorRGB(uint32_t mb, uint32_t r, uint32_t g, uint32_t b)
{
    rm_mb_set_color_rgb(mb, BITF(r), BITF(g), BITF(b));
    return 0;
}
uint32_t COM_MeshBuilder2_SetTexture(uint32_t mb, uint32_t t) { rm_mb_set_texture(mb, t); return 0; }
uint32_t COM_MeshBuilder2_GetFace(uint32_t mb, uint32_t i, uint32_t out) { MU32(out) = rm_mb_face_handle(mb, (int)i); return 0; }
uint32_t COM_MeshBuilder2_GetFaceCount(uint32_t mb) { return (uint32_t)rm_mb_face_count(mb); }

uint32_t COM_AnimSet_Load(uint32_t as, uint32_t file, uint32_t name, uint32_t flags, uint32_t cb, uint32_t arg,
                          uint32_t parent)
{
    (void)name; (void)flags; (void)cb; (void)arg;
    return rm_animset_load(as, H(file), parent) ? 0 : 1;
}

uint32_t COM_AnimSet_SetTime(uint32_t as, uint32_t t) { rm_animset_set_time(as, BITF(t)); return 0; }

uint32_t COM_Viewport_Clear(uint32_t vp)
{
    (void)vp;
    dx_frame_begin();
    rm_viewport_clear(MU32(0x4a388c));
    return 0;
}

uint32_t COM_Viewport_Render(uint32_t vp, uint32_t scene) { (void)vp; dx_frame_begin(); rm_viewport_render(scene); return 0; }
uint32_t COM_Viewport_SetFront(uint32_t vp, uint32_t v) { (void)vp; rm_viewport_set_front(BITF(v)); return 0; }
uint32_t COM_Viewport_SetBack(uint32_t vp, uint32_t v) { (void)vp; rm_viewport_set_back(BITF(v)); return 0; }
uint32_t COM_Viewport_SetField(uint32_t vp, uint32_t v) { (void)vp; rm_viewport_set_field(BITF(v)); return 0; }
uint32_t COM_Device2_Update(uint32_t d) { (void)d; return 0; }

uint32_t COM_DDSurf_Blt(uint32_t dst, uint32_t dr, uint32_t src, uint32_t sr, uint32_t flags, uint32_t fx)
{
    rect_t d, s;
    if (dst == DD_PRIMARY) { dx_present(); return 0; }
    if (dst == DD_BACK) dx_frame_begin();
    if (dr) d = (rect_t){ M32(dr), M32(dr + 4), M32(dr + 8), M32(dr + 12) };
    if (sr) s = (rect_t){ M32(sr), M32(sr + 4), M32(sr + 8), M32(sr + 12) };
    if (flags & DDBLT_COLORFILL) {
        uint32_t c = fx ? MU32(fx + 0x50) : 0;    /* DDBLTFX.dwFillColor (RGB565) */
        uint32_t rgb = ((c >> 11 & 31) * 255 / 31) << 16 | ((c >> 5 & 63) * 255 / 63) << 8 | (c & 31) * 255 / 31;
        dd_fill(dst, dr ? &d : NULL, rgb);
        return 0;
    }
    dd_blt(dst, dr ? &d : NULL, src, sr ? &s : NULL, flags);
    return 0;
}

/* Lock exposes the surface as RGB565 in VM memory (DDSURFACEDESC.lPitch at +0x10, lpSurface at +0x24). */
static struct { uint32_t surf, buf; } locks[4];

uint32_t COM_DDSurf_Lock(uint32_t s, uint32_t rect, uint32_t desc, uint32_t flags, uint32_t ev)
{
    (void)rect; (void)flags; (void)ev;
    int w = dd_width(s), h = dd_height(s);
    if (!w || s == DD_BACK || s == DD_PRIMARY) return 1;
    int slot = 0;
    while (slot < 3 && locks[slot].surf && locks[slot].surf != s) slot++;
    uint32_t buf = vm_malloc((uint32_t)(w * h * 2));
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) MU16(buf + (uint32_t)(y * w + x) * 2) = dd_get565(s, x, y);
    locks[slot].surf = s;
    locks[slot].buf = buf;
    MU32(desc + 0x08) = (uint32_t)h;
    MU32(desc + 0x0c) = (uint32_t)w;
    M32(desc + 0x10) = w * 2;
    MU32(desc + 0x24) = buf;
    return 0;
}

uint32_t COM_DDSurf_Unlock(uint32_t s, uint32_t ptr)
{
    (void)ptr;
    for (int i = 0; i < 4; i++) {
        if (locks[i].surf != s) continue;
        int w = dd_width(s), h = dd_height(s);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) dd_put565(s, x, y, MU16(locks[i].buf + (uint32_t)(y * w + x) * 2));
        vm_free(locks[i].buf);
        locks[i].surf = 0;
        return 0;
    }
    return 0;
}

uint32_t COM_DDSurf_GetDC(uint32_t s, uint32_t out) { MU32(out) = s; return 0; }
uint32_t COM_DDSurf_ReleaseDC(uint32_t s, uint32_t hdc) { (void)s; (void)hdc; return 0; }

uint32_t COM_D3DDev2_SetRenderState(uint32_t d, uint32_t st, uint32_t v) { (void)d; im_set_render_state((int)st, v); return 0; }
uint32_t COM_D3DDev2_BeginScene(uint32_t d) { (void)d; dx_frame_begin(); return 0; }
uint32_t COM_D3DDev2_EndScene(uint32_t d) { (void)d; return 0; }

uint32_t COM_D3DDev2_DrawPrimitive(uint32_t d, uint32_t prim, uint32_t vt, uint32_t verts, uint32_t n, uint32_t fl)
{
    (void)d; (void)fl;
    dx_frame_begin();
    if (vt == 1) { im_draw_lit(prim, verts, n); return 0; }
    im_draw((int)prim, (const tlvertex_t *)VA(verts), (int)n);
    return 0;
}

uint32_t COM_Texture2_GetHandle(uint32_t t, uint32_t dev, uint32_t out) { (void)dev; MU32(out) = t; return 0; }

/* ================================================================== IM materials / 3D vertices */

static uint8_t materials[256][0x50];
static int nmaterials = 1;
static uint8_t material_used[256];
static uint32_t cur_material;
static mat4 uv_world, uv_view;

#define MAT_BASE 0x69000000u

uint32_t COM_D3D2_CreateMaterial(uint32_t d3d, uint32_t out, uint32_t unk)
{
    (void)d3d; (void)unk;
    /* one per effect particle, released with it: reuse free slots so live particles keep theirs */
    int i;
    for (i = 0; i < 255 && material_used[nmaterials]; i++) nmaterials = nmaterials % 255 + 1;
    if (i == 255) plat_log("material slots exhausted");
    material_used[nmaterials] = 1;
    memset(materials[nmaterials], 0, 0x50);
    MU32(out) = MAT_BASE + (uint32_t)nmaterials;
    nmaterials = nmaterials % 255 + 1;
    return 0;
}

static void material_release(uint32_t m) { material_used[m & 0xff] = 0; }

uint32_t COM_Material2_SetMaterial(uint32_t m, uint32_t mat)
{
    if ((m & 0xff000000u) == MAT_BASE) memcpy(materials[m & 0xff], VA(mat), 0x50);
    return 0;
}

uint32_t COM_Material2_GetHandle(uint32_t m, uint32_t dev, uint32_t out) { (void)dev; MU32(out) = m; return 0; }

uint32_t COM_D3DDev2_SetLightState(uint32_t d, uint32_t st, uint32_t v)
{
    (void)d;
    if (st == 1) cur_material = v;   /* D3DLIGHTSTATE_MATERIAL */
    return 0;
}

void im_set_uv_transform(const mat4 *world, const mat4 *view) { uv_world = *world; uv_view = *view; }

/* DrawPrimitive with D3DVT_VERTEX (x,y,z,nx,ny,nz,tu,tv): lit by the current material with no lights,
 * i.e. colour = emissive, alpha = diffuse alpha. */
static void im_draw_lit(uint32_t prim, uint32_t verts, uint32_t n)
{
    static pvert_t buf[3072];
    if (prim != 4 || n > 3072) return;
    float e[4] = { 0, 0, 0, 0 };
    if ((cur_material & 0xff000000u) == MAT_BASE) {
        const uint8_t *m = materials[cur_material & 0xff];
        memcpy(&e[3], m + 0x10, 4);      /* diffuse a */
        memcpy(e, m + 0x34, 12);         /* emissive rgb */
    }
#define C8(v) ((uint32_t)((v) < 0 ? 0 : (v) > 1 ? 255 : (v) * 255.0f + 0.5f))
    uint32_t col = C8(e[3]) << 24 | C8(e[0]) << 16 | C8(e[1]) << 8 | C8(e[2]);
    float um = 1, vm = 1;
    int tex = dd_texture(im_get_render_state(1), &um, &vm);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t v = verts + i * 32;
        buf[i] = (pvert_t){ MF(v), MF(v + 4), MF(v + 8), MF(v + 24) * um, MF(v + 28) * vm, col };
    }
    mat4 mv;
    mat_mul(&mv, &uv_world, &uv_view);
    plat_set_modelview((const float *)&mv);
    plat_set_fog(0, 0, 0, 0);
    int blend = BLEND_NONE;
    if (im_get_render_state(27)) {
        uint32_t sb = im_get_render_state(19), db = im_get_render_state(20);
        blend = sb == 2 && db == 2 ? BLEND_ADD : sb == 1 && db == 3 ? BLEND_MUL : BLEND_ALPHA;
    }
    /* The game never enables D3D alpha testing: only the colour key (texture alpha) drops texels.
     * The backends alpha-test the modulated alpha, so blend modes that ignore alpha must not let
     * the material's diffuse alpha (0 for the additive effects) discard the whole effect. */
    if (blend != BLEND_ALPHA)
        for (uint32_t i = 0; i < n; i++) buf[i].color |= 0xff000000u;
    int cull_none = im_get_render_state(22) == 1;   /* D3DCULL_NONE */
    if (cull_none) plat_set_cull(0);
    plat_draw_tris(tex, buf, (int)n, blend, im_get_render_state(14) != 0, im_get_render_state(7) != 0);
    if (cull_none) plat_set_cull(1);
}
