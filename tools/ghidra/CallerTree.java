// Args: <outFile> <depth> <name-or-0xaddr>...   caller tree of each target, indented, with call sites; stops at
// named functions (IC_*, known names) unless "all" is passed as depth suffix (e.g. 4all). Up to 12 callers per node.
// Also prints data (vtable) references to every node, since virtual entries have no direct callers.
// @category Sims3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.PrintWriter;
import java.util.*;

public class CallerTree extends GhidraScript {
    PrintWriter out; int max; boolean all;
    void walk(Function f, int d, Set<Function> path) {
        if (d >= max || !path.add(f)) return;
        int n = 0;
        StringBuilder vt = new StringBuilder();
        for (Reference r : getReferencesTo(f.getEntryPoint()))
            if (r.getReferenceType().isData()) vt.append(r.getFromAddress()).append(' ');
        if (vt.length() > 0) out.printf("%s  [data refs: %s]%n", "  ".repeat(d + 1), vt.toString().trim());
        for (Reference r : getReferencesTo(f.getEntryPoint())) {
            if (!r.getReferenceType().isCall() && !r.getReferenceType().isJump()) continue;
            Function c = getFunctionContaining(r.getFromAddress());
            if (c == null) continue;
            if (n++ >= 12) { out.printf("%s...%n", "  ".repeat(d + 1)); break; }
            boolean named = !c.getName().startsWith("FUN_") && !c.getName().startsWith("thunk_");
            out.printf("%s<- %s@%s (site %s)%n", "  ".repeat(d + 1), c.getName(), c.getEntryPoint(), r.getFromAddress());
            if (!named || all) walk(c, d + 1, path);
        }
        path.remove(f);
    }
    private Function resolve(String s) {
        if (s.startsWith("0x")) return getFunctionContaining(toAddr(Long.decode(s)));
        for (Symbol sym : currentProgram.getSymbolTable().getSymbols(s)) { Function f = getFunctionAt(sym.getAddress()); if (f != null) return f; }
        return null;
    }
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        all = a[1].endsWith("all"); max = Integer.parseInt(a[1].replace("all", ""));
        try (PrintWriter o = new PrintWriter(a[0], "UTF-8")) {
            out = o;
            for (int i = 2; i < a.length; i++) {
                Function f = resolve(a[i]);
                if (f == null) {  // no function there (e.g. an unanalysed thunk): list raw references instead
                    out.println("no function at " + a[i] + "; refs:");
                    if (a[i].startsWith("0x")) for (Reference r : getReferencesTo(toAddr(Long.decode(a[i])))) {
                        Function c = getFunctionContaining(r.getFromAddress());
                        out.printf("  <- %s %s (%s)%n", r.getFromAddress(), c == null ? "-" : c.getName(), r.getReferenceType());
                    }
                    continue;
                }
                out.printf("== %s@%s%n", f.getName(), f.getEntryPoint());
                walk(f, 0, new HashSet<>());
            }
        }
    }
}
