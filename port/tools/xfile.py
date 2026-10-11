"""Reader for DirectX .x files in uncompressed binary format ("xof 0303bin 0032").

Produces a tree of XNode(template, name, values, children). `values` is the flat list of
data tokens (ints/floats/strings) in the order they appear, which is enough to decode the
standard templates because their layouts are fixed.
"""
import struct

TOK_NAME, TOK_STRING, TOK_INTEGER, TOK_GUID = 1, 2, 3, 5
TOK_INT_LIST, TOK_FLOAT_LIST = 6, 7
TOK_OBRACE, TOK_CBRACE = 0x0A, 0x0B
TOK_OPAREN, TOK_CPAREN, TOK_OBRACKET, TOK_CBRACKET = 0x0C, 0x0D, 0x0E, 0x0F
TOK_OANGLE, TOK_CANGLE, TOK_DOT, TOK_COMMA, TOK_SEMICOLON = 0x10, 0x11, 0x12, 0x13, 0x14
TOK_TEMPLATE = 0x1F
TOK_WORD, TOK_DWORD, TOK_FLOAT, TOK_DOUBLE, TOK_CHAR = 0x28, 0x29, 0x2A, 0x2B, 0x2C
TOK_UCHAR, TOK_SWORD, TOK_SDWORD, TOK_VOID, TOK_LPSTR = 0x2D, 0x2E, 0x2F, 0x30, 0x31
TOK_UNICODE, TOK_CSTRING, TOK_ARRAY = 0x32, 0x33, 0x34


class XNode:
    __slots__ = ("template", "name", "values", "children", "ref")

    def __init__(self, template, name=None):
        self.template, self.name, self.values, self.children, self.ref = template, name, [], [], None

    def __repr__(self):
        return f"<{self.template} {self.name!r} vals={len(self.values)} kids={len(self.children)}>"

    def find(self, template):
        return [c for c in self.children if c.template == template]


class XReader:
    def __init__(self, data):
        if data[:4] != b"xof " or data[8:12] != b"bin ":
            raise ValueError("not a binary .x file: %r" % data[:16])
        self.d, self.p = data, 16
        self.float64 = data[12:16] == b"0064"

    def _u16(self):
        v = struct.unpack_from("<H", self.d, self.p)[0]; self.p += 2; return v

    def _u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]; self.p += 4; return v

    def tokens(self):
        d = self.d
        while self.p < len(d):
            t = self._u16()
            if t == TOK_NAME or t == TOK_STRING:
                n = self._u32(); s = d[self.p:self.p + n].decode("cp932", "replace"); self.p += n
                if t == TOK_STRING: self._u16()  # terminator token
                yield (t, s)
            elif t == TOK_INTEGER:
                yield (t, self._u32())
            elif t == TOK_GUID:
                self.p += 16; yield (t, None)
            elif t == TOK_INT_LIST:
                n = self._u32(); v = struct.unpack_from("<%dI" % n, d, self.p); self.p += 4 * n
                yield (t, list(v))
            elif t == TOK_FLOAT_LIST:
                n = self._u32()
                fmt, sz = ("<%dd", 8) if self.float64 else ("<%df", 4)
                v = struct.unpack_from(fmt % n, d, self.p); self.p += sz * n
                yield (t, list(v))
            else:
                yield (t, None)

    def parse(self):
        root = XNode("ROOT")
        stack, pending, toks = [root], [], self.tokens()
        for t, v in toks:
            if t == TOK_TEMPLATE:  # skip template definitions
                depth = 0
                for t2, _ in toks:
                    if t2 == TOK_OBRACE: depth += 1
                    elif t2 == TOK_CBRACE:
                        depth -= 1
                        if depth == 0: break
                pending = []
            elif t == TOK_NAME:
                pending.append(v)
            elif t == TOK_OBRACE:
                if pending:
                    node = XNode(pending[0], pending[1] if len(pending) > 1 else None)
                else:
                    node = XNode("?")
                pending = []
                stack[-1].children.append(node)
                stack.append(node)
            elif t == TOK_CBRACE:
                if pending:  # reference: { Name }
                    ref = XNode("REF", pending[0]); ref.ref = pending[0]
                    stack[-1].children.append(ref); pending = []
                stack.pop()
            elif t in (TOK_INT_LIST, TOK_FLOAT_LIST):
                stack[-1].values.extend(v)
            elif t == TOK_INTEGER:
                stack[-1].values.append(v)
            elif t == TOK_STRING:
                stack[-1].values.append(v)
        return root


def load(path):
    with open(path, "rb") as f:
        return XReader(f.read()).parse()


def dump(node, depth=0, out=print):
    out("  " * depth + repr(node) + (" " + str(node.values[:12]) if node.values else ""))
    for c in node.children:
        dump(c, depth + 1, out)
