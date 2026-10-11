/* Mini Direct3D Retained Mode. See rm.h.
 *
 * Lighting: the game never creates a light, and every material in its .x files carries an
 * emissive term, so a lit D3DRM face resolves to clamp(emissive) modulated by the texture.
 * Face colour only contributes its alpha (used for fades / sorted transparency).
 */
#include "rm.h"
#include "../fmt/xfile.h"
#include "../fmt/bmp.h"
#include "../plat/plat.h"
#include "../core/vm.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* D3DRM uniform scaling: the field of view applies to the larger viewport dimension. */
#define FIELD_ON_LARGER_DIMENSION 1

enum { T_FREE, T_FRAME, T_MESHB, T_ANIMSET, T_TEXTURE, T_USERVIS, T_FACE };

typedef struct {
    float emissive[3];
    uint32_t color;   /* ARGB face colour (diffuse + alpha) */
    rmh tex;
} rmmat_t;

typedef struct {
    vec3 *pos;
    float *uv;
    int nv;
    int *idx, *fstart, *fcount;
    int nf;
    rmmat_t *fmat;         /* per face */
    /* render cache: triangles batched by (texture, colour) */
    int dirty;
    struct batch { int tex; uint32_t color; int start, count; float sphere[4]; void *gpu; } *batches;
    int nbatches;
    pvert_t *verts;
    vec3 center;
} meshb_t;

typedef struct { float time; float v[4]; } akey_t;
typedef struct {
    rmh frame;
    akey_t *rot, *scl, *pos;
    int nrot, nscl, npos;
} anim_t;
typedef struct { anim_t *anims; int n; } animset_t;

typedef struct {
    int w, h;              /* padded (power of two) */
    int plat;
    char name[96];
    uint8_t *rgba;
} tex_t;

typedef struct {
    rm_user_cb cb;
    void *arg;
} uvis_t;

typedef struct obj {
    int type, refs;
    rm_destroy_cb dcb;
    uint32_t dcb_fn, dcb_arg;
    char name[64];
    uint32_t appdata;
    union {
        struct {
            rmh parent;
            rmh *kids; int nkids;
            rmh *vis; int nvis;
            mat4 local;
            vec3 rot_axis; float rot_theta; rmh rot_ref;
            vec3 vel; int vel_rot;
            /* scene state (root frames only) */
            uint32_t bg;
            int fog_on;
            float fog_start, fog_end;
            uint32_t fog_color;
        } fr;
        meshb_t mb;
        animset_t as;
        tex_t tex;
        uvis_t uv;
        struct { rmh mb; int face; } face;
    };
} obj_t;

#define MAX_OBJS 16384
#define HANDLE_BASE 0x70000000u
static obj_t *objs[MAX_OBJS];
static int next_free = 1;

static struct {
    rmh camera;
    int w, h;
    float front, back, field;
    mat4 view;
    float sx, sy;
    int fog_on;
    float fog_start, fog_end;
    uint32_t fog_color;
} vp = { 0, SCREEN_W, SCREEN_H, 1.0f, 100.0f, 0.5f };

/* ------------------------------------------------------------------ math */

void mat_identity(mat4 *m)
{
    memset(m, 0, sizeof *m);
    m->m[0][0] = m->m[1][1] = m->m[2][2] = m->m[3][3] = 1;
}

void mat_mul(mat4 *out, const mat4 *a, const mat4 *b)
{
    mat4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j] +
                        a->m[i][3] * b->m[3][j];
    *out = r;
}

