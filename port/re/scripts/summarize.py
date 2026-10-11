"""Per-function summary of game code: size, callees, callers, referenced strings."""
import re, sys, collections
src = open(sys.argv[1], encoding="latin-1").read()
parts = re.split(r"^// ===== (\S+) @ ([0-9a-f]+)$", src, flags=re.M)
funcs = {}
for i in range(1, len(parts), 3):
    a = int(parts[i + 1], 16)
    if a < 0x444000: funcs[a] = parts[i + 2]
callers = collections.defaultdict(set)
for a, b in funcs.items():
    for c in set(re.findall(r"FUN_00([0-9a-f]{6})", b)):
        callers[int(c, 16)].add(a)
for a, b in sorted(funcs.items()):
    callees = sorted({int(c, 16) for c in re.findall(r"FUN_00([0-9a-f]{6})", b)} - {a})
    strs = sorted(set(re.findall(r"\bs_([A-Za-z0-9_]+?)_00[0-9a-f]{6}", b)))[:6]
    com = sorted(set(re.findall(r"/\* ([A-Z][A-Za-z0-9]+::\w+) \*/", b)))[:6]
    print(f"{a:06x} lines={b.count(chr(10)):4d} from={','.join('%06x' % c for c in sorted(callers[a]))[:60]} "
          f"to={','.join('%06x' % c for c in callees if c < 0x444000)[:90]} str={strs} com={com}")
