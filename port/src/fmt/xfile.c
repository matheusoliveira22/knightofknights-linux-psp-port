#include "xfile.h"
#include "../plat/plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

enum { T_NAME = 1, T_STRING = 2, T_INTEGER = 3, T_GUID = 5, T_INT_LIST = 6, T_FLOAT_LIST = 7,
       T_OBRACE = 0x0a, T_CBRACE = 0x0b, T_TEMPLATE = 0x1f, T_EOF = 0, T_NUMBER_TXT = 0x100 };

typedef struct {
    const uint8_t *d;
    size_t n, p;
    int text, f64;
    /* current token */
    int tok;
    char str[256];
    xval_t *list;
    int nlist, caplist;
} lex_t;

static uint32_t u32(lex_t *L) { uint32_t v = 0; if (L->p + 4 <= L->n) memcpy(&v, L->d + L->p, 4); L->p += 4; return v; }
static uint16_t u16(lex_t *L) { uint16_t v = 0; if (L->p + 2 <= L->n) memcpy(&v, L->d + L->p, 2); L->p += 2; return v; }

static void list_push(lex_t *L, float f, int32_t i)
{
    if (L->nlist == L->caplist) {
        L->caplist = L->caplist ? L->caplist * 2 : 64;
        L->list = realloc(L->list, L->caplist * sizeof(xval_t));
    }
    L->list[L->nlist].f = f;
    L->list[L->nlist].i = i;
    L->nlist++;
}

static int next_bin(lex_t *L)
{
    L->nlist = 0;
    if (L->p + 2 > L->n) return L->tok = T_EOF;
    int t = u16(L);
    switch (t) {
    case T_NAME: case T_STRING: {
        uint32_t len = u32(L);
        size_t avail = L->p < L->n ? L->n - L->p : 0;   /* truncated file */
        if (len > avail) len = (uint32_t)avail;
        size_t c = len < sizeof L->str - 1 ? len : sizeof L->str - 1;
        memcpy(L->str, L->d + L->p, c);
        L->str[c] = 0;
        L->p += len;
        if (t == T_STRING) u16(L);
        break;
    }
    case T_INTEGER: { int32_t v = (int32_t)u32(L); list_push(L, (float)v, v); break; }
    case T_GUID: L->p += 16; break;
    case T_INT_LIST: {
        uint32_t c = u32(L);
        for (uint32_t i = 0; i < c && L->p + 4 <= L->n; i++) { int32_t v = (int32_t)u32(L); list_push(L, (float)v, v); }
        break;
    }
    case T_FLOAT_LIST: {
        uint32_t c = u32(L);
        for (uint32_t i = 0; i < c && L->p + (L->f64 ? 8 : 4) <= L->n; i++) {
            float f;
            if (L->f64) { double d; memcpy(&d, L->d + L->p, 8); L->p += 8; f = (float)d; }
            else { memcpy(&f, L->d + L->p, 4); L->p += 4; }
            list_push(L, f, (int32_t)f);
        }
        break;
    }
    default: break;
    }
    return L->tok = t;
}

static int next_txt(lex_t *L)
{
    L->nlist = 0;
    for (;;) {
        while (L->p < L->n && (isspace(L->d[L->p]) || L->d[L->p] == ',' || L->d[L->p] == ';')) L->p++;
        if (L->p >= L->n) return L->tok = T_EOF;
        if (L->d[L->p] == '#' || (L->d[L->p] == '/' && L->p + 1 < L->n && L->d[L->p + 1] == '/')) {
            while (L->p < L->n && L->d[L->p] != '\n') L->p++;
            continue;
        }
        break;
    }
    uint8_t c = L->d[L->p];
    if (c == '{') { L->p++; return L->tok = T_OBRACE; }
    if (c == '}') { L->p++; return L->tok = T_CBRACE; }
    if (c == '<') { while (L->p < L->n && L->d[L->p] != '>') L->p++; L->p++; return L->tok = T_GUID; }
    if (c == '"') {
        size_t s = ++L->p, k = 0;
        while (L->p < L->n && L->d[L->p] != '"') L->p++;
        k = L->p - s < sizeof L->str - 1 ? L->p - s : sizeof L->str - 1;
        memcpy(L->str, L->d + s, k);
        L->str[k] = 0;
        L->p++;
        return L->tok = T_STRING;
    }
    if (c == '-' || c == '+' || c == '.' || isdigit(c)) {
        char buf[64];
        size_t k = 0;
        int isf = 0;
        while (L->p < L->n && k < sizeof buf - 1 &&
               (isdigit(L->d[L->p]) || strchr("+-.eE", L->d[L->p]))) {
            if (strchr(".eE", L->d[L->p])) isf = 1;
            buf[k++] = L->d[L->p++];
        }
        buf[k] = 0;
        if (isf) { float f = strtof(buf, 0); list_push(L, f, (int32_t)f); }
        else { int32_t v = (int32_t)strtol(buf, 0, 10); list_push(L, (float)v, v); }
        return L->tok = T_NUMBER_TXT;
    }
    size_t s = L->p, k;
    while (L->p < L->n && !isspace(L->d[L->p]) && !strchr("{};,\"", L->d[L->p])) L->p++;
    k = L->p - s < sizeof L->str - 1 ? L->p - s : sizeof L->str - 1;
    memcpy(L->str, L->d + s, k);
    L->str[k] = 0;
    if (!strcmp(L->str, "template")) return L->tok = T_TEMPLATE;
    return L->tok = T_NAME;
}

