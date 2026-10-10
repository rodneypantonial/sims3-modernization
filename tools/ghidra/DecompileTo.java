// Writes decompiled C of selected functions (and optionally their callees) to one file.
// Args: <outFile> <depth> <name-or-0xaddr>...   depth 0 = only the listed functions.
// Headless: -process TS3W.exe -noanalysis -readOnly -scriptPath tools/ghidra -postScript DecompileTo.java out.c 1 IC_Route_Plan
// @category Sims3
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import java.io.PrintWriter;
import java.util.*;

public class DecompileTo extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        int depth = Integer.parseInt(args[1]);
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        Set<Function> done = new LinkedHashSet<>();
        Deque<Map.Entry<Function, Integer>> todo = new ArrayDeque<>();
        for (int i = 2; i < args.length; i++) {
            Function f = resolve(args[i]);
            if (f == null) println("not found: " + args[i]);
            else todo.add(Map.entry(f, 0));
        }
        try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
            while (!todo.isEmpty()) {
                var e = todo.poll();
                Function f = e.getKey();
                if (f.isThunk() || !done.add(f)) continue;
                DecompileResults r = ifc.decompileFunction(f, 120, monitor);
                out.printf("// ===== %s @ %s (depth %d)%s%n", f.getName(), f.getEntryPoint(), e.getValue(),
                        f.getComment() == null ? "" : " " + f.getComment());
                out.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// decompile failed: " + r.getErrorMessage());
                if (e.getValue() < depth)
                    for (Function c : f.getCalledFunctions(monitor)) todo.add(Map.entry(c, e.getValue() + 1));
            }
        }
        println("decompiled " + done.size() + " functions to " + args[0]);
    }

    private Function resolve(String s) {
        if (s.startsWith("0x")) return getFunctionContaining(toAddr(Long.decode(s)));
        for (Symbol sym : currentProgram.getSymbolTable().getSymbols(s)) {
            Function f = getFunctionAt(sym.getAddress());
            if (f != null) return f;
        }
        return null;
    }
}