void mat_invert_affine(mat4 *out, const mat4 *m)
{
    /* general 3x3 inverse (frames may carry scale) + translation */
    const float (*a)[4] = m->m;
    float c00 = a[1][1] * a[2][2] - a[1][2] * a[2][1];
    float c01 = a[1][2] * a[2][0] - a[1][0] * a[2][2];
    float c02 = a[1][0] * a[2][1] - a[1][1] * a[2][0];
    float det = a[0][0] * c00 + a[0][1] * c01 + a[0][2] * c02;
    float id = fabsf(det) > 1e-20f ? 1.0f / det : 0.0f;
    mat4 r;
    mat_identity(&r);
    r.m[0][0] = c00 * id;
    r.m[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * id;
    r.m[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * id;
    r.m[1][0] = c01 * id;
    r.m[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * id;
    r.m[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * id;
    r.m[2][0] = c02 * id;
    r.m[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * id;
    r.m[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * id;
    for (int j = 0; j < 3; j++)
        r.m[3][j] = -(a[3][0] * r.m[0][j] + a[3][1] * r.m[1][j] + a[3][2] * r.m[2][j]);
    *out = r;
}

void vec_xform(vec3 *o, const vec3 *v, const mat4 *m)
{
    vec3 r = { v->x * m->m[0][0] + v->y * m->m[1][0] + v->z * m->m[2][0] + m->m[3][0],
               v->x * m->m[0][1] + v->y * m->m[1][1] + v->z * m->m[2][1] + m->m[3][1],
               v->x * m->m[0][2] + v->y * m->m[1][2] + v->z * m->m[2][2] + m->m[3][2] };
    *o = r;
}

void vec_xform_dir(vec3 *o, const vec3 *v, const mat4 *m)
{
    vec3 r = { v->x * m->m[0][0] + v->y * m->m[1][0] + v->z * m->m[2][0],
               v->x * m->m[0][1] + v->y * m->m[1][1] + v->z * m->m[2][1],
               v->x * m->m[0][2] + v->y * m->m[1][2] + v->z * m->m[2][2] };
    *o = r;
}

static vec3 v3(float x, float y, float z) { vec3 r = { x, y, z }; return r; }
static vec3 vcross(vec3 a, vec3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static vec3 vnorm(vec3 a)
{
    float l = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
    return l > 1e-12f ? v3(a.x / l, a.y / l, a.z / l) : a;
}

/* D3DRM quaternion -> row-vector matrix. The game's .x data (bind pose FrameTransformMatrix vs.
 * first rotation key) shows D3DRM uses the standard right-handed formula without transposing. */
void quat_to_mat(mat4 *m, float w, float x, float y, float z)
{
    mat_identity(m);
    m->m[0][0] = 1 - 2 * (y * y + z * z);
    m->m[0][1] = 2 * (x * y - w * z);
    m->m[0][2] = 2 * (x * z + w * y);
    m->m[1][0] = 2 * (x * y + w * z);
    m->m[1][1] = 1 - 2 * (x * x + z * z);
    m->m[1][2] = 2 * (y * z - w * x);
    m->m[2][0] = 2 * (x * z - w * y);
    m->m[2][1] = 2 * (y * z + w * x);
    m->m[2][2] = 1 - 2 * (x * x + y * y);
}

/* D3DRM AddRotation/SetRotation: a positive angle about +y turns +z towards +x (left-handed,
 * clockwise looking down the axis), i.e. the conjugate of the animation-key quaternion above.
 * With the opposite sign the movement code settles facing away and walks backwards. */
static void axis_angle_mat(mat4 *m, float x, float y, float z, float theta)
{
    vec3 a = vnorm(v3(x, y, z));
    float s = -sinf(theta / 2);
    quat_to_mat(m, cosf(theta / 2), a.x * s, a.y * s, a.z * s);
}

/* ------------------------------------------------------------------ objects */

static obj_t *get(rmh h, int type)
{
    if (!h) return NULL;
    uint32_t i = h - HANDLE_BASE;
    if (i >= MAX_OBJS || !objs[i] || (type && objs[i]->type != type)) return NULL;
    return objs[i];
}

static rmh alloc_obj(int type)
{
    for (int tries = 0; tries < MAX_OBJS; tries++) {
        int i = next_free;
        next_free = next_free + 1 >= MAX_OBJS ? 1 : next_free + 1;
        if (!objs[i]) {
            objs[i] = calloc(1, sizeof(obj_t));
            objs[i]->type = type;
            objs[i]->refs = 1;
            return HANDLE_BASE + i;
        }
    }
    vm_fatal("rm: object table full");
    return 0;
}

static void destroy(rmh h);

void rm_addref(rmh h) { obj_t *o = get(h, 0); if (o) o->refs++; }

void rm_release(rmh h)
{
    obj_t *o = get(h, 0);
    if (o && --o->refs <= 0) destroy(h);
}

int rm_is_frame(rmh h) { return get(h, T_FRAME) != NULL; }
void rm_set_appdata(rmh h, uint32_t v) { obj_t *o = get(h, 0); if (o) o->appdata = v; }
uint32_t rm_get_appdata(rmh h) { obj_t *o = get(h, 0); return o ? o->appdata : 0; }
void rm_set_name(rmh h, const char *n) { obj_t *o = get(h, 0); if (o) snprintf(o->name, sizeof o->name, "%s", n ? n : ""); }
const char *rm_get_name(rmh h) { obj_t *o = get(h, 0); return o ? o->name : ""; }

rmh rm_named_object(const char *name)
{
    for (int i = 1; i < MAX_OBJS; i++)
        if (objs[i] && objs[i]->type == T_FRAME && !strcmp(objs[i]->name, name)) return HANDLE_BASE + i;
    return 0;
}

static void batches_free(meshb_t *m)
{
    for (int i = 0; i < m->nbatches; i++) plat_static_free(m->batches[i].gpu);
    free(m->batches);
    m->batches = NULL;
    m->nbatches = 0;
}

static void meshb_free(meshb_t *m)
{
    for (int i = 0; i < m->nf; i++) if (m->fmat[i].tex) rm_release(m->fmat[i].tex);
    free(m->pos); free(m->uv); free(m->idx); free(m->fstart); free(m->fcount); free(m->fmat);
    batches_free(m); free(m->verts);
    memset(m, 0, sizeof *m);
}

void rm_add_destroy_callback(rmh h, rm_destroy_cb fn, uint32_t cb, uint32_t arg)
{
    obj_t *o = get(h, 0);
    if (!o) return;
    o->dcb = fn;
    o->dcb_fn = cb;
    o->dcb_arg = arg;
}

static void destroy(rmh h)
{
    obj_t *o = get(h, 0);
    uint32_t i = h - HANDLE_BASE;
    if (o->dcb) o->dcb(h, o->dcb_fn, o->dcb_arg);
    switch (o->type) {
    case T_FRAME:
        for (int k = 0; k < o->fr.nkids; k++) {
            obj_t *c = get(o->fr.kids[k], T_FRAME);
            if (c) c->fr.parent = 0;
            rm_release(o->fr.kids[k]);
        }
        for (int k = 0; k < o->fr.nvis; k++) rm_release(o->fr.vis[k]);
        free(o->fr.kids);
        free(o->fr.vis);
        break;
    case T_MESHB: meshb_free(&o->mb); break;
    case T_ANIMSET:
        for (int k = 0; k < o->as.n; k++) {
            free(o->as.anims[k].rot); free(o->as.anims[k].scl); free(o->as.anims[k].pos);
        }
        free(o->as.anims);
        break;
    case T_TEXTURE:
        plat_tex_destroy(o->tex.plat);
        free(o->tex.rgba);
        break;
    case T_FACE: rm_release(o->face.mb); break;
    case T_USERVIS: free(o->uv.arg); break;   /* the callback context is owned by the visual */
    }
    free(o);
    objs[i] = NULL;
}

void rm_init(void) { }

/* ------------------------------------------------------------------ frames */

#define FR(h) (&get(h, T_FRAME)->fr)

rmh rm_frame_create(rmh parent)
{
    rmh h = alloc_obj(T_FRAME);
    obj_t *o = get(h, T_FRAME);
    mat_identity(&o->fr.local);
    if (parent) {
        rm_frame_add_child(parent, h);
    }
    return h;
}

rmh rm_frame_parent(rmh f) { obj_t *o = get(f, T_FRAME); return o ? o->fr.parent : 0; }

static void list_add(rmh **arr, int *n, rmh v)
{
    *arr = realloc(*arr, (*n + 1) * sizeof(rmh));
    (*arr)[(*n)++] = v;
}

static int list_remove(rmh *arr, int *n, rmh v)
{
    for (int i = 0; i < *n; i++)
        if (arr[i] == v) {
            memmove(arr + i, arr + i + 1, (*n - i - 1) * sizeof(rmh));
            (*n)--;
            return 1;
        }
    return 0;
}

void rm_frame_add_child(rmh f, rmh child)
{
    obj_t *p = get(f, T_FRAME), *c = get(child, T_FRAME);
    if (!p || !c || c->fr.parent == f) return;
    rm_addref(child);
    if (c->fr.parent) rm_frame_delete_child(c->fr.parent, child);
    c->fr.parent = f;
    list_add(&p->fr.kids, &p->fr.nkids, child);
}

void rm_frame_delete_child(rmh f, rmh child)
{
    obj_t *p = get(f, T_FRAME), *c = get(child, T_FRAME);
    if (!p || !c) return;
    if (list_remove(p->fr.kids, &p->fr.nkids, child)) {
        c->fr.parent = 0;
        rm_release(child);
    }
}

void rm_frame_add_visual(rmh f, rmh v)
{
    obj_t *p = get(f, T_FRAME);
    if (!p || !get(v, 0)) return;
    rm_addref(v);
    list_add(&p->fr.vis, &p->fr.nvis, v);
}

void rm_frame_delete_visual(rmh f, rmh v)
{
    obj_t *p = get(f, T_FRAME);
    if (p && list_remove(p->fr.vis, &p->fr.nvis, v)) rm_release(v);
}

int rm_frame_child_count(rmh f) { obj_t *o = get(f, T_FRAME); return o ? o->fr.nkids : 0; }
rmh rm_frame_child(rmh f, int i) { obj_t *o = get(f, T_FRAME); return o && i < o->fr.nkids ? o->fr.kids[i] : 0; }
int rm_frame_visual_count(rmh f) { obj_t *o = get(f, T_FRAME); return o ? o->fr.nvis : 0; }
rmh rm_frame_visual(rmh f, int i) { obj_t *o = get(f, T_FRAME); return o && i < o->fr.nvis ? o->fr.vis[i] : 0; }

void rm_frame_world(rmh f, mat4 *out)
{
    obj_t *o = get(f, T_FRAME);
    if (!o) { mat_identity(out); return; }
    if (o->fr.parent) {
        mat4 pw;
        rm_frame_world(o->fr.parent, &pw);
        mat_mul(out, &o->fr.local, &pw);
    } else {
        *out = o->fr.local;
    }
}

void rm_frame_get_transform(rmh f, mat4 *out)
{
    obj_t *o = get(f, T_FRAME);
    if (o) *out = o->fr.local; else mat_identity(out);
}

/* world matrix of the reference frame (NULL reference = the frame's parent) */
static void ref_world(rmh f, rmh ref, mat4 *out)
{
    if (ref) rm_frame_world(ref, out);
    else rm_frame_world(rm_frame_parent(f), out);
}

/* Replace a frame's local matrix so its world matrix becomes `w`. */
static void set_world(rmh f, const mat4 *w)
{
    obj_t *o = get(f, T_FRAME);
    if (!o) return;
    if (o->fr.parent) {
        mat4 pw, ipw;
        rm_frame_world(o->fr.parent, &pw);
        mat_invert_affine(&ipw, &pw);
        mat_mul(&o->fr.local, w, &ipw);
    } else {
        o->fr.local = *w;
    }
}

void rm_frame_set_position(rmh f, rmh ref, float x, float y, float z)
{
    obj_t *o = get(f, T_FRAME);
    if (!o) return;
    mat4 rw, w;
    vec3 p = { x, y, z };
    ref_world(f, ref, &rw);
    vec_xform(&p, &p, &rw);
    rm_frame_world(f, &w);
    w.m[3][0] = p.x; w.m[3][1] = p.y; w.m[3][2] = p.z;
    set_world(f, &w);
}

void rm_frame_get_position(rmh f, rmh ref, vec3 *out)
{
    mat4 w, rw, irw;
    rm_frame_world(f, &w);
    vec3 p = { w.m[3][0], w.m[3][1], w.m[3][2] };
    if (!ref) { *out = p; return; }   /* world space */
    rm_frame_world(ref, &rw);
    mat_invert_affine(&irw, &rw);
    vec_xform(out, &p, &irw);
}

static void set_world_rotation(rmh f, const mat4 *rot_world)
{
    mat4 w;
    rm_frame_world(f, &w);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) w.m[i][j] = rot_world->m[i][j];
    set_world(f, &w);
}

/* rotation whose z axis is `dir` and whose y axis is `up` made orthogonal to it */
static void orient_mat(mat4 *r, float dx, float dy, float dz, float ux, float uy, float uz)
{
    vec3 d = vnorm(v3(dx, dy, dz));
    vec3 x = vnorm(vcross(v3(ux, uy, uz), d));
    vec3 y = vcross(d, x);
    mat_identity(r);
    r->m[0][0] = x.x; r->m[0][1] = x.y; r->m[0][2] = x.z;
    r->m[1][0] = y.x; r->m[1][1] = y.y; r->m[1][2] = y.z;
    r->m[2][0] = d.x; r->m[2][1] = d.y; r->m[2][2] = d.z;
}

void rm_frame_set_orientation(rmh f, rmh ref, float dx, float dy, float dz, float ux, float uy, float uz)
{
    mat4 r, rw, w;
    orient_mat(&r, dx, dy, dz, ux, uy, uz);
    ref_world(f, ref, &rw);
    rw.m[3][0] = rw.m[3][1] = rw.m[3][2] = 0;
    mat_mul(&w, &r, &rw);
    set_world_rotation(f, &w);
}

void rm_frame_get_orientation(rmh f, rmh ref, vec3 *dir, vec3 *up)
{
    mat4 w, rw, irw;
    rm_frame_world(f, &w);
    vec3 z = { w.m[2][0], w.m[2][1], w.m[2][2] }, y = { w.m[1][0], w.m[1][1], w.m[1][2] };
    if (!ref) { *dir = z; *up = y; return; }   /* world space */
    rm_frame_world(ref, &rw);
    mat_invert_affine(&irw, &rw);
    vec_xform_dir(dir, &z, &irw);
    vec_xform_dir(up, &y, &irw);
}

void rm_frame_set_quaternion(rmh f, rmh ref, float w, float x, float y, float z)
{
    mat4 r, rw, out;
    quat_to_mat(&r, w, x, y, z);
    ref_world(f, ref, &rw);
    rw.m[3][0] = rw.m[3][1] = rw.m[3][2] = 0;
    mat_mul(&out, &r, &rw);
    set_world_rotation(f, &out);
}

void rm_frame_set_rotation(rmh f, rmh ref, float x, float y, float z, float theta)
{
    obj_t *o = get(f, T_FRAME);
    if (!o) return;
    o->fr.rot_axis = v3(x, y, z);
    o->fr.rot_theta = theta;
    o->fr.rot_ref = ref;
}

void rm_frame_set_velocity(rmh f, rmh ref, float x, float y, float z, int with_rotation)
{
    obj_t *o = get(f, T_FRAME);
    if (!o) return;
    mat4 rw;
    vec3 v = { x, y, z };
    if (ref) { rm_frame_world(ref, &rw); vec_xform_dir(&v, &v, &rw); }
    o->fr.vel = v;
    o->fr.vel_rot = with_rotation;
}

static void combine(rmh f, int how, const mat4 *m)
{
    obj_t *o = get(f, T_FRAME);
    if (!o) return;
    if (how == RMCOMBINE_REPLACE) o->fr.local = *m;
    else if (how == RMCOMBINE_BEFORE) mat_mul(&o->fr.local, m, &o->fr.local);
    else mat_mul(&o->fr.local, &o->fr.local, m);
}

void rm_frame_add_scale(rmh f, int how, float sx, float sy, float sz)
{
    mat4 m;
    mat_identity(&m);
    m.m[0][0] = sx; m.m[1][1] = sy; m.m[2][2] = sz;
    combine(f, how, &m);
}

void rm_frame_add_rotation(rmh f, int how, float x, float y, float z, float theta)
{
    mat4 m;
    axis_angle_mat(&m, x, y, z, theta);
    combine(f, how, &m);
}

void rm_frame_add_translation(rmh f, int how, float x, float y, float z)
{
    mat4 m;
    mat_identity(&m);
    m.m[3][0] = x; m.m[3][1] = y; m.m[3][2] = z;
    combine(f, how, &m);
}

void rm_frame_add_transform(rmh f, int how, const mat4 *m) { combine(f, how, m); }

void rm_frame_transform(rmh f, vec3 *dst, const vec3 *src)
{
    mat4 w;
    rm_frame_world(f, &w);
    vec_xform(dst, src, &w);
}

void rm_frame_inverse_transform(rmh f, vec3 *dst, const vec3 *src)
{
    mat4 w, iw;
    rm_frame_world(f, &w);
    mat_invert_affine(&iw, &w);
    vec_xform(dst, src, &iw);
}

void rm_frame_lookat(rmh f, rmh target, rmh ref, int constraint)
{
    (void)constraint;
    vec3 tp, fp, up = { 0, 1, 0 };
    mat4 rw;
    rm_frame_get_position(target, 0, &tp);
    rm_frame_get_position(f, 0, &fp);
    if (ref) {
        rm_frame_world(ref, &rw);
        up = v3(rw.m[1][0], rw.m[1][1], rw.m[1][2]);
    }
    /* positions and up are world space, so the rotation is too (a null ref in SetOrientation would
     * mean the parent, which is wrong for particles under a rotated effect frame) */
    mat4 r;
    orient_mat(&r, tp.x - fp.x, tp.y - fp.y, tp.z - fp.z, up.x, up.y, up.z);
    set_world_rotation(f, &r);
}

void rm_frame_move(rmh f, float delta)
{
    obj_t *o = get(f, T_FRAME);
    if (!o) return;
    if (o->fr.rot_theta != 0) {
        /* the axis is given in the reference frame (null = parent); rotate about it through the
         * frame's own origin. A frame used as its own reference spins about its local axis. */
        mat4 r;
        vec3 a = o->fr.rot_axis;
        int own = o->fr.rot_ref == f;
        if (!own && o->fr.rot_ref && o->fr.rot_ref != o->fr.parent) {
            mat4 rw, pw, ipw;
            rm_frame_world(o->fr.rot_ref, &rw);
            vec_xform_dir(&a, &a, &rw);
            rm_frame_world(o->fr.parent, &pw);
            mat_invert_affine(&ipw, &pw);
            vec_xform_dir(&a, &a, &ipw);
        }
        axis_angle_mat(&r, a.x, a.y, a.z, o->fr.rot_theta * delta);
        vec3 t = { o->fr.local.m[3][0], o->fr.local.m[3][1], o->fr.local.m[3][2] };
        o->fr.local.m[3][0] = o->fr.local.m[3][1] = o->fr.local.m[3][2] = 0;
        if (own) mat_mul(&o->fr.local, &r, &o->fr.local);
        else mat_mul(&o->fr.local, &o->fr.local, &r);
        o->fr.local.m[3][0] = t.x; o->fr.local.m[3][1] = t.y; o->fr.local.m[3][2] = t.z;
    }
    if (o->fr.vel.x != 0 || o->fr.vel.y != 0 || o->fr.vel.z != 0) {
        vec3 p;
        rm_frame_get_position(f, 0, &p);
        rm_frame_set_position(f, 0, p.x + o->fr.vel.x * delta, p.y + o->fr.vel.y * delta, p.z + o->fr.vel.z * delta);
    }
    for (int i = 0; i < o->fr.nkids; i++) rm_frame_move(o->fr.kids[i], delta);
}

void rm_frame_set_color(rmh f, uint32_t argb) { (void)f; (void)argb; }
void rm_frame_set_zbuffer_mode(rmh f, int mode) { (void)f; (void)mode; }
void rm_frame_set_sort_mode(rmh f, int mode) { (void)f; (void)mode; }

void rm_scene_set_background(rmh s, uint32_t argb) { obj_t *o = get(s, T_FRAME); if (o) o->fr.bg = argb; }
uint32_t rm_scene_background(rmh s) { obj_t *o = get(s, T_FRAME); return o ? o->fr.bg : 0; }
void rm_scene_set_fog_enable(rmh s, int on) { obj_t *o = get(s, T_FRAME); if (o) o->fr.fog_on = on; }
void rm_scene_set_fog_params(rmh s, float a, float b, float d)
{
    (void)d;
    obj_t *o = get(s, T_FRAME);
    if (o) { o->fr.fog_start = a; o->fr.fog_end = b; }
}
void rm_scene_set_fog_color(rmh s, uint32_t argb) { obj_t *o = get(s, T_FRAME); if (o) o->fr.fog_color = argb; }

/* ------------------------------------------------------------------ textures */

static int pow2(int v) { int p = 1; while (p < v) p <<= 1; return p; }

rmh rm_texture_from_rgba(int w, int h, const uint8_t *rgba)
{
    rmh t = alloc_obj(T_TEXTURE);
    tex_t *x = &get(t, T_TEXTURE)->tex;
    x->w = pow2(w);
    x->h = pow2(h);
    x->rgba = calloc((size_t)x->w * x->h, 4);
    for (int y = 0; y < x->h; y++)
        memcpy(x->rgba + (size_t)y * x->w * 4, rgba + (size_t)(y % h) * w * 4, (size_t)w * 4);
    x->plat = plat_tex_create(x->w, x->h, x->rgba);
    return t;
}

static rmh tex_cache_find(const char *name)
{
    for (int i = 1; i < MAX_OBJS; i++)
        if (objs[i] && objs[i]->type == T_TEXTURE && !strcmp(objs[i]->tex.name, name)) return HANDLE_BASE + i;
    return 0;
}

rmh rm_texture_load(const char *path)
{
    char key[96];
    snprintf(key, sizeof key, "%s", path);
    for (char *p = key; *p; p++) *p = (char)tolower((unsigned char)*p);
    rmh t = tex_cache_find(key);
    if (t) { rm_addref(t); return t; }
    bmp_t b;
    if (!bmp_load(path, &b)) {
        plat_log("rm: missing texture '%s'", path);
        return 0;
    }
    /* D3DRM keeps the DIB's bottom-up row order: v = 0 is the image's bottom row (the face
     * meshes put the chin at v = 0), so store the rows flipped. */
    uint8_t *rgba = malloc((size_t)b.w * b.h * 4);
    for (int i = 0; i < b.w * b.h; i++) {
        const uint8_t *s = b.rgb + ((size_t)(b.h - 1 - i / b.w) * b.w + i % b.w) * 3;
        rgba[i * 4] = s[0];
        rgba[i * 4 + 1] = s[1];
        rgba[i * 4 + 2] = s[2];
        rgba[i * 4 + 3] = 255;
    }
    t = rm_texture_from_rgba(b.w, b.h, rgba);
    snprintf(get(t, T_TEXTURE)->tex.name, sizeof key, "%s", key);
    free(rgba);
    bmp_free(&b);
    return t;
}

int rm_texture_plat_id(rmh t) { obj_t *o = get(t, T_TEXTURE); return o ? o->tex.plat : 0; }

void rm_texture_set_transparency(rmh t, int on, uint32_t key)
{
    obj_t *o = get(t, T_TEXTURE);
    if (!o) return;
    tex_t *x = &o->tex;
    for (int i = 0; i < x->w * x->h; i++) {
        uint8_t *p = x->rgba + i * 4;
        uint32_t c = p[0] << 16 | p[1] << 8 | p[2];
        p[3] = on && c == (key & 0xffffff) ? 0 : 255;
    }
    plat_tex_update(x->plat, x->rgba);
}

/* ------------------------------------------------------------------ mesh builders */

rmh rm_mb_create(void) { return alloc_obj(T_MESHB); }

static void mat_from_x(rmmat_t *m, const xnode_t *mat)
{
    xcur_t c = { mat, 0 };
    float r = xc_float(&c), g = xc_float(&c), b = xc_float(&c), a = xc_float(&c);
    xc_float(&c); /* power */
    xc_float(&c); xc_float(&c); xc_float(&c); /* specular */
    m->emissive[0] = xc_float(&c);
    m->emissive[1] = xc_float(&c);
    m->emissive[2] = xc_float(&c);
#define C8(v) ((uint32_t)((v) < 0 ? 0 : (v) > 1 ? 255 : (v) * 255.0f + 0.5f))
    m->color = C8(a) << 24 | C8(r) << 16 | C8(g) << 8 | C8(b);
    m->tex = 0;
    for (int i = 0; i < mat->nkids; i++)
        if (!strcmp(mat->kids[i]->tmpl, "TextureFilename") && mat->kids[i]->nstrs && mat->kids[i]->strs[0][0])
            m->tex = rm_texture_load(mat->kids[i]->strs[0]);
}

/* Append an X Mesh node (transformed by `xf`) to a mesh builder. */
static void mb_add_xmesh(meshb_t *m, xnode_t *root, const xnode_t *mesh, const mat4 *xf)
{
    xcur_t c = { mesh, 0 };
    int nv = xc_int(&c);
    int v0 = m->nv;
    m->pos = realloc(m->pos, (m->nv + nv) * sizeof(vec3));
    m->uv = realloc(m->uv, (m->nv + nv) * 2 * sizeof(float));
    for (int i = 0; i < nv; i++) {
        vec3 p;
        p.x = xc_float(&c); p.y = xc_float(&c); p.z = xc_float(&c);
        if (xf) vec_xform(&p, &p, xf);
        m->pos[v0 + i] = p;
        m->uv[(v0 + i) * 2] = m->uv[(v0 + i) * 2 + 1] = 0;
    }
    m->nv += nv;
    int nf = xc_int(&c);
    int f0 = m->nf;
    m->fstart = realloc(m->fstart, (m->nf + nf) * sizeof(int));
    m->fcount = realloc(m->fcount, (m->nf + nf) * sizeof(int));
    m->fmat = realloc(m->fmat, (m->nf + nf) * sizeof(rmmat_t));
    int nidx = 0;
    for (int i = 0; i < m->nf; i++) nidx += m->fcount[i];
    for (int i = 0; i < nf; i++) {
        int n = xc_int(&c);
        m->idx = realloc(m->idx, (nidx + n) * sizeof(int));
        m->fstart[f0 + i] = nidx;
        m->fcount[f0 + i] = n;
        for (int k = 0; k < n; k++) m->idx[nidx + k] = v0 + xc_int(&c);
        nidx += n;
        rmmat_t def = { { 0, 0, 0 }, 0xffffffffu, 0 };
        m->fmat[f0 + i] = def;
    }
    m->nf += nf;

    for (int k = 0; k < mesh->nkids; k++) {
        const xnode_t *ch = mesh->kids[k];
        if (!strcmp(ch->tmpl, "MeshTextureCoords")) {
            xcur_t t = { ch, 0 };
            int n = xc_int(&t);
            for (int i = 0; i < n && i < nv; i++) {
                m->uv[(v0 + i) * 2] = xc_float(&t);
                m->uv[(v0 + i) * 2 + 1] = xc_float(&t);
            }
        } else if (!strcmp(ch->tmpl, "MeshMaterialList")) {
            xcur_t t = { ch, 0 };
            int nm = xc_int(&t), nfi = xc_int(&t);
            rmmat_t *mats = calloc(nm > 0 ? nm : 1, sizeof(rmmat_t));
            int mi = 0;
            for (int j = 0; j < ch->nkids && mi < nm; j++) {
                const xnode_t *mn = ch->kids[j];
                if (!strcmp(mn->tmpl, "Material")) mat_from_x(&mats[mi++], mn);
                else if (!strcmp(mn->tmpl, "REF") || (!mn->tmpl[0] && mn->nkids)) {
                    const char *nm2 = !strcmp(mn->tmpl, "REF") ? mn->name : mn->kids[0]->name;
                    xnode_t *ref = x_find_named(root, "Material", nm2);
                    if (ref) mat_from_x(&mats[mi++], ref);
                }
            }
            for (int i = 0; i < nf; i++) {
                int fi = i < nfi ? xc_int(&t) : (nfi ? 0 : 0);
                if (fi < 0 || fi >= mi) fi = 0;
                if (mi) {
                    m->fmat[f0 + i] = mats[fi];
                    if (mats[fi].tex) rm_addref(mats[fi].tex);
                }
            }
            for (int i = 0; i < mi; i++) if (mats[i].tex) rm_release(mats[i].tex);
            free(mats);
        }
    }
    m->dirty = 1;
}

int rm_mb_load(rmh h, const char *path)
{
    obj_t *o = get(h, T_MESHB);
    if (!o) return 0;
    xnode_t *root = x_load(path);
    if (!root) { plat_log("rm: cannot load mesh '%s'", path); return 0; }
    for (int i = 0; i < root->nkids; i++)
        if (!strcmp(root->kids[i]->tmpl, "Mesh")) { mb_add_xmesh(&o->mb, root, root->kids[i], NULL); break; }
    x_free(root);
    return 1;
}

static meshb_t *MB(rmh h) { obj_t *o = get(h, T_MESHB); return o ? &o->mb : NULL; }

void rm_mb_set_color(rmh h, uint32_t argb)
{
    meshb_t *m = MB(h);
    if (!m) return;
    for (int i = 0; i < m->nf; i++) m->fmat[i].color = argb;
    m->dirty = 1;
}

void rm_mb_set_color_rgb(rmh h, float r, float g, float b)
{
    rm_mb_set_color(h, 0xff000000u | C8(r) << 16 | C8(g) << 8 | C8(b));
}

void rm_mb_set_texture(rmh h, rmh tex)
{
    meshb_t *m = MB(h);
    if (!m) return;
    for (int i = 0; i < m->nf; i++) {
        if (tex) rm_addref(tex);
        if (m->fmat[i].tex) rm_release(m->fmat[i].tex);
        m->fmat[i].tex = tex;
    }
    m->dirty = 1;
}

void rm_mb_set_emissive(rmh h, float r, float g, float b)
{
    meshb_t *m = MB(h);
    if (!m) return;
    for (int i = 0; i < m->nf; i++) {
        m->fmat[i].emissive[0] = r; m->fmat[i].emissive[1] = g; m->fmat[i].emissive[2] = b;
    }
    m->dirty = 1;
}

void rm_mb_scale(rmh h, float sx, float sy, float sz)
{
    meshb_t *m = MB(h);
    if (!m) return;
    for (int i = 0; i < m->nv; i++) { m->pos[i].x *= sx; m->pos[i].y *= sy; m->pos[i].z *= sz; }
    m->dirty = 1;
}

void rm_mb_translate(rmh h, float x, float y, float z)
{
    meshb_t *m = MB(h);
    if (!m) return;
    for (int i = 0; i < m->nv; i++) { m->pos[i].x += x; m->pos[i].y += y; m->pos[i].z += z; }
    m->dirty = 1;
}

void rm_mb_set_quality(rmh h, uint32_t q) { (void)h; (void)q; }
void rm_mb_set_perspective(rmh h, int on) { (void)h; (void)on; }
int rm_mb_face_count(rmh h) { meshb_t *m = MB(h); return m ? m->nf : 0; }
int rm_mb_vertex_count(rmh h) { meshb_t *m = MB(h); return m ? m->nv : 0; }

int rm_mb_face_vertices(rmh h, int face, vec3 *out, int max)
{
    meshb_t *m = MB(h);
    if (!m || face < 0 || face >= m->nf) return 0;
    int n = m->fcount[face];
    for (int i = 0; i < n && i < max; i++) out[i] = m->pos[m->idx[m->fstart[face] + i]];
    return n;
}

int rm_mb_face_uvs(rmh h, int face, float *uv, int max)
{
    meshb_t *m = MB(h);
    if (!m || face < 0 || face >= m->nf) return 0;
    int n = m->fcount[face];
    for (int i = 0; i < n && i < max; i++) {
        int vi = m->idx[m->fstart[face] + i];
        uv[i * 2] = m->uv[vi * 2];
        uv[i * 2 + 1] = m->uv[vi * 2 + 1];
    }
    return n;
}

void rm_mb_box(rmh h, vec3 *mn, vec3 *mx)
{
    meshb_t *m = MB(h);
    *mn = v3(0, 0, 0); *mx = v3(0, 0, 0);
    if (!m || !m->nv) return;
    *mn = *mx = m->pos[0];
    for (int i = 1; i < m->nv; i++) {
        vec3 p = m->pos[i];
        if (p.x < mn->x) mn->x = p.x; if (p.y < mn->y) mn->y = p.y; if (p.z < mn->z) mn->z = p.z;
        if (p.x > mx->x) mx->x = p.x; if (p.y > mx->y) mx->y = p.y; if (p.z > mx->z) mx->z = p.z;
    }
}

rmh rm_mb_face_handle(rmh mb, int face)
{
    rmh h = alloc_obj(T_FACE);
    obj_t *o = get(h, T_FACE);
    o->face.mb = mb;
    o->face.face = face;
    rm_addref(mb);
    return h;
}

void rm_face_set_color(rmh f, uint32_t argb)
{
    obj_t *o = get(f, T_FACE);
    meshb_t *m = o ? MB(o->face.mb) : NULL;
    if (!m || o->face.face >= m->nf) return;
    m->fmat[o->face.face].color = argb;
    m->dirty = 1;
}

void rm_face_set_texture(rmh f, rmh tex)
{
    obj_t *o = get(f, T_FACE);
    meshb_t *m = o ? MB(o->face.mb) : NULL;
    if (!m || o->face.face >= m->nf) return;
    if (tex) rm_addref(tex);
    if (m->fmat[o->face.face].tex) rm_release(m->fmat[o->face.face].tex);
    m->fmat[o->face.face].tex = tex;
    m->dirty = 1;
}

void rm_mb_textures_transparent(rmh h, uint32_t key)
{
    meshb_t *m = MB(h);
    if (!m) return;
    for (int i = 0; i < m->nf; i++) {
        rmh t = m->fmat[i].tex;
        obj_t *o = get(t, T_TEXTURE);
        if (!o || o->tex.name[0] == '*') continue;   /* already a private copy */
        rmh c = alloc_obj(T_TEXTURE);
        tex_t *x = &get(c, T_TEXTURE)->tex;
        x->w = o->tex.w; x->h = o->tex.h;
        x->rgba = malloc((size_t)x->w * x->h * 4);
        memcpy(x->rgba, o->tex.rgba, (size_t)x->w * x->h * 4);
        x->plat = plat_tex_create(x->w, x->h, x->rgba);
        x->name[0] = '*';
        rm_texture_set_transparency(c, 1, key);
        for (int j = i; j < m->nf; j++)
            if (m->fmat[j].tex == t) { rm_addref(c); rm_release(t); m->fmat[j].tex = c; }
        rm_release(c);
    }
    m->dirty = 1;
}

static uint32_t face_rgba(const rmmat_t *fm)
{
    return (fm->color & 0xff000000u) | C8(fm->emissive[0]) << 16 | C8(fm->emissive[1]) << 8 | C8(fm->emissive[2]);
}

/* bounding sphere (box centre, farthest vertex) of verts [start, end): culling and the GE path */
static void batch_sphere(const pvert_t *vs, int start, int end, float sphere[4])
{
    vec3 lo = { 1e30f, 1e30f, 1e30f }, hi = { -1e30f, -1e30f, -1e30f };
    for (int k = start; k < end; k++) {
        const pvert_t *v = &vs[k];
        if (v->x < lo.x) lo.x = v->x; if (v->y < lo.y) lo.y = v->y; if (v->z < lo.z) lo.z = v->z;
        if (v->x > hi.x) hi.x = v->x; if (v->y > hi.y) hi.y = v->y; if (v->z > hi.z) hi.z = v->z;
    }
    vec3 c = v3((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2);
    float r2 = 0;
    for (int k = start; k < end; k++) {
        const pvert_t *v = &vs[k];
        float dx = v->x - c.x, dy = v->y - c.y, dz = v->z - c.z, d = dx * dx + dy * dy + dz * dz;
        if (d > r2) r2 = d;
    }
    sphere[0] = c.x; sphere[1] = c.y; sphere[2] = c.z; sphere[3] = sqrtf(r2) * 1.001f + 1e-4f;
}

/* Large opaque batches (a level's floor or hedges under one texture) are split into compact chunks
 * of at most CHUNK_TRIS triangles: a whole-room batch always reaches past the near plane and would
 * take the CPU clip path every frame, while small chunks can be frustum culled and only the few
 * near the camera need clipping. Recursive median split along the longest axis of the centroids. */
#define CHUNK_TRIS 64
typedef struct { pvert_t v[3]; } tri3_t;
static int split_axis;
static int tri_cmp(const void *a, const void *b)
{
    const pvert_t *p = ((const tri3_t *)a)->v, *q = ((const tri3_t *)b)->v;
    float ca, cb;
    if (split_axis == 0) { ca = p[0].x + p[1].x + p[2].x; cb = q[0].x + q[1].x + q[2].x; }
    else if (split_axis == 1) { ca = p[0].y + p[1].y + p[2].y; cb = q[0].y + q[1].y + q[2].y; }
    else { ca = p[0].z + p[1].z + p[2].z; cb = q[0].z + q[1].z + q[2].z; }
    return ca < cb ? -1 : ca > cb;
}

static void emit_chunks(meshb_t *m, int tex, uint32_t col, int start, int end)
{
    int ntri = (end - start) / 3;
    if (ntri > CHUNK_TRIS && (col >> 24) == 255) {
        float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
        for (int k = start; k < end; k++) {
            const float c[3] = { m->verts[k].x, m->verts[k].y, m->verts[k].z };
            for (int a = 0; a < 3; a++) { if (c[a] < lo[a]) lo[a] = c[a]; if (c[a] > hi[a]) hi[a] = c[a]; }
        }
        split_axis = hi[0] - lo[0] >= hi[1] - lo[1] ? 0 : 1;
        if (hi[2] - lo[2] > hi[split_axis] - lo[split_axis]) split_axis = 2;
        qsort(m->verts + start, (size_t)ntri, sizeof(tri3_t), tri_cmp);
        int mid = start + ntri / 2 * 3;
        emit_chunks(m, tex, col, start, mid);
        emit_chunks(m, tex, col, mid, end);
        return;
    }
    m->batches = realloc(m->batches, (m->nbatches + 1) * sizeof *m->batches);
    struct batch *b = &m->batches[m->nbatches++];
    b->tex = tex; b->color = col; b->start = start; b->count = end - start; b->gpu = NULL;
    batch_sphere(m->verts, start, end, b->sphere);
}

static void mb_build(meshb_t *m)
{
    batches_free(m);
    free(m->verts);
    int ntri = 0;
    for (int i = 0; i < m->nf; i++) if (m->fcount[i] >= 3) ntri += m->fcount[i] - 2;
    m->verts = malloc((ntri ? ntri : 1) * 3 * sizeof(pvert_t));
    char *done = calloc(m->nf ? m->nf : 1, 1);
    int out = 0;
    for (int i = 0; i < m->nf; i++) {
        if (done[i]) continue;
        int tex = rm_texture_plat_id(m->fmat[i].tex);
        uint32_t col = face_rgba(&m->fmat[i]);
        int start = out;
        for (int j = i; j < m->nf; j++) {
            if (done[j] || rm_texture_plat_id(m->fmat[j].tex) != tex || face_rgba(&m->fmat[j]) != col) continue;
            done[j] = 1;
            const int *ix = m->idx + m->fstart[j];
            for (int k = 1; k + 1 < m->fcount[j]; k++) {
                int tri[3] = { ix[0], ix[k], ix[k + 1] };
                for (int t = 0; t < 3; t++) {
                    pvert_t *v = &m->verts[out++];
                    v->x = m->pos[tri[t]].x; v->y = m->pos[tri[t]].y; v->z = m->pos[tri[t]].z;
                    v->u = m->uv[tri[t] * 2]; v->v = m->uv[tri[t] * 2 + 1];
                    v->color = col;
                }
            }
        }
        emit_chunks(m, tex, col, start, out);
    }
    free(done);
    vec3 mn, mx;
    m->center = v3(0, 0, 0);
    if (m->nv) {
        mn = mx = m->pos[0];
        for (int i = 1; i < m->nv; i++) {
            vec3 p = m->pos[i];
            if (p.x < mn.x) mn.x = p.x; if (p.y < mn.y) mn.y = p.y; if (p.z < mn.z) mn.z = p.z;
            if (p.x > mx.x) mx.x = p.x; if (p.y > mx.y) mx.y = p.y; if (p.z > mx.z) mx.z = p.z;
        }
        m->center = v3((mn.x + mx.x) / 2, (mn.y + mx.y) / 2, (mn.z + mx.z) / 2);
    }
    m->dirty = 0;
}

/* ------------------------------------------------------------------ animation sets */

rmh rm_animset_create(void) { return alloc_obj(T_ANIMSET); }

static void load_frame(xnode_t *root, xnode_t *xf, rmh parent, rmh *created, int *ncreated)
{
    rmh f = rm_frame_create(parent);
    rm_set_name(f, xf->name);
    if (*ncreated < 1024) created[(*ncreated)++] = f;
    for (int i = 0; i < xf->nkids; i++) {
        xnode_t *c = xf->kids[i];
        if (!strcmp(c->tmpl, "FrameTransformMatrix")) {
            obj_t *o = get(f, T_FRAME);
            xcur_t cur = { c, 0 };
            for (int r = 0; r < 4; r++)
                for (int k = 0; k < 4; k++) o->fr.local.m[r][k] = xc_float(&cur);
        } else if (!strcmp(c->tmpl, "Mesh")) {
            rmh mb = rm_mb_create();
            mb_add_xmesh(&get(mb, T_MESHB)->mb, root, c, NULL);
            rm_frame_add_visual(f, mb);
            rm_release(mb);
        } else if (!strcmp(c->tmpl, "Frame")) {
            load_frame(root, c, f, created, ncreated);
        }
    }
    rm_release(f); /* parent keeps it alive */
}

static void read_keys(const xnode_t *k, anim_t *a)
{
    xcur_t c = { k, 0 };
    int type = xc_int(&c), n = xc_int(&c);
    akey_t *keys = calloc(n > 0 ? n : 1, sizeof(akey_t));
    for (int i = 0; i < n; i++) {
        keys[i].time = (float)xc_int(&c);
        int nv = xc_int(&c);
        for (int j = 0; j < nv; j++) {
            float v = xc_float(&c);
            if (j < 4) keys[i].v[j] = v;
        }
    }
    if (type == 0) { a->rot = keys; a->nrot = n; }
    else if (type == 1) { a->scl = keys; a->nscl = n; }
    else if (type == 2) { a->pos = keys; a->npos = n; }
    else free(keys);
}

int rm_animset_load(rmh h, const char *path, rmh parent)
{
    obj_t *o = get(h, T_ANIMSET);
    if (!o) return 0;
    xnode_t *root = x_load(path);
    if (!root) { plat_log("rm: cannot load animset '%s'", path); return 0; }
    rmh created[1024];
    int nc = 0;
    for (int i = 0; i < root->nkids; i++) {
        xnode_t *c = root->kids[i];
        if (!strcmp(c->tmpl, "Frame")) load_frame(root, c, parent, created, &nc);
        else if (!strcmp(c->tmpl, "Mesh")) {
            rmh mb = rm_mb_create();
            mb_add_xmesh(&get(mb, T_MESHB)->mb, root, c, NULL);
            rm_frame_add_visual(parent, mb);
            rm_release(mb);
        }
    }
    for (int i = 0; i < root->nkids; i++) {
        xnode_t *set = root->kids[i];
        if (strcmp(set->tmpl, "AnimationSet")) continue;
        for (int j = 0; j < set->nkids; j++) {
            xnode_t *an = set->kids[j];
            if (strcmp(an->tmpl, "Animation")) continue;
            anim_t a = { 0 };
            for (int k = 0; k < an->nkids; k++) {
                xnode_t *c = an->kids[k];
                const char *ref = NULL;
                if (!strcmp(c->tmpl, "REF")) ref = c->name;
                else if (!c->tmpl[0] && c->nkids && !strcmp(c->kids[0]->tmpl, "REF")) ref = c->kids[0]->name;
                if (ref) {
                    for (int q = 0; q < nc; q++)
                        if (!strcmp(rm_get_name(created[q]), ref)) { a.frame = created[q]; break; }
                } else if (!strcmp(c->tmpl, "AnimationKey")) {
                    read_keys(c, &a);
                }
            }
            if (!a.frame) { free(a.rot); free(a.scl); free(a.pos); continue; }
            o->as.anims = realloc(o->as.anims, (o->as.n + 1) * sizeof(anim_t));
            o->as.anims[o->as.n++] = a;
        }
    }
    x_free(root);
    return 1;
}

static void interp(const akey_t *k, int n, float t, float out[4], int is_quat)
{
    if (n == 1 || t <= k[0].time) { memcpy(out, k[0].v, 16); return; }
    if (t >= k[n - 1].time) { memcpy(out, k[n - 1].v, 16); return; }
    int i = 0;
    while (i + 1 < n && k[i + 1].time < t) i++;
    float span = k[i + 1].time - k[i].time;
    float a = span > 0 ? (t - k[i].time) / span : 0;
    const float *p = k[i].v, *q = k[i + 1].v;
    if (!is_quat) {
        for (int j = 0; j < 3; j++) out[j] = p[j] + (q[j] - p[j]) * a;
        return;
    }
    float dot = p[0] * q[0] + p[1] * q[1] + p[2] * q[2] + p[3] * q[3];
    float sq = 1;
    if (dot < 0) { dot = -dot; sq = -1; }
    float s0, s1;
    if (dot > 0.9995f) { s0 = 1 - a; s1 = a; }
    else {
        float th = acosf(dot), st = sinf(th);
        s0 = sinf((1 - a) * th) / st;
        s1 = sinf(a * th) / st;
    }
    for (int j = 0; j < 4; j++) out[j] = s0 * p[j] + s1 * sq * q[j];
}

void rm_animset_set_time(rmh h, float t)
{
    obj_t *o = get(h, T_ANIMSET);
    if (!o) return;
    for (int i = 0; i < o->as.n; i++) {
        anim_t *a = &o->as.anims[i];
        obj_t *fo = get(a->frame, T_FRAME);
        if (!fo) continue;
        mat4 m, s, r;
        mat_identity(&m);
        if (a->nscl) {
            float v[4];
            interp(a->scl, a->nscl, t, v, 0);
            mat_identity(&s);
            s.m[0][0] = v[0]; s.m[1][1] = v[1]; s.m[2][2] = v[2];
            m = s;
        }
        if (a->nrot) {
            float q[4];
            interp(a->rot, a->nrot, t, q, 1);
            quat_to_mat(&r, q[0], q[1], q[2], q[3]);
            mat_mul(&m, &m, &r);
        } else if (a->nscl) {
            /* scale keys only: keep the frame's current rotation (its axes with the old scale removed) */
            mat_identity(&r);
            for (int x = 0; x < 3; x++) {
                vec3 ax = vnorm(v3(fo->fr.local.m[x][0], fo->fr.local.m[x][1], fo->fr.local.m[x][2]));
                r.m[x][0] = ax.x; r.m[x][1] = ax.y; r.m[x][2] = ax.z;
            }
            mat_mul(&m, &m, &r);
        }
        if (a->npos) {
            float p[4];
            interp(a->pos, a->npos, t, p, 0);
            m.m[3][0] = p[0]; m.m[3][1] = p[1]; m.m[3][2] = p[2];
        } else {
            m.m[3][0] = fo->fr.local.m[3][0]; m.m[3][1] = fo->fr.local.m[3][1]; m.m[3][2] = fo->fr.local.m[3][2];
        }
        if (a->nrot || a->nscl) fo->fr.local = m;
        else { fo->fr.local.m[3][0] = m.m[3][0]; fo->fr.local.m[3][1] = m.m[3][1]; fo->fr.local.m[3][2] = m.m[3][2]; }
    }
}

/* ------------------------------------------------------------------ user visuals */

rmh rm_uservisual_create(rm_user_cb cb, void *arg)
{
    rmh h = alloc_obj(T_USERVIS);
    obj_t *o = get(h, T_USERVIS);
    o->uv.cb = cb;
    o->uv.arg = arg;
    return h;
}

/* ------------------------------------------------------------------ viewport */

void rm_viewport_init(rmh camera, int w, int h)
{
    vp.camera = camera;
    vp.w = w;
    vp.h = h;
}

void rm_viewport_set_camera(rmh camera) { vp.camera = camera; }
void rm_viewport_set_front(float v) { if (v > 0) vp.front = v; } /* D3DRM rejects <= 0 */
void rm_viewport_set_back(float v) { if (v > vp.front) vp.back = v; }
void rm_viewport_set_field(float v) { if (v > 0) vp.field = v; }

static void update_projection(void)
{
#if FIELD_ON_LARGER_DIMENSION
    if (vp.w >= vp.h) { vp.sx = 1.0f / vp.field; vp.sy = vp.sx * vp.w / vp.h; }
    else { vp.sy = 1.0f / vp.field; vp.sx = vp.sy * vp.h / vp.w; }
#else
    if (vp.w >= vp.h) { vp.sy = 1.0f / vp.field; vp.sx = vp.sy * vp.h / vp.w; }
    else { vp.sx = 1.0f / vp.field; vp.sy = vp.sx * vp.w / vp.h; }
#endif
}

void rm_viewport_clear(rmh scene)
{
    plat_clear(1, rm_scene_background(scene) & 0xffffff, 1);
}

void rm_viewport_project(const vec3 *world, float *sx, float *sy, float *sz)
{
    vec3 v;
    vec_xform(&v, world, &vp.view);
    float z = v.z > 1e-6f ? v.z : 1e-6f;
    *sx = SCREEN_W * 0.5f * (1.0f + vp.sx * v.x / z);
    *sy = SCREEN_H * 0.5f * (1.0f - vp.sy * v.y / z);
    *sz = v.z;
}

typedef struct { float depth; mat4 mv; meshb_t *m; int batch; } tdraw_t;
static tdraw_t *tlist;
static int ntlist, captlist;

static void draw_mb(meshb_t *m, const mat4 *world)
{
    if (m->dirty) mb_build(m);
    mat4 mv;
    mat_mul(&mv, world, &vp.view);
    int set = 0;
    for (int i = 0; i < m->nbatches; i++) {
        struct batch *b = &m->batches[i];
        if ((b->color >> 24) < 255) {
            vec3 c;
            vec_xform(&c, &m->center, &mv);
            if (ntlist == captlist) {
                captlist = captlist ? captlist * 2 : 64;
                tlist = realloc(tlist, captlist * sizeof *tlist);
            }
            tdraw_t *t = &tlist[ntlist++];
            t->depth = c.z; t->mv = mv; t->m = m; t->batch = i;
            continue;
        }
        if (!set) { plat_set_modelview(&mv.m[0][0]); set = 1; }
        plat_draw_tris_static(b->tex, m->verts + b->start, b->count, &b->gpu, b->sphere, BLEND_NONE, 1, 1);
    }
}

static void render_frame(rmh f, const mat4 *parent_world, int depth)
{
    obj_t *o = get(f, T_FRAME);
    if (!o || depth > 64) return;
    mat4 w;
    mat_mul(&w, &o->fr.local, parent_world);
    for (int i = 0; i < o->fr.nvis; i++) {
        obj_t *v = get(o->fr.vis[i], 0);
        if (!v) continue;
        if (v->type == T_MESHB) draw_mb(&v->mb, &w);
        else if (v->type == T_FRAME) render_frame(o->fr.vis[i], &w, depth + 1);
        else if (v->type == T_USERVIS && v->uv.cb) {
            v->uv.cb(v->uv.arg, &w, &vp.view);
            plat_set_fog(vp.fog_on, vp.fog_start, vp.fog_end, vp.fog_color);
        }
    }
    for (int i = 0; i < o->fr.nkids; i++) render_frame(o->fr.kids[i], &w, depth + 1);
}

static int tcmp(const void *a, const void *b)
{
    float da = ((const tdraw_t *)a)->depth, db = ((const tdraw_t *)b)->depth;
    return da < db ? 1 : da > db ? -1 : 0;
}

void rm_viewport_render(rmh scene)
{
    obj_t *s = get(scene, T_FRAME);
    if (!s || !vp.camera) return;
    extern uint64_t prof_us[16];
    uint64_t t0 = plat_time_us();
    mat4 cw, id;
    rm_frame_world(vp.camera, &cw);
    mat_invert_affine(&vp.view, &cw);
    update_projection();
    plat_set_projection(vp.sx, vp.sy, vp.front, vp.back);
    vp.fog_on = s->fr.fog_on;
    vp.fog_start = s->fr.fog_start;
    vp.fog_end = s->fr.fog_end;
    vp.fog_color = s->fr.fog_color & 0xffffff;
    plat_set_fog(vp.fog_on, vp.fog_start, vp.fog_end, vp.fog_color);
    ntlist = 0;
    mat_identity(&id);
    render_frame(scene, &id, 0);
    qsort(tlist, ntlist, sizeof *tlist, tcmp);
    for (int i = 0; i < ntlist; i++) {
        tdraw_t *t = &tlist[i];
        struct batch *b = &t->m->batches[t->batch];
        plat_set_modelview(&t->mv.m[0][0]);
        plat_draw_tris_static(b->tex, t->m->verts + b->start, b->count, &b->gpu, b->sphere, BLEND_ALPHA, 0, 1);
    }
    plat_set_fog(0, 0, 0, 0);
    prof_us[3] += plat_time_us() - t0;
}
