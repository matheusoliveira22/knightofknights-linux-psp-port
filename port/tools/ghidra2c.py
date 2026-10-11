"""Translate Ghidra decompiler output into C for the port's flat 32-bit VM model.

usage: tools/.venv/bin/python tools/ghidra2c.py <all.c> <global_types.tsv> <addr>... > out.c

Model: every pointer is a 32-bit virtual address (uint32_t). Memory is accessed through typed
lvalue macros M8/MU8/M16/MU16/M32/MU32/M64/MU64/MF/MD (core/vmc.h). Globals DAT_xxxxxxxx become
M*(0xxxxxxxxx) with the type Ghidra inferred in that function; address-taken locals and local
arrays live on the VM stack. Constructs the translator cannot handle are emitted as TODO_...
identifiers so the result does not compile until a human has fixed them.
"""
import re, sys, os
from pycparser import c_parser, c_ast

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../re/scripts"))
from comvt import VT  # noqa: E402

# ------------------------------------------------------------------------------------------ types
I8, U8, I16, U16, I32, U32, I64, U64 = [("int", n, s) for n, s in
                                        ((1, 1), (1, 0), (2, 1), (2, 0), (4, 1), (4, 0), (8, 1), (8, 0))]
F32, F64, VOID, CODE = ("float", 4), ("float", 8), ("void",), ("code",)
def PTR(t): return ("ptr", t)
def is_ptr(t): return t[0] in ("ptr", "array")
def is_float(t): return t[0] == "float"
def is_int(t): return t[0] == "int"
def target(t): return t[1]
def size(t):
    if t[0] == "int" or t[0] == "float": return t[1]
    if t[0] == "ptr": return 4
    if t[0] == "array": return size(t[1]) * t[2]
    if t[0] == "struct": return STRUCTS.get(t[1], (0, {}))[0]
    if t[0] == "code": return 1
    return 1

STRUCTS = {  # name -> (size, {field: (offset, type)})
    "RECT": (16, {"left": (0, I32), "top": (4, I32), "right": (8, I32), "bottom": (12, I32)}),
    "D3DVECTOR": (12, {"x": (0, F32), "y": (4, F32), "z": (8, F32)}),
    "_D3DVECTOR": (12, {"x": (0, F32), "y": (4, F32), "z": (8, F32)}),
}
for alias in ("tagRECT", "_RECT"): STRUCTS[alias] = STRUCTS["RECT"]

NAMED = {
    "undefined": U8, "undefined1": U8, "byte": U8, "uchar": U8, "BYTE": U8, "bool": U8, "char": I8,
    "CHAR": I8, "undefined2": U16, "ushort": U16, "WORD": U16, "short": I16, "wchar_t": U16, "WCHAR": U16,
    "undefined4": U32, "uint": U32, "DWORD": U32, "UINT": U32, "ULONG": U32, "uint3": U32,
    "int": I32, "LONG": I32, "BOOL": I32, "HRESULT": I32, "MMRESULT": U32, "int3": I32, "long": I32,
    "undefined3": U32, "undefined5": U64, "undefined6": U64, "undefined7": U64, "undefined8": U64, "ulonglong": U64, "longlong": I64, "__int64": I64, "LONGLONG": I64,
    "float": F32, "double": F64, "float10": F64, "void": VOID, "code": CODE,
    "HWND": U32, "HANDLE": U32, "HGDIOBJ": U32, "HFONT": U32, "HDC": U32, "HINSTANCE": U32, "HMMIO": U32,
    "HGLOBAL": U32, "LPVOID": PTR(VOID), "LPARAM": I32, "WPARAM": U32, "LRESULT": I32, "ATOM": U16,
    "SIZE_T": U32, "LPJOYINFOEX": PTR(U8), "HPSTR": PTR(I8), "LPSTR": PTR(I8), "LPCSTR": PTR(I8), "LPRECT": PTR(("struct", "RECT")),
    "LPDWORD": PTR(U32), "LPOVERLAPPED": PTR(VOID), "LPUNKNOWN": PTR(VOID), "HMENU": U32,
}

def parse_type_name(s):
    s = s.strip()
    n = 0
    while s.endswith("*"): s, n = s[:-1].strip(), n + 1
    m = re.match(r"(\w+)\[(\d+)\]$", s)
    if m:
        t = ("array", NAMED.get(m.group(1), U8), int(m.group(2)))
    elif s in NAMED: t = NAMED[s]
    elif s in STRUCTS: t = ("struct", s)
    else: t = U32 if n == 0 else VOID
    for _ in range(n): t = PTR(t)
    return t

def ctype(t):
    if t[0] == "int": return {(1, 1): "int8_t", (1, 0): "uint8_t", (2, 1): "int16_t", (2, 0): "uint16_t",
                              (4, 1): "int32_t", (4, 0): "uint32_t", (8, 1): "int64_t", (8, 0): "uint64_t"}[t[1:]]
    if t == F32: return "float"
    if t == F64: return "double"
    if t == VOID: return "void"
    return "uint32_t"   # pointers / VAs

def mem(t):
    if t[0] == "int": return {(1, 1): "M8", (1, 0): "MU8", (2, 1): "M16", (2, 0): "MU16", (4, 1): "M32",
                              (4, 0): "MU32", (8, 1): "M64", (8, 0): "MU64"}[t[1:]]
    if t == F32: return "MF"
    if t == F64: return "MD"
    return "MU32"

