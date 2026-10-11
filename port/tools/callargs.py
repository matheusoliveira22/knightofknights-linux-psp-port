"""Per callee: mode of the number of pushes between the previous call (or function start) and each
direct call site. A good estimate of cdecl argument counts in MSVC code."""
import re, sys, collections
ins = []
for l in open(sys.argv[1]):
    m = re.match(r"\s+([0-9a-f]+):\t(?:[0-9a-f ]+\t)?(.*)", l)
    if m and m.group(2).strip() and not re.fullmatch(r"[0-9a-f ]+", m.group(2).strip()):
        ins.append((int(m.group(1), 16), m.group(2).strip()))
starts = set(int(x.split("\t")[0], 16) for x in open(sys.argv[2]))
counts = collections.defaultdict(list)
pushes = 0
for a, t in ins:
    if a in starts or t.startswith("ret") or t.startswith("jmp"): pushes = 0
    if t.startswith("push"): pushes += 1
    elif re.match(r"(add|sub)\s+esp,", t) or t.startswith("pop"): pushes = max(0, pushes - 1) if t.startswith("pop") else 0
    elif t.startswith("call"):
        m = re.match(r"call\s+0x([0-9a-f]+)$", t)
        if m: counts[int(m.group(1), 16)].append(pushes)
        pushes = 0
for f, c in sorted(counts.items()):
    mode = collections.Counter(c).most_common(1)[0][0]
    print("%x\t%d\t%d\t%s" % (f, mode, max(c), ",".join(map(str, c[:12]))))
