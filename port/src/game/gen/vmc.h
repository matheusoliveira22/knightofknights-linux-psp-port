/* Runtime support for the machine-translated game code (tools/ghidra2c.py output, hand-fixed).
 * All pointers are 32-bit VM addresses; see core/vm.h. */
#ifndef KOK_VMC_H
#define KOK_VMC_H
#include "../../core/vm.h"
#include "../../core/crt.h"
#include "../../rm/rm.h"
#include "../../dx/dx.h"
#include "../../plat/plat.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

/* ---- memory (packed access keeps MIPS happy with the odd unaligned original access; a constant,
 * aligned address gets a plain access: lui + lw into the static image) */
#define VMT_ALIGNED_(a, n) __builtin_choose_expr(__builtin_constant_p(a), \
    !((uint32_t)(__builtin_constant_p(a) ? (a) : 0) & ((n) - 1)), 0)
#define VMT_(T, a) (*__builtin_choose_expr(VMT_ALIGNED_(a, sizeof(T)), (T *)VA(a), \
    &((struct __attribute__((packed, may_alias)) { T v; } *)VA(a))->v))
#define M8(a)   (*(int8_t *)VA(a))
#define MU8(a)  (*(uint8_t *)VA(a))
#define M16(a)  VMT_(int16_t, a)
#define MU16(a) VMT_(uint16_t, a)
#define M32(a)  VMT_(int32_t, a)
#define MU32(a) VMT_(uint32_t, a)
#define M64(a)  VMT_(int64_t, a)
#define MU64(a) VMT_(uint64_t, a)
#define MF(a)   VMT_(float, a)
#define MD(a)   VMT_(double, a)

/* ---- VM stack for address-taken locals */
extern uint32_t vm_sp;
uint32_t vm_salloc(uint32_t n);
#define VFRAME   const uint32_t _vsp = vm_sp
#define VNOFRAME const uint32_t _vsp = 0; (void)_vsp
#define VRET     do { if (_vsp) vm_sp = _vsp; return; } while (0)
#define VRETV(x) do { __typeof__(x) _rv = (x); if (_vsp) vm_sp = _vsp; return _rv; } while (0)

