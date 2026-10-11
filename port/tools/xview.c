/* Model viewer for checking the .x loader / animation conventions against the game data.
 * usage: xview <datadir> <model-path> [anim]   arrows rotate, +/- zoom, space toggles animation */
#include "plat/plat.h"
#include "rm/rm.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: xview <datadir> <model> [anim]\n"); return 1; }
    if (!plat_init(argc, argv)) return 1;
    rm_init();
    rmh scene = rm_frame_create(0);
    rmh cam = rm_frame_create(scene);
    rmh obj = rm_frame_create(scene);
    int anim = argc > 3;
    rmh as = 0;
    if (anim) {
        as = rm_animset_create();
        rm_animset_load(as, argv[2], obj);
        /* XV_ATTACH=<frame name>=<mesh path> hangs a mesh on a named frame (weapons) */
        if (getenv("XV_ATTACH")) {
            char spec[256], *eq;
            snprintf(spec, sizeof spec, "%s", getenv("XV_ATTACH"));
            if ((eq = strchr(spec, '='))) {
                *eq = 0;
                rmh mb = rm_mb_create();
                rm_mb_load(mb, eq + 1);
                rm_frame_add_visual(rm_named_object(spec), mb);
            }
        }
    } else {
        rmh mb = rm_mb_create();
        rm_mb_load(mb, argv[2]);
        rm_frame_add_visual(obj, mb);
    }
    rm_scene_set_background(scene, 0x203040);
    rm_viewport_init(cam, SCREEN_W, SCREEN_H);
    rm_viewport_set_field(0.75f);
    float yaw = 0, pitch = 0.3f, dist = 4, t = 0, ty = 0.5f;
    /* XV_VIEW=yaw,pitch,dist,target_y presets the camera (scripted screenshots) */
    if (getenv("XV_VIEW")) sscanf(getenv("XV_VIEW"), "%f,%f,%f,%f", &yaw, &pitch, &dist, &ty);
    int play = 1;
    while (plat_pump()) {
        uint8_t k[256];
        plat_keyboard_state(k);
        if (k[0x25] & 0x80) yaw -= 0.03f;
        if (k[0x27] & 0x80) yaw += 0.03f;
        if (k[0x26] & 0x80) pitch += 0.02f;
        if (k[0x28] & 0x80) pitch -= 0.02f;
        if (k[0xbb] & 0x80) dist *= 0.98f;
        if (k[0xbd] & 0x80) dist *= 1.02f;
        static int sp;
        if ((k[0x20] & 0x80) && !sp) play = !play;
        sp = k[0x20] & 0x80;
        float cx = sinf(yaw) * cosf(pitch) * dist, cy = sinf(pitch) * dist, cz = -cosf(yaw) * cosf(pitch) * dist;
        rm_frame_set_position(cam, scene, cx, cy + ty, cz);
        rm_frame_set_orientation(cam, scene, -cx, -cy, -cz, 0, 1, 0);
        if (as && play) { rm_animset_set_time(as, t); t += 1; if (t > 200) t = 0; }
        plat_frame_begin(0);
        rm_viewport_clear(scene);
        rm_viewport_render(scene);
        plat_frame_end();
    }
    plat_shutdown();
    return 0;
}
