// Args: <outFile> <lo> <hi> <depth>  external callers of functions in [lo,hi): callee, caller, caller tags, nearest named ancestors (BFS up to depth)
// @category Sims3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import java.io.PrintWriter;
import java.util.*;

public class ExtCallers extends GhidraScript {
    long lo, hi;
    boolean inside(Function f) { long e = f.getEntryPoint().getOffset(); return e >= lo && e < hi; }
    String tags(Function f) {
        Set<String> s = new LinkedHashSet<>();
        for (Address ad : f.getBody().getAddresses(true)) {
            for (Reference r : getReferencesFrom(ad)) {
                Data d = getDataAt(r.getToAddress());
                if (d != null && d.hasStringValue()) {
                    String v = String.valueOf(d.getValue());
                    if (v.contains("/") && !v.contains("\\") && v.length() < 80) s.add(v);
                }
            }
            if (s.size() >= 3) break;
        }
        return String.join(" | ", s);
    }
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        lo = Long.decode(a[1]); hi = Long.decode(a[2]);
        int depth = Integer.parseInt(a[3]);
        Map<Function, Set<String>> calls = new LinkedHashMap<>();
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(toAddr(lo), true);
        while (it.hasNext()) {
            Function f = it.next();
            if (f.getEntryPoint().getOffset() >= hi) break;
            for (Function c : f.getCallingFunctions(monitor))
                if (!inside(c)) calls.computeIfAbsent(c, k -> new TreeSet<>()).add(f.getEntryPoint().toString());
        }
        try (PrintWriter out = new PrintWriter(a[0], "UTF-8")) {
            for (var e : calls.entrySet()) {
                Function c = e.getKey();
                // nearest named ancestors
                Set<String> named = new TreeSet<>();
                Set<Function> seen = new HashSet<>(List.of(c));
                List<Function> lvl = List.of(c);
                for (int d = 0; d < depth && named.size() < 8; d++) {
                    List<Function> nx = new ArrayList<>();
                    for (Function f : lvl) for (Function p : f.getCallingFunctions(monitor)) {
                        if (!seen.add(p)) continue;
                        if (!p.getName().startsWith("FUN_") && !p.getName().startsWith("thunk_")) named.add(p.getName() + "@" + (d + 1));
                        else nx.add(p);
                    }
                    lvl = nx;
                }
                out.printf("%s\t%s\t%s\t%s\t%s%n", c.getEntryPoint(), c.getName(), String.join(",", e.getValue()), tags(c), String.join(" ", named));
            }
        }
        println(calls.size() + " external callers");
    }
}
