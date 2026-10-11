/* Hand-ported effect helpers that Ghidra could not decompile cleanly. */
#include "gen/vmc.h"

/* FUN_00407790(file, out): flatten an effect mesh into D3DVERTEX triangles.
 * out[0] = face count * 3, out[1] -> vertices {x,y,z, nx,ny,nz, u, 1-v} (0x20 bytes each). */
uint32_t f_407790(uint32_t file, uint32_t out)
{
    rmh mb = rm_mb_create();
    rm_mb_load(mb, (const char *)VA(file));
    uint32_t nf = (uint32_t)rm_mb_face_count(mb);
    MU32(out) = nf * 3;
    uint32_t verts = vm_malloc(nf * 3 * 0x20);
    MU32(out + 4) = verts;
    uint32_t o = verts;
    for (uint32_t f = 0; f < nf; f++) {
        vec3 p[16];
        float uv[32];
        int n = rm_mb_face_vertices(mb, (int)f, p, 16);
        rm_mb_face_uvs(mb, (int)f, uv, 16);
        for (int i = 0; i < n && i < 3; i++, o += 0x20) {
            MF(o) = p[i].x; MF(o + 4) = p[i].y; MF(o + 8) = p[i].z;
            MF(o + 12) = 0; MF(o + 16) = 0; MF(o + 20) = 0;
            MF(o + 24) = uv[i * 2];
            MF(o + 28) = MF(0x44d1cc) - uv[i * 2 + 1];
        }
    }
    rm_release(mb);
    return 1;
}

/* FUN_0043e810: load the 16 effect meshes obj/eff/eff00..eff15 into the model table at 0x485eb0 */
uint32_t f_43e810(void)
{
    for (uint32_t i = 0; i < 16; i++) f_407790(0x470a38 - i * 0x10, 0x485eb0 + i * 8);
    return 1;
}

/* Point texture slot 0x27 (0x4a3818 texture / 0x4a381c handle) at the work surface 0x488c64. The
 * original QueryInterfaces an IDirect3DTexture2 there, which holds its own reference on the
 * surface (released by the fades and by FUN_0042d310's slot cleanup). */
static void fade_tex_bind(void)
{
    uint32_t s = MU32(0x488c64);
    if (MU32(0x4a3818) != s) {
        if (MU32(0x4a3818)) dd_release(MU32(0x4a3818));
        dd_addref(s);
    }
    MU32(0x4a3818) = s;
    MU32(0x4a381c) = s;
}

/* FUN_004390c0: (re)create the two 256x256 work surfaces; 0x488c64 is cleared to black and becomes
 * the IDirect3DTexture2 in slot 0x27 (0x4a3818 texture / 0x4a381c handle) used by fades. */
void f_4390c0(void)
{
    if (MU32(0x488c60)) { dd_release(MU32(0x488c60)); MU32(0x488c60) = 0; }
    if (MU32(0x488c64)) { dd_release(MU32(0x488c64)); MU32(0x488c64) = 0; }
    MU32(0x488c60) = dd_create_surface(256, 256);
    MU32(0x488c64) = dd_create_surface(256, 256);
    dd_fill(MU32(0x488c64), NULL, 0);
    fade_tex_bind();
}

/* FUN_00438f80: copy the back buffer (scaled to 256x256) into the fade texture surface */
void f_438f80(void)
{
    dd_capture_back(MU32(0x488c64));
    fade_tex_bind();
}

/* ---------------------------------------------------------------- screen fades (table 0x46ef10)
 * Sprite 99 (0x49ed9c) is a full screen quad textured with slot 0x27 (surface 0x488c64).
 * 0x488c68 = fade mode, 0x488c6c = progress, 0x488c70 = speed (sign = direction). */

#define FADE_PROG MF(0x488c6c)
#define FADE_SPD  MF(0x488c70)
#define SPR99     0x49ed9cu

static void fade_quad_full(void)
{
    MF(SPR99 + 0x20) = (float)M32(0x488c54);   /* v1.x */
    MF(SPR99 + 0x44) = (float)M32(0x488c58);   /* v2.y */
    MF(SPR99 + 0x00) = 0; MF(SPR99 + 0x04) = 0;
    MF(SPR99 + 0x24) = 0; MF(SPR99 + 0x40) = 0;
    MF(SPR99 + 0x60) = MF(SPR99 + 0x20); MF(SPR99 + 0x64) = MF(SPR99 + 0x44);
    MU32(SPR99 + 0x80) = 0x27;   /* texture slot */
    MU32(SPR99 + 0x88) = 0;      /* blend: alpha */
}

