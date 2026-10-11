/* Charge gauges (FUN_00440070), reconstructed from the disassembly. Ghidra lost the x87 stack
 * value shared between the branches (the gauges' bottom edge) and read the float stack temps
 * through integer loads, which put the gauges far off screen at the wrong size. */
#include "gen/vmc.h"

/* sprite table 0x49b778: 0x8c per sprite, four D3DTLVERTEX (x +0, y +4, tu +0x18, tv +0x1c);
 * the gauges only set vertices 0 and 2 (top-left / bottom-right) */
#define SX0(s) (0x49b778u + (s) * 0x8cu)
#define SX2(s) (SX0(s) + 0x40)
#define X(v)  MF((v) + 0x00)
#define Y_(v) MF((v) + 0x04)
#define TU(v) MF((v) + 0x18)
#define TV(v) MU32((v) + 0x1c)

static const uint32_t TV_FULL = 0x3f7f0000u, TV_MAX = 0x3f3f0000u;   /* 0.99609375, 0.74609375 */

/* charge value (0..3 levels) -> position within the current level */
static float level(float v)
{
    if (!(v < MF(0x44d27c))) v -= v < MF(0x44d1f0) ? MF(0x44d27c) : MF(0x44d1f0);
    return v * MF(0x44dae4);
}

/* one gauge: `c` its charge, `kx_fill` the x scale constant while filling (grows from the left
 * gauge at +K[ad8] or shrinks from the right one), `kx_full` the x scale once charged,
 * `right` = 1 for gauges filling right-to-left. `full_tu` is the tu set at a full charge
 * (left gauges 1/256, right gauges 0); `set_tu_max` = 0 skips tu at the maximum level. */
static void gauge(int s, float c, float W, float H, float Y, uint32_t kx_base, uint32_t kx_full,
                  int right, float full_tu, int set_tu_max)
{
    uint32_t v0 = SX0(s), v2 = SX2(s);
    if (c < MF(0x44dae0)) {
        float x = (right ? MF(kx_base) + c * MF(0x44dadc) : MF(kx_base) - c * MF(0x44dadc)) * W * MF(0x44d954);
        TV(v2) = TV(v0) = TV_FULL;
        X(v2) = X(v0) = x;
        Y_(v2) = Y_(v0) = Y;
        TU(v2) = TU(v0) = (MF(0x44d370) - c * MF(0x44dad0)) * MF(0x44d274);
        return;
    }
    X(v2) = X(v0) = W * MF(kx_full);
    if (c < MF(0x44d1cc)) {
        float t = (c - MF(0x44dae0)) * MF(0x44dac8);
        TU(v2) = TU(v0) = full_tu;
        Y_(v0) = (MF(0x44d268) - t) * H * MF(0x44d94c);
        Y_(v2) = Y;
        MF(v0 + 0x1c) = (MF(0x44d394) - t) * MF(0x44d274);
    } else {
        if (set_tu_max) TU(v2) = TU(v0) = full_tu;
        TV(v0) = TV_MAX;
        Y_(v0) = H * MF(0x44dac4);
        Y_(v2) = Y;
    }
    TV(v2) = TV_FULL;
}

void f_440070(void)
{
    float W = (float)M32(0x488c54), H = (float)M32(0x488c58);
    float Y = H * MF(0x44dad4);   /* bottom edge of every gauge */
    /* player 1: left hand (sprite 0) fills from the left, right hand (sprite 1) from the right */
    gauge(0, level(MF(0x4a3aec)), W, H, Y, 0x44dad8, 0x44dacc, 0, 1.0f / 256, 1);
    gauge(1, level(MF(0x4a3af0)), W, H, Y, 0x44dad8, 0x44dac0, 1, 0.0f, 1);
    if (M32(0x4a3820) <= 0) {
        MU8(0x49b914) = 0;
        MU8(0x49b9a0) = 0;
        return;
    }
    /* player 2 */
    gauge(2, level(MF(0x4a3be4)), W, H, Y, 0x44dabc, 0x44dab8, 0, 0.0f, 0);
    gauge(3, level(MF(0x4a3be8)), W, H, Y, 0x44dabc, 0x44dab4, 1, 0.0f, 0);
}