# ------------------------------------------------------------------------------------------ prelude
PRELUDE = "\n".join("typedef int %s;" % n for n in list(NAMED) + list(STRUCTS) + ["code_t", "__RF32", "__RF64", "__RI32"]
                    if n not in ("int", "char", "short", "long", "float", "double", "void")) + "\n"

COM_GLOBALS = {
    0x4a3864: "DDraw2", 0x4a3858: "DDSurf", 0x4a385c: "DDSurf", 0x4a3860: "DDSurf", 0x4a3880: "RM2",
    0x4a3884: "Device2", 0x4a3888: "Viewport", 0x4a388c: "Frame2", 0x4a3890: "Frame2", 0x4a3894: "Frame2",
    0x4a3898: "Frame2", 0x49b670: "DMPerf", 0x49b678: "DMLoader", 0x49b6a8: "DSound", 0x4a3878: "D3D2",
    0x4a387c: "D3DDev2",
}
RM_FAMILY = ("Frame2", "MeshBuilder2", "AnimSet", "Anim", "Viewport", "Array", "RM2", "Texture", "Material",
             "DDSurf", "D3DDev2", "DSBuf")
API = {
    "wsprintfA": "w_wsprintfA", "lstrcatA": "w_lstrcatA", "lstrlenA": "w_lstrlenA", "lstrcpyA": "w_lstrcpyA",
    "_strlen": "w_strlen", "_strcmp": "w_strcmp", "_strncpy": "w_strncpy", "_memset": "w_memset",
    "_strchr": "w_strchr", "_strstr": "w_strstr", "_strncmp": "w_strncmp", "SetRect": "w_SetRect",
    "timeGetTime": "ms_time", "GetKeyboardState": "w_GetKeyboardState", "_malloc": "vm_malloc",
    "operator_new": "vm_malloc", "MessageBoxA": "w_MessageBoxA", "D3DRMVectorDotProduct": "w_D3DRMVectorDotProduct",
    "D3DRMVectorModulus": "w_D3DRMVectorModulus", "D3DRMVectorNormalize": "w_D3DRMVectorNormalize",
    "SelectObject": "w_SelectObject", "SetTextColor": "w_SetTextColor", "SetBkColor": "w_SetBkColor",
    "SetBkMode": "w_SetBkMode", "DrawTextA": "w_DrawTextA", "TextOutA": "w_TextOutA",
    "CONCAT31": "CONCAT31", "CONCAT22": "CONCAT22", "CONCAT11": "CONCAT11", "CONCAT21": "CONCAT21",
    "CONCAT13": "CONCAT13", "CONCAT12": "CONCAT12", "CONCAT24": "CONCAT24", "CONCAT44": "CONCAT44",
    "SUB84": "SUB84", "SUB41": "SUB41", "SUB42": "SUB42", "ZEXT48": "ZEXT48", "SQRT": "SQRT", "ABS": "ABS",
    "NAN": "NAN", "fsin": "sin", "fcos": "cos", "builtin_memcpy": "w_memcpy", "joyGetPosEx": "w_joyGetPosEx",
    "Sleep": "w_Sleep", "__ftol": "w_ftol", "x87_helper_444fb0": "w_x87_444fb0",
}
# offset/argc -> method for calls whose object type is not a known global (decided from usage)
COM_DEFAULTS = {
    (0x38, 1): ("AnimSet", "SetTime"), (0x60, 2): ("Frame2", "GetPosition"), (0x48, 1): ("Frame2", "AddVisual"),
    (0x7c, 1): ("MeshBuilder2", "SetPerspective"), (0xf4, 4): ("Frame2", "SetPosition"),
    (0x14, 5): ("DDSurf", "Blt"), (0x80, 1): ("DDSurf", "Unlock"), (0x98, 1): ("Frame2", "DeleteChild"),
    (0x30, 6): ("AnimSet", "Load"), (0x2c, 1): ("Frame2", "AddChild"), (0xf0, 7): ("Frame2", "SetOrientation"),
    (0x2c, 5): ("MeshBuilder2", "Load"), (0x78, 1): ("MeshBuilder2", "SetQuality"),
    (0x10, 2): ("Frame2", "AddDestroyCallback"), (0xc, 1): ("Material2", "SetMaterial"), (0x44, 1): ("DDSurf", "GetDC"), (0x68, 1): ("DDSurf", "ReleaseDC"), (0xc, 2): ("Texture2", "GetHandle"), (0x4c, 1): ("Frame2", "GetChildren"), (0x80, 1): ("Frame2", "GetVisuals"),
}
SPECIAL_FUNCS = {0x444ac9: "ms_rand", 0x444abf: "ms_srand", 0x444bc3: "vm_free"}

# ------------------------------------------------------------------------------------------ input
def load_functions(path):
    src = open(path, encoding="latin-1").read()
    parts = re.split(r"^// ===== (\S+) @ ([0-9a-f]+)$", src, flags=re.M)
    return {int(parts[i + 1], 16): parts[i + 2] for i in range(1, len(parts), 3)}

def load_gtypes(path):
    g = {}
    for line in open(path):
        f, name, addr, ty, sz = line.rstrip("\n").split("\t")
        g.setdefault(int(f, 16), {})[name] = (int(addr, 16) if addr != "?" else None, ty)
    return g

