/* Hand-ported math helpers. */
#include "gen/vmc.h"

/* FUN_0042a730(out, P, A, B): project point P onto the line A->B (vectors passed by value).
 * Reconstructed from the disassembly. Returns `out` (left in EAX). */
uint32_t f_42a730(uint32_t out, uint32_t px, uint32_t py, uint32_t pz, uint32_t ax, uint32_t ay, uint32_t az,
                  uint32_t bx, uint32_t by, uint32_t bz)
{
    float d[3] = { BITF(bx) - BITF(ax), BITF(by) - BITF(ay), BITF(bz) - BITF(az) };
    float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (l != 0) { d[0] /= l; d[1] /= l; d[2] /= l; }
    else { d[0] = 1; d[1] = 0; d[2] = 0; }
    float t = (BITF(px) * d[0] + BITF(py) * d[1] + BITF(pz) * d[2]) -
              (BITF(ax) * d[0] + BITF(ay) * d[1] + BITF(az) * d[2]);
    MF(out) = d[0] * t + BITF(ax);
    MF(out + 4) = d[1] * t + BITF(ay);
    MF(out + 8) = d[2] * t + BITF(az);
    return out;
}

static float dist3(const float *a, const float *b)
{
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}

/* FUN_0042a900(out_point, out_dist, P, V0, V1, V2): closest point to P on the edges of triangle
 * V0V1V2 (each edge: projection of P clamped to the nearer endpoint when it falls outside). */
void f_42a900(uint32_t out_pt, uint32_t out_dist, uint32_t px, uint32_t py, uint32_t pz,
              uint32_t v0x, uint32_t v0y, uint32_t v0z, uint32_t v1x, uint32_t v1y, uint32_t v1z,
              uint32_t v2x, uint32_t v2y, uint32_t v2z)
{
    const float P[3] = { BITF(px), BITF(py), BITF(pz) };
    const float V[3][3] = { { BITF(v0x), BITF(v0y), BITF(v0z) }, { BITF(v1x), BITF(v1y), BITF(v1z) },
                            { BITF(v2x), BITF(v2y), BITF(v2z) } };
    float best = 10000.0f, bp[3] = { 0, 0, 0 };
    const uint32_t sp0 = vm_sp;
    uint32_t tmp = vm_salloc(12);
    for (int e = 0; e < 3; e++) {
        const float *A = V[e], *B = V[(e + 1) % 3];
        f_42a730(tmp, px, py, pz, BITU(A[0]), BITU(A[1]), BITU(A[2]), BITU(B[0]), BITU(B[1]), BITU(B[2]));
        float q[3] = { MF(tmp), MF(tmp + 4), MF(tmp + 8) };
        int atA = q[0] == A[0] && q[1] == A[1] && q[2] == A[2];
        int atB = q[0] == B[0] && q[1] == B[1] && q[2] == B[2];
        if (!atA && !atB) {
            float da[3] = { A[0] - q[0], A[1] - q[1], A[2] - q[2] }, db[3] = { B[0] - q[0], B[1] - q[1], B[2] - q[2] };
            if (da[0] * db[0] + da[1] * db[1] + da[2] * db[2] > MF(0x44d1e4)) {
                /* outside the segment: snap to the nearer endpoint */
                const float *n = dist3(q, A) < dist3(q, B) ? A : B;
                q[0] = n[0]; q[1] = n[1]; q[2] = n[2];
            }
        }
        float d = dist3(P, q);
        if (d < best) { best = d; bp[0] = q[0]; bp[1] = q[1]; bp[2] = q[2]; }
    }
    vm_sp = sp0;
    MF(out_pt) = bp[0]; MF(out_pt + 4) = bp[1]; MF(out_pt + 8) = bp[2];
    MF(out_dist) = best;
}
