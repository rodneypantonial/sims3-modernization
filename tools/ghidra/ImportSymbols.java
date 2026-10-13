// Applies our TS3W.exe symbol file (tools/ghidra/symbols/ts3w-1.67.symbols.csv) to the open program.
// CSV columns: address,name,type,comment. type: function | icall (create the function if missing, then name it),
// label (an instruction inside a function), data (a global, vtable or string). The comment starts with a
// "[verified: ...]" or "[unverified: ...]" tag; unverified rows are applied too, with that tag in the comment.
//
// GUI: Script Manager -> category Sims3 -> ImportSymbols (asks for the CSV).
// Headless (writes the project, so no -readOnly):
//   analyzeHeadless <projectDir> <projectName> -process TS3W.exe -noanalysis -scriptPath tools/ghidra
//     -postScript ImportSymbols.java tools/ghidra/symbols/ts3w-1.67.symbols.csv [keep] [force]
// Options: "keep"  = do not rename functions or labels that already have a user-defined name
//          "force" = apply even when the program's SHA-256 is not the Steam 1.67 TS3W.exe
// @category Sims3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public class ImportSymbols extends GhidraScript {
    static final String STEAM_167_SHA256 = "10b2423e2368e161774462b0ef1960231ad0af298d564a7bb6a6e78c5e880609";

    // Minimal RFC 4180 field split (quoted fields may contain commas and doubled quotes).
    static List<String> split(String line) {
        List<String> out = new ArrayList<>();
        StringBuilder f = new StringBuilder();
        boolean q = false;
        for (int i = 0; i < line.length(); i++) {
            char c = line.charAt(i);
            if (q) {
                if (c == '"' && i + 1 < line.length() && line.charAt(i + 1) == '"') { f.append('"'); i++; }
                else if (c == '"') q = false;
                else f.append(c);
            } else if (c == '"') q = true;
            else if (c == ',') { out.add(f.toString()); f.setLength(0); }
            else f.append(c);
        }
        out.add(f.toString());
        return out;
    }

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        File csv = args.length > 0 ? new File(args[0]) : askFile("Symbol CSV (ts3w-1.67.symbols.csv)", "Import");
        List<String> opts = Arrays.asList(args);
        boolean keep = opts.contains("keep"), force = opts.contains("force");

        String sha = currentProgram.getExecutableSHA256();
        if (!STEAM_167_SHA256.equalsIgnoreCase(sha) && !force) {
            printerr("Program SHA-256 " + sha + " is not the Steam 1.67 TS3W.exe; addresses would be wrong. Pass 'force' to apply anyway.");
            return;
        }
        if (currentProgram.getImageBase().getOffset() != 0x400000L) {
            printerr("Image base is not 0x400000; rebase the program to 0x400000 first.");
            return;
        }

        List<String> lines = Files.readAllLines(csv.toPath(), StandardCharsets.UTF_8);
        int fns = 0, created = 0, labels = 0, kept = 0, failed = 0;
        for (String line : lines.subList(1, lines.size())) {
            if (line.isBlank()) continue;
            List<String> p = split(line);
            Address a = toAddr(Long.decode(p.get(0)));
            String name = p.get(1), type = p.get(2), comment = p.size() > 3 ? p.get(3) : "";
            try {
                if (type.equals("function") || type.equals("icall")) {
                    Function f = getFunctionAt(a);
                    if (f == null) {
                        if (getInstructionAt(a) == null) disassemble(a);
                        f = createFunction(a, name);
                        if (f == null) { println("could not create function " + name + " at " + a); failed++; continue; }
                        created++;
                    }
                    if (keep && f.getSymbol().getSource() == SourceType.USER_DEFINED && !f.getName().equals(name)) { kept++; continue; }
                    f.setName(name, SourceType.USER_DEFINED);
                    f.setComment(comment);
                    fns++;
                } else {
                    Symbol s = getSymbolAt(a);
                    if (keep && s != null && s.getSource() == SourceType.USER_DEFINED && !s.getName().equals(name)) { kept++; continue; }
                    createLabel(a, name, true, SourceType.USER_DEFINED);
                    setEOLComment(a, comment);
                    labels++;
                }
            } catch (Exception e) {
                println("failed " + name + " at " + a + ": " + e.getMessage());
                failed++;
            }
        }
        println("ImportSymbols: " + fns + " functions named (" + created + " created), " + labels + " labels, " + kept + " kept, " + failed + " failed");
    }
}
