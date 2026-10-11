/* Frame profiler. prof_us[] (dx.c) accumulates microseconds for the once-a-second perf log; pf[]
 * counts events of the current frame and is printed for frames slower than KOK_SPIKE us (sys.c). */
#ifndef KOK_PROF_H
#define KOK_PROF_H
#include <stdint.h>
enum {
    PF_MB_BUILD, PF_MB_TRIS,     /* mesh builder rebuilds (emissive changes...) and their triangles */
    PF_MB_LOAD, PF_FRAME_NEW,    /* .x loads, frames created */
    PF_FILE_READ, PF_FILE_KB,    /* files read and their size */
    PF_FX,                       /* effects spawned (FUN_00407360) */
    PF_TEX_ROWS,                 /* surface rows converted for upload */
    PF_GPU_ALLOC, PF_GPU_RETIRED,/* PSP: GE vertex/texture buffers allocated, retired buffers freed */
    PF_N
};
extern uint32_t pf[PF_N];
#endif
