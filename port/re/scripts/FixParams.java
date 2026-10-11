// Extend functions' parameter lists to a known stack-argument count (args: addr:count ...).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;
import java.util.*;

public class FixParams extends GhidraScript {
    public void run() throws Exception {
        for (String a : getScriptArgs()) {
            String[] p = a.split(":");
            Function f = getFunctionAt(toAddr(Long.parseLong(p[0], 16)));
            int n = Integer.parseInt(p[1]);
            List<Variable> params = new ArrayList<>();
            Parameter[] old = f.getParameters();
            for (int i = 0; i < n; i++) {
                DataType t = Undefined4DataType.dataType;
                params.add(new ParameterImpl("param_" + (i + 1), t, currentProgram));
            }
            f.updateFunction("__cdecl", null, params, Function.FunctionUpdateType.DYNAMIC_STORAGE_FORMAL_PARAMS,
                             true, SourceType.USER_DEFINED);
            println("fixed " + f.getName() + " -> " + n + " params");
        }
    }
}
