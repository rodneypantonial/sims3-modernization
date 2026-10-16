// Args: <outFile> <lo> <hi>   dword table dump: addr, value, function at value, refs to addr (vtable starts)
// @category Sims3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;

public class DumpPtrs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        long lo = Long.decode(a[1]), hi = Long.decode(a[2]);
        try (PrintWriter out = new PrintWriter(a[0], "UTF-8")) {
            for (long p = lo; p < hi; p += 4) {
                Address ad = toAddr(p);
                long v = getInt(ad) & 0xffffffffL;
                Function f = getFunctionAt(toAddr(v));
                StringBuilder refs = new StringBuilder();
                for (Reference r : getReferencesTo(ad)) {
                    Function rf = getFunctionContaining(r.getFromAddress());
                    refs.append(rf == null ? r.getFromAddress().toString() : rf.getName()).append(' ');
                }
                out.printf("%08x\t%08x\t%s\t%s%n", p, v, f == null ? "" : f.getName(), refs);
            }
        }
    }
}
