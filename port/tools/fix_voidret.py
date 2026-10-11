"""Make void functions whose (garbage EAX) result callers read return uint32_t 0: fix_voidret.py addr..."""
import re, sys, glob
import subprocess
addrs = sys.argv[1:]
if addrs == ["auto"]:
    # find void callees whose results are used, from the compiler's diagnostics
    out = subprocess.run(["cmake", "--build", "build-pc", "--target", "kok", "--", "-k"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True).stdout
    found = set()
    for m in re.finditer(r"(src/game/gen/g_\w+\.c):(\d+):(\d+): error: void value not ignored", out):
        line = open(m.group(1)).read().split("\n")[int(m.group(2)) - 1][int(m.group(3)) - 1:]
        f = re.search(r"f_([0-9a-f]+)\(", line)
        if f: found.add(f.group(1))
    addrs = sorted(found)
    print("void-ret:", " ".join(addrs))
# functions whose EAX at return is a parameter (checked in the disassembly); others return garbage
RETEXPR = {"42a860": "param_1", "42aef0": "param_1", "42af20": "param_1", "428860": "(uint32_t)param_3"}
for path in glob.glob("src/game/gen/g_*.c") + ["src/game/gen/protos.h"]:
    s = open(path).read()
    for a in addrs:
        s = re.sub(r"^void f_%s\(" % a, "uint32_t f_%s(" % a, s, flags=re.M)
        m = re.search(r"^uint32_t f_%s\([^)]*\)\n\{" % a, s, flags=re.M)
        if m and path.endswith(".c"):
            end = s.index("\n}\n", m.end())
            body = s[m.end():end].replace("VRET;", "VRETV(%s);" % RETEXPR.get(a, "0"))
            s = s[:m.end()] + body + s[end:]
    open(path, "w").write(s)
