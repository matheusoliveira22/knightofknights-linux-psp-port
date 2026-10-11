/* Mini Direct3D Retained Mode: the subset of IDirect3DRM2 / Frame2 / MeshBuilder2 /
 * AnimationSet / Viewport behaviour that Knight of Knights uses.
 *
 * Objects are referenced by 32-bit handles (rmh) so the game can keep storing them in its
 * 32-bit global tables exactly like it stored COM pointers. Handle 0 is NULL.
 */
#ifndef KOK_RM_H
#define KOK_RM_H
#include <stdint.h>

typedef uint32_t rmh;
typedef struct { float x, y, z; } vec3;
typedef struct { float m[4][4]; } mat4;   /* row-vector convention, row-major (D3D) */

enum { RMCOMBINE_REPLACE = 0, RMCOMBINE_BEFORE = 1, RMCOMBINE_AFTER = 2 };

void rm_init(void);

/* generic */
void rm_addref(rmh h);
void rm_release(rmh h);
int rm_is_frame(rmh h);
void rm_set_appdata(rmh h, uint32_t v);
uint32_t rm_get_appdata(rmh h);
void rm_set_name(rmh h, const char *name);
const char *rm_get_name(rmh h);
rmh rm_named_object(const char *name);
typedef void (*rm_destroy_cb)(rmh obj, uint32_t cb, uint32_t arg);
void rm_add_destroy_callback(rmh h, rm_destroy_cb fn, uint32_t cb, uint32_t arg);    /* IDirect3DRM::GetNamedObject (frames only) */

/* frames */
rmh rm_frame_create(rmh parent);
rmh rm_frame_parent(rmh f);
void rm_frame_add_child(rmh f, rmh child);
void rm_frame_delete_child(rmh f, rmh child);
void rm_frame_add_visual(rmh f, rmh visual);
void rm_frame_delete_visual(rmh f, rmh visual);
int rm_frame_child_count(rmh f);
rmh rm_frame_child(rmh f, int i);
int rm_frame_visual_count(rmh f);
rmh rm_frame_visual(rmh f, int i);
void rm_frame_set_position(rmh f, rmh ref, float x, float y, float z);
void rm_frame_get_position(rmh f, rmh ref, vec3 *out);
void rm_frame_set_orientation(rmh f, rmh ref, float dx, float dy, float dz, float ux, float uy, float uz);
void rm_frame_get_orientation(rmh f, rmh ref, vec3 *dir, vec3 *up);
void rm_frame_set_rotation(rmh f, rmh ref, float x, float y, float z, float theta);
void rm_frame_set_velocity(rmh f, rmh ref, float x, float y, float z, int with_rotation);
void rm_frame_add_scale(rmh f, int combine, float sx, float sy, float sz);
void rm_frame_add_rotation(rmh f, int combine, float x, float y, float z, float theta);
void rm_frame_add_translation(rmh f, int combine, float x, float y, float z);
void rm_frame_add_transform(rmh f, int combine, const mat4 *m);
void rm_frame_get_transform(rmh f, mat4 *out);    /* local */
void rm_frame_set_quaternion(rmh f, rmh ref, float w, float x, float y, float z);
void rm_frame_transform(rmh f, vec3 *dst, const vec3 *src);         /* local -> world */
void rm_frame_inverse_transform(rmh f, vec3 *dst, const vec3 *src); /* world -> local */
void rm_frame_lookat(rmh f, rmh target, rmh ref, int constraint);
void rm_frame_move(rmh f, float delta);  /* applies rotation/velocity recursively */
void rm_frame_world(rmh f, mat4 *out);
void rm_frame_set_color(rmh f, uint32_t argb);
void rm_frame_set_zbuffer_mode(rmh f, int mode);
void rm_frame_set_sort_mode(rmh f, int mode);
/* scene (root frame) state */
void rm_scene_set_background(rmh scene, uint32_t argb);
uint32_t rm_scene_background(rmh scene);
void rm_scene_set_fog_enable(rmh scene, int on);
void rm_scene_set_fog_params(rmh scene, float start, float end, float density);
void rm_scene_set_fog_color(rmh scene, uint32_t argb);

/* textures */
rmh rm_texture_load(const char *path);          /* IDirect3DRM::LoadTexture; cached by name */
rmh rm_texture_from_rgba(int w, int h, const uint8_t *rgba);
int rm_texture_plat_id(rmh t);
void rm_texture_set_transparency(rmh t, int on, uint32_t key_rgb);

/* mesh builders */
rmh rm_mb_create(void);
int rm_mb_load(rmh mb, const char *path);
void rm_mb_set_color(rmh mb, uint32_t argb);          /* all faces */
void rm_mb_set_color_rgb(rmh mb, float r, float g, float b);
void rm_mb_set_texture(rmh mb, rmh tex);              /* all faces */
void rm_mb_set_emissive(rmh mb, float r, float g, float b); /* via material */
void rm_mb_scale(rmh mb, float sx, float sy, float sz);
void rm_mb_translate(rmh mb, float x, float y, float z);
void rm_mb_set_quality(rmh mb, uint32_t q);
void rm_mb_set_perspective(rmh mb, int on);
int rm_mb_face_count(rmh mb);
int rm_mb_vertex_count(rmh mb);
int rm_mb_face_vertices(rmh mb, int face, vec3 *out, int max); /* returns vertex count */
void rm_mb_box(rmh mb, vec3 *mn, vec3 *mx);
int rm_mb_face_uvs(rmh mb, int face, float *uv, int max);       /* returns vertex count */
rmh rm_mb_face_handle(rmh mb, int face);
/* give every face texture a private copy with colour-key transparency (Face::GetTexture +
 * Texture::SetDecalTransparency(TRUE) + SetDecalTransparentColor(key) on a freshly loaded mesh) */
void rm_mb_textures_transparent(rmh mb, uint32_t key_rgb);              /* IDirect3DRMFace stand-in */
void rm_face_set_color(rmh face, uint32_t argb);
void rm_face_set_texture(rmh face, rmh tex);

/* animation sets */
rmh rm_animset_create(void);
int rm_animset_load(rmh as, const char *path, rmh parent);
void rm_animset_set_time(rmh as, float t);

/* user visuals: a draw callback invoked during Viewport::Render; arg is malloc()ed and owned by
 * the visual (freed when it is destroyed) */
typedef void (*rm_user_cb)(void *arg, const mat4 *world, const mat4 *view);
rmh rm_uservisual_create(rm_user_cb cb, void *arg);

/* viewport / camera */
void rm_viewport_init(rmh camera, int width, int height);
void rm_viewport_set_camera(rmh camera);
void rm_viewport_set_front(float v);
void rm_viewport_set_back(float v);
void rm_viewport_set_field(float v);
void rm_viewport_clear(rmh scene);
void rm_viewport_render(rmh scene);
void rm_viewport_project(const vec3 *world, float *sx, float *sy, float *sz); /* to 640x480 */

/* math */
void mat_identity(mat4 *m);
void mat_mul(mat4 *out, const mat4 *a, const mat4 *b);   /* out = a * b */
void mat_invert_affine(mat4 *out, const mat4 *m);
void vec_xform(vec3 *out, const vec3 *v, const mat4 *m);
void vec_xform_dir(vec3 *out, const vec3 *v, const mat4 *m);
void quat_to_mat(mat4 *m, float w, float x, float y, float z);

#endif
