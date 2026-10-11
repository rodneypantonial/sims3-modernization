// Prints functions that reference defined strings matching a regex.
// Args: <outFile> <java-regex or @file-with-regex> (use @file when the regex has | : cmd.exe eats it)
// Headless: -process TS3W.exe -noanalysis -readOnly -scriptPath tools/ghidra -postScript StringXrefs.java out.txt "RoutingJob|JobThread"
// @category Sims3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;
import java.util.regex.Pattern;

public class StringXrefs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        String rx = args[1].startsWith("@") ? java.nio.file.Files.readString(java.nio.file.Path.of(args[1].substring(1))).trim() : args[1];
        Pattern re = Pattern.compile(rx);
        int hits = 0;
        try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
            for (Data d : currentProgram.getListing().getDefinedData(true)) {
                if (!d.hasStringValue()) continue;
                String s = String.valueOf(d.getValue());
                if (!re.matcher(s).find()) continue;
                for (Reference r : getReferencesTo(d.getAddress())) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    out.printf("%s\t%s\t%s\t%s%n", d.getAddress(), r.getFromAddress(),
                            f == null ? "-" : f.getName() + "@" + f.getEntryPoint(), s.replace('\n', ' '));
                    hits++;
                }
            }
        }
        println(hits + " references written to " + args[0]);
    }
}
