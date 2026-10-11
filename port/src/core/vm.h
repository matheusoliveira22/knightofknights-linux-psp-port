/* Virtual 32-bit address space mirroring the original exe's data.
 *
 * The game code indexes its globals and tables by absolute address (e.g. 0x479998 + i*0x228),
 * so the port keeps the same layout: the .rdata/.data/.bss range of "Knight of Knights.exe"
 * is loaded at startup into vm_img and accessed through G32()/GF()... with the original VAs.
 * Values the game stores in memory that were pointers become 32-bit VAs: either into the image,
 * into the heap arena (vm_malloc), or opaque object handles (see rm.h / dx.h).
 */
#ifndef KOK_VM_H
#define KOK_VM_H
#include <stdint.h>
#include <stddef.h>

#define VM_IMG_BASE 0x00401000u
#define VM_IMG_END  0x004a9000u
#define VM_IMG_SIZE (VM_IMG_END - VM_IMG_BASE)
#define VM_HEAP_BASE 0x10000000u
#define VM_STACK_BASE 0x20000000u
#define VM_STACK_SIZE (512u << 10)
extern uint8_t vm_stack[];
extern uint8_t vm_handle_page[];

/* static and 16-byte aligned (VM_IMG_BASE is page aligned), so a constant image address folds to a
 * link-time address with the original's alignment */
extern uint8_t vm_img[VM_IMG_SIZE] __attribute__((aligned(16)));
extern uint8_t *vm_heap;
extern uint32_t vm_heap_size;

int vm_load_image(const char *exe_path);
void vm_fatal(const char *fmt, ...) __attribute__((noreturn, cold));

/* VA -> host: one bounds-checked lookup by the top address nibble (image, heap, stack); anything
 * else (object handles, bad addresses) goes out of line. Every memory access of the generated code
 * goes through here, so it must stay small. */
typedef struct { uint32_t lo, len; uint8_t *host; uint32_t pad; } vm_seg_t;
extern vm_seg_t vm_seg[16];
void *VA_slow(uint32_t a);

static inline void *VA(uint32_t a)
{
    if (__builtin_constant_p(a) && a - VM_IMG_BASE < VM_IMG_SIZE) return vm_img + (a - VM_IMG_BASE);
    const vm_seg_t *s = &vm_seg[a >> 28];
    uint32_t off = a - s->lo;
    if (__builtin_expect(off < s->len, 1)) return s->host + off;
    return VA_slow(a);
}

/* Reverse map of a host pointer inside the image/heap back to a VA. */
uint32_t vm_va_of(const void *p);

#define G8(a)   (*(int8_t *)VA(a))
#define GU8(a)  (*(uint8_t *)VA(a))
#define G16(a)  (*(int16_t *)VA(a))
#define GU16(a) (*(uint16_t *)VA(a))
#define G32(a)  (*(int32_t *)VA(a))
#define GU32(a) (*(uint32_t *)VA(a))
#define GF(a)   (*(float *)VA(a))
#define GSTR(a) ((char *)VA(a))
/* doubles in the image are only 4-byte aligned in places; go through memcpy. */
double GD(uint32_t a);
void GD_SET(uint32_t a, double v);

uint32_t vm_malloc(uint32_t size);
void vm_free(uint32_t va);

#endif