def preprocess(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"\b(s|u)_[^\s,;()\[\]]*?_([0-9a-f]{8})(?![0-9a-f])", r"s_str_\2", text)
    text = re.sub(r"\b__(cdecl|fastcall|stdcall|thiscall)\b", "", text)
    text = text.replace("::", "__")
    text = re.sub(r"^(\s*)([A-Za-z_]\w*):\s*$", r"\1\2: ;", text, flags=re.M)
    text = re.sub(r"\b(case [^:\n]*|default):\s*$", r"\1: ;", text, flags=re.M)
    text = re.sub(r"\bstack0x([0-9a-f]+)\b", r"stackaddr_\1", text)
    return text

# ------------------------------------------------------------------------------------------ translator
LISTING = {}
def load_listing(path):
    for line in open(path):
        a, t, n = line.rstrip("\n").split("\t")
        LISTING[int(a, 16)] = t

class Untranslatable(Exception):
    pass

class Fn:
    def __init__(self, addr, gt, sigs):
        self.addr, self.gt, self.sigs = addr, gt, sigs
        self.vars = {}       # name -> type
        self.vmvars = set()  # locals living in VM memory (address taken / arrays / structs)
        self.frame = {}      # stack-named locals -> offset below the entry stack pointer
        self.vtab = {}       # local -> object node whose vtable it holds (iVar = *obj)
        self.alias = {}      # local -> COM global it copies (piVar = DAT_004a3880)
        self.todo = []

    # ---- helpers
    def todo_mark(self, what):
        self.todo.append(what)
        return "TODO_" + re.sub(r"\W", "_", what)[:40]

    def gvar(self, name):
        m = re.match(r"_?DAT_([0-9a-f]{8})$", name)
        if m:
            a = int(m.group(1), 16)
            if name in self.gt: ty = self.gt[name][1]
            else: ty = LISTING.get(a, "undefined")   # address-of uses follow the listing type
            return a, parse_type_name(ty)
        m = re.match(r"PTR_\w*?_?([0-9a-f]{8})$", name)
        if m: return int(m.group(1), 16), PTR(VOID)
        return None

    # ---- expressions: return (code, type, is_lvalue_mem)
    def expr(self, n):
        meth = getattr(self, "e_" + type(n).__name__, None)
        if not meth:
            return self.todo_mark(type(n).__name__), U32
        return meth(n)

    def e_Constant(self, n):
        v = n.value
        if n.type in ("int", "unsigned int", "long int", "unsigned long int", "long long int"):
            vv = v.rstrip("uUlL")
            iv = int(vv, 0) if not (vv.startswith("0") and len(vv) > 1 and not vv.startswith("0x")) else int(vv, 8)
            t = U32 if iv > 0x7fffffff or "u" in v.lower() else I32
            return v, t
        if n.type in ("float", "double"):
            return (v if "." in v or "e" in v.lower() else v + ".0"), F64 if n.type == "double" and not v.endswith(("f", "F")) else F32
        if n.type == "char": return v, I8
        if n.type == "string": return self.todo_mark("string_literal"), PTR(I8)
        return v, I32

    def e_ID(self, n):
        name = n.name
        m = re.match(r"stackaddr_([0-9a-f]+)$", name)
        if m:
            off = 0x100000000 - int(m.group(1), 16)
            self.frame.setdefault("__stack_extent", 0)
            self.frame["__stack_extent"] = max(self.frame["__stack_extent"], off)
            return "MU32(_fr - 0x%x)" % off, U32
        if name in self.vars:
            t = self.vars[name]
            if name in self.frame:
                o = self.frame[name]
                a = "(_fr - 0x%x)" % o if o >= 0 else "(_fr + 0x%x)" % -o
                if t[0] in ("array", "struct"): return a, PTR(t[1]) if t[0] == "array" else PTR(t)
                return "%s%s" % (mem(t), a), t
            if name in self.vmvars:
                if t[0] in ("array", "struct"): return name, PTR(t[1]) if t[0] == "array" else PTR(t)
                return "%s(%s)" % (mem(t), name), t
            return name, t
        g = self.gvar(name)
        if g:
            a, t = g
            if t[0] == "array": return "0x%x" % a, PTR(t[1])
            return "%s(0x%x)" % (mem(t), a), t
        m = re.match(r"s_\w*_([0-9a-f]{8})$", name)
        if m: return "0x%x" % int(m.group(1), 16), PTR(I8)
        m = re.match(r"(FUN|LAB)_([0-9a-f]{8})$", name)
        if m: return "0x%x" % int(m.group(2), 16), PTR(CODE)
        if name in ("true", "false"): return ("1" if name == "true" else "0"), I32
        return self.todo_mark(name), U32

    def addr_of(self, n):
        """&n -> (code, type of pointee)"""
        if isinstance(n, c_ast.ID):
            m = re.match(r"stackaddr_([0-9a-f]+)$", n.name)
            if m:
                off = 0x100000000 - int(m.group(1), 16)
                self.frame["__stack_extent"] = max(self.frame.get("__stack_extent", 0), off)
                return "(_fr - 0x%x)" % off, U32
            if n.name in self.vars:
                t = self.vars[n.name]
                if n.name in self.frame:
                    o = self.frame[n.name]
                    return ("(_fr - 0x%x)" % o if o >= 0 else "(_fr + 0x%x)" % -o), t
                if n.name not in self.vmvars: return self.todo_mark("addr_of_reg_" + n.name), t
                return n.name, t
            g = self.gvar(n.name)
            if g:
                a, t = g
                return "0x%x" % a, (t[1] if t[0] == "array" else t)
            m = re.match(r"(s|LAB|FUN)_\w*?([0-9a-f]{8})$", n.name)
            if m: return "0x%x" % int(m.group(2), 16), I8
        if isinstance(n, c_ast.UnaryOp) and n.op == "*":
            c, t = self.expr(n.expr)
            return c, target(t) if is_ptr(t) else U8
        if isinstance(n, c_ast.ArrayRef):
            a, et = self.index_addr(n)
            return a, et
        if isinstance(n, c_ast.StructRef):
            a, ft = self.field_addr(n)
            return a, ft
        return self.todo_mark("addr_of"), U8

    def index_addr(self, n):
        base, bt = self.expr(n.name)
        idx, it = self.expr(n.subscript)
        et = target(bt) if is_ptr(bt) else U8
        if not is_ptr(bt): self.todo.append("index of non-pointer")
        sz = size(et) or 1
        return ("(%s + (uint32_t)(%s) * %d)" % (base, idx, sz) if sz != 1 else "(%s + (uint32_t)(%s))" % (base, idx)), et

    def e_ArrayRef(self, n):
        a, et = self.index_addr(n)
        if et[0] in ("array", "struct"): return a, PTR(et)
        return "%s(%s)" % (mem(et), a), et

    def field_addr(self, n):
        if n.type == "->":
            base, bt = self.expr(n.name)
            st = target(bt) if is_ptr(bt) else None
        else:
            base, st = self.addr_of(n.name)
        if not st or st[0] != "struct" or st[1] not in STRUCTS:
            return self.todo_mark("field_" + n.field.name), U32
        off, ft = STRUCTS[st[1]][1].get(n.field.name, (None, None))
        if off is None: return self.todo_mark("field_" + n.field.name), U32
        return "(%s + %d)" % (base, off), ft

    def partial(self, n):
        m = re.fullmatch(r"_(\d+)_(\d+)_", n.field.name) if n.type == "." else None
        if not m: return None
        return int(m.group(1)), int(m.group(2))

    def e_StructRef(self, n):
        pf = self.partial(n)
        if pf:
            off, sz = pf
            et = {1: U8, 2: U16, 4: U32, 8: U64}.get(sz, U32)
            if isinstance(n.name, c_ast.ID) and (n.name.name in self.frame or n.name.name in self.vmvars):
                a, _ = self.addr_of(n.name)
                return "%s(%s + %d)" % (mem(et), a, off), et
            if isinstance(n.name, c_ast.ID) and self.gvar(n.name.name):
                a = self.gvar(n.name.name)[0]
                return "%s(0x%x)" % (mem(et), a + off), et
            if isinstance(n.name, c_ast.ID) and re.match(r"s_str_([0-9a-f]{8})$", n.name.name):
                a = int(n.name.name[-8:], 16)
                return "%s(0x%x)" % (mem(et), a + off), et
            c, t = self.expr(n.name)
            if sz == 3: return "((uint32_t)((uint64_t)(%s) >> %d) & 0xffffffu)" % (c, off * 8), U32
            return "((%s)((uint64_t)(%s) >> %d))" % (ctype(et), c, off * 8), et
        a, ft = self.field_addr(n)
        return "%s(%s)" % (mem(ft), a), ft

    def e_UnaryOp(self, n):
        if n.op == "&":
            c, t = self.addr_of(n.expr)
            return c, PTR(t)
        if n.op == "*":
            c, t = self.expr(n.expr)
            if not is_ptr(t):
                self.todo.append("deref non-pointer")
                return "MU32(%s)" % c, U32
            et = target(t)
            if et == CODE: return c, PTR(CODE)
            if et[0] in ("array", "struct"): return c, PTR(et)
            if et == VOID: et = U8
            return "%s(%s)" % (mem(et), c), et
        if n.op == "sizeof": return self.todo_mark("sizeof"), U32
        c, t = self.expr(n.expr)
        if n.op in ("p++", "p--", "++", "--") and is_ptr(t):
            step = size(target(t)) or 1
            op = "+=" if "+" in n.op else "-="
            return "(%s %s %d)" % (c, op, step), t
        if n.op in ("p++", "p--"): return "%s%s" % (c, n.op[1:]), t
        if n.op == "!": return "!(%s)" % c, I32
        return "%s(%s)" % (n.op, c), t

    def arith_type(self, a, b):
        if is_float(a) or is_float(b): return F64 if F64 in (a, b) else F32
        if a[0] != "int" or b[0] != "int": return U32
        sz = max(a[1], b[1], 4)
        uns = (a[1] == sz and not a[2]) or (b[1] == sz and not b[2])
        return ("int", sz, 0 if uns else 1)

    def e_BinaryOp(self, n):
        l, lt = self.expr(n.left)
        r, rt = self.expr(n.right)
        op = n.op
        if op in ("+", "-") and is_ptr(lt) and not is_ptr(rt):
            sz = size(target(lt)) or 1
            return ("(%s %s (uint32_t)(%s) * %d)" % (l, op, r, sz) if sz != 1 else "(%s %s (uint32_t)(%s))" % (l, op, r)), lt
        if op == "+" and is_ptr(rt) and not is_ptr(lt):
            sz = size(target(rt)) or 1
            return "(%s + (uint32_t)(%s) * %d)" % (r, l, sz), rt
        if op == "-" and is_ptr(lt) and is_ptr(rt):
            sz = size(target(lt)) or 1
            return "((int32_t)(%s - %s) / %d)" % (l, r, sz), I32
        if op in ("<", ">", "<=", ">=", "==", "!=", "&&", "||"):
            return "(%s %s %s)" % (l, op, r), I32
        if op in ("<<", ">>"):
            return "(%s %s %s)" % (l, op, r), lt if lt[0] == "int" else U32
        return "(%s %s %s)" % (l, op, r), self.arith_type(lt, rt)

    def convert(self, code, frm, to):
        """implicit conversion for assignment/argument passing: same-size int<->float means bit copy"""
        if to[0] == "float" and frm[0] in ("int", "ptr") and frm != I8 and not re.fullmatch(r"-?\d+", code.strip("()")):
            if size(frm) == 4 and to == F32: return "BITF(%s)" % code
        if to[0] == "float" and frm[0] == "int" and re.fullmatch(r"0x[0-9a-fA-F]+", code) and to == F32:
            return "BITF(%s)" % code
        if to[0] in ("int", "ptr") and frm == F32 and size(to) == 4: return "BITU(%s)" % code
        return code

    def e_Assignment(self, n):
        rv = n.rvalue
        while isinstance(rv, c_ast.Cast): rv = rv.expr
        if isinstance(n.lvalue, c_ast.ID) and n.op == "=":
            if isinstance(rv, c_ast.ID) and self.gvar(rv.name) and self.gvar(rv.name)[0] in COM_GLOBALS:
                self.alias[n.lvalue.name] = COM_GLOBALS[self.gvar(rv.name)[0]]
            else:
                self.alias.pop(n.lvalue.name, None)
            if isinstance(rv, c_ast.UnaryOp) and rv.op == "*":
                o = rv.expr
                while isinstance(o, c_ast.Cast): o = o.expr
                self.vtab[n.lvalue.name] = o
            else:
                self.vtab.pop(n.lvalue.name, None)
        if isinstance(n.lvalue, c_ast.StructRef) and self.partial(n.lvalue) and n.op == "=":
            off, sz = self.partial(n.lvalue)
            base = n.lvalue.name
            if isinstance(base, c_ast.ID) and not (base.name in self.frame or base.name in self.vmvars
                                                   or self.gvar(base.name)):
                c, t = self.expr(base)
                r, rt = self.expr(n.rvalue)
                mask = (1 << (sz * 8)) - 1
                return "(%s = (%s)(((uint64_t)(%s) & ~((uint64_t)0x%x << %d)) | (((uint64_t)(%s) & 0x%x) << %d)))" % (
                    c, ctype(t), c, mask, off * 8, r, mask, off * 8), t
        if isinstance(n.lvalue, c_ast.ID) and n.op == "=" and n.lvalue.name in self.vars \
                and self.vars[n.lvalue.name][0] == "array" and size(self.vars[n.lvalue.name]) in (1, 2, 4, 8):
            at = self.vars[n.lvalue.name]
            a, _ = self.addr_of(n.lvalue)
            et = {1: U8, 2: U16, 4: U32, 8: U64}[size(at)]
            r, rt = self.expr(n.rvalue)
            return "(%s(%s) = (%s)(%s))" % (mem(et), a, ctype(et), r), et
        l, lt = self.expr(n.lvalue)
        r, rt = self.expr(n.rvalue)
        if n.op in ("+=", "-=") and is_ptr(lt):
            sz = size(target(lt)) or 1
            return "(%s %s (uint32_t)(%s) * %d)" % (l, n.op, r, sz), lt
        if n.op == "=": r = self.convert(r, rt, lt)
        return "(%s %s %s)" % (l, n.op, r), lt

    def e_Cast(self, n):
        c, t = self.expr(n.expr)
        tn = n.to_type.type
        mk = " ".join(tn.type.names) if isinstance(tn, c_ast.TypeDecl) and isinstance(tn.type, c_ast.IdentifierType) else ""
        if mk == "__RF32":
            if is_float(t): return "(float)(%s)" % c, F32
            return "BITF(%s)" % c, F32
        if mk == "__RF64":
            if is_float(t): return "(double)(%s)" % c, F64
            return "BITD(%s)" % c, F64
        if mk == "__RI32":
            if t == F32: return "BITU(%s)" % c, U32
            if t == F64: return "BITU((float)(%s))" % c, U32
            return "(uint32_t)(%s)" % c, U32
        tt = self.typename(n.to_type)
        if (is_ptr(tt) or tt == CODE) and is_float(t): return "BITU((float)(%s))" % c, tt
        if is_ptr(tt) or tt == CODE: return c, tt
        if is_ptr(t) and tt[0] == "int": return "(%s)%s" % (ctype(tt), c), tt
        return "(%s)(%s)" % (ctype(tt), c), tt

    def e_TernaryOp(self, n):
        c, _ = self.expr(n.cond)
        a, at = self.expr(n.iftrue)
        b, bt = self.expr(n.iffalse)
        return "(%s ? %s : %s)" % (c, a, b), at

    def e_ExprList(self, n):
        parts = [self.expr(e) for e in n.exprs]
        return "(" + ", ".join(p[0] for p in parts) + ")", parts[-1][1]

    def com_call(self, fexpr, args):
        """(**(code **)(*X + off))(X, args) -> COM_Iface_Method(X, args)"""
        # fexpr: UnaryOp('*', Cast(code**, BinaryOp(+, UnaryOp(*, X), off)))
        inner = fexpr.expr
        while isinstance(inner, (c_ast.Cast,)) or (isinstance(inner, c_ast.UnaryOp) and inner.op == "*"
                                                   and isinstance(inner.expr, c_ast.Cast)):
            inner = inner.expr
        off = 0
        if isinstance(inner, c_ast.BinaryOp) and inner.op == "+" and isinstance(inner.right, c_ast.Constant):
            off = int(inner.right.value.rstrip("uUlL"), 0)
            inner = inner.left
        while isinstance(inner, c_ast.Cast): inner = inner.expr
        if isinstance(inner, c_ast.ID) and inner.name in self.vtab:
            objn = self.vtab[inner.name]
        elif isinstance(inner, c_ast.ID) and inner.name in self.vars and args:
            objn = args[0]          # vtable pointer held in a local: the object is `this`
        elif not (isinstance(inner, c_ast.UnaryOp) and inner.op == "*"): return None
        else: objn = inner.expr
        while isinstance(objn, c_ast.Cast): objn = objn.expr
        obj, _ = self.expr(objn)
        argc = len(args) - 1
        if argc < 0 and getattr(self, "retslot", None) is not None:
            idx0 = off // 4
            cands = {(i, VT[i][idx0][0], VT[i][idx0][1]) for i in RM_FAMILY if idx0 < len(VT[i])}
            cands |= {(i, m, a) for (o, a), (i, m) in COM_DEFAULTS.items() if o == off}
            if isinstance(objn, c_ast.ID):
                g0 = self.gvar(objn.name)
                if g0 and g0[0] in COM_GLOBALS:
                    ifc = COM_GLOBALS[g0[0]]
                    cands = {(ifc, VT[ifc][idx0][0], VT[ifc][idx0][1])} if idx0 < len(VT[ifc]) else set()
            defs = {(i, m, a) for (o, a), (i, m) in COM_DEFAULTS.items() if o == off}
            if len(defs) == 1 and not (isinstance(objn, c_ast.ID) and self.gvar(objn.name)
                                       and self.gvar(objn.name)[0] in COM_GLOBALS):
                cands = defs
            names = {(i, m) for i, m, a in cands}
            argcs = {a for i, m, a in cands}
            if len(names) == 1 and len(argcs) == 1 or len(argcs) == 1 and len({m for i, m in names}) == 1:
                i, m, a = sorted(cands)[0]
                sa = self.stack_args(a + 1)
                self.todo.append("stack-args %s::%s" % (i, m))
                return "COM_%s_%s(%s)" % (i, m, ", ".join(sa)), U32
        iface = None
        if isinstance(objn, c_ast.ID):
            g = self.gvar(objn.name)
            if g and g[0] in COM_GLOBALS: iface = COM_GLOBALS[g[0]]
            elif objn.name in self.alias: iface = self.alias[objn.name]
        idx = off // 4
        if iface:
            meth = VT[iface][idx][0] if idx < len(VT[iface]) else "?"
        else:
            if idx <= 2: iface, meth = "Unk", ["QueryInterface", "AddRef", "Release"][idx]
            else:
                c = [(i, VT[i][idx][0]) for i in RM_FAMILY if idx < len(VT[i]) and VT[i][idx][1] == argc]
                if (off, argc) in COM_DEFAULTS: iface, meth = COM_DEFAULTS[(off, argc)]
                elif len(c) == 1: iface, meth = c[0]
                else:
                    return "TODO_COM_%x(%s)" % (off, ", ".join(self.expr(a)[0] for a in args)), U32
        conv = []
        for a in args:
            c, t = self.expr(a)
            conv.append(("BITU(%s)" % c) if t == F32 else c)
        return "COM_%s_%s(%s)" % (iface, meth, ", ".join(conv)), U32

    def e_FuncCall(self, n):
        args = n.args.exprs if n.args else []
        f = n.name
        if isinstance(f, c_ast.UnaryOp) and f.op == "*" and isinstance(f.expr, (c_ast.Cast, c_ast.UnaryOp)):
            r = self.com_call(f, args)
            if r: return r
        if isinstance(f, c_ast.ID):
            name = f.name
            m = re.match(r"FUN_([0-9a-f]{8})$", name)
            if m:
                a = int(m.group(1), 16)
                if a in SPECIAL_FUNCS: fname, sig = SPECIAL_FUNCS[a], None
                else: fname, sig = "f_%x" % a, self.sigs.get(a)
                if sig and len(args) < len(sig[1]) and not args and getattr(self, "retslot", None) is not None:
                    self.todo.append("stack-args f_%x" % a)
                    return "%s(%s)" % (fname, ", ".join(self.stack_args(len(sig[1])))), sig[0]
                out = []
                for i, x in enumerate(args):
                    c, t = self.expr(x)
                    pt = sig[1][i] if sig and i < len(sig[1]) else U32
                    if pt == F64: c = "(double)(%s)" % c
                    elif is_float(t): c = "BITU((float)(%s))" % c
                    out.append(c)
                rt = sig[0] if sig else U32
                return "%s(%s)" % (fname, ", ".join(out)), rt
            if name in API:
                out = []
                for x in args:
                    c, t = self.expr(x)
                    out.append(c)
                rt = F64 if name in ("SQRT", "fsin", "fcos", "ABS", "x87_helper_444fb0") else I64 if name == "__ftol" else U32
                if name in ("SQRT", "ABS"):
                    rt = self.expr(args[0])[1]
                return "%s(%s)" % (API[name], ", ".join(out)), rt
            return self.todo_mark("call_" + name) + "(%s)" % ", ".join(self.expr(x)[0] for x in args), U32
        # call through computed pointer (function tables)
        fc, ft = self.expr(f.expr if isinstance(f, c_ast.UnaryOp) and f.op == "*" else f)
        return "CALLVA(%s)(%s)" % (fc, ", ".join(self.expr(x)[0] for x in args)), U32

    # ---- statements
    def typename(self, tn):
        t = tn.type if isinstance(tn, c_ast.Typename) else tn
        n = 0
        while isinstance(t, c_ast.PtrDecl): t, n = t.type, n + 1
        if isinstance(t, c_ast.TypeDecl) and isinstance(t.type, c_ast.IdentifierType):
            base = " ".join(t.type.names)
        else:
            base = "undefined4"
        base = {"unsigned int": "uint", "unsigned char": "byte", "unsigned short": "ushort"}.get(base, base)
        return parse_type_name(base + "*" * n)

    def decl_type(self, d):
        t = d.type
        if isinstance(t, c_ast.ArrayDecl):
            et = self.typename(t.type)
            dim = int(t.dim.value, 0) if t.dim is not None else 1
            return ("array", et, dim)
        return self.typename(t)

    def note_retaddr(self, n):
        """Ghidra shows `push <return address>` as a store of a .text constant into a stack slot"""
        if isinstance(n, c_ast.Assignment) and n.op == "=" and isinstance(n.lvalue, c_ast.ID) \
                and n.lvalue.name in self.frame and isinstance(n.rvalue, (c_ast.Constant, c_ast.Cast)):
            rv = n.rvalue.expr if isinstance(n.rvalue, c_ast.Cast) else n.rvalue
            if isinstance(rv, c_ast.Constant) and rv.type in ("float", "double"):
                import struct as _st
                try:
                    v = _st.unpack("<I", _st.pack("<f", float(rv.value.rstrip("fF"))))[0]
                except (ValueError, OverflowError):
                    return
                if 0x401000 <= v < 0x444000: self.retslot = self.frame[n.lvalue.name]
                return
            if isinstance(rv, c_ast.Constant) and rv.type == "int":
                try:
                    v = int(rv.value.rstrip("uUlL"), 0)
                except ValueError:
                    return
                if 0x401000 <= v < 0x444000: self.retslot = self.frame[n.lvalue.name]

    def stack_args(self, count):
        """arguments of a call whose pushes Ghidra lost: slots above the return-address slot"""
        if getattr(self, "retslot", None) is None: return None
        r = self.retslot
        return ["MU32(_fr - 0x%x)" % (r - 4 * (i + 1)) for i in range(count)]

    def stmt(self, n, ind):
        p = "    " * ind
        k = type(n).__name__
        self.note_retaddr(n)
        if k == "Compound":
            return "\n".join(self.stmt(s, ind) for s in (n.block_items or []))
        if k == "Decl":
            return ""  # handled up-front
        if k == "Return":
            if n.expr is None: return p + "VRET;"
            c, t = self.expr(n.expr)
            c = self.convert(c, t, self.ret)
            return p + "VRETV(%s);" % c
        if k == "If":
            c, _ = self.expr(n.cond)
            s = p + "if (%s) {\n%s\n%s}" % (c, self.stmt(n.iftrue, ind + 1), p)
            if n.iffalse is not None:
                s += " else {\n%s\n%s}" % (self.stmt(n.iffalse, ind + 1), p)
            return s
        if k == "While":
            c, _ = self.expr(n.cond)
            return p + "while (%s) {\n%s\n%s}" % (c, self.stmt(n.stmt, ind + 1), p)
        if k == "DoWhile":
            c, _ = self.expr(n.cond)
            return p + "do {\n%s\n%s} while (%s);" % (self.stmt(n.stmt, ind + 1), p, c)
        if k == "For":
            i = self.expr(n.init)[0] if n.init else ""
            c = self.expr(n.cond)[0] if n.cond else ""
            x = self.expr(n.next)[0] if n.next else ""
            return p + "for (%s; %s; %s) {\n%s\n%s}" % (i, c, x, self.stmt(n.stmt, ind + 1), p)
        if k == "Switch":
            c, _ = self.expr(n.cond)
            return p + "switch (%s) {\n%s\n%s}" % (c, self.stmt(n.stmt, ind + 1), p)
        if k == "Case":
            v, _ = self.expr(n.expr)
            return p + "case %s:\n%s" % (v, "\n".join(self.stmt(s, ind + 1) for s in n.stmts))
        if k == "Default":
            return p + "default:\n%s" % "\n".join(self.stmt(s, ind + 1) for s in n.stmts)
        if k == "Label":
            return "%s:\n%s" % (n.name, self.stmt(n.stmt, ind))
        if k == "Goto": return p + "goto %s;" % n.name
        if k == "Break": return p + "break;"
        if k == "Continue": return p + "continue;"
        if k == "EmptyStatement": return p + ";"
        c, _ = self.expr(n)
        return p + c + ";"

    def translate(self, fd):
        decl = fd.decl
        fdecl = decl.type
        self.ret = self.typename(fdecl.type)
        params = []
        if fdecl.args:
            for a in fdecl.args.params:
                if isinstance(a, c_ast.Typename): continue
                t = self.decl_type(a)
                self.vars[a.name] = t
                params.append((a.name, t))
        locals_ = []
        for item in fd.body.block_items or []:
            if isinstance(item, c_ast.Decl):
                t = self.decl_type(item)
                self.vars[item.name] = t
                locals_.append((item.name, t))
        # address-taken / aggregate locals live in VM memory
        class V(c_ast.NodeVisitor):
            def __init__(s): s.taken = set()
            def visit_UnaryOp(s, n):
                if n.op == "&" and isinstance(n.expr, c_ast.ID): s.taken.add(n.expr.name)
                if n.op == "&" and isinstance(n.expr, c_ast.StructRef) and isinstance(n.expr.name, c_ast.ID):
                    s.taken.add(n.expr.name.name)
                s.generic_visit(n)
            def visit_StructRef(s, n):
                if n.type == "." and isinstance(n.name, c_ast.ID): s.taken.add(n.name.name)
                s.generic_visit(n)
        v = V()
        v.visit(fd.body)
        for name, t in locals_:
            m = re.match(r"(?:local|[a-z]{1,3}Stack)_([0-9a-f]+)$", name)
            if m:
                self.frame[name] = int(m.group(1), 16)
            elif name in v.taken or t[0] in ("array", "struct"):
                self.vmvars.add(name)
        pv = [n for n, t in params if n in v.taken]
        # x86 cdecl arguments are contiguous on the stack; if any is address-taken, keep them all
        # in the VM frame (callers may pass vectors by value and the callee walk them by pointer)
        self.pframe = {}
        if pv:
            for i, (n_, t_) in enumerate(params):
                self.pframe[n_] = 4 + 4 * i
                self.frame[n_] = -(4 + 4 * i)
        fp = [n for n, t in params if t == F32 and n not in self.pframe]
        body = self.stmt(fd.body, 1)
        def pdecl(n, t):
            if t == F32 or n in self.pframe: return "uint32_t %s_bits" % n
            return "%s %s" % (ctype(t), n)
        sig = ", ".join(pdecl(n, t) for n, t in params) or "void"
        out = ["%s f_%x(%s)" % (ctype(self.ret), self.addr, sig), "{"]
        uses_vm = bool(self.vmvars) or bool(self.frame)
        out.append("    VFRAME;" if uses_vm else "    VNOFRAME;")
        if self.frame:
            top = max([o for o in self.frame.values()] + [0]) + 8
            above = 4 + 4 * len(params) if self.pframe else 0
            out.append("    const uint32_t _fr = vm_salloc(0x%x) + 0x%x;" % (top + above, top))
        for n_, o in self.pframe.items():
            out.append("    MU32(_fr + 0x%x) = %s_bits;" % (o, n_))
        for n in fp:
            out.append("    float %s = BITF(%s_bits);" % (n, n))
        for name, t in locals_:
            if name in self.frame:
                continue
            if name in self.vmvars:
                out.append("    uint32_t %s = vm_salloc(%d);" % (name, max(size(t), 4)))
            elif t[0] in ("array", "struct"):
                out.append("    uint32_t %s = vm_salloc(%d);" % (name, max(size(t), 4)))
            else:
                out.append("    %s %s = 0;" % (ctype(t), name))
        out.append(body)
        if self.ret == VOID and not body.rstrip().endswith("VRET;"): out.append("    VRET;")
        out.append("}")
        return "\n".join(out)


