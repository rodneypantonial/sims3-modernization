// Names TS3W.exe functions after the Mono internal calls bound to them.
// Input: CSV "name,address" from tools/internal-calls-map.ts ({fn*, name*} table), path passed as script arg.
// Headless: analyzeHeadless <proj> TS3 -process TS3W.exe -noanalysis -scriptPath tools/ghidra
//           -postScript ApplyInternalCallNames.java game-copy/analysis/internal-calls-map.csv
// @category Sims3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.*;
import java.util.List;

public class ApplyInternalCallNames extends GhidraScript {
    @Override
    protected void run() throws Exception {
        List<String> lines = Files.readAllLines(Path.of(getScriptArgs()[0]));
        int named = 0, created = 0, cleared = 0;
        // re-runs must not leave stale names behind (an earlier map was shifted by one entry)
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (f.getName().startsWith("IC_")) { f.setName(null, SourceType.DEFAULT); f.setComment(null); cleared++; }
        }
        println("cleared " + cleared + " old IC_ names");
        for (String line : lines.subList(1, lines.size())) {
            String[] p = line.split(",");
            Address a = toAddr(Long.decode(p[1]));
            // "ScriptCore.Route::Route_Plan" -> "IC_Route_Plan"; keeps names unique and greppable
            String name = "IC_" + p[0].substring(p[0].indexOf("::") + 2);
            Function f = getFunctionAt(a);
            if (f == null) {
                disassemble(a);
                f = createFunction(a, name);
                if (f == null) { println("could not create function at " + a + " for " + p[0]); continue; }
                created++;
            }
            f.setName(name, SourceType.USER_DEFINED);
            f.setComment(p[0]);
            named++;
        }
        println("named " + named + " functions (" + created + " created)");
    }
}
