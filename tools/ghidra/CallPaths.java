// Shortest call paths from source functions to a target (BFS over direct calls).
// Args: <outFile> <maxDepth> <target> <source>...   names or 0xaddr; source "IC_*" = every internal call.
// @category Sims3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import java.io.PrintWriter;
import java.util.*;

public class CallPaths extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        int maxDepth = Integer.parseInt(a[1]);
        Function target = resolve(a[2]);
        if (target == null) { println("target not found: " + a[2]); return; }
        List<Function> sources = new ArrayList<>();
        for (int i = 3; i < a.length; i++) {
            if (a[i].equals("IC_*")) {
                for (Function f : currentProgram.getFunctionManager().getFunctions(true))
                    if (f.getName().startsWith("IC_")) sources.add(f);
            } else {
                Function f = resolve(a[i]);
                if (f == null) println("source not found: " + a[i]); else sources.add(f);
            }
        }
        // Reverse BFS from the target over callers gives each function's distance and next hop.
        Map<Function, Function> next = new HashMap<>();
        Map<Function, Integer> dist = new HashMap<>();
        Deque<Function> q = new ArrayDeque<>(List.of(target));
        dist.put(target, 0);
        while (!q.isEmpty()) {
            Function f = q.poll();
            if (dist.get(f) >= maxDepth) continue;
            for (Function c : f.getCallingFunctions(monitor))
                if (!dist.containsKey(c)) { dist.put(c, dist.get(f) + 1); next.put(c, f); q.add(c); }
        }
        int found = 0;
        try (PrintWriter out = new PrintWriter(a[0], "UTF-8")) {
            for (Function s : sources) {
                if (!dist.containsKey(s)) continue;
                StringBuilder sb = new StringBuilder(s.getName());
                for (Function f = s; next.containsKey(f); ) { f = next.get(f); sb.append(" -> ").append(f.getName()); }
                out.println(dist.get(s) + "\t" + sb);
                found++;
            }
        }
        println(found + " of " + sources.size() + " sources reach " + target.getName() + " within " + maxDepth);
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
