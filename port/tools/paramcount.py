"""Estimate each function's stack-argument count from the callee's own [esp+X] accesses.
Tracks ESP linearly: push/pop, sub/add esp, and stdcall-style indirect calls that pop the pushes
made since the previous call."""
import re, sys
lines = [l for l in open(sys.argv[1]) if re.match(r"\s+[0-9a-f]+:\t", l)]
ins = []
for l in lines:
    p = l.rstrip("\n").split("\t")
    a = int(p[0].strip()[:-1], 16); t = p[-1].strip()
    if t and not re.fullmatch(r"[0-9a-f ]+", t): ins.append((a, t))
starts = sorted(int(x.split("\t")[0], 16) for x in open(sys.argv[2]) if int(x.split("\t")[0], 16) < 0x444000)
res = {}
for si, s in enumerate(starts):
    e = starts[si + 1] if si + 1 < len(starts) else 0x444000
    d = 0; pend = []; maxarg = 0
    for a, t in ins:
        if a < s or a >= e: continue
        m = re.match(r"(push|pop)\s", t)
        if m:
            if m.group(1) == "push": d -= 4; pend.append(4)
            else: d += 4
            continue
        m = re.match(r"(sub|add)\s+esp,0x([0-9a-f]+)$", t)
        if m:
            n = int(m.group(2), 16)
            d += -n if m.group(1) == "sub" else n
            if m.group(1) == "add": pend = []
            continue
        if t.startswith("call") and ("PTR" in t or re.match(r"call\s+e", t)):
            d += sum(pend); pend = []
            continue
        if t.startswith("call"):
            pend = []
            continue
        for m in re.finditer(r"\[esp\+0x([0-9a-f]+)\]", t):
            off = int(m.group(1), 16) + d
            if off >= 4: maxarg = max(maxarg, (off - 4) // 4 + 1)
        if t.startswith("ret"): d = 0; pend = []
    res[s] = maxarg
for s, n in res.items(): print("%x\t%d" % (s, n))
