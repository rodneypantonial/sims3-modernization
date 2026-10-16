// Prints every reference to the given addresses (globals, locks, vtables, functions): from, type, function, instruction.
// Args: <outFile> <0xaddr>...
// Headless: -process TS3W.exe -noanalysis -readOnly -scriptPath tools/ghidra -postScript XrefsTo.java out.txt 0xFB2CD0
// @category Sims3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;

public class XrefsTo extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        Listing lst = currentProgram.getListing();
        try (PrintWriter out = new PrintWriter(a[0], "UTF-8")) {
            for (int i = 1; i < a.length; i++) {
                Address t = toAddr(Long.decode(a[i]));
                int n = 0;
                for (Reference r : getReferencesTo(t)) {
                    Address from = r.getFromAddress();
                    Function f = getFunctionContaining(from);
                    Instruction ins = lst.getInstructionAt(from);
                    out.printf("%s\t%s\t%s\t%s\t%s%n", t, from, r.getReferenceType(),
                        f == null ? "-" : f.getName() + "@" + f.getEntryPoint(), ins == null ? "" : ins.toString());
                    n++;
                }
                out.printf("# %s: %d refs%n", t, n);
            }
        }
    }
}
