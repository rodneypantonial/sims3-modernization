// Prints every instruction that uses one of the given 32-bit constants (e.g. resource type ids), with its function.
// Args: <outFile> <0xvalue>...
// Headless: -process TS3W.exe -noanalysis -readOnly -scriptPath tools/ghidra -postScript ScalarRefs.java out.txt 0x319E4F1D
// @category Sims3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import java.io.PrintWriter;
import java.util.*;

public class ScalarRefs extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        Set<Long> want = new HashSet<>();
        for (int i = 1; i < args.length; i++) want.add(Long.decode(args[i]) & 0xffffffffL);
        int hits = 0;
        try (PrintWriter out = new PrintWriter(args[0], "UTF-8")) {
            for (Instruction ins : currentProgram.getListing().getInstructions(true)) {
                for (int op = 0; op < ins.getNumOperands(); op++)
                    for (Object o : ins.getOpObjects(op)) {
                        // constants and direct memory operands (globals such as [0x011ed034]) both match
                        long v = o instanceof Scalar s ? s.getUnsignedValue() : o instanceof ghidra.program.model.address.Address a ? a.getOffset() : -1;
                        if (v >= 0 && want.contains(v & 0xffffffffL)) {
                            Function f = getFunctionContaining(ins.getAddress());
                            out.printf("%s 0x%08x %s @ %s | %s%n", ins.getAddress(), v & 0xffffffffL,
                                    f == null ? "?" : f.getName(), f == null ? "-" : f.getEntryPoint(), ins);
                            hits++;
                        }
                    }
            }
        }
        println("scalar hits " + hits + " to " + args[0]);
    }
}