static int next(lex_t *L) { return L->text ? next_txt(L) : next_bin(L); }

static xnode_t *node_new(const char *tmpl, const char *name)
{
    xnode_t *n = calloc(1, sizeof *n);
    snprintf(n->tmpl, sizeof n->tmpl, "%s", tmpl ? tmpl : "");
    snprintf(n->name, sizeof n->name, "%s", name ? name : "");
    return n;
}

static void node_add_kid(xnode_t *p, xnode_t *k)
{
    p->kids = realloc(p->kids, (p->nkids + 1) * sizeof *p->kids);
    p->kids[p->nkids++] = k;
}

static void node_add_vals(xnode_t *n, const xval_t *v, int c)
{
    if (!c) return;
    n->vals = realloc(n->vals, (n->nvals + c) * sizeof *v);
    memcpy(n->vals + n->nvals, v, c * sizeof *v);
    n->nvals += c;
}

static void node_add_str(xnode_t *n, const char *s)
{
    n->strs = realloc(n->strs, (n->nstrs + 1) * sizeof *n->strs);
    n->strs[n->nstrs++] = strdup(s);
}

xnode_t *x_load(const char *path)
{
    size_t size;
    uint8_t *d = plat_read_file(path, &size);
    if (!d) return NULL;
    if (size < 16 || memcmp(d, "xof ", 4)) { free(d); return NULL; }
    lex_t L = { d, size, 16 };
    L.text = !memcmp(d + 8, "txt ", 4);
    L.f64 = !memcmp(d + 12, "0064", 4);

    xnode_t *root = node_new("ROOT", NULL);
    xnode_t *stack[64];
    int sp = 0;
    stack[sp++] = root;
    char pend[2][64];
    int npend = 0;

    while (next(&L) != T_EOF) {
        if (L.tok == T_TEMPLATE) {
            int depth = 0;
            while (next(&L) != T_EOF) {
                if (L.tok == T_OBRACE) depth++;
                else if (L.tok == T_CBRACE && --depth == 0) break;
            }
            npend = 0;
        } else if (L.tok == T_NAME) {
            if (npend < 2) snprintf(pend[npend++], 64, "%s", L.str);
        } else if (L.tok == T_OBRACE) {
            xnode_t *n = node_new(npend > 0 ? pend[0] : "", npend > 1 ? pend[1] : NULL);
            npend = 0;
            node_add_kid(stack[sp - 1], n);
            if (sp < 64) stack[sp++] = n;
        } else if (L.tok == T_CBRACE) {
            if (npend) { /* { Name } reference */
                node_add_kid(stack[sp - 1], node_new("REF", pend[0]));
                npend = 0;
            }
            if (sp > 1) sp--;
        } else if (L.tok == T_STRING) {
            node_add_str(stack[sp - 1], L.str);
        } else if (L.nlist) {
            node_add_vals(stack[sp - 1], L.list, L.nlist);
        }
    }
    free(L.list);
    free(d);
    return root;
}

void x_free(xnode_t *n)
{
    if (!n) return;
    for (int i = 0; i < n->nkids; i++) x_free(n->kids[i]);
    for (int i = 0; i < n->nstrs; i++) free(n->strs[i]);
    free(n->kids);
    free(n->strs);
    free(n->vals);
    free(n);
}

xnode_t *x_find_named(xnode_t *root, const char *tmpl, const char *name)
{
    if (!root) return NULL;
    if ((!tmpl || !strcmp(root->tmpl, tmpl)) && !strcmp(root->name, name)) return root;
    for (int i = 0; i < root->nkids; i++) {
        xnode_t *r = x_find_named(root->kids[i], tmpl, name);
        if (r) return r;
    }
    return NULL;
}
