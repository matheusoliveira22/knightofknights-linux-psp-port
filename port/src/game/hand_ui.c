/* Options screen (game state 6, FUN_00430110), reconstructed from the disassembly. */
#include "gen/vmc.h"

typedef struct { int32_t l, t, r, b; } R;

/* label sheet (surface record 0x49efb0) source rects and screen rects; names follow Ghidra's
 * stack locals of the original (local_b0 ...) */
static const R L_b0 = { 0, 0, 0x26, 0x20 }, L_a0 = { 0x26, 0, 0x52, 0x20 }, L_90 = { 0x52, 0, 0x8b, 0x20 },
               L_80 = { 0x8b, 0, 0xd9, 0x20 }, L_70 = { 0xd9, 0, 0x115, 0x20 }, L_60 = { 0, 0x20, 0x56, 0x40 },
               L_50 = { 0x56, 0x20, 0xad, 0x40 }, L_40 = { 0xad, 0x20, 0x10d, 0x40 }, L_30 = { 0, 0x40, 0x35, 0x60 },
               L_20 = { 0x35, 0x40, 0x66, 0x60 }, L_10 = { 0x66, 0x40, 0x96, 0x60 };
static const R D_160 = { 0x1c9, 0xcb, 0x1ef, 0xeb }, D_150 = { 0x1ef, 0xcb, 0x21b, 0xeb },
               D_140 = { 0x156, 0x8b, 399, 0xab }, D_130 = { 399, 0x8b, 0x1dd, 0xab }, D_120 = { 0x1dd, 0x8b, 0x219, 0xab },
               D_110 = { 0x10b, 0xab, 0x161, 0xcb }, D_100 = { 0x161, 0xab, 0x1b8, 0xcb }, D_f0 = { 0x1b8, 0xab, 0x218, 0xcb },
               D_e0 = { 0x181, 0xcb, 0x1b6, 0xeb }, D_d0 = { 0x1b6, 0xcb, 0x1e7, 0xeb }, D_c0 = { 0x1e7, 0xcb, 0x217, 0xeb };

static void label(const R *src, const R *dst, int dy)
{
    rect_t s = { src->l, src->t, src->r, src->b }, d = { dst->l, dst->t + dy, dst->r, dst->b + dy };
    dd_blt(MU32(0x49ee60), &d, MU32(0x49efb0), &s, DDBLT_WAIT | DDBLT_KEYSRC);
}

static void toggle(int32_t on, int dy) { if (on == 1) label(&L_b0, &D_160, dy); else label(&L_a0, &D_150, dy); }

static int32_t *opt(int i) { return (int32_t *)VA(0x488f28 + i * 4); }   /* aligned option words */

void f_430110(void)
{
    int32_t *state = &M32(0x4a84a4), *cur = &M32(0x4a84ac);
    switch (*state) {
    case 0:
        M32(0x488c68) = 1;
        *state = 1;
        MF(0x488c6c) = 0.0f;
        MU32(0x488c70) = 0xbccccccd;
        return;
    case 1: {
        rect_t r = { M32(0x49ee64), M32(0x49ee68), M32(0x49ee6c), M32(0x49ee70) };
        dd_blt(MU32(0x49ee60), &r, MU32(0x49ef08), &r, DDBLT_WAIT);
        if (MF(0x488c6c) == MF(0x44d1e4)) (*state)++;
        return;
    }
    case 2: {
        f_439240(0x480340);
        uint32_t in = MU32(0x4a3aac), prev = MU32(0x4a3ab0);
        if ((prev & 0xf) == 0 && (in & 0xf) == 1 && *cur > 0) { (*cur)--; f_4029e0(6); }
        else if ((prev & 0xf) == 0 && (in & 0xf) == 2 && *cur < 8) { (*cur)++; f_4029e0(6); }
        else if ((prev & 0xf) == 0 && ((in & 0xf) == 4 || (in & 0xf) == 8)) {
            int dir = (in & 0xf) == 8 ? 1 : -1;
            f_4029e0(5);
            if (*cur <= 2) {
                int32_t *o = opt(*cur);
                if (dir < 0 && *o > 0) (*o)--;
                if (dir > 0 && *o < 2) (*o)++;
            } else if (*cur <= 7) {
                *opt(*cur) = *opt(*cur) == 0;
            }
        } else if ((in & 0x30) && *cur == 8) {
            f_4029e0(5);
            f_43dee0();
            (*state)++;
        } else if (game_menu_cancel()) {   /* port: back = Exit */
            f_43dee0();
            (*state)++;
        }
        /* cursor sprite 0 */
        int32_t h = M32(0x488c58);
        MU8(0x49b7fc) = 1;
        float base = (float)(h * *cur) * MF(0x44d950);
        float y0 = (float)(h * 0x8c) * MF(0x44d94c) + base, y1 = (float)(h * 0xac) * MF(0x44d94c) + base;
        MF(0x49b77c) = y0; MF(0x49b79c) = y0; MF(0x49b7bc) = y1; MF(0x49b7dc) = y1;
        rect_t r = { M32(0x49ee64), M32(0x49ee68), M32(0x49ee6c), M32(0x49ee70) };
        dd_blt(MU32(0x49ee60), &r, MU32(0x49ef08), &r, DDBLT_WAIT);
        static const R *s0[3] = { &L_90, &L_80, &L_70 }, *d0[3] = { &D_140, &D_130, &D_120 };
        static const R *s1[3] = { &L_60, &L_50, &L_40 }, *d1[3] = { &D_110, &D_100, &D_f0 };
        static const R *s2[3] = { &L_30, &L_20, &L_10 }, *d2[3] = { &D_e0, &D_d0, &D_c0 };
        int32_t o0 = *opt(0), o1 = *opt(1), o2 = *opt(2);
        if (o0 >= 0 && o0 <= 2) label(s0[o0], d0[o0], 0);
        if (o1 >= 0 && o1 <= 2) label(s1[o1], d1[o1], 0);
        if (o2 >= 0 && o2 <= 2) label(s2[o2], d2[o2], 0);
        toggle(*opt(3), 0x20);
        toggle(*opt(4), 0x40);
        toggle(*opt(5), 0x60);
        toggle(*opt(6), 0x80);
        toggle(*opt(7), 0xa0);
        MU8(0x49b888) = *cur == 1;
        return;
    }
    case 3:
        M32(0x488c68) = 1;
        MF(0x488c6c) = 0.0f;
        MU32(0x488c70) = 0x3ccccccd;
        *state = 4;
        return;
    case 4:
        if (MF(0x44d948) <= MF(0x488c6c)) {
            *state = 0;
            M32(0x4a3848) = 2;
            M32(0x488c50) = 0;
            f_430cb0();
            f_430c20();
        }
        return;
    }
}
