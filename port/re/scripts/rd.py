"""Read typed values from the exe's image: rd.py <type> <hexaddr> [count]
types: i8 u8 i16 u16 i32 u32 f32 f64 str sjis hex"""
import struct, sys, os
EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../game/Knight050/Knight of Knights.exe")
d = open(EXE, "rb").read()
pe = struct.unpack_from("<I", d, 0x3C)[0]
o = pe + 24 + struct.unpack_from("<H", d, pe + 20)[0]
secs = [struct.unpack_from("<8sIIII", d, o + 40 * i) for i in range(struct.unpack_from("<H", d, pe + 6)[0])]

def off(va):
    rva = va - 0x400000
    for nm, vs, sva, rs, ro in secs:
        if sva <= rva < sva + rs:
            return rva - sva + ro
    return None  # bss

def read(t, va, n=1):
    fm = {"i8": "b", "u8": "B", "i16": "h", "u16": "H", "i32": "i", "u32": "I", "f32": "f", "f64": "d"}
    p = off(va)
    if t in ("str", "sjis"):
        if p is None: return "<bss>"
        e = d.index(b"\0", p)
        return d[p:e].decode("cp932" if t == "sjis" else "latin-1", "replace")
    if t == "hex":
        return d[p:p + n].hex(" ") if p is not None else "<bss>"
    if p is None: return ["<bss>"] * n
    return list(struct.unpack_from("<%d%s" % (n, fm[t]), d, p))

if __name__ == "__main__":
    t, a = sys.argv[1], int(sys.argv[2], 16)
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    print(read(t, a, n))
