/* HUD text routines FUN_004326c0 (move name), FUN_00432a30 (last hit enemy's name) and FUN_00435340
 * (player level), reconstructed from the disassembly. In the decompile the stack frame was 8 bytes
 * off after the prologue, so the text rects were read as (x, y, 0, 0) and nothing was ever drawn.
 *
 * Each routine formats one string, pads it with spaces and DrawTexts it either into the 256x256
 * text texture 0x488c64 (option 0x488f38 == 1: rect 0,0,256,160, DC defaults = opaque black
 * background) or onto the back buffer (transparent background) at a per-player rect given in
 * 640x480 units. They return 0 when they drew (the caller then shows the text sprite), 1 otherwise. */
#include "gen/vmc.h"
#include <stdio.h>
#include <string.h>

typedef struct { int32_t l, t, r, b; } r640_t;

/* Texture path: the original draws every routine's text into the one shared surface 0x488c64,
 * just before that routine's sprite is drawn. Here each routine (and player) draws into its own
 * surface and the sprite is redirected to it, so the unchanged text of the next frame changes no
 * pixels and nothing has to be uploaded again. */
enum { TXT_MOVE, TXT_ENEMY, TXT_LEVEL, TXT_N };
static uint32_t txt_surf[TXT_N][2];

/* width: pad to this many characters; right: pad on the left (player 2's texts are right aligned) */
static uint32_t hud_text(const char *txt, int width, int right, const r640_t *rc, int which, int pl)
{
    int tex = M32(0x488f38) == 1;
    uint32_t dst = MU32(0x4a385c);
    if (tex) {
        uint32_t *sp = &txt_surf[which][pl != 0];
        if (!*sp) { *sp = dd_create_surface(256, 256); dd_fill(*sp, NULL, 0); }
        dst = *sp;
    }
    uint32_t sp0 = vm_sp;
    uint32_t hdc_p = vm_salloc(4), buf = vm_salloc(0x80), rect = vm_salloc(16);
    COM_DDSurf_GetDC(dst, hdc_p);
    uint32_t hdc = MU32(hdc_p);
    w_SelectObject(hdc, MU32(tex ? 0x4a8518 : 0x4a851c));
    w_SetTextColor(hdc, 0xffffff);
    w_SetBkColor(hdc, 0);
    if (!tex) w_SetBkMode(hdc, 1);

    char s[0x80] = "";
    int n = (int)strlen(txt), pad = width - n;
    if (right) for (int i = 0; i < pad; i++) strcat(s, " ");
    strncat(s, txt, sizeof s - 1 - strlen(s));
    if (!right) for (int i = 0; i < pad; i++) strcat(s, " ");
    memcpy(VA(buf), s, sizeof s);

    if (tex) {
        M32(rect) = 0; M32(rect + 4) = 0; M32(rect + 8) = 0x100; M32(rect + 12) = 0xa0;
    } else {
        int32_t w = M32(0x488c54), h = M32(0x488c58);
        M32(rect) = w * rc->l / 640; M32(rect + 4) = h * rc->t / 480;
        M32(rect + 8) = w * rc->r / 640; M32(rect + 12) = h * rc->b / 480;
    }
    w_DrawTextA(hdc, buf, -1, rect, 0x10);
    COM_DDSurf_ReleaseDC(dst, hdc);
    vm_sp = sp0;
    if (tex) im_redirect_next(MU32(0x4a381c), dst);   /* texture slot 0x27 = 0x488c64 */
    return 0;
}

/* FUN_004326c0: name of the special move (state 0xe, move id > 20) under the player's gauge */
uint32_t f_4326c0(int32_t actor)
{
    static const r640_t rc[2] = { { 154, 34, 282, 50 }, { 358, 34, 486, 50 } };
    if (!M8(actor + 0x140) || M32(actor + 0x6c) != 0xe || M32(actor + 0x74) <= 0x14) return 1;
    int32_t combo = f_412bd0(actor);
    int pl = M32(actor + 0x13c) != 0;
    return hud_text((const char *)VA(0x4a2740 + combo * 40), 16, pl, &rc[pl], TXT_MOVE, pl);
}

/* FUN_00432a30: kind name of the enemy the player hit last, while its HP gauge is shown */
uint32_t f_432a30(int32_t actor)
{
    static const r640_t rc[2] = { { 144, 416, 272, 432 }, { 368, 416, 496, 432 } };
    if (!M8(actor + 0x140)) return 1;
    int32_t pl = M32(actor + 0x13c);
    if (M32(0x4a3b0c + pl * 0xf8) <= 0) return 1;
    int32_t tgt = M32(0x4a3b08 + pl * 0xf8);
    if (!M8(0x480480 + tgt * 0x144) || tgt <= 1) return 1;
    uint32_t kind = MU32(0x480470 + tgt * 0x144);
    return hud_text((const char *)VA(0x478d68 + kind * 0xb0), 16, pl != 0, &rc[pl != 0], TXT_ENEMY, pl);
}

/* FUN_00435340: "Lv%d" (0x46bf0c) next to the player's HP gauge */
uint32_t f_435340(int32_t actor)
{
    static const r640_t rc[2] = { { 118, 34, 160, 66 }, { 480, 34, 522, 66 } };
    if (!M8(actor + 0x140)) return 1;
    int32_t pl = M32(actor + 0x13c);
    char s[32];
    snprintf(s, sizeof s, (const char *)VA(0x46bf0c), M32(0x480404 + pl * 0x144));
    return hud_text(s, 8, 0, &rc[pl != 0], TXT_LEVEL, pl);
}
