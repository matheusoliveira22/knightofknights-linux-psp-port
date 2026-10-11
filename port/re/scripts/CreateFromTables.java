// Creates functions at the given entry points (args: file with hex addresses) unless they lie
// inside an existing function (switch-case labels), then lets auto-analysis follow their calls.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;

public class CreateFromTables extends GhidraScript {
    @Override
    public void run() throws Exception {
        String txt = new String(Files.readAllBytes(Paths.get(getScriptArgs()[0]))).trim();
        int made = 0, skipped = 0;
        for (String s : txt.split("\\s+")) {
            Address a = toAddr(Long.parseLong(s, 16));
            if (getFunctionContaining(a) != null) { skipped++; continue; }
            disassemble(a);
            if (createFunction(a, null) != null) made++;
        }
        analyzeChanges(currentProgram);
        println("created " + made + " skipped " + skipped);
    }
}
