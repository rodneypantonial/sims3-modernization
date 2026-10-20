// Memory writes of a native closure as the decompiler sees them (docs/121 §8): BFS over direct calls from each root to
// <depth>, decompile, and print every STORE left in the high p-code (stack locals are variables there, so they drop
// out) with the origin of its pointer: pN = the function's parameter N, g:0xADDR = a global, ret:NAME = a call's return
// value, ? = other. One TSV row per (root, function, store). "!0xaddr" entries are not expanded when reached.
// Args: <outFile> <depth> <0xaddr|!0xaddr>...
// Headless: -process TS3W.exe -noanalysis -readOnly -scriptPath tools/ghidra -postScript DecompStores.java out.tsv 4 0x744DB0
// @category Sims3
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.pcode.*;
import java.io.PrintWriter;
import java.util.*;

public class DecompStores extends GhidraScript {
    DecompInterface ifc;
    final Map<Function, List<String>> cache = new HashMap<>();

    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        int depth = Integer.parseInt(a[1]);
        ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
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
            out.println("root\tdepth\tfunc\torigin\tstore");
            for (Function root : roots) {
                Map<Function, Integer> seen = new LinkedHashMap<>();
                Deque<Function> q = new ArrayDeque<>();
                seen.put(root, 0); q.add(root);
                while (!q.isEmpty()) {
                    Function f = q.poll();
                    int d = seen.get(f);
                    if (f.isThunk()) { Function t = f.getThunkedFunction(true); if (t != null && !seen.containsKey(t)) { seen.put(t, d); q.add(t); } continue; }
                    for (String s : stores(f)) out.printf("%s\t%d\t%s@%s\t%s%n", root.getEntryPoint(), d, f.getName(), f.getEntryPoint(), s);
                    if (d < depth && !noExpand.contains(f))
                        for (Function c : f.getCalledFunctions(monitor)) if (!seen.containsKey(c)) { seen.put(c, d + 1); q.add(c); }
                }
            }
        }
    }

    List<String> stores(Function f) {
        return cache.computeIfAbsent(f, k -> {
            List<String> r = new ArrayList<>();
            DecompileResults res = ifc.decompileFunction(k, 60, monitor);
            HighFunction h = res.getHighFunction();
            if (h == null) { r.add("?\tdecompile failed"); return r; }
            Iterator<PcodeOpAST> it = h.getPcodeOps();
            while (it.hasNext()) {
                PcodeOpAST op = it.next();
                if (op.getOpcode() != PcodeOp.STORE) continue;
                String o = origin(op.getInput(1), 0);
                if (o.equals("stack")) continue;
                r.add(o + "\t" + op.getSeqnum().getTarget() + " " + op);
            }
            return r;
        });
    }

    String origin(Varnode v, int n) {
        if (v == null || n > 12) return "?";
        if (v.isConstant()) return String.format("g:0x%X", v.getOffset());
        HighVariable hv = v.getHigh();
        if (hv instanceof HighParam p) return "p" + (p.getSlot() + 1);
        if (hv instanceof HighLocal && v.getDef() == null) return "stack";
        if (v.isAddress()) return String.format("g:0x%X", v.getOffset());
        PcodeOp d = v.getDef();
        if (d == null) return v.isRegister() ? "reg" : (v.isAddrTied() ? "stack" : "?");
        switch (d.getOpcode()) {
            case PcodeOp.CALL: { Function c = getFunctionAt(d.getInput(0).getAddress()); return "ret:" + (c == null ? d.getInput(0).getAddress() : c.getName()); }
            case PcodeOp.CALLIND: return "ret:indirect";
            case PcodeOp.LOAD: return "*" + origin(d.getInput(1), n + 1);
            case PcodeOp.PTRSUB: case PcodeOp.PTRADD: case PcodeOp.INT_ADD: case PcodeOp.INT_SUB: case PcodeOp.CAST: case PcodeOp.COPY: case PcodeOp.INT_ZEXT: case PcodeOp.MULTIEQUAL: case PcodeOp.INDIRECT: {
                Varnode b = d.getInput(0);
                if (b.isRegister() && b.getAddress().getOffset() == 0x10) return "stack"; // ESP
                if (b.isConstant() && d.getNumInputs() > 1) b = d.getInput(1);
                return origin(b, n + 1);
            }
            default: return "?";
        }
    }
}
