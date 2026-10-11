/* DirectX .x file reader (binary "bin 0032" and text "txt 0032", uncompressed). */
#ifndef KOK_XFILE_H
#define KOK_XFILE_H
#include <stdint.h>

typedef struct { float f; int32_t i; } xval_t;

typedef struct xnode {
    char tmpl[32];        /* template name, "REF" for {name} references, "" for anonymous braces */
    char name[64];
    xval_t *vals;
    char **strs;          /* string values (TextureFilename) */
    int nvals, nstrs;
    struct xnode **kids;
    int nkids;
} xnode_t;

xnode_t *x_load(const char *path);         /* NULL if missing or unparsable */
void x_free(xnode_t *n);
xnode_t *x_find_named(xnode_t *root, const char *tmpl, const char *name); /* depth-first */

/* Sequential value reader used by the template decoders. */
typedef struct { const xnode_t *n; int pos; } xcur_t;
static inline int32_t xc_int(xcur_t *c) { return c->pos < c->n->nvals ? c->n->vals[c->pos++].i : 0; }
static inline float xc_float(xcur_t *c) { return c->pos < c->n->nvals ? c->n->vals[c->pos++].f : 0.f; }

#endif
