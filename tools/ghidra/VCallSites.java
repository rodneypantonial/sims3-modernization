// Lists indirect calls through [reg+disp] for the given displacements, with the preceding instructions,
// plus callers of named imports.
// Args: <outFile> <disp list hex, comma> <import names, comma>
// @category Sims3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.scalar.Scalar;
import java.io.PrintWriter;
import java.util.*;

public class VCallSites extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        Set<Long> disps = new HashSet<>();
        for (String s : a[1].split("[+]")) disps.add(Long.decode(s));
        Listing lst = currentProgram.getListing();
        try (PrintWriter out = new PrintWriter(a[0], "UTF-8")) {
            for (String imp : a[2].split("[+]")) {
                for (Symbol s : currentProgram.getSymbolTable().getSymbols(imp)) {
                    for (Reference r : getReferencesTo(s.getAddress())) {
                        Function f = getFunctionContaining(r.getFromAddress());
                        out.printf("IMP\t%s\t%s\t%s%n", imp, r.getFromAddress(), f == null ? "-" : f.getName() + "@" + f.getEntryPoint());
                    }
                }
            }
            for (Instruction ins : lst.getInstructions(true)) {
                if (!ins.getFlowType().isCall() || !ins.getFlowType().isComputed()) continue;
                if (ins.getNumOperands() != 1) continue;
                Object[] objs = ins.getOpObjects(0);
                long d = -1;
                if (objs.length == 2 && objs[0] instanceof ghidra.program.model.lang.Register && objs[1] instanceof Scalar && ins.toString().contains("[")) {
                    if (((ghidra.program.model.lang.Register) objs[0]).getName().equals("ESP")) continue;
                    d = ((Scalar) objs[1]).getUnsignedValue();
                } else if (objs.length == 1 && objs[0] instanceof ghidra.program.model.lang.Register) {
                    String rn = ((ghidra.program.model.lang.Register) objs[0]).getName();
                    Instruction q = ins.getPrevious();
                    for (int i = 0; i < 8 && q != null; i++, q = q.getPrevious()) {
                        String t = q.toString();
                        if (t.startsWith("MOV " + rn + ",dword ptr [") && !t.contains("ESP")) {
                            Object[] qo = q.getOpObjects(1);
                            if (qo.length == 2 && qo[1] instanceof Scalar) d = ((Scalar) qo[1]).getUnsignedValue();
                            else if (qo.length == 1 && qo[0] instanceof ghidra.program.model.lang.Register) d = 0;
                            break;
                        }
                    }
                }
                if (!disps.contains(d)) continue;
                StringBuilder prev = new StringBuilder();
                Instruction p = ins.getPrevious();
                List<String> pl = new ArrayList<>();
                for (int i = 0; i < 16 && p != null; i++, p = p.getPrevious()) pl.add(p.toString());
                Collections.reverse(pl);
                for (String s : pl) prev.append(s).append(" ; ");
                Function f = getFunctionContaining(ins.getAddress());
                out.printf("VC\t0x%X\t%s\t%s\t%s\t%s%n", d, ins.getAddress(), f == null ? "-" : f.getName() + "@" + f.getEntryPoint(), ins, prev);
            }
        }
    }
}
