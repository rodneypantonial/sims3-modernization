// AddRef/Release error-path scan (docs/193 §8 item 4). Per function: AddRef = indirect call through vtable slot 0 with
// only `this`, Release = slot +4 with only `this` (EA refcount interface convention, docs/39/40/47). Forward may-dataflow
// over the decompiler's basic blocks: an AddRef'd variable stays "owned" until Released, stored, copied into a stack
// struct, passed to another call, or returned; the null edge of `if (p)` owns nothing. A return reached with an owned variable is a leak path. Rows where the same function has both a
// clean return and a leaking return for that AddRef site are marked "asym" (error-path signature); the rest are
// usually ownership hand-offs the heuristic cannot see. Static heuristic: every row needs a read before it counts.
// Prefilter: only functions with an indirect call through a register are decompiled.
// Args: <outFile> [lo hi]   (hex range, default whole program)
// Headless: -process TS3W.exe -noanalysis -readOnly -scriptPath tools/ghidra -postScript RefPathScan.java out.tsv
// @category Sims3
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.block.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.pcode.*;
import java.io.PrintWriter;
import java.util.*;

public class RefPathScan extends GhidraScript {
    // slot of a CALLIND target: 0 or 4 when target = LOAD(LOAD(x) + slot) and the call's only arg is x; else -1
    static int slot(PcodeOp op) {
        // indirect calls with an unknown prototype carry no `this` input: the object comes from the vtable load chain
        if (op.getNumInputs() > 2) return -1;
        Varnode t = strip(op.getInput(0)), self = op.getNumInputs() == 2 ? op.getInput(1) : null;
        PcodeOp ld = t.getDef();
        if (ld == null || ld.getOpcode() != PcodeOp.LOAD) return -1;
        Varnode addr = strip(ld.getInput(1));
        int s = 0;
        PcodeOp ad = addr.getDef();
        if (ad != null && (ad.getOpcode() == PcodeOp.INT_ADD || ad.getOpcode() == PcodeOp.PTRSUB || ad.getOpcode() == PcodeOp.PTRADD)
                && ad.getInput(1).isConstant()) {
            long c = ad.getInput(1).getOffset();
            if (ad.getOpcode() == PcodeOp.PTRADD) c *= ad.getInput(2).getOffset();
            s = (int) c; addr = strip(ad.getInput(0));
        }
        if (s != 0 && s != 4) return -1;
        PcodeOp vt = addr.getDef();
        if (vt == null || vt.getOpcode() != PcodeOp.LOAD) return -1;
        Varnode obj = strip(vt.getInput(1));
        if (self != null && !same(obj, strip(self))) return -1;
        OBJ.put(op, obj);
        return s;
    }
    static final Map<PcodeOp, Varnode> OBJ = new HashMap<>();
    static Varnode strip(Varnode v) {
        PcodeOp d = v.getDef();
        while (d != null && (d.getOpcode() == PcodeOp.CAST || d.getOpcode() == PcodeOp.COPY)) { v = d.getInput(0); d = v.getDef(); }
        return v;
    }
    static boolean same(Varnode a, Varnode b) {
        if (a == b) return true;
        HighVariable ha = a.getHigh(), hb = b.getHigh();
        if (ha != null && ha == hb) return true;
        PcodeOp d = a.getDef();
        while (d != null && (d.getOpcode() == PcodeOp.CAST || d.getOpcode() == PcodeOp.COPY)) { a = d.getInput(0); if (a == b) return true; d = a.getDef(); }
        return false;
    }
    // identity of an object pointer: a field re-read (`this->p`) keys the same at every read, so a Release through a
    // fresh load still matches the AddRef
    static String key(Varnode v) { return v == null ? null : key(v, 0); }
    static String key(Varnode v, int depth) {
        v = strip(v);
        PcodeOp d = v.getDef();
        if (depth < 4 && d != null) {
            int oc = d.getOpcode();
            if (oc == PcodeOp.LOAD) return "*(" + key(d.getInput(1), depth + 1) + ")";
            if ((oc == PcodeOp.INT_ADD || oc == PcodeOp.PTRSUB) && d.getInput(1).isConstant())
                return key(d.getInput(0), depth + 1) + "+" + d.getInput(1).getOffset();
        }
        if (v.isConstant()) return "#" + v.getOffset();
        if (v.isAddress()) return "@" + v.getAddress();
        HighVariable h = v.getHigh();
        return h != null ? "H" + System.identityHashCode(h) : "V" + v;
    }

    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        long lo = a.length > 2 ? Long.decode(a[1]) : 0, hi = a.length > 2 ? Long.decode(a[2]) : Long.MAX_VALUE;
        DecompInterface ifc = new DecompInterface();
        ifc.toggleCCode(false);
        ifc.openProgram(currentProgram);
        int scanned = 0, decomp = 0, rows = 0;
        try (PrintWriter out = new PrintWriter(a[0], "UTF-8")) {
            out.println("function\tname\taddref_site\tleak_return\tclean_returns\tleak_returns\tkind\tblocks");
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (monitor.isCancelled()) break;
                long ep = f.getEntryPoint().getOffset();
                if (ep < lo || ep >= hi || f.isThunk() || f.isExternal()) continue;
                scanned++;
                boolean s0 = false, s4 = false;
                for (Instruction in : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                    if (!in.getMnemonicString().equals("CALL")) continue;
                    String o = in.toString().toUpperCase().replace(" ", "");
                    // MSVC: `mov r,[vt]; call r` or `call [vt+n]`; slots are checked in p-code below
                    if (o.matches("CALLE(AX|BX|CX|DX|SI|DI|BP)|CALLDWORDPTR\\[E(AX|BX|CX|DX|SI|DI|BP)\\]")) s0 = true;
                    if (o.matches("CALLE(AX|BX|CX|DX|SI|DI|BP)|CALLDWORDPTR\\[E(AX|BX|CX|DX|SI|DI|BP)\\+0X4\\]")) s4 = true;
                }
                if (!(s0 && s4)) continue;
                DecompileResults r = ifc.decompileFunction(f, 30, monitor);
                if (r == null || !r.decompileCompleted()) continue;
                decomp++; OBJ.clear();
                HighFunction h = r.getHighFunction();
                ArrayList<PcodeBlockBasic> bl = h.getBasicBlocks();
                // per block: ordered events
                Map<PcodeBlockBasic, Set<PcodeOp>> in = new HashMap<>(), outS = new HashMap<>();
                Map<PcodeOp, String> addrefVar = new HashMap<>();
                for (PcodeBlockBasic b : bl) {
                    Iterator<PcodeOp> it = b.getIterator();
                    while (it.hasNext()) {
                        PcodeOp op = it.next();
                        if (op.getOpcode() == PcodeOp.CALLIND && slot(op) == 0) {
                            String k = key(OBJ.get(op));
                            if (k != null) addrefVar.put(op, k);
                        }
                    }
                }
                if (addrefVar.isEmpty()) continue;
                for (PcodeBlockBasic b : bl) { in.put(b, new HashSet<>()); outS.put(b, new HashSet<>()); }
                boolean changed = true; int iter = 0;
                while (changed && iter++ < 50) {
                    changed = false;
                    for (PcodeBlockBasic b : bl) {
                        Set<PcodeOp> s = new HashSet<>();
                        for (int i = 0; i < b.getInSize(); i++) {
                            PcodeBlockBasic p = (PcodeBlockBasic) b.getIn(i);
                            Set<PcodeOp> e = new HashSet<>(outS.get(p));
                            kill(e, addrefVar, nullKey(p, b)); // `if (p) p->Release()`: the null edge owns nothing
                            s.addAll(e);
                        }
                        in.put(b, s);
                        Set<PcodeOp> o = transfer(b, new HashSet<>(s), addrefVar, null);
                        if (!o.equals(outS.get(b))) { outS.put(b, o); changed = true; }
                    }
                }
                Map<PcodeOp, List<String>> leaks = new LinkedHashMap<>();
                Map<PcodeOp, Integer> clean = new HashMap<>();
                int returns = 0;
                for (PcodeBlockBasic b : bl) {
                    List<PcodeOp[]> hits = new ArrayList<>();
                    Set<PcodeOp> live = transfer(b, new HashSet<>(in.get(b)), addrefVar, hits);
                    for (PcodeOp[] hp : hits) {
                        returns++;
                        for (PcodeOp ar : addrefVar.keySet()) {
                            if (hp[1] == ar) leaks.computeIfAbsent(ar, k -> new ArrayList<>()).add(hp[0].getSeqnum().getTarget().toString());
                        }
                    }
                }
                // clean returns per addref = returns reached by any path from the addref without it live
                for (PcodeOp ar : leaks.keySet()) {
                    List<String> lr = leaks.get(ar);
                    Set<String> uniq = new TreeSet<>(lr);
                    int cleanN = countCleanReturns(bl, ar, addrefVar, in);
                    String kind = cleanN > 0 ? "asym" : escapes(bl, addrefVar.get(ar)) ? "handoff" : "noRelease";
                    out.printf("0x%x\t%s\t%s\t%s\t%d\t%d\t%s\t%d%n", ep, f.getName(), ar.getSeqnum().getTarget(),
                            String.join(",", uniq), cleanN, uniq.size(), kind, bl.size());
                    rows++;
                }
                if (decomp % 500 == 0) println("progress decompiled=" + decomp + " rows=" + rows + " at 0x" + Long.toHexString(ep));
            }
        }
        println("scanned " + scanned + " decompiled " + decomp + " rows " + rows);
    }

    // returns where `ar` is not live but ar's block reaches them: approximate by "return blocks reachable from ar's block
    // whose IN set lacks ar and that are not the same leak return"
    int countCleanReturns(ArrayList<PcodeBlockBasic> bl, PcodeOp ar, Map<PcodeOp, String> addrefVar, Map<PcodeBlockBasic, Set<PcodeOp>> in) {
        PcodeBlockBasic start = null;
        for (PcodeBlockBasic b : bl) if (b.contains(ar.getSeqnum().getTarget())) { start = b; break; }
        if (start == null) return 0;
        Set<PcodeBlockBasic> seen = new HashSet<>(); Deque<PcodeBlockBasic> q = new ArrayDeque<>(); q.add(start); seen.add(start);
        int n = 0;
        while (!q.isEmpty()) {
            PcodeBlockBasic b = q.poll();
            List<PcodeOp[]> hits = new ArrayList<>();
            Set<PcodeOp> init = new HashSet<>(in.get(b));
            if (b == start) init.remove(ar);
            boolean hasRet = false; boolean leak = false;
            Iterator<PcodeOp> it = b.getIterator();
            while (it.hasNext()) if (it.next().getOpcode() == PcodeOp.RETURN) hasRet = true;
            if (hasRet) {
                transfer(b, init, addrefVar, hits);
                for (PcodeOp[] hp : hits) if (hp[1] == ar) leak = true;
                if (!leak) n++;
            }
            for (int i = 0; i < b.getOutSize(); i++) { PcodeBlockBasic s = (PcodeBlockBasic) b.getOut(i); if (seen.add(s)) q.add(s); }
        }
        return n;
    }

    // walk ops; live = set of AddRef ops whose object is still owned. hits (if not null) gets {returnOp, addrefOp}.
    Set<PcodeOp> transfer(PcodeBlockBasic b, Set<PcodeOp> live, Map<PcodeOp, String> addrefVar, List<PcodeOp[]> hits) {
        Iterator<PcodeOp> it = b.getIterator();
        while (it.hasNext()) {
            PcodeOp op = it.next();
            int oc = op.getOpcode();
            if (oc == PcodeOp.CALLIND || oc == PcodeOp.CALL) {
                int s = oc == PcodeOp.CALLIND ? slot(op) : -1;
                if (s == 0 && addrefVar.containsKey(op)) { live.add(op); continue; }
                int first = 1;
                if (s == 4) { kill(live, addrefVar, key(OBJ.get(op))); continue; }
                for (int i = first; i < op.getNumInputs(); i++) kill(live, addrefVar, key(op.getInput(i)));
            } else if (oc == PcodeOp.STORE) {
                kill(live, addrefVar, key(op.getInput(2)));
            } else if (oc == PcodeOp.COPY && op.getOutput() != null && op.getOutput().getAddress().isStackAddress()) {
                kill(live, addrefVar, key(op.getInput(0))); // copied into a stack struct that is handed on
            } else if (oc == PcodeOp.RETURN) {
                for (int i = 1; i < op.getNumInputs(); i++) kill(live, addrefVar, key(op.getInput(i)));
                if (hits != null) for (PcodeOp ar : live) hits.add(new PcodeOp[]{op, ar});
            }
        }
        return live;
    }
    // key of X when p ends in `if (X == 0)` / `if (X != 0)` and b is the X == 0 successor, else null
    static String nullKey(PcodeBlockBasic p, PcodeBlockBasic b) {
        PcodeOp last = p.getLastOp();
        if (last == null || last.getOpcode() != PcodeOp.CBRANCH || p.getOutSize() != 2) return null;
        Varnode c = strip(last.getInput(1));
        boolean neg = false;
        PcodeOp d = c.getDef();
        while (d != null && d.getOpcode() == PcodeOp.BOOL_NEGATE) { neg = !neg; c = strip(d.getInput(0)); d = c.getDef(); }
        if (d == null || (d.getOpcode() != PcodeOp.INT_EQUAL && d.getOpcode() != PcodeOp.INT_NOTEQUAL)) return null;
        Varnode x;
        if (d.getInput(1).isConstant() && d.getInput(1).getOffset() == 0) x = d.getInput(0);
        else if (d.getInput(0).isConstant() && d.getInput(0).getOffset() == 0) x = d.getInput(1);
        else return null;
        boolean nullWhenTrue = (d.getOpcode() == PcodeOp.INT_EQUAL) != neg;
        PcodeBlock nullSucc = nullWhenTrue ? p.getTrueOut() : p.getFalseOut();
        return nullSucc == b ? key(x) : null;
    }
    // the key is stored, returned or passed to a call anywhere in the function (ownership hand-off somewhere)
    static boolean escapes(ArrayList<PcodeBlockBasic> bl, String k) {
        for (PcodeBlockBasic b : bl) {
            Iterator<PcodeOp> it = b.getIterator();
            while (it.hasNext()) {
                PcodeOp op = it.next();
                int oc = op.getOpcode();
                if (oc == PcodeOp.STORE && k.equals(key(op.getInput(2)))) return true;
                if (oc == PcodeOp.RETURN || oc == PcodeOp.CALL || (oc == PcodeOp.CALLIND && slot(op) < 0))
                    for (int i = 1; i < op.getNumInputs(); i++) if (k.equals(key(op.getInput(i)))) return true;
            }
        }
        return false;
    }
    static void kill(Set<PcodeOp> live, Map<PcodeOp, String> addrefVar, String k) {
        if (k == null) return;
        live.removeIf(ar -> k.equals(addrefVar.get(ar)));
    }
}
