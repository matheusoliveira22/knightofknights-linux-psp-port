/* FUN_0043ec30: player turning/movement from the stick, reconstructed from the disassembly
 * (the decompile lost the vector temporaries). actor = player record (0x144 bytes, [0] = frame). */
#include "gen/vmc.h"
#include <stdlib.h>

void f_43ec30(uint32_t actor)
{
    rmh fr = MU32(actor);
    rmh cam = MU32(0x4a3898);
    vec3 v, d, dir, up;
    rm_frame_get_position(fr, cam, &v);                 /* actor in camera space */
    v.y = 0;
    v.z = -1.0f;
    rm_frame_transform(cam, &v, &v);                    /* camera -> world */
    rm_frame_inverse_transform(fr, &d, &v);             /* world -> actor local */
    rm_frame_get_orientation(fr, MU32(0x4a388c), &dir, &up);

    uint32_t in = MU32(0x4a3aac + MU32(actor + 0x13c) * 0xf8) & 0xf;
    if (!in) return;
    float val = 0, u[3] = { up.x, up.y, up.z };
    int clamp = 1;
    switch (in) {
    case 1:  val = d.x;        u[0] = 0;  u[1] = 0; u[2] = 1;  break;
    case 2:  val = -d.x;       u[0] = 0;  u[1] = 0; u[2] = -1; break;
    case 4:  val = -d.z;       u[0] = -1; u[1] = 0; u[2] = 0;  break;
    case 8:  val = d.z;        u[0] = 1;  u[1] = 0; u[2] = 0;  break;
    case 5:  val = d.x - d.z;  u[0] = -1; u[1] = 0; u[2] = 1;  break;
    case 9:  val = d.x + d.z;  u[0] = 1;  u[1] = 0; u[2] = 1;  break;
    case 6:  val = -d.x - d.z; u[0] = -1; u[1] = 0; u[2] = -1; break;
    case 10: val = d.z - d.x;  u[0] = 1;  u[1] = 0; u[2] = -1; break;
    default: clamp = 0; break;    /* opposite directions pressed: nothing set */
    }
    if (clamp) {
        if (val > MF(0x44d1e0)) val = 4.0f;
        else if (val < MF(0x44d238)) val = -4.0f;
    }
    float dot = dir.x * u[0] + dir.y * u[1] + dir.z * u[2];
    float r = val;
    if (dot > MF(0x44d1e4)) r = val > MF(0x44d1e4) ? MF(0x44d1e0) : MF(0x44d238);
    float theta = (float)((double)(r * MF(0x44d1e0)) * MD(0x44d228));
    rm_frame_set_rotation(fr, fr, 0, 1.0f, 0, theta);

    int32_t fps = M32(0x488f48);
    float k = (float)(60 / fps), kneg = (float)(-60 / fps);
    float spd = MF(actor + 0xbc), t = MF(actor + 0x64), z;
    switch (M32(actor + 0x6c)) {
    case 1:
        if (dot > 0) z = k * spd * MF(0x44da88);
        else if (t > MF(0x44d2ac) && t < MF(0x44d940)) z = kneg * spd;
        else z = k * spd * MF(0x44d93c);
        break;
    case 2:
        if (dot > 0) z = k * spd * MF(0x44d93c);
        else if (t > MF(0x44d40c) && t < MF(0x44d938)) z = k * spd * MF(0x44d934);
        else z = k * spd * MF(0x44d8d8);
        break;
    case 3:
        if (!(t < MF(0x44d2f8))) return;
        if (dot > 0) z = k * spd * MF(0x44da88);
        else if (M32(actor + 0x70) == 1) z = kneg * spd;
        else if (M32(actor + 0x70) == 2) z = k * spd * MF(0x44d934);
        else return;
        break;
    case 14:
        if (M32(actor + 0x74) != 0x11 && M32(actor + 0x74) != 0x12) return;
        if (dot > 0) z = k * spd * MF(0x44da88);
        else if (M32(actor + 0x70) == 1) z = kneg * spd;
        else if (M32(actor + 0x70) == 2) z = k * spd * MF(0x44d934);
        else return;
        break;
    default:
        return;
    }
    rm_frame_set_position(fr, fr, 0, 0, z);
    if (getenv("KOK_TRACE")) {
        vec3 p;
        rm_frame_get_position(fr, MU32(0x4a388c), &p);
        plat_log("move in=%x state=%d spd=%g z=%g theta=%g dot=%g pos=(%g %g %g)", in, M32(actor + 0x6c), spd, z, theta, dot, p.x, p.y, p.z);
    }
}
