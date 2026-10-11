// Like DecompileAll, but re-emits the C from the token tree and marks casts that are bit
// reinterpretations (no INT2FLOAT/FLOAT2FLOAT/TRUNC op in the operand) as (__RF32)/(__RF64)/(__RI32).
// args: outdir
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.pcode.*;
import java.io.*;
import java.util.*;

public class DecompileMarked extends GhidraScript {
    List<ClangToken> toks;
    void flat(ClangNode n) {
        if (n instanceof ClangToken) toks.add((ClangToken) n);
        for (int i = 0; i < n.numChildren(); i++) flat(n.Child(i));
    }
    static boolean isFloat(String t) { return t.equals("float") || t.equals("double") || t.equals("float10"); }
    static boolean isInt4(String t) {
        return t.equals("int") || t.equals("uint") || t.equals("undefined4") || t.equals("DWORD") || t.equals("LONG");
    }
    String mnem(ClangToken t) { PcodeOp o = t.getPcodeOp(); return o == null ? "" : o.getMnemonic(); }
    /* index just past the primary operand starting at i (tokens), skipping spaces */
    int operandEnd(int i) {
        int n = toks.size();
        while (i < n && toks.get(i).toString().trim().isEmpty()) i++;
        if (i >= n) return i;
        String s = toks.get(i).toString();
        int depth = 0;
        if (s.equals("(")) {
            for (; i < n; i++) {
                String x = toks.get(i).toString();
                if (x.equals("(") || x.equals("[")) depth++;
                if (x.equals(")") || x.equals("]")) { depth--; if (depth == 0) return i + 1; }
            }
            return n;
        }
        if (s.equals("*") || s.equals("-") || s.equals("&") || s.equals("~") || s.equals("!")) return operandEnd(i + 1);
        i++;
        while (i < n) {
            String x = toks.get(i).toString();
            if (x.equals("[") || x.equals("(")) {
                depth = 0;
                for (; i < n; i++) {
                    String y = toks.get(i).toString();
                    if (y.equals("(") || y.equals("[")) depth++;
                    if (y.equals(")") || y.equals("]")) { depth--; if (depth == 0) { i++; break; } }
                }
            } else if (x.equals("->") || x.equals(".")) i += 2;
            else break;
        }
        return i;
    }
    static boolean feedsConv(Varnode v, int depth) {
        if (v == null || depth > 4) return false;
        Iterator<PcodeOp> it = v.getDescendants();
        while (it.hasNext()) {
            PcodeOp d = it.next();
            int oc = d.getOpcode();
            if (oc == PcodeOp.FLOAT_INT2FLOAT || oc == PcodeOp.FLOAT_FLOAT2FLOAT || oc == PcodeOp.FLOAT_TRUNC) return true;
            if ((oc == PcodeOp.COPY || oc == PcodeOp.CAST || oc == PcodeOp.MULTIEQUAL || oc == PcodeOp.INDIRECT
                    || oc == PcodeOp.STORE) && feedsConv(d.getOutput(), depth + 1)) return true;
        }
        return false;
    }
    int nextNonSpace(int i) {
        while (i < toks.size() && !(toks.get(i) instanceof ClangBreak) && toks.get(i).toString().trim().isEmpty()) i++;
        return i;
    }
    @Override
    public void run() throws Exception {
        String dir = getScriptArgs()[0];
        PrintWriter out = new PrintWriter(new FileWriter(dir + "/all_marked.c"));
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        int marked = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (f.isThunk() || f.getEntryPoint().getOffset() >= 0x444000) continue;
            DecompileResults r = ifc.decompileFunction(f, 120, monitor);
            out.println("// ===== " + f.getName() + " @ " + f.getEntryPoint());
            if (!r.decompileCompleted()) { out.println("// failed"); continue; }
            toks = new ArrayList<>();
            flat(r.getCCodeMarkup());
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < toks.size(); i++) {
                ClangToken t = toks.get(i);
                if (t instanceof ClangBreak) { sb.append("\n"); for (int k = 0; k < ((ClangBreak) t).getIndent(); k++) sb.append(" "); continue; }
                String s = t.toString();
                int ti = nextNonSpace(i + 1), ci = ti < toks.size() ? nextNonSpace(ti + 1) : ti;
                if (s.equals("(") && ci < toks.size() && toks.get(ti) instanceof ClangTypeToken
                        && toks.get(ci).toString().equals(")")) {
                    String ty = toks.get(ti).toString();
                    boolean toFloat = isFloat(ty), toInt = isInt4(ty);
                    if (toFloat || toInt) {
                        int end = operandEnd(ci + 1);
                        boolean conv = false;
                        int bdepth = 0;
                        /* is the cast's value consumed by a float operator? */
                        boolean floatParent = false;
                        {
                            int pk = i - 1;
                            while (pk >= 0 && (toks.get(pk).toString().trim().isEmpty() || toks.get(pk).toString().equals("("))) pk--;
                            if (pk >= 0 && mnem(toks.get(pk)).startsWith("FLOAT_")) floatParent = true;
                        }
                        if (!floatParent) {
                            int nk = end;
                            while (nk < toks.size() && (toks.get(nk).toString().trim().isEmpty() || toks.get(nk).toString().equals(")"))) nk++;
                            if (nk < toks.size() && mnem(toks.get(nk)).startsWith("FLOAT_")) floatParent = true;
                        }
                        int oi = nextNonSpace(ci + 1), ot = oi < toks.size() ? nextNonSpace(oi + 1) : oi;
                        if (toFloat && oi < toks.size() && toks.get(oi).toString().equals("(") && ot < toks.size()
                                && toks.get(ot) instanceof ClangTypeToken && isInt4(toks.get(ot).toString())) conv = true;
                        for (int k = ci + 1; k < end; k++) {
                            String tx = toks.get(k).toString();
                            if (tx.equals("[")) bdepth++;
                            if (tx.equals("]")) bdepth--;
                            String om = mnem(toks.get(k));
                            if (bdepth == 0 && toks.get(k) instanceof ClangOpToken && om.startsWith("INT_")
                                    && !om.equals("INT_ZEXT") && !om.equals("INT_SEXT") && floatParent) conv = true;
                            String m = mnem(toks.get(k));
                            if (m.equals("INT2FLOAT") || m.equals("FLOAT2FLOAT") || m.equals("TRUNC") || m.equals("ROUND")) conv = true;
                            /* data flow: the operand value feeds a conversion op */
                            ClangToken tk = toks.get(k);
                            Varnode vn = null;
                            if (tk instanceof ClangVariableToken) vn = ((ClangVariableToken) tk).getVarnode();
                            PcodeOp po = tk.getPcodeOp();
                            if (vn == null && po != null && po.getOpcode() == PcodeOp.LOAD) vn = po.getOutput();
                            if (feedsConv(vn, 0)) conv = true;
                            if (po != null && feedsConv(po.getOutput(), 0)) conv = true;
                        }
                        /* integer casts of integer operands are plain C casts: only mark when the
                           operand is float-ish (a float load or float variable) */
                        boolean operandFloat = false;
                        for (int k = ci + 1; k < end; k++) {
                            ClangToken x = toks.get(k);
                            if (x instanceof ClangVariableToken && ((ClangVariableToken) x).getHighVariable() != null) {
                                DataType_check: {
                                    String dt = ((ClangVariableToken) x).getHighVariable().getDataType().getDisplayName();
                                    if (dt.startsWith("float")) operandFloat = true;
                                }
                            }
                            if (mnem(x).startsWith("FLOAT_")) operandFloat = true;
                        }
                        if (!conv && (toFloat || operandFloat)) {
                            String mk = toFloat ? (ty.equals("float") ? "__RF32" : "__RF64") : "__RI32";
                            sb.append("(" + mk + ")");
                            i = ci;
                            marked++;
                            continue;
                        }
                    }
                }
                sb.append(s);
            }
            out.println(sb.toString());
        }
        out.close();
        println("marked casts: " + marked);
    }
}
