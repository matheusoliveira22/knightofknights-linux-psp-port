/* Hand-ported map loading (0x4052a0, 0x405a40, 0x405f70). */
#include "gen/vmc.h"
#include <stdlib.h>

/* ---- FUN_004052a0 ---- */
/* Load dat/map/NN/mapMM.dat (6 piece records of 0x20 bytes + placement data) and build the map. */
uint32_t f_4052a0(int32_t param_1, uint32_t param_2)
{
    VFRAME;
    uint32_t path = vm_salloc(0x20), dir = vm_salloc(0x20), tmp = vm_salloc(0x20);
    uint32_t fmt;
    if (param_1 < 10) fmt = (int32_t)param_2 > 9 ? 0x454658 : 0x454670;
    else fmt = (int32_t)param_2 > 9 ? 0x454628 : 0x454640;
    w_wsprintfA(path, fmt, M32(0x4a3838), param_2);
    memset(VA(0x4a1950), 0, 0xfc * 4);
    size_t n = 0;
    uint8_t *d = plat_read_file((const char *)VA(path), &n);
    if (!d) {
        w_MessageBoxA(0, 0x454618, 0x478d64, 0x40000);
        VRETV(1);
    }
    memcpy(VA(0x4a1950), d, n < 0x3f0 ? n : 0x3f0);
    free(d);
    w_wsprintfA(dir, param_1 > 9 ? 0x4545fc : 0x454608, M32(0x4a3834));
    /* prefix the six piece names (at +4 of each 0x20 record) with the object directory */
    for (uint32_t rec = 0x4a1950; rec < 0x4a1a10; rec += 0x20) {
        w_lstrcpyA(tmp, dir);
        MU8(rec + 0x0e) = 0;
        w_lstrcatA(tmp, rec + 4);
        w_lstrcpyA(rec + 4, tmp);
    }
    int32_t iVar5 = M32(0x488bbc);
    int32_t iVar6 = iVar5 * 0x228;
    if (M32(0x4799b4 + iVar6) == 2) {
        int32_t iVar10 = 0;
        uint32_t piVar3 = 0x4a1a38, piVar8 = 0x4799e0 + iVar6;
        do {
            int32_t v10 = M32(piVar8 + 40);
            M32(piVar3 - 40) = M32(piVar8 - 40);
            int32_t iVar7 = M32(piVar8);
            M32(piVar3) = iVar7;
            M32(piVar3 + 40) = v10;
            if (iVar7 == 4 || iVar7 == 5 || iVar7 == 6) {
                int32_t o = (iVar10 + iVar5 * 0x2e) * 0xc;
                M32(piVar3 + 0x50) = M32(0x479a58 + o);
                int32_t t = M32(0x479a60 + o);
                M32(piVar3 + 0x78) = M32(0x479a5c + o);
                M32(piVar3 + 0xa0) = t;
            }
            piVar3 += 4;
            iVar10++;
            piVar8 += 4;
        } while ((int32_t)piVar3 < 0x4a1a60);
    } else if (M32(0x4799a0 + iVar6) != 1 && M32(0x4a3830) != 0) {
        f_403c70(M32(0x4a3838));
    }
    f_4050e0();
    if (0 < M32(0x4a1950)) f_4055f0();
    if (0 < M32(0x4a1970)) f_4056f0();
    if (0 < M32(0x4a1990)) f_405740();
    if (M32(0x488f30) < 2) {
        if (0 < M32(0x4a19b0)) f_405830();
        if (0 < M32(0x4a19d0)) f_405930();
    }
    if (M32(0x488f30) < 1 && 0 < M32(0x4a19f0)) f_405a40();
    f_405bd0();
    f_424840();
    VRETV(1);
}

