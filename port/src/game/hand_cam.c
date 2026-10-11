/* Camera (0x437f80, 0x4380b0, 0x438200), reconstructed from the disassembly: Ghidra lost the
 * vector arguments these functions pass through registers and stack copies. */
#include "gen/vmc.h"

#define CAM     MU32(0x4a3898)
#define TARGET  MU32(0x4a3894)
#define SCENE   MU32(0x4a388c)
#define HALF    MF(0x44d1c8)          /* 0.5 */
#define CAMDIST MF(0x4a1d20)          /* map: camera distance */

static float len3(float x, float y, float z) { return sqrtf(x * x + y * y + z * z); }

/* distance target -> camera measured in camera space */
static float target_dist(void)
{
    vec3 v;
    rm_frame_get_position(TARGET, CAM, &v);
    return len3(v.x, v.y, v.z);
}

static float player_gap(void)
{
    return len3(MF(0x48034c) - MF(0x480490), MF(0x480350) - MF(0x480494), MF(0x480354) - MF(0x480498));
}

/* FUN_004380b0: camera mode 0 - fixed point from the map, dolly to keep the distance */
void f_4380b0(void)
{
    rm_frame_set_position(CAM, SCENE, MF(0x4a1d24), MF(0x4a1d2c), MF(0x4a1d34));
    rm_frame_lookat(CAM, TARGET, SCENE, 0);
    float d = target_dist();
    if (MU8(0x4805c4)) rm_frame_set_position(CAM, CAM, 0, 0, (d - player_gap()) - CAMDIST * HALF);
    else rm_frame_set_position(CAM, CAM, 0, 0, d - CAMDIST);
}

/* FUN_00438200: camera mode 1 - on the rail A->B (map data) closest to the target */
void f_438200(void)
{
    rm_frame_set_position(CAM, SCENE, MF(0x4a1d24), MF(0x4a1d2c), MF(0x4a1d34));
    vec3 t;
    rm_frame_get_position(TARGET, SCENE, &t);
    const uint32_t sp0 = vm_sp;
    uint32_t out = vm_salloc(12);
    f_42a730(out, BITU(t.x), BITU(t.y), BITU(t.z), MU32(0x4a1d24), MU32(0x4a1d2c), MU32(0x4a1d34),
             MU32(0x4a1d28), MU32(0x4a1d30), MU32(0x4a1d38));
    float px = MF(out), py = MF(out + 4), pz = MF(out + 8);
    vm_sp = sp0;
    rm_frame_set_position(CAM, SCENE, px, CAMDIST * MF(0x44d1f4) + py, pz);
    rm_frame_lookat(CAM, TARGET, SCENE, 0);
    if (M32(0x488f3c) != 1) return;
    float d = target_dist();
    if (M32(0x4a3820) > 0) {
        float dd = d - player_gap();
        rm_frame_set_position(CAM, CAM, 0, (dd - CAMDIST) * MF(0x44d2bc), dd - CAMDIST * HALF);
    } else {
        float dd = d - CAMDIST;
        rm_frame_set_position(CAM, CAM, 0, dd * MF(0x44d2bc), dd);
    }
    rm_frame_lookat(CAM, TARGET, SCENE, 0);
}

/* FUN_00437f80: per-frame camera: target point, camera mode, fog distance */
void f_437f80(void)
{
    if (M32(0x4a3820) > 0)
        rm_frame_set_position(TARGET, SCENE, (MF(0x480490) + MF(0x48034c)) * HALF,
                              (MF(0x480494) + MF(0x480350)) * HALF + MF(0x44d24c),
                              (MF(0x480498) + MF(0x480354)) * HALF);
    else
        rm_frame_set_position(TARGET, MU32(0x480340), 0, 0.75f, 0);
    switch (M32(0x4a1d1c)) {
    case 0: f_4380b0(); break;
    case 1: f_438200(); break;
    case 2: f_438530(); break;
    }
    vec3 v;
    rm_frame_get_position(TARGET, CAM, &v);
    float d = len3(v.x, v.y, v.z);
    rm_scene_set_fog_enable(SCENE, 1);
    rm_scene_set_fog_params(SCENE, d, d + MF(0x44d204), 1.0f);
    rm_scene_set_fog_color(SCENE, 0);
}
