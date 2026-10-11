// Dumps every function's decompiled C into <outdir>/all.c and a symbol index (args: outdir).
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class DecompileAll extends GhidraScript {
    @Override
    public void run() throws Exception {
        String dir = getScriptArgs()[0];
        PrintWriter out = new PrintWriter(new FileWriter(dir + "/all.c"));
        PrintWriter idx = new PrintWriter(new FileWriter(dir + "/functions.tsv"));
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (monitor.isCancelled()) break;
            if (f.isThunk() || f.isExternal()) continue;
            idx.println(f.getEntryPoint() + "\t" + f.getName() + "\t" + f.getBody().getNumAddresses());
            DecompileResults r = ifc.decompileFunction(f, 120, monitor);
            out.println("// ===== " + f.getName() + " @ " + f.getEntryPoint());
            out.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// failed: " + r.getErrorMessage());
        }
        out.close();
        idx.close();
        // Strings with their referencing functions, for locating subsystems.
        PrintWriter sx = new PrintWriter(new FileWriter(dir + "/string_xrefs.tsv"));
        for (Data d : currentProgram.getListing().getDefinedData(true)) {
            if (!d.hasStringValue()) continue;
            for (Reference ref : getReferencesTo(d.getAddress())) {
                Function f = getFunctionContaining(ref.getFromAddress());
                sx.println(d.getAddress() + "\t" + (f == null ? "-" : f.getName()) + "\t" + ref.getFromAddress() + "\t" + d.getDefaultValueRepresentation());
            }
        }
        sx.close();
    }
}
