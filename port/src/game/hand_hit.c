/* FUN_004282b0: does an attack from `att` (actor record) with hit frame `hitfr` connect with actor
 * number `ti`? Returns 1 = no hit, 0 = hit (then applied through FUN_00428860).
 * Reconstructed from the disassembly (Ghidra lost track of the stack in this function). */
#include "gen/vmc.h"

#define T(off) (0x480340u + ti * 0x144u + (off))

uint32_t f_4282b0(uint32_t att, uint32_t hitfr, uint32_t ti_, uint32_t reach_bits, uint32_t p5, uint32_t p6)
{
    uint32_t ti = ti_;
    rmh scene = MU32(0x4a388c);
    vec3 v, w, u;
    rm_frame_get_position(hitfr, scene, &v);
    MU32(T(0x10c)) = 0; MU32(T(0x110)) = 0; MU32(T(0x114)) = 0; MU32(T(0x118)) = 0;
    if (MU8(T(0x140)) == 0) return 1;
    int32_t a = M32(att + 0x13c);
    if ((int32_t)ti == a) return 1;
    if (a >= 2) {
        if ((int32_t)ti >= 2) return 1;            /* enemies do not hurt each other */
    } else if ((int32_t)ti < 2 && M32(0x4a3820) == 1) {
        return 1;                                  /* co-op: no friendly fire */
    }
    int32_t s = M32(T(0x12c));
    if (s == 1 || s == 2 || s > 3 || M32(T(0x11c)) > 0) return 1;
    if (s == 0) {
        int32_t st = M32(T(0x6c));
        float t = MF(T(0x64));
        if (st == 7 && !(t < MF(0x44d33c))) return 1;
        if (st == 8 && !(t < MF(0x44d350))) return 1;
        if (st == 9 && !(t < MF(0x44d368))) return 1;
        if (st == 10 && !(t < MF(0x44d33c))) return 1;
        if (st == 11 && !(t < MF(0x44d33c))) return 1;
    }
    float dy = MF(T(0x10)) + MF(T(0xc0)) - v.y, dx = MF(T(0x0c)) - v.x, dz = MF(T(0x14)) - v.z;
    float dist = sqrtf(dz * dz + dx * dx + dy * dy);
    if (!(BITF(reach_bits) * MF(0x44d908) + MF(T(0xc0)) > dist)) return 1;

    if (ms_rand() % 1000 < M32(T(0xec))) M32(T(0x118)) = 1;
    if (hitfr != MU32(att + 0x34) && hitfr != MU32(att + 0x38)) M32(T(0x110)) = 1;
    rmh tfr = MU32(T(0));
    if (M32(T(0x110)) == 0) rm_frame_get_position(MU32(att), tfr, &w);
    else rm_frame_get_position(hitfr, tfr, &w);
    if (w.z > MF(0x44d1e4)) M32(T(0x10c)) = 1;
    if (M32(T(0x10c)) == 0 && M32(T(0x6c)) == 0xe) {
        float t = MF(T(0x64));
        int32_t sub = M32(T(0x74));
        if (sub == 0xf && t > MD(0x44d900) && t < MD(0x44d8f8)) M32(T(0x114)) = 1;
        else if (sub == 0x10 && t > MD(0x44d8f0) && t < MD(0x44d8e8)) M32(T(0x114)) = 1;
    }
    int32_t who = M32(T(0x13c));
    if ((who == 0 || who == 1) && M32(T(0x6c)) == 0xe && M32(T(0x74)) == 0x29 && M32(T(0x10c)) == 0) {
        M32(T(0x11c)) = 0x1e;
        M32(T(0x2c)) = 3;
        M32(T(0x30)) = 0;
        if (M32(T(0x78)) == 0) { M32(T(0x74)) = 0x1d; MF(T(0x64)) = 246.0f; }
        else if (M32(T(0x78)) == 1) { M32(T(0x74)) = 0x27; MF(T(0x64)) = 320.0f; }
        return 1;
    }
    int32_t kind = (int32_t)p6;
    if (M32(T(0x10c))) {
        if (kind == 0 || kind == 1) kind = 2;
        else if (kind == 3 || kind == 4) kind = 5;
    }
    if ((int32_t)ti > 1) {
        uint32_t r = 0x478dcc + MU32(T(0x130)) * 0xb0;
        int done = 0;
        if (M32(r) == 1) {
            if (kind == 4) { kind = 1; done = 1; }
            else if (kind == 5) { kind = 2; done = 1; }
            else if (kind > 2) { kind = 0; done = 1; }
        }
        if (!done) {
            if ((M32(r + 4) == 1 && kind == 6) || (M32(r + 8) == 1 && kind == 7) || (M32(r + 0xc) == 1 && kind == 8))
                kind = 3;
        }
    }
    if (M32(T(0x12c)) == 0) {
        int front = M32(T(0x10c)) != 0;
        int toward = front && kind <= 5 && M32(T(0x6c)) != 0xb && M32(T(0x6c)) != 0xa;
        float ax, az;
        if (M32(T(0x110)) == 0) {
            rm_frame_get_position(tfr, MU32(att), &u);
            float dot = w.x * u.x + w.y * u.y + w.z * u.z;
            if ((double)dot > MD(0x44d8e0) && u.z < MF(0x44d1e4))
                rm_frame_set_orientation(tfr, scene, MF(att + 0xc) - MF(T(0x0c)), 0, MF(att + 0x14) - MF(T(0x14)), 0, 1, 0);
            ax = MF(att + 0xc); az = MF(att + 0x14);
        } else {
            ax = v.x; az = v.z;
        }
        if (toward) rm_frame_set_orientation(tfr, scene, ax - MF(T(0x0c)), 0, az - MF(T(0x14)), 0, 1, 0);
        else rm_frame_set_orientation(tfr, scene, MF(T(0x0c)) - ax, 0, MF(T(0x14)) - az, 0, 1, 0);
    }
    if ((int32_t)ti > 1 && M32(T(0x12c)) == 0) M32(0x485f50 + MU32(T(0x134)) * 16) = a;
    if (M32(T(0x114)) == 0 && M32(T(0x12c)) == 0) f_428860((int32_t)T(0), p5, kind);
    return 0;
}
