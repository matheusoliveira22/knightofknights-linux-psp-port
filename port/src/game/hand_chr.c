/* Hand-ported character helpers (Ghidra lost the stack arguments of these COM loops). */
#include "gen/vmc.h"
#include <stdio.h>

static void collect(rmh f, uint32_t actor)
{
    for (int i = 0; i < rm_frame_visual_count(f); i++) {
        rmh v = rm_frame_visual(f, i);
        if (rm_is_frame(v)) { collect(v, actor); continue; }
        if (!rm_mb_face_count(v)) continue;
        uint32_t n = MU32(actor + 0x28), list = MU32(actor + 0x24);
        int dup = 0;
        for (uint32_t k = 0; k < n; k++) if (MU32(list + k * 4) == v) dup = 1;
        if (dup) continue;
        uint32_t nl = vm_malloc((n + 1) * 4);
        for (uint32_t k = 0; k < n; k++) MU32(nl + k * 4) = MU32(list + k * 4);
        if (list) vm_free(list);
        rm_addref(v);
        MU32(nl + n * 4) = v;
        MU32(actor + 0x24) = nl;
        MU32(actor + 0x28) = n + 1;
    }
    for (int i = 0; i < rm_frame_child_count(f); i++) collect(rm_frame_child(f, i), actor);
}

/* FUN_00424130(actor): collect the materials of every face under the actor's frame into the
 * list at actor+0x24 (count at +0x28). The port keeps mesh builders: every material of a
 * character is tinted identically (FUN_00424460), so per-mesh emissive is equivalent. */
uint32_t f_424130(uint32_t actor)
{
    collect(MU32(actor), actor);
    return 1;
}

/* FUN_00424460(actor, r, g, b): Material::SetEmissive(r,g,b); SetPower(0); SetSpecular(0,0,0) */
void f_424460(int32_t actor, uint32_t r, uint32_t g, uint32_t b)
{
    uint32_t list = MU32(actor + 0x24);
    if (!list) return;
    for (int32_t i = 0; i < M32(actor + 0x28); i++)
        rm_mb_set_emissive(MU32(list + i * 4), BITF(r), BITF(g), BITF(b));
}

/* FUN_00426f50(frame): recursively rename a frame tree "AnimationFrame %d" (global counter 0x4a8480) */
void f_426f50(uint32_t frame)
{
    for (int i = 0; i < rm_frame_child_count(frame); i++) f_426f50(rm_frame_child(frame, i));
    char name[32];
    snprintf(name, sizeof name, (const char *)VA(0x46148c), M32(0x4a8480));
    rm_set_name(frame, name);
    M32(0x4a8480)++;
}