static void fade_uv(uint32_t lo, uint32_t hi)
{
    /* tu,tv of the four TL vertices (+0x18,+0x1c in each 0x20 record) */
    MU32(SPR99 + 0x18) = lo; MU32(SPR99 + 0x1c) = lo;
    MU32(SPR99 + 0x38) = hi; MU32(SPR99 + 0x3c) = lo;
    MU32(SPR99 + 0x58) = lo; MU32(SPR99 + 0x5c) = hi;
    MU32(SPR99 + 0x78) = hi; MU32(SPR99 + 0x7c) = hi;
}

/* alpha = progress while fading out (speed > 0), 1 - progress while fading back in; `k` is the
 * fade's alpha scale constant (0x44d38c or 0x44d608) */
static void fade_alpha_k(uint32_t k)
{
    float a = FADE_SPD <= MF(0x44d1e4) ? MF(0x44d1cc) - FADE_PROG : FADE_PROG;
    uint32_t c = (uint32_t)(int32_t)(a * MF(k)) << 24 | 0xffffff;
    MU32(SPR99 + 0x10) = c; MU32(SPR99 + 0x30) = c; MU32(SPR99 + 0x50) = c; MU32(SPR99 + 0x70) = c;
}

static void fade_alpha(void) { fade_alpha_k(0x44d38c); }

/* common tail: advance progress; returns 1 when the fade finished */
static int fade_step(void)
{
    MU8(SPR99 + 0x84) = 1;
    if (FADE_SPD <= MF(0x44d1e4)) FADE_PROG -= FADE_SPD;
    else FADE_PROG += FADE_SPD;
    if (MF(0x44d1cc) < FADE_PROG) {
        FADE_PROG = 1.0f;
        if (FADE_SPD < MF(0x44d1e4)) {
            M32(0x488c68) = 0;
            FADE_PROG = 0;
            FADE_SPD = 0;
            MU8(SPR99 + 0x84) = 0;
            return 1;
        }
    }
    return 0;
}

static void fade_start_solid(uint32_t rgb)
{
    dd_fill(MU32(0x488c64), NULL, rgb);
    fade_tex_bind();
    fade_quad_full();
    fade_uv(0, 0);
    FADE_PROG = 0.01f;
    if (FADE_SPD < MF(0x44d1e4)) {
        MU8(SPR99 + 0x84) = 1;
        uint32_t c = 0xfcffffffu;
        MU32(SPR99 + 0x10) = c; MU32(SPR99 + 0x30) = c; MU32(SPR99 + 0x50) = c; MU32(SPR99 + 0x70) = c;
    }
}

/* FUN_00438540: fade through black */
void f_438540(void)
{
    if (FADE_PROG == MF(0x44d1e4)) { fade_start_solid(0x000000); return; }
    fade_uv(0, 0);
    if (fade_step()) return;
    fade_alpha();
}

/* FUN_004387d0: fade through white */
void f_4387d0(void)
{
    if (FADE_PROG == MF(0x44d1e4)) { fade_start_solid(0xffffff); return; }
    fade_uv(0, 0);
    if (fade_step()) {
        dd_fill(MU32(0x488c64), NULL, 0x000000);
        return;
    }
    fade_alpha();
}

/* zoom/blur fades: the captured frame drawn slightly magnified (uv 0.05..0.95) */
static void zoom_start(int capture, uint32_t blend)
{
    if (capture) f_438f80();
    fade_uv(0x3d4ccccd, 0x3f733333);
    MU32(SPR99 + 0x80) = 0x27;
    MU32(SPR99 + 0x88) = blend;
    FADE_PROG = 0.01f;
}

/* FUN_00438af0: zoom fade, additive, recapturing every frame */
void f_438af0(void)
{
    if (FADE_PROG == MF(0x44d1e4)) { zoom_start(0, 1); return; }
    MU8(SPR99 + 0x84) = 1;
    f_438f80();
    if (fade_step()) return;
    fade_alpha_k(0x44d608);
}

/* FUN_00438c70: zoom fade on a single captured frame */
void f_438c70(void)
{
    if (FADE_PROG == MF(0x44d1e4)) { zoom_start(1, 0); return; }
    if (fade_step()) return;
    fade_alpha();
}

/* FUN_00438e00: zoom fade, alpha, recapturing every frame */
void f_438e00(void)
{
    if (FADE_PROG == MF(0x44d1e4)) { zoom_start(0, 0); return; }
    MU8(SPR99 + 0x84) = 1;
    f_438f80();
    if (fade_step()) return;
    fade_alpha_k(0x44d608);
}

/* FUN_00407360: spawn an effect in one of the 100 slots at 0x4a3c98 (0xb8 bytes each) with
 * `count` particles (0x3c bytes each: [0] frame, [1] IDirect3DMaterial2, [2..9] user visual
 * record, [0xe] uv phase). Reconstructed from the disassembly: Ghidra dropped the stack stores
 * that fill the D3DMATERIAL (diffuse alpha 1.0, emissive = effect colour 0x4a3cf8..0x4a3d00). */
