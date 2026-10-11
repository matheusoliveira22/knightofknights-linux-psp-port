/* FUN_0042c7e0: ring menu weapon pages (0 = right hand, 1 = left hand), reconstructed from the
 * disassembly. The decompile invented a `this` argument and lost the angle counter, which the
 * original keeps in the slot of its second argument (0, 360, ... < 2880 -> 8 icons). */
#include "gen/vmc.h"
#include <math.h>

void f_42c7e0(int32_t player, int32_t page)
{
    uint32_t r = player * 0x68;
    MU32(0x48bb70 + r) = MU32(0x49fecc + (player * 0x340 + page) * 4);
    uint32_t tbl = 0x49fee4 + player * 0xd00;
    for (int32_t i = 0, ang = 0; ang < 0xb40; i++, ang += 0x168, tbl += 4) {
        uint32_t icon = 0x48bb18 + (i + player * 0x1a) * 4;
        int empty = M32(tbl) == 1000;
        if (empty) COM_Frame2_AddVisual(MU32(icon), MU32(0x48bb38 + r));
        else COM_Frame2_AddVisual(MU32(icon), MU32(0x488cac + MU32(0x4a3b1c + (i + player * 0x3e) * 4) * 8));
        double a = (double)ang * MF(0x44d2bc) * MD(0x44d228);
        float x = (float)(sin(a) * MF(0x44d1c0)), z = (float)(cos(a) * MF(0x44d1c0));
        uint32_t f = MU32(icon), ref = MU32(0x48bb14 + r);
        float spin = (float)((float)(60 / M32(0x488f48)) * MF(0x44d230) * MD(0x44d228));
        if (empty) {
            COM_Frame2_SetPosition(f, ref, BITU(x), 0, BITU(z));
            COM_Frame2_SetOrientation(f, ref, 0, 0, 0x3f800000, 0, 0x3f800000, 0);
            COM_Frame2_SetRotation(f, f, 0, 0x3f800000, 0, BITU(spin));
        } else {
            COM_Frame2_SetPosition(f, ref, BITU(x), 0x3e800000, BITU(z));
            COM_Frame2_SetOrientation(f, ref, 0, 0x3f800000, 0, 0, 0, 0xbf800000);
            COM_Frame2_SetRotation(f, f, 0, 0, 0x3f800000, BITU(spin));
        }
    }
}
