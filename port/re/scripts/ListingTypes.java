// For every DAT_ address referenced in the decompile, export the listing data type (args: in list, out tsv)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;
import java.nio.file.*;

public class ListingTypes extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        PrintWriter out = new PrintWriter(new FileWriter(a[1]));
        for (String s : Files.readAllLines(Paths.get(a[0]))) {
            if (s.isEmpty()) continue;
            Data d = getDataAt(toAddr(Long.parseLong(s, 16)));
            String t = d == null ? "undefined" : d.getDataType().getDisplayName();
            int len = d == null ? 1 : d.getLength();
            out.println(s + "\t" + t + "\t" + len);
        }
        out.close();
    }
}