uint32_t f_407360(int32_t frame, int32_t actor, uint32_t type, uint32_t p4, int32_t count, uint32_t p6,
                  uint32_t p7, uint32_t p8, uint32_t p9, uint32_t p10_bits, uint32_t p11, uint32_t p12,
                  uint32_t p13, uint32_t p14, uint32_t p15, uint32_t p16, uint32_t p17, uint32_t p18,
                  uint32_t p19, uint32_t p20, uint32_t p21, uint32_t p22, uint32_t p23, uint32_t p24,
                  uint32_t p25, uint32_t p26, uint32_t p27)
{
    if (!frame) return 0;
    int32_t slot = -1;
    for (uint32_t p = 0x4a3d4c, i = 0; p < 0x4a852c; p += 0xb8, i++) {
        if (MU8(0x480480 + actor * 0x144) == 0) return 0;
        if (p == 0x4a8474) return 0;
        if (M32(p) == 0) { M32(p) = 1; slot = (int32_t)i; break; }
    }
    if (slot < 0) return 0;
    uint32_t s = (uint32_t)slot * 0xb8;
    uint32_t parts = vm_malloc((uint32_t)count * 0x3c);
    MU32(0x4a3ca0 + s) = parts;
    MU32(0x4a3c9c + s) = (uint32_t)frame;
    M32(0x4a3d18 + s) = actor;
    MU32(0x4a3d14 + s) = 0;
    MU32(0x4a3ca8 + s) = type;
    MU32(0x4a3cac + s) = p4;
    M32(0x4a3ca4 + s) = count;
    MU32(0x4a3cb0 + s) = p6;
    MU32(0x4a3cb4 + s) = p7;
    MU32(0x4a3cb8 + s) = p8;
    MU32(0x4a3d04 + s) = p9;
    MF(0x4a3d08 + s) = (float)(int32_t)(60 / M32(0x488f48)) * BITF(p10_bits);
    MU32(0x4a3cbc + s) = p11; MU32(0x4a3cc0 + s) = p12; MU32(0x4a3cc4 + s) = p13;
    MU32(0x4a3cd4 + s) = p14; MU32(0x4a3cd8 + s) = p15; MU32(0x4a3cdc + s) = p16;
    MU32(0x4a3ce0 + s) = p17; MU32(0x4a3ce4 + s) = p18; MU32(0x4a3ce8 + s) = p19;
    MU32(0x4a3cec + s) = p20; MU32(0x4a3cf0 + s) = p21; MU32(0x4a3cf4 + s) = p22;
    MU32(0x4a3cf8 + s) = p23; MU32(0x4a3cfc + s) = p24; MU32(0x4a3d00 + s) = p25;
    MU32(0x4a3d0c + s) = p26; MU32(0x4a3d10 + s) = p27;
    rmh scene = MU32(0x4a388c);
    MU32(0x4a3c98 + s) = rm_frame_create(scene);

    const uint32_t sp0 = vm_sp;
    uint32_t mat = vm_salloc(0x50);
    MU32(mat) = 0x50;
    MF(mat + 0x10) = 1.0f;                       /* diffuse alpha */
    MU32(mat + 0x34) = MU32(0x4a3cf8 + s);      /* emissive = effect colour */
    MU32(mat + 0x38) = MU32(0x4a3cfc + s);
    MU32(mat + 0x3c) = MU32(0x4a3d00 + s);
    rmh sframe = MU32(0x4a3c98 + s);
    for (int32_t i = 0; i < count; i++) {
        uint32_t part = parts + (uint32_t)i * 0x3c;
        COM_D3D2_CreateMaterial(MU32(0x4a3878), part + 4, 0);
        COM_Material2_SetMaterial(MU32(part + 4), mat);
        MU32(part) = rm_frame_create(sframe);
        f_440c80(MU32(part), part + 8, slot, i, 0x485eb0 + type * 8, MU32(0x4a3d04 + s),
                 M32(0x4a3cac + s), M32(0x4a3cb0 + s), 1);
        rm_frame_set_position(MU32(part), sframe, 0, 0, 0);
        rm_frame_set_orientation(MU32(part), sframe, 0, 0, 1, 0, 1, 0);
        if (M32(0x4a3cb8 + s) == 1) {
            M32(part + 0x38) = i % 16;
            int32_t n = M32(parts + 0x10);   /* vertex count of particle 0's user visual */
            for (int32_t k = 0; k < n; k++) {
                MU32(MU32(part + 0xc) + k * 0xc) = MU32(MU32(part + 8) + k * 0x20 + 0x18);
                MU32(MU32(part + 0xc) + k * 0xc + 4) = MU32(MU32(part + 8) + k * 0x20 + 0x1c);
            }
        }
    }
    vm_sp = sp0;
    rm_frame_set_position(sframe, scene, 0, 100.0f, 0);
    rm_frame_set_orientation(sframe, scene, 0, 0, 1, 0, 1, 0);
    return 0;
}
