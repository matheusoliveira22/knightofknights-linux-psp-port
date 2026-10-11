// Dump decompiler tokens with their p-code ops for one function (args: out, addr)
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.pcode.*;
import java.io.*;

public class TokDump extends GhidraScript {
    PrintWriter out;
    void walk(ClangNode n, int d) {
        if (n instanceof ClangToken) {
            ClangToken t = (ClangToken) n;
            PcodeOp op = t.getPcodeOp();
            String s = t.toString();
            if (s.trim().length() > 0)
                out.println(n.getClass().getSimpleName() + "\t" + s + "\t" + (op == null ? "" : op.getMnemonic()) + "\t" + t.getMinAddress());
        }
        for (int i = 0; i < n.numChildren(); i++) walk(n.Child(i), d + 1);
    }
    public void run() throws Exception {
        String[] a = getScriptArgs();
        out = new PrintWriter(new FileWriter(a[0]));
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        Function f = getFunctionAt(toAddr(Long.parseLong(a[1], 16)));
        DecompileResults r = ifc.decompileFunction(f, 120, monitor);
        walk(r.getCCodeMarkup(), 0);
        out.close();
    }
}
