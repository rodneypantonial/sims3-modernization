// Writes every function as "entry,end,name" (hex addresses, sorted) for symbolizing live stack traces.
// Args: <outFile>
// @category Sims3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;

public class ExportFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        int n = 0;
        try (PrintWriter out = new PrintWriter(getScriptArgs()[0], "UTF-8")) {
            out.println("entry,end,name");
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                out.printf("0x%x,0x%x,%s%n", f.getEntryPoint().getOffset(), f.getBody().getMaxAddress().getOffset(), f.getName());
                n++;
            }
        }
        println("exported " + n + " functions");
    }
}
