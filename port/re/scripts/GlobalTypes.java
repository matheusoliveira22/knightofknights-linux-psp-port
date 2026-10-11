// Exports, per decompiled function, the data type of every global symbol it references
// (args: output tsv). Rows: func_addr \t symbol \t global_addr \t type \t size
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.pcode.*;
import java.io.*;
import java.util.*;

public class GlobalTypes extends GhidraScript {
    void walk(ClangNode n, Function f, Set<String> seen, PrintWriter out) {
        if (n instanceof ClangVariableToken) {
            HighVariable hv = ((ClangVariableToken) n).getHighVariable();
            if (hv instanceof HighGlobal) {
                HighSymbol s = hv.getSymbol();
                String key = n.toString();
                if (s != null && seen.add(key)) {
                    out.println(f.getEntryPoint() + "\t" + key + "\t" +
                        (s.getStorage().isMemoryStorage() ? s.getStorage().getMinAddress().toString() : "?") +
                        "\t" + hv.getDataType().getDisplayName() + "\t" + hv.getDataType().getLength());
                }
            }
        }
        for (int i = 0; i < n.numChildren(); i++) walk(n.Child(i), f, seen, out);
    }
    @Override
    public void run() throws Exception {
        PrintWriter out = new PrintWriter(new FileWriter(getScriptArgs()[0]));
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (f.isThunk() || f.getEntryPoint().getOffset() >= 0x444000) continue;
            DecompileResults r = ifc.decompileFunction(f, 120, monitor);
            if (!r.decompileCompleted()) continue;
            walk(r.getCCodeMarkup(), f, new HashSet<>(), out);
        }
        out.close();
    }
}