/* ---- FUN_00405a40 ---- */
/* Map piece 6 ("j" layer): mesh whose textures use black as a transparent colour key. */
uint32_t f_405a40(void)
{
    if (MU32(0x488c98)) {
        COM_Frame2_DeleteChild(MU32(0x4a3890), MU32(0x488c98));
        if (MU32(0x488c9c)) { COM_Unk_Release(MU32(0x488c9c)); MU32(0x488c9c) = 0; }
        if (MU32(0x488c98)) { COM_Unk_Release(MU32(0x488c98)); MU32(0x488c98) = 0; }
    }
    COM_RM2_CreateFrame(MU32(0x4a3880), MU32(0x4a3890), 0x488c98);
    COM_Frame2_SetPosition(MU32(0x488c98), MU32(0x4a3890), 0, 0, 0);
    COM_Frame2_SetOrientation(MU32(0x488c98), MU32(0x4a3890), 0, 0, 0x3f800000, 0, 0x3f800000, 0);
    COM_RM2_CreateMeshBuilder(MU32(0x4a3880), 0x488c9c);
    COM_MeshBuilder2_Load(MU32(0x488c9c), 0x4a19f4, 0, 0, 0, 0);
    COM_MeshBuilder2_SetPerspective(MU32(0x488c9c), 1);
    COM_MeshBuilder2_SetQuality(MU32(0x488c9c), 0x88);
    COM_Frame2_AddVisual(MU32(0x488c98), MU32(0x488c9c));
    rm_mb_textures_transparent(MU32(0x488c9c), 0);
    return 1;
}

/* ---- FUN_00405f70 ---- */
/* Build a collision face list from a mesh file (reconstructed from the disassembly).
 * out: [0] -> n records of 0x34 bytes {v0,v1,v2 (+offset), normal, class}, [1] -> n*4 bytes, [2] = n.
 * class from the normal's angle to +Y: <=30 floor 0, <89 slope 1, <91 wall 2, else 3. */
uint32_t f_405f70(uint32_t file, uint32_t out, uint32_t ox_bits, uint32_t oy_bits, uint32_t oz_bits)
{
    float ox = BITF(ox_bits), oy = BITF(oy_bits), oz = BITF(oz_bits);
    rmh mb = rm_mb_create();
    if (!rm_mb_load(mb, (const char *)VA(file))) { rm_release(mb); return 0; }
    uint32_t n = (uint32_t)rm_mb_face_count(mb);
    MU32(out + 8) = n;
    uint32_t recs = vm_malloc(n * 0x34);
    MU32(out) = recs;
    MU32(out + 4) = vm_malloc(n * 4);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t r = recs + i * 0x34;
        vec3 v[16] = { { 0, 0, 0 } };   /* degenerate faces (< 3 vertices) get a zero normal */
        int nv = rm_mb_face_vertices(mb, (int)i, v, 16);
        for (int k = nv > 0 ? nv : 1; k < 3; k++) v[k] = v[0];
        for (int k = 0; k < 3; k++) {
            vec3 p = k < nv ? v[k] : v[0];
            MF(r + k * 12) = p.x + ox;
            MF(r + k * 12 + 4) = p.y + oy;
            MF(r + k * 12 + 8) = p.z + oz;
        }
        vec3 e1 = { v[1].x - v[0].x, v[1].y - v[0].y, v[1].z - v[0].z };
        vec3 e2 = { v[2].x - v[0].x, v[2].y - v[0].y, v[2].z - v[0].z };
        vec3 nn = { e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
        float l = sqrtf(nn.x * nn.x + nn.y * nn.y + nn.z * nn.z);
        if (l > 0) { nn.x /= l; nn.y /= l; nn.z /= l; }
        MF(r + 0x24) = nn.x; MF(r + 0x28) = nn.y; MF(r + 0x2c) = nn.z;
        double ang = acos(nn.y > 1 ? 1.0 : nn.y < -1 ? -1.0 : (double)nn.y) * MF(0x44d1dc);
        int cls;
        if (!(ang > MF(0x44d1d8))) cls = 0;
        else if (ang < MF(0x44d1d4)) cls = 1;
        else if (ang < MF(0x44d1d0)) cls = 2;
        else cls = 3;
        M32(r + 0x30) = cls;
    }
    rm_release(mb);
    return 1;
}

