// Lists every instruction that writes memory at [reg + <offset>] (any base register except ESP/EBP), with its
// function and the written value when it is an immediate. Finds the writers of one object field (docs/121 §8: the
// lot-grid dirty byte +0x1CC). Args: <outFile> <0xoffset> [0xlo 0xhi]  (optional address range of the code to scan)
// Headless: -process TS3W.exe -noanalysis -readOnly -scriptPath tools/ghidra -postScript OffsetWrites.java out.txt 0x1CC
// @category Sims3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import java.io.PrintWriter;

public class OffsetWrites extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        long off = Long.decode(a[1]);
        AddressSetView set = a.length > 3 ? new AddressSet(toAddr(Long.decode(a[2])), toAddr(Long.decode(a[3])))
                : currentProgram.getMemory().getExecuteSet();
        int n = 0;
        try (PrintWriter out = new PrintWriter(a[0], "UTF-8")) {
            for (Instruction ins : currentProgram.getListing().getInstructions(set, true)) {
                if (ins.getNumOperands() < 1) continue;
                String m = ins.getMnemonicString();
                if (m.startsWith("CMP") || m.startsWith("TEST") || m.startsWith("PUSH") || m.startsWith("LEA") || m.startsWith("CALL") || m.startsWith("J")) continue;
                Object[] o = ins.getOpObjects(0);
                boolean hasOff = false, stack = false, reg = false;
                for (Object x : o) {
                    if (x instanceof Scalar s && s.getUnsignedValue() == off) hasOff = true;
                    if (x instanceof Register r) { reg = true; String b = r.getBaseRegister().getName(); if (b.equals("ESP") || b.equals("EBP")) stack = true; }
                }
                if (!hasOff || stack || !reg || ins.getOperandRefType(0) == null || !ins.getOperandRefType(0).isWrite()) continue;
                Function f = getFunctionContaining(ins.getAddress());
                out.printf("%s\t%s\t%s%n", ins.getAddress(), f == null ? "?" : f.getName() + "@" + f.getEntryPoint(), ins);
                n++;
            }
        }
        println("writes at +" + a[1] + ": " + n);
    }
}
