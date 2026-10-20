// Store scan of a native closure (docs/121 §8): BFS over direct calls from each root to <depth>; per function prints the
// stores that are not to the stack (absolute = global, register-based = heap/object), LOCK-prefixed ops, indirect calls
// and calls to Lock_Acquire 0x4E16F0. Stack = the address depends on ESP, or on EBP in a function that sets EBP only
// from ESP. One TSV row per (root, function); "!0xaddr" entries are not roots and are not expanded when reached.
// Args: <outFile> <depth> <0xaddr|!0xaddr>...
// Headless: -process TS3W.exe -noanalysis -readOnly -scriptPath tools/ghidra -postScript StoreScan.java out.tsv 4 0x744DB0
// @category Sims3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.*;
import ghidra.program.model.pcode.*;
import java.io.PrintWriter;
import java.util.*;

public class StoreScan extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        int depth = Integer.parseInt(a[1]);
        Set<Function> noExpand = new HashSet<>();
        List<Function> roots = new ArrayList<>();
        for (int i = 2; i < a.length; i++) {
            boolean stop = a[i].startsWith("!");
            Address ad = toAddr(Long.decode(stop ? a[i].substring(1) : a[i]));
            Function f = getFunctionContaining(ad);
            if (f == null) { disassemble(ad); f = createFunction(ad, null); }
            if (f == null) { println("not found " + a[i]); continue; }
            if (stop) noExpand.add(f); else roots.add(f);
        }
        try (PrintWriter out = new PrintWriter(a[0], "UTF-8")) {
          out.println("root\tdepth\taddr\tname\tinstr\tglobalWrites\theapStores\tlockOps\tlockAcq\tindirectCalls\theapSample");
          for (Function root : roots) {
            Map<Function, Integer> seen = new LinkedHashMap<>();
            Deque<Function> q = new ArrayDeque<>();
            seen.put(root, 0); q.add(root);
            while (!q.isEmpty()) {
                Function f = q.poll();
                int d = seen.get(f);
                if (f.isThunk()) { Function t = f.getThunkedFunction(true); if (t != null && !seen.containsKey(t)) { seen.put(t, d); q.add(t); } continue; }
                out.print(root.getEntryPoint() + "	");
                scan(f, d, out);
                if (d < depth && !noExpand.contains(f))
                    for (Function c : f.getCalledFunctions(monitor)) if (!seen.containsKey(c)) { seen.put(c, d + 1); q.add(c); }
            }
          }
        }
        println("scanned " + roots.size() + " roots");
    }

    void scan(Function f, int d, PrintWriter out) {
        Listing l = currentProgram.getListing();
        boolean ebpFrameOnly = true;
        for (Instruction ins : l.getInstructions(f.getBody(), true)) {
            Register r0 = ins.getRegister(0);
            if (r0 != null && r0.getName().equals("EBP") && !ins.getMnemonicString().startsWith("PUSH")
                    && !ins.toString().equals("MOV EBP,ESP") && !ins.getMnemonicString().startsWith("POP")) ebpFrameOnly = false;
        }
        int n = 0, heap = 0, lock = 0, acq = 0, ind = 0;
        Set<String> globals = new TreeSet<>();
        List<String> sample = new ArrayList<>();
        for (Instruction ins : l.getInstructions(f.getBody(), true)) {
            n++;
            if (ins.toString().contains("LOCK")) lock++; // Ghidra prints the prefix as a mnemonic suffix (".LOCK")
            if (ins.getFlowType().isCall()) {
                if (ins.getFlowType().isComputed()) ind++;
                for (Address t : ins.getFlows()) if (t.getOffset() == 0x4E16F0L) acq++;
            }
            PcodeOp[] ops = ins.getPcode();
            Map<Varnode, PcodeOp> def = new HashMap<>();
            for (PcodeOp op : ops) {
                if (op.getOutput() != null) def.put(op.getOutput(), op);
                if (op.getOpcode() != PcodeOp.STORE) continue;
                Set<String> regs = new HashSet<>();
                Long cst = walk(op.getInput(1), def, regs, 0);
                if (regs.contains("ESP") || (regs.contains("EBP") && ebpFrameOnly)) continue;
                if (regs.isEmpty() && cst != null) globals.add(String.format("0x%X", cst));
                else { heap++; if (sample.size() < 6) sample.add(ins.toString()); }
            }
        }
        out.printf("%d\t%s\t%s\t%d\t%s\t%d\t%d\t%d\t%d\t%s%n", d, f.getEntryPoint(), f.getName(), n,
                String.join(" ", globals), heap, lock, acq, ind, String.join(" | ", sample));
    }

    // registers the address depends on (inside this instruction's pcode); returns the constant when there is none
    Long walk(Varnode v, Map<Varnode, PcodeOp> def, Set<String> regs, int k) {
        if (v.isConstant()) return v.getOffset();
        if (v.isRegister()) { Register r = currentProgram.getRegister(v.getAddress(), v.getSize()); regs.add(r == null ? "?" : r.getBaseRegister().getName()); return null; }
        PcodeOp p = def.get(v);
        if (p == null || k > 8) { regs.add("?"); return null; }
        Long sum = null; boolean allConst = true;
        for (Varnode in : p.getInputs()) { Long c = walk(in, def, regs, k + 1); if (c == null) allConst = false; else sum = c; }
        return allConst ? sum : null;
    }
}
