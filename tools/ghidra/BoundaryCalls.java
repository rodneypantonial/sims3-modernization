// Boundary of a code range (a statically linked library): references in from outside, calls out, strings used inside.
// Args: <outFile> <lo hex> <hi hex>   (range is [lo, hi))
// Lines: FN entry size name | IN from fromFunc toEntry toName refType | OUT fromEntry fromName to toName kind | STR from string
//        | EXT fromEntry importName | IND fromEntry computedCallCount
// @category Sims3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.PrintWriter;

public class BoundaryCalls extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        Address lo = toAddr(Long.decode(a[1])), hi = toAddr(Long.decode(a[2]));
        java.util.function.Predicate<Address> in = x -> x.compareTo(lo) >= 0 && x.compareTo(hi) < 0;
        Listing lst = currentProgram.getListing();
        try (PrintWriter out = new PrintWriter(a[0], "UTF-8")) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(lo, true)) {
                if (!in.test(f.getEntryPoint())) break;
                out.printf("FN\t%s\t%d\t%s%n", f.getEntryPoint(), f.getBody().getNumAddresses(), f.getName());
                for (Reference r : getReferencesTo(f.getEntryPoint())) {
                    if (in.test(r.getFromAddress())) continue;
                    Function c = getFunctionContaining(r.getFromAddress());
                    out.printf("IN\t%s\t%s\t%s\t%s\t%s%n", r.getFromAddress(), c == null ? "-" : c.getName() + "@" + c.getEntryPoint(),
                            f.getEntryPoint(), f.getName(), r.getReferenceType());
                }
                int computed = 0;
                for (Instruction ins : lst.getInstructions(f.getBody(), true)) {
                    FlowType ft = ins.getFlowType();
                    if (ft.isCall() && ft.isComputed()) computed++;
                    for (Reference r : ins.getReferencesFrom()) {
                        Address t = r.getToAddress();
                        if (r.getReferenceType().isFlow() && !in.test(t) && t.isMemoryAddress()) {
                            Function g = getFunctionAt(t);
                            if (g == null) g = getFunctionContaining(t);
                            out.printf("OUT\t%s\t%s\t%s\t%s\t%s%n", f.getEntryPoint(), f.getName(), t, g == null ? "-" : g.getName(), r.getReferenceType());
                        } else if (r.getReferenceType().isData()) {
                            Data d = lst.getDataAt(t);
                            if (d != null && d.hasStringValue()) out.printf("STR\t%s\t%s%n", ins.getAddress(), String.valueOf(d.getValue()).replace('\n', ' ').replace('\t', ' '));
                            Symbol s = getSymbolAt(t);
                            if (s != null && s.isExternal() || (d != null && d.isPointer() && d.getValue() instanceof Address p && p.isExternalAddress()))
                                out.printf("EXT\t%s\t%s%n", f.getEntryPoint(), s != null ? s.getName(true) : d.getValue());
                        }
                    }
                }
                if (computed > 0) out.printf("IND\t%s\t%d%n", f.getEntryPoint(), computed);
            }
        }
        println("written " + a[0]);
    }
}