def signature_table(funcs):
    """addr -> (return type, [param types]) from Ghidra's function headers"""
    sigs = {}
    for a, body in funcs.items():
        m = re.search(r"^\s*([\w \*]+?)\s*\**\s*(?:__\w+\s+)?FUN_[0-9a-f]{8}\s*\(([^)]*)\)", body, re.M)
        if not m: continue
        head = re.search(r"^([\w \*]+?)\s*(?:__\w+\s+)?FUN_[0-9a-f]{8}\s*\(", body, re.M).group(1)
        params = []
        ps = m.group(2).strip()
        if ps and ps != "void":
            for p in ps.split(","):
                p = p.strip()
                pm = re.match(r"(.*?)(\w+)$", p)
                params.append(parse_type_name(pm.group(1).replace(" ", "")) if pm else U32)
        sigs[a] = (parse_type_name(head.replace(" ", "")), params)
    return sigs


def translate_function(a, funcs, gtypes, sigs):
    text = preprocess(funcs[a])
    parser = c_parser.CParser()
    ast = parser.parse(PRELUDE + text, filename="f_%x" % a)
    fd = [e for e in ast.ext if isinstance(e, c_ast.FuncDef)][0]
    fn = Fn(a, gtypes.get(a, {}), sigs)
    code = fn.translate(fd)
    return code, fn.todo


def main():
    funcs = load_functions(sys.argv[1])
    gtypes = load_gtypes(sys.argv[2])
    sigs = signature_table(funcs)
    for s in sys.argv[3:]:
        a = int(s, 16)
        try:
            code, todo = translate_function(a, funcs, gtypes, sigs)
            print("/* FUN_%08x%s */" % (a, (" TODO: " + "; ".join(sorted(set(todo)))[:300]) if todo else ""))
            print(code + "\n")
        except Exception as e:
            print("/* FUN_%08x: translation failed: %s */\n" % (a, str(e).replace("*/", "")[:200]))


if __name__ == "__main__":
    main()