/* ---- bit reinterpretation (x86 stack slots / registers carry raw 32-bit values) */
static inline float BITF(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static inline uint32_t BITU(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static inline double BITD(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }

/* ---- Ghidra helpers */
#define CONCAT11(a, b) ((uint16_t)(((uint32_t)(a) << 8) | (uint8_t)(b)))
#define CONCAT12(a, b) (((uint32_t)(a) << 16) | (uint16_t)(b))
#define CONCAT13(a, b) (((uint32_t)(a) << 24) | ((uint32_t)(b) & 0xffffffu))
#define CONCAT21(a, b) ((((uint32_t)(a) << 8) | (uint8_t)(b)) & 0xffffffu)
#define CONCAT22(a, b) (((uint32_t)(a) << 16) | (uint16_t)(b))
#define CONCAT31(a, b) (((uint32_t)(a) << 8) | (uint8_t)(b))
#define CONCAT24(a, b) (((uint64_t)(a) << 32) | (uint32_t)(b))
#define CONCAT44(a, b) (((uint64_t)(a) << 32) | (uint32_t)(b))
#define SUB84(x, n) ((uint32_t)((uint64_t)(x) >> ((n) * 8)))
#define SUB41(x, n) ((uint8_t)((uint32_t)(x) >> ((n) * 8)))
#define SUB42(x, n) ((uint16_t)((uint32_t)(x) >> ((n) * 8)))
#define ZEXT48(x) ((uint64_t)(uint32_t)(x))
#define SQRT(x) sqrt(x)
#define ABS(x) fabs(x)
#define NAN(x) isnan(x)
static inline int64_t w_ftol(double x) { return (int64_t)x; }
double w_x87_444fb0(double x);

/* ---- function pointers stored in the original image (state tables, handlers) */
void *fn_lookup(uint32_t va);
#define CALLVA(va) ((uint32_t (*)())fn_lookup(va))

/* ---- Win32 */
int w_wsprintfA(uint32_t dst, uint32_t fmt, ...);
uint32_t w_lstrcatA(uint32_t dst, uint32_t src);
int32_t w_lstrlenA(uint32_t s);
uint32_t w_lstrcpyA(uint32_t dst, uint32_t src);
#define w_strlen w_lstrlenA
int32_t w_strcmp(uint32_t a, uint32_t b);
int32_t w_strncmp(uint32_t a, uint32_t b, uint32_t n);
uint32_t w_strncpy(uint32_t d, uint32_t s, uint32_t n);
uint32_t w_memset(uint32_t d, int c, uint32_t n);
uint32_t w_memcpy(uint32_t d, uint32_t s, uint32_t n);
uint32_t w_strchr(uint32_t s, int c);
uint32_t w_strstr(uint32_t s, uint32_t t);
int32_t w_SetRect(uint32_t r, int32_t l, int32_t t, int32_t rr, int32_t b);
int32_t w_GetKeyboardState(uint32_t buf);
int32_t w_MessageBoxA(uint32_t hwnd, uint32_t text, uint32_t cap, uint32_t flags);
uint32_t w_joyGetPosEx(uint32_t id, uint32_t info);
void w_Sleep(uint32_t ms);
float w_D3DRMVectorDotProduct(uint32_t a, uint32_t b);
float w_D3DRMVectorModulus(uint32_t a);
uint32_t w_D3DRMVectorNormalize(uint32_t a);
/* GDI on surface DCs: the HDC is the surface handle */
uint32_t w_SelectObject(uint32_t hdc, uint32_t font);
uint32_t w_SetTextColor(uint32_t hdc, uint32_t bgr);
uint32_t w_SetBkColor(uint32_t hdc, uint32_t bgr);
int32_t w_SetBkMode(uint32_t hdc, int32_t mode);
int32_t w_DrawTextA(uint32_t hdc, uint32_t str, int32_t n, uint32_t rect, uint32_t flags);
int32_t w_TextOutA(uint32_t hdc, int32_t x, int32_t y, uint32_t str, int32_t n);

/* ---- COM shims (objects are rm/dx handles; float arguments arrive as raw bits) */
uint32_t COM_Unk_Release(uint32_t o);
uint32_t COM_Unk_AddRef(uint32_t o);
uint32_t COM_Unk_QueryInterface(uint32_t o, uint32_t iid, uint32_t out);
#define COM_Frame2_Release(o) COM_Unk_Release(o)
#define COM_DDSurf_Release(o) COM_Unk_Release(o)
uint32_t COM_RM2_CreateFrame(uint32_t rm, uint32_t parent, uint32_t out);
uint32_t COM_RM2_CreateMeshBuilder(uint32_t rm, uint32_t out);
uint32_t COM_RM2_CreateAnimationSet(uint32_t rm, uint32_t out);
uint32_t COM_RM2_GetNamedObject(uint32_t rm, uint32_t name, uint32_t out);
uint32_t COM_RM2_CreateUserVisual(uint32_t rm, uint32_t cb, uint32_t arg, uint32_t out);
uint32_t COM_Frame2_SetPosition(uint32_t f, uint32_t ref, uint32_t x, uint32_t y, uint32_t z);
uint32_t COM_Frame2_GetPosition(uint32_t f, uint32_t ref, uint32_t out);
uint32_t COM_Frame2_SetOrientation(uint32_t f, uint32_t ref, uint32_t dx, uint32_t dy, uint32_t dz,
                                   uint32_t ux, uint32_t uy, uint32_t uz);
uint32_t COM_Frame2_SetRotation(uint32_t f, uint32_t ref, uint32_t x, uint32_t y, uint32_t z, uint32_t th);
uint32_t COM_Frame2_AddScale(uint32_t f, uint32_t how, uint32_t x, uint32_t y, uint32_t z);
uint32_t COM_Frame2_AddRotation(uint32_t f, uint32_t how, uint32_t x, uint32_t y, uint32_t z, uint32_t th);
uint32_t COM_Frame2_LookAt(uint32_t f, uint32_t target, uint32_t ref, uint32_t constraint);
uint32_t COM_Frame2_AddChild(uint32_t f, uint32_t c);
uint32_t COM_Frame2_AddDestroyCallback(uint32_t f, uint32_t cb, uint32_t arg);
uint32_t COM_Frame2_DeleteChild(uint32_t f, uint32_t c);
uint32_t COM_Frame2_AddVisual(uint32_t f, uint32_t v);
uint32_t COM_Frame2_DeleteVisual(uint32_t f, uint32_t v);
uint32_t COM_Frame2_GetChildren(uint32_t f, uint32_t out);
uint32_t COM_Frame2_GetVisuals(uint32_t f, uint32_t out);
uint32_t COM_Frame2_GetParent(uint32_t f, uint32_t out);
uint32_t COM_Frame2_Move(uint32_t f, uint32_t delta);
uint32_t COM_Frame2_Transform(uint32_t f, uint32_t dst, uint32_t src);
uint32_t COM_Frame2_InverseTransform(uint32_t f, uint32_t dst, uint32_t src);
uint32_t COM_Frame2_SetSceneBackground(uint32_t f, uint32_t c);
uint32_t COM_Frame2_SetSceneFogEnable(uint32_t f, uint32_t on);
uint32_t COM_Frame2_SetSceneFogParams(uint32_t f, uint32_t a, uint32_t b, uint32_t d);
uint32_t COM_Frame2_SetSceneFogColor(uint32_t f, uint32_t c);
uint32_t COM_Frame2_GetName(uint32_t o, uint32_t size_va, uint32_t buf);
uint32_t COM_Frame2_SetName(uint32_t o, uint32_t name);
uint32_t COM_Array_GetSize(uint32_t a);
uint32_t COM_Array_GetElement(uint32_t a, uint32_t i, uint32_t out);
uint32_t COM_MeshBuilder2_Load(uint32_t mb, uint32_t file, uint32_t name, uint32_t flags, uint32_t cb, uint32_t arg);
uint32_t COM_MeshBuilder2_SetPerspective(uint32_t mb, uint32_t on);
uint32_t COM_MeshBuilder2_SetQuality(uint32_t mb, uint32_t q);
uint32_t COM_MeshBuilder2_Scale(uint32_t mb, uint32_t x, uint32_t y, uint32_t z);
uint32_t COM_MeshBuilder2_SetColor(uint32_t mb, uint32_t c);
uint32_t COM_MeshBuilder2_SetColorRGB(uint32_t mb, uint32_t r, uint32_t g, uint32_t b);
uint32_t COM_MeshBuilder2_SetTexture(uint32_t mb, uint32_t t);
uint32_t COM_MeshBuilder2_GetFace(uint32_t mb, uint32_t i, uint32_t out);
uint32_t COM_MeshBuilder2_GetFaceCount(uint32_t mb);
uint32_t COM_AnimSet_Load(uint32_t as, uint32_t file, uint32_t name, uint32_t flags, uint32_t cb, uint32_t arg,
                          uint32_t parent);
uint32_t COM_AnimSet_SetTime(uint32_t as, uint32_t t);
uint32_t COM_Viewport_Clear(uint32_t vp);
uint32_t COM_Viewport_Render(uint32_t vp, uint32_t scene);
uint32_t COM_Viewport_SetFront(uint32_t vp, uint32_t v);
uint32_t COM_Viewport_SetBack(uint32_t vp, uint32_t v);
uint32_t COM_Viewport_SetField(uint32_t vp, uint32_t v);
uint32_t COM_Device2_Update(uint32_t d);
uint32_t COM_DDSurf_Blt(uint32_t dst, uint32_t dr, uint32_t src, uint32_t sr, uint32_t flags, uint32_t fx);
uint32_t COM_DDSurf_Lock(uint32_t s, uint32_t rect, uint32_t desc, uint32_t flags, uint32_t ev);
uint32_t COM_DDSurf_Unlock(uint32_t s, uint32_t ptr);
uint32_t COM_DDSurf_GetDC(uint32_t s, uint32_t out);
uint32_t COM_DDSurf_ReleaseDC(uint32_t s, uint32_t hdc);
uint32_t COM_D3DDev2_SetRenderState(uint32_t d, uint32_t st, uint32_t v);
uint32_t COM_D3DDev2_BeginScene(uint32_t d);
uint32_t COM_D3DDev2_EndScene(uint32_t d);
uint32_t COM_D3DDev2_DrawPrimitive(uint32_t d, uint32_t prim, uint32_t vt, uint32_t verts, uint32_t n, uint32_t fl);
uint32_t COM_Texture2_GetHandle(uint32_t t, uint32_t dev, uint32_t out);
uint32_t COM_D3D2_CreateMaterial(uint32_t d3d, uint32_t out, uint32_t unk);
uint32_t COM_Material2_SetMaterial(uint32_t m, uint32_t mat);
uint32_t COM_Material2_GetHandle(uint32_t m, uint32_t dev, uint32_t out);
uint32_t COM_D3DDev2_SetLightState(uint32_t d, uint32_t st, uint32_t v);
/* world/view of the RM user visual being rendered (D3DRM sets them before the callback) */
void im_set_uv_transform(const mat4 *world, const mat4 *view);

/* GDI target of the text routines (the HDC Ghidra lost in a register): option 0x488f38 == 1 draws into
 * the 256x256 text surface, otherwise straight into the back buffer. */
#define HDC_TEXT (M32(0x488f38) == 1 ? MU32(0x488c64) : MU32(0x4a385c))

/* frame begin / present (Viewport::Clear starts a frame, the Blt to the primary ends it) */
void dx_frame_begin(void);
void prof_collide(void);   /* FUN_00429a00 timed for the perf log (sys.c) */
void dx_present(void);

#include "protos.h"
#endif
