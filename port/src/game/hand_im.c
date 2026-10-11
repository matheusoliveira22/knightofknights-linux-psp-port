/* Hand-ported Direct3D immediate-mode drawing of particle user visuals (0x4408f0). */
#include "gen/vmc.h"

/* FUN_004408f0: render one particle mesh. rec (the user visual argument, a particle record + 8):
 * [0] D3DVERTEX* [2] vertex count [3] effect slot [4] particle index [5] texture index
 * (byte)[6] z-buffer [7] blend mode (0 alpha, 1 additive, 2 multiplicative). */
uint32_t f_4408f0(uint32_t rec, uint32_t dev, uint32_t viewport)
{
    (void)viewport;
    COM_D3DDev2_SetRenderState(dev, 7, MU8(rec + 0x18) ? 1 : 0);   /* ZENABLE */
    COM_D3DDev2_SetRenderState(dev, 0x1c, 0);                        /* FOGENABLE */
    COM_D3DDev2_SetRenderState(dev, 0x1b, 1);                        /* ALPHABLENDENABLE */
    COM_D3DDev2_SetRenderState(dev, 4, 1);
    COM_D3DDev2_SetRenderState(dev, 0x1d, 0);
    COM_D3DDev2_SetRenderState(dev, 0x16, 1);                        /* CULL_NONE */
    switch (M32(rec + 0x1c)) {
    case 0:
        COM_D3DDev2_SetRenderState(dev, 0xe, 1);
        COM_D3DDev2_SetRenderState(dev, 0x13, 5);
        COM_D3DDev2_SetRenderState(dev, 0x14, 6);
        break;
    case 1:
        COM_D3DDev2_SetRenderState(dev, 0xe, 0);
        COM_D3DDev2_SetRenderState(dev, 0x13, 2);
        COM_D3DDev2_SetRenderState(dev, 0x14, 2);
        break;
    case 2:
        COM_D3DDev2_SetRenderState(dev, 0xe, 0);
        COM_D3DDev2_SetRenderState(dev, 0x13, 1);
        COM_D3DDev2_SetRenderState(dev, 0x14, 3);
        break;
    }
    COM_D3DDev2_SetRenderState(dev, 1, MU32(0x4a36e4 + MU32(rec + 0x14) * 8));
    uint32_t mat = MU32(MU32(0x4a3ca0 + M32(rec + 0xc) * 0xb8) + 4 + M32(rec + 0x10) * 0x3c);
    COM_D3DDev2_SetLightState(dev, 1, mat);
    COM_D3DDev2_DrawPrimitive(dev, 4, 1, MU32(rec), MU32(rec + 8), 1);
    COM_D3DDev2_SetRenderState(dev, 0x16, 3);
    return 1;
}
