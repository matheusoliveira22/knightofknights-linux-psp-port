"""One-shot generation of src/game/gen/ from the Ghidra decompile (then maintained by hand).
usage: tools/.venv/bin/python tools/gen_game.py"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ghidra2c as g

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = os.path.join(ROOT, "src/game/gen")
funcs = g.load_functions(os.path.join(ROOT, "re/decomp/all_marked.c"))
gtypes = g.load_gtypes(os.path.join(ROOT, "re/decomp/global_types.tsv"))
sigs = g.signature_table(funcs)
g.load_listing(os.path.join(ROOT, "re/decomp/listing_types.tsv"))
game = sorted(a for a in funcs if a < 0x444000)

protos, files = [], {}
for a in game:
    try:
        code, todo = g.translate_function(a, funcs, gtypes, sigs)
        head = code.split("\n", 1)[0]
        note = ("/* TODO: %s */\n" % "; ".join(sorted(set(todo)))[:400].replace("*/", "")) if todo else ""
    except Exception as e:
        rt, ps = sigs.get(a, (g.U32, []))
        head = "%s f_%x(%s)" % (g.ctype(rt), a, ", ".join("%s param_%d" % (g.ctype(t), i + 1)
                                                       for i, t in enumerate(ps)) or "void")
        code = head + "\n{\n    TODO_translation_failed; /* %s */\n}" % str(e).replace("*/", "")[:150]
        note = ""
    protos.append(head + ";")
    files.setdefault(a >> 14, []).append("/* ---- FUN_%08x ---- */\n%s%s\n" % (a, note, code))

os.makedirs(OUT, exist_ok=True)
with open(os.path.join(OUT, "protos.h"), "w") as f:
    f.write("/* Prototypes of the ported game functions (generated, maintained by hand). */\n")
    f.write("#ifndef KOK_PROTOS_H\n#define KOK_PROTOS_H\n#include <stdint.h>\n")
    f.write("\n".join(protos) + "\n#endif\n")
for k, chunks in files.items():
    with open(os.path.join(OUT, "g_%06x.c" % (k << 14)), "w") as f:
        f.write('/* Game code 0x%06x-0x%06x, translated from the Ghidra decompile by tools/ghidra2c.py\n'
                ' * and fixed by hand. Compare with re/decomp/all_annot.c. */\n#include "vmc.h"\n\n' % (k << 14, (k + 1) << 14))
        f.write("\n".join(chunks))
with open(os.path.join(OUT, "fnreg.c"), "w") as f:
    f.write('/* VA -> function registry for code pointers stored in the original data (generated). */\n')
    f.write('#include "vmc.h"\n#include <stdlib.h>\n\nstatic const struct { uint32_t va; void *fn; } reg[] = {\n')
    for a in game: f.write("    { 0x%x, (void *)f_%x },\n" % (a, a))
    f.write('};\n\nvoid *fn_lookup(uint32_t va)\n{\n    int lo = 0, hi = (int)(sizeof reg / sizeof reg[0]) - 1;\n'
            '    while (lo <= hi) {\n        int m = (lo + hi) / 2;\n        if (reg[m].va == va) return reg[m].fn;\n'
            '        if (reg[m].va < va) lo = m + 1; else hi = m - 1;\n    }\n'
            '    vm_fatal("call through unknown code address %08x", va);\n    return NULL;\n}\n')
print(len(game), "functions,", len(files), "files")
