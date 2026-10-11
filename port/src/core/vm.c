#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#ifndef PSP
#include <execinfo.h>
#include <unistd.h>
#endif

uint8_t vm_img[VM_IMG_SIZE] __attribute__((aligned(16)));
uint8_t *vm_heap;
uint8_t vm_stack[VM_STACK_SIZE];
uint8_t vm_handle_page[128];
uint32_t vm_heap_size;
vm_seg_t vm_seg[16] = {
    [0] = { VM_IMG_BASE, VM_IMG_SIZE, vm_img, 0 },
    [2] = { VM_STACK_BASE, VM_STACK_SIZE, vm_stack, 0 },   /* [1] heap: set by vm_load_image */
};

void *VA_slow(uint32_t a)
{
    /* object handles: the game still loads vtable pointers through them (dead values) */
    if (a - 0x60000000u < 0x20000000u) return vm_handle_page + (a & 0x3f);
    vm_fatal("bad VA %08x", a);
}

void vm_fatal(const char *fmt, ...)
{
    va_list ap;
    char msg[256];
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    fprintf(stderr, "FATAL: %s\n", msg);
    extern void plat_log(const char *fmt, ...);
    plat_log("FATAL: %s", msg);
#ifdef PSP
    extern void plat_fatal_screen(const char *msg);
    plat_fatal_screen(msg);
#else
    void *bt[32];
    int n = backtrace(bt, 32);
    backtrace_symbols_fd(bt, n, 2);
#endif
    abort();
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

int vm_load_image(const char *exe_path)
{
    FILE *f = fopen(exe_path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(n);
    if (fread(d, 1, n, f) != (size_t)n) { fclose(f); free(d); return 0; }
    fclose(f);

    memset(vm_img, 0, VM_IMG_SIZE);
    uint32_t pe = rd32(d + 0x3c);
    uint32_t base = rd32(d + pe + 24 + 28);
    unsigned nsec = rd16(d + pe + 6);
    const uint8_t *sec = d + pe + 24 + rd16(d + pe + 20);
    for (unsigned i = 0; i < nsec; i++, sec += 40) {
        uint32_t va = base + rd32(sec + 12), rawsz = rd32(sec + 16), rawoff = rd32(sec + 20);
        uint32_t vsz = rd32(sec + 8);
        uint32_t sz = rawsz < vsz ? rawsz : vsz;
        if (va < VM_IMG_BASE || va >= VM_IMG_END) continue; /* .rsrc */
        if (va + sz > VM_IMG_END) sz = VM_IMG_END - va;
        memcpy(vm_img + (va - VM_IMG_BASE), d + rawoff, sz);
    }
    free(d);

#ifdef PSP
    vm_heap_size = 4u << 20;
#else
    vm_heap_size = 16u << 20;
#endif
    vm_heap = calloc(1, vm_heap_size);
    if (!vm_heap) vm_fatal("out of memory (VM heap)");
    vm_seg[1] = (vm_seg_t){ VM_HEAP_BASE, vm_heap_size, vm_heap, 0 };
    return 1;
}

uint32_t vm_va_of(const void *p)
{
    const uint8_t *b = p;
    if (b >= vm_img && b < vm_img + VM_IMG_SIZE) return VM_IMG_BASE + (uint32_t)(b - vm_img);
    if (b >= vm_heap && b < vm_heap + vm_heap_size) return VM_HEAP_BASE + (uint32_t)(b - vm_heap);
    if (b >= vm_stack && b < vm_stack + VM_STACK_SIZE) return VM_STACK_BASE + (uint32_t)(b - vm_stack);
    vm_fatal("pointer %p is outside the VM", p);
    return 0;
}

double GD(uint32_t a) { double v; memcpy(&v, VA(a), 8); return v; }
void GD_SET(uint32_t a, double v) { memcpy(VA(a), &v, 8); }

/* Heap arena: first-fit free list with 16-byte headers, blocks kept in address order. */
typedef struct { uint32_t size, used, prev_free, next_free; } blk_t;
static int heap_ready;

static blk_t *blk(uint32_t off) { return (blk_t *)(vm_heap + off); }

static void heap_init(void)
{
    blk_t *b = blk(0);
    b->size = vm_heap_size;
    b->used = 0;
    heap_ready = 1;
}

uint32_t vm_malloc(uint32_t size)
{
    if (!heap_ready) heap_init();
    size = (size + 15u) & ~15u;
    uint32_t need = size + sizeof(blk_t);
    for (uint32_t off = 0; off < vm_heap_size; off += blk(off)->size) {
        blk_t *b = blk(off);
        if (b->used || b->size < need) continue;
        if (b->size - need >= 64) {
            blk_t *rest = blk(off + need);
            rest->size = b->size - need;
            rest->used = 0;
            b->size = need;
        }
        b->used = 1;
        memset(b + 1, 0, size);
        return VM_HEAP_BASE + off + sizeof(blk_t);
    }
    vm_fatal("vm_malloc(%u): arena exhausted", size);
    return 0;
}

void vm_free(uint32_t va)
{
    if (!va) return;
    uint32_t off = va - VM_HEAP_BASE - sizeof(blk_t);
    if (off >= vm_heap_size) vm_fatal("vm_free(%08x): not a heap block", va);
    blk(off)->used = 0;
    /* coalesce the whole arena lazily: merge forward runs of free blocks */
    for (uint32_t o = 0; o < vm_heap_size; o += blk(o)->size) {
        blk_t *b = blk(o);
        if (b->used) continue;
        while (o + b->size < vm_heap_size && !blk(o + b->size)->used) b->size += blk(o + b->size)->size;
    }
}
