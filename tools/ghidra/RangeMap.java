// Args: <outFile> <lo> <hi> [<lo> <hi>...]  per function in ranges: entry,end,size,name,callers,data refs(vtables),strings,callees
// @category Sims3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import java.io.PrintWriter;
import java.util.*;

public class RangeMap extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        try (PrintWriter out = new PrintWriter(a[0], "UTF-8")) {
            for (int i = 1; i + 1 < a.length; i += 2) {
                Address lo = toAddr(Long.decode(a[i])), hi = toAddr(Long.decode(a[i + 1]));
                FunctionIterator it = currentProgram.getFunctionManager().getFunctions(lo, true);
                while (it.hasNext()) {
                    Function f = it.next();
                    if (f.getEntryPoint().compareTo(hi) >= 0) break;
                    StringBuilder callers = new StringBuilder(), drefs = new StringBuilder(), strs = new StringBuilder(), callees = new StringBuilder();
                    Set<Function> cs = f.getCallingFunctions(monitor);
                    int n = 0;
                    for (Function c : cs) { if (n++ < 25) callers.append(c.getName()).append('@').append(c.getEntryPoint()).append(' '); }
                    for (Reference r : getReferencesTo(f.getEntryPoint())) {
                        if (r.getReferenceType().isData() || getFunctionContaining(r.getFromAddress()) == null && !r.getReferenceType().isCall())
                            drefs.append(r.getFromAddress()).append(' ');
                    }
                    Set<String> seen = new LinkedHashSet<>();
                    for (Address ad : f.getBody().getAddresses(true)) {
                        for (Reference r : getReferencesFrom(ad)) {
                            Data d = getDataAt(r.getToAddress());
                            if (d != null && d.hasStringValue()) seen.add(String.valueOf(d.getValue()).replace('\t', ' ').replace('\n', ' '));
                        }
                    }
                    for (String s : seen) strs.append('"').append(s.length() > 60 ? s.substring(0, 60) : s).append("\" ");
                    for (Function c : f.getCalledFunctions(monitor)) callees.append(c.getName()).append('@').append(c.getEntryPoint()).append(' ');
                    out.printf("%s\t%s\t%d\t%s\tN=%d\t%s\tD:%s\tS:%s\tC:%s%n", f.getEntryPoint(), f.getBody().getMaxAddress(),
                            f.getBody().getNumAddresses(), f.getName(), cs.size(), callers, drefs, strs, callees);
                }
            }
        }
    }
}
