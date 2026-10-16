// Args: <outFile>  for each caller of Service_ctor 0x59ECC0: ctor func, immediate pointers stored (vtable candidates) with slots +0x1C/+0x20, strings in func
// @category Sims3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.scalar.Scalar;
import java.io.*;
import java.util.*;

public class Services extends GhidraScript {
    public void run() throws Exception {
        try (PrintWriter out = new PrintWriter(getScriptArgs()[0], "UTF-8")) {
            Address ctor = toAddr(0x59ECC0L);
            Set<Function> seen = new LinkedHashSet<>();
            for (Reference r : getReferencesTo(ctor)) { Function f = getFunctionContaining(r.getFromAddress()); if (f != null) seen.add(f); }
            Listing L = currentProgram.getListing();
            for (Function f : seen) {
                StringBuilder sb = new StringBuilder();
                InstructionIterator ii = L.getInstructions(f.getBody(), true);
                while (ii.hasNext()) { Instruction ins = ii.next();
                    if (!ins.getMnemonicString().equals("MOV")) {
                        for (Reference ref : ins.getReferencesFrom()) { Data d = L.getDataAt(ref.getToAddress()); if (d != null && d.hasStringValue()) sb.append(" str=\"").append(d.getValue()).append('"'); }
                        continue; }
                    for (int op = 0; op < ins.getNumOperands(); op++) for (Object o : ins.getOpObjects(op)) if (o instanceof Scalar && op == 1) {
                        long v = ((Scalar) o).getUnsignedValue();
                        if (v >= 0xF80000L && v < 0x1180000L) {
                            Address va = toAddr(v);
                            try { long s1c = getInt(va.add(0x1C)) & 0xffffffffL, s20 = getInt(va.add(0x20)) & 0xffffffffL;
                                if (s1c >= 0x401000L && s1c < 0xF80000L && s20 >= 0x401000L && s20 < 0xF80000L)
                                    sb.append(String.format(" vt=%x(+1C=%x +20=%x)", v, s1c, s20));
                            } catch (Exception e) {}
                            Data d = L.getDataAt(va); if (d != null && d.hasStringValue()) sb.append(" str=\"").append(d.getValue()).append('"');
                        } }
                    for (Reference ref : ins.getReferencesFrom()) { Data d = L.getDataAt(ref.getToAddress()); if (d != null && d.hasStringValue()) sb.append(" str=\"").append(d.getValue()).append('"'); }
                }
                out.println(String.format("%x %s%s", f.getEntryPoint().getOffset(), f.getName(), sb));
            }
        }
    }
}
