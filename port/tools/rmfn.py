"""Remove translated function blocks from a generated file: rmfn.py <file> <addr>..."""
import re, sys
path, addrs = sys.argv[1], {int(a, 16) for a in sys.argv[2:]}
src = open(path).read()
parts = re.split(r"(?=^/\* ---- FUN_[0-9a-f]{8} ---- \*/$)", src, flags=re.M)
keep = [p for p in parts if not (m := re.match(r"/\* ---- FUN_([0-9a-f]{8})", p)) or int(m.group(1), 16) not in addrs]
open(path, "w").write("".join(keep))
print("removed", len(parts) - len(keep))
