// Args: <icCsv name,address> <outTsv> <depth>  per internal call: BFS over direct calls to <depth>, reporting
// thread checks, posts, locks, managed callbacks, managed allocation, device calls, writable globals read/written
// (to depth 3), heap stores (depth <= 1), virtual call count (not followed; vcAll/tr = computed calls and undisassembled body tails anywhere in the closure: a row with either > 0 is never SAFE) and named callees. Used by docs/data/ic-safety-*.md.
// Calls into the Mono/script-host block (0xD70000-0xEC0000) are recorded but not expanded.
// @category Sims3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class IcSafetyDepth4 extends GhidraScript {
    final Map<Long, String> flag = new HashMap<>();
    void f(long a, String t) { flag.put(a, t); }

    public void run() throws Exception {
        String[] a = getScriptArgs();
        int depth = Integer.parseInt(a[2]);
        f(0x7D99F0L, "thr:GetSimThreadId"); f(0x7D99D0L, "thr:GetMainThreadId"); f(0x60E820L, "thr:Device_IsOwnerThread");
        f(0x7D98C0L, "post:Dispatch7D98C0"); f(0x692F70L, "post:PostRenderMessage"); f(0x593E60L, "post:post593E60");
        f(0x50A780L, "post:UTFWin_QueuePendingTexture"); f(0x686E50L, "post:PhysicsSimMsg_ctor");
        f(0x4E16F0L, "lock:Lock_Acquire"); f(0x82C6E0L, "lock:Route_PlanLocked"); f(0x82EDB0L, "lock:RoutingWorld_ObstacleQuery");
        f(0xEA1400L, "mgd:mono_runtime_invoke"); f(0xE5A600L, "mgd:interp_runtime_invoke"); f(0xE59FD0L, "mgd:interp_invoke_with_state");
        f(0xD75FA0L, "mgd:ScriptHost_InvokeDelegate"); f(0xD7EED0L, "mgd:InvokeDirect_A"); f(0xD7F210L, "mgd:InvokeDirect_B");
        f(0xD7F000L, "mgd:ScriptHost_RunTask"); f(0xEA1290L, "mgd:mono_field_set_value"); f(0x78F2B0L, "evt:World_FireEvent");
        f(0xEA30F0L, "alloc:mono_array_new"); f(0xEA36F0L, "alloc:mono_object_new"); f(0xEA40B0L, "alloc:mono_string_new");
        f(0xE4BE30L, "alloc:GC_malloc"); f(0xE4A2B0L, "alloc:GC_allocobj");
        // [inferred] from the host slot +0x138 body: invoke after method lookup, string/array helpers
        f(0xEA1090L, "mgd:invoke_EA1090"); f(0xEA3FE0L, "alloc:string_EA3FE0"); f(0xEA3140L, "alloc:EA3140"); f(0xEA4160L, "alloc:EA4160");
        long[] dat = {0x11D8A8CL, 0x11D8A88L, 0x11CE2CCL};
        String[] dtag = {"thr:simTid", "thr:mainTid", "post:jobmgr"};
        List<String[]> ics = new ArrayList<>();
        for (String l : Files.readAllLines(Paths.get(a[0]))) { String[] p = l.split(","); if (p.length >= 2 && p[1].startsWith("0x")) ics.add(p); }
        try (PrintWriter out = new PrintWriter(a[1], "UTF-8")) {
            for (String[] ic : ics) {
                Address ea = toAddr(Long.decode(ic[1]));
                Function r = getFunctionAt(ea);
                if (r == null) r = getFunctionContaining(ea);
                if (r == null) { out.println(ic[0] + "\t" + ic[1] + "\tNOFUNC"); continue; }
                Map<Function, Integer> dist = new HashMap<>(); Map<Function, Function> par = new HashMap<>();
                ArrayDeque<Function> q = new ArrayDeque<>(); dist.put(r, 0); q.add(r);
                Map<String, String> hit = new TreeMap<>();
                Map<String, String> glob = new TreeMap<>();
                Set<String> named = new LinkedHashSet<>();
                int gw = 0, heap = 0, vcalls = 0, vcAll = 0, trunc = 0;
                while (!q.isEmpty() && dist.size() < 1500) {
                    Function fn = q.poll(); int d = dist.get(fn);
                    Function th = fn.isThunk() && fn.getThunkedFunction(true) != null ? fn.getThunkedFunction(true) : fn;
                    String nm = th.getName(); long off = fn.getEntryPoint().getOffset();
                    String fl = flag.get(off);
                    if (fl == null) {
                        if (nm.contains("CriticalSection") || nm.startsWith("WaitFor") || nm.startsWith("Interlocked") || nm.contains("Mutex") || nm.startsWith("SetEvent")) fl = "lock:" + nm;
                        else if (nm.startsWith("mono_string_new") || nm.startsWith("mono_array_new") || nm.startsWith("GC_malloc")) fl = "alloc:" + nm;
                        else if (nm.startsWith("Texture_") || nm.startsWith("Device_") || nm.startsWith("IndexBuffer_") || nm.startsWith("VertexBuffer_")) fl = "dev:" + nm;
                        else if (nm.startsWith("ObjectService_Create") || nm.startsWith("ObjectService_Clone") || nm.contains("DestroyById") || nm.startsWith("RoutingWorld_Rebuild") || nm.startsWith("RoutingWorld_Remove") || nm.startsWith("RoutingWorld_ApplyPending")) fl = "mut:" + nm;
                    }
                    if (fl != null && !hit.containsKey(fl)) hit.put(fl, d + ":" + path(fn, par));
                    String nm2 = fn.getName().startsWith("FUN_") ? nm : fn.getName();
                    if (d > 0 && d <= 3 && !nm2.startsWith("FUN_") && named.size() < 24) named.add(nm2 + "@" + d);
                    // GC and interpreter internals are not expanded; host and mono API functions are
                    boolean runtime = off >= 0xE40000L && off < 0xE67000L;
                    if (fn.isThunk() && th != fn && !th.isExternal() && !dist.containsKey(th)) { dist.put(th, d); par.put(th, fn); q.add(th); }
                    if (fn.isExternal() || fn.isThunk() || (runtime && d > 0)) continue;
                    List<Function> extra = new ArrayList<>();
                    int hostCd = 0; long vbase = 0x108B690L; Map<String, Long> loadDisp = new HashMap<>();
                    InstructionIterator ii = currentProgram.getListing().getInstructions(fn.getBody(), true);
                    while (ii.hasNext()) {
                        Instruction ins = ii.next();
                        String mn = ins.getMnemonicString();
                        if (hostCd > 0) hostCd--;
                        // unresolved code anywhere in the closure: computed call/jump, or a body that falls into undisassembled bytes (SACS_SetParamImpl)
                        if (ins.getFlowType().isComputed() && (mn.equals("CALL") || ins.getFlowType().isJump())) vcAll++;
                        Address fall = ins.getFallThrough();
                        if (fall != null && !fn.getBody().contains(fall) && getFunctionAt(fall) == null) trunc++;
                        if (mn.equals("CALL") && !ins.getFlowType().isComputed()) {
                            Address[] fl2 = ins.getFlows();
                            if (fl2.length > 0 && fl2[0].getOffset() == 0x7692B0L) { hostCd = 16; vbase = 0x108B690L; }
                        }
                        if (mn.equals("MOV") && ins.getNumOperands() == 2 && (ins.getOperandType(1) & ghidra.program.model.lang.OperandType.DYNAMIC) != 0) {
                            Object[] d0 = ins.getOpObjects(0); long ld = 0;
                            for (Object o : ins.getOpObjects(1)) if (o instanceof Scalar) ld = ((Scalar) o).getUnsignedValue();
                            if (d0.length == 1 && d0[0] instanceof Register) loadDisp.put(((Register) d0[0]).getName(), ld);
                        }
                        if (mn.equals("CALL") && ins.getFlowType().isComputed()) {
                            if (d <= 1) vcalls++;
                            if (hostCd > 0) {
                                // ScriptHost_GetWorld returns MonoScriptHost+8, whose vtable is 0x108B690 (MonoScriptHost_ctor)
                                Long dsp = null; Object[] c0 = ins.getOpObjects(0);
                                for (Object o : c0) if (o instanceof Scalar) dsp = ((Scalar) o).getUnsignedValue();
                                if (dsp == null && c0.length == 1 && c0[0] instanceof Register) dsp = loadDisp.get(((Register) c0[0]).getName());
                                if (dsp != null) {
                                    long disp = dsp;
                                    long tgt = 0;
                                    if (disp < (vbase == 0x108B690L ? 0x188 : 0x50)) {
                                        tgt = getInt(toAddr(vbase + disp)) & 0xFFFFFFFFL;
                                        Function hf = getFunctionAt(toAddr(tgt));
                                        if (hf == null) try { hf = createFunction(toAddr(tgt), null); } catch (Exception e) { hf = null; }
                                        String hk = (vbase == 0x108B690L ? "host:+0x" : "host0:+0x") + Long.toHexString(disp) + "=" + (hf != null ? hf.getName() : Long.toHexString(tgt));
                                        if (!hit.containsKey(hk)) hit.put(hk, d + ":" + path(fn, par));
                                        if (hf != null) extra.add(hf);
                                    }
                                    // +0x17C (0xD7CFD0) returns the primary MonoScriptHost (vtable 0x108B818); follow its next vcall
                                    if (tgt == 0xD7CFD0L && vbase == 0x108B690L) { vbase = 0x108B818L; hostCd = 17; }
                                }
                                if (hostCd != 17) hostCd = 0; else hostCd = 16;
                            }
                        }
                        for (Reference ref : ins.getReferencesFrom()) {
                            Address t = ref.getToAddress(); long to = t.getOffset();
                            if (to == 0x11D4BBCL && ref.getReferenceType().isRead() || to == 0x7692B0L && ref.getReferenceType().isCall()) { hostCd = 16; vbase = 0x108B690L; }
                            for (int k = 0; k < dat.length; k++) if (to == dat[k] && !hit.containsKey(dtag[k])) hit.put(dtag[k], d + ":" + path(fn, par));
                            if (!t.isMemoryAddress() || ref.getReferenceType().isCall() || ref.getReferenceType().isFlow()) continue;
                            MemoryBlock b = getMemoryBlock(t);
                            if (b == null || !b.isWrite() || b.isExecute()) continue;
                            boolean w = ref.getReferenceType().isWrite();
                            if (w) gw++;
                            if (d <= 3) {
                                Symbol s = getSymbolAt(t);
                                String key = (s != null && !s.getName().startsWith("DAT_") ? s.getName() + "=" : "") + Long.toHexString(to);
                                String old = glob.getOrDefault(key, "");
                                String m = w ? "W" : (ref.getReferenceType().isRead() ? "R" : "A");
                                if (!old.contains(m)) glob.put(key, old + m);
                            }
                        }
                        if (d <= 1 && ins.getNumOperands() >= 1 && (ins.getOperandType(0) & ghidra.program.model.lang.OperandType.DYNAMIC) != 0
                                && !mn.equals("CALL") && !mn.equals("JMP") && !mn.equals("PUSH") && !mn.equals("CMP") && !mn.equals("TEST")
                                && !mn.startsWith("FLD") && !mn.startsWith("FI") && !mn.startsWith("FCOM") && !mn.startsWith("FMUL") && !mn.startsWith("FADD") && !mn.startsWith("FSUB") && !mn.startsWith("FDIV") && !mn.equals("LEA")) {
                            boolean stack = false, hasReg = false;
                            for (Object o : ins.getOpObjects(0)) if (o instanceof Register) { hasReg = true; String rn = ((Register) o).getName(); if (rn.equals("ESP") || rn.equals("EBP")) stack = true; }
                            if (hasReg && !stack) heap++;
                        }
                    }
                    if (d >= depth || runtime || (fl != null && !fl.startsWith("evt:") && !fl.startsWith("mut:") && !fl.startsWith("thr:"))) continue;
                    if (nm.startsWith("GeneralAllocator_") || nm.startsWith("Lock_")) continue;
                    List<Function> cs = new ArrayList<>(fn.getCalledFunctions(monitor)); cs.addAll(extra);
                    for (Function c : cs) if (!dist.containsKey(c)) { dist.put(c, d + 1); par.put(c, fn); q.add(c); }
                }
                StringBuilder g = new StringBuilder(); int gn = 0;
                for (Map.Entry<String, String> e : glob.entrySet()) { if (gn++ < 32) g.append(e.getKey()).append(':').append(e.getValue()).append(' '); }
                out.println(ic[0] + "\t" + ic[1] + "\t" + r.getName() + "\tsz=" + r.getBody().getNumAddresses() + "\tn=" + dist.size()
                    + "\tvc=" + vcalls + "\tvcAll=" + vcAll + "\ttr=" + trunc + "\theap=" + heap + "\tgw=" + gw + "\tglobN=" + glob.size() + "\t" + hit + "\tglob=" + g + "\tnamed=" + String.join(" ", named) + (dist.size() <= 20 ? "\tall=" + dist.keySet().stream().map(x -> x.getName()).sorted().reduce("", (p, s) -> p + " " + s) : ""));
            }
        }
    }
    String path(Function f, Map<Function, Function> par) {
        StringBuilder s = new StringBuilder(); int n = 0;
        for (Function x = f; x != null && n < 8; x = par.get(x), n++) s.insert(0, (x.getName().startsWith("FUN_") ? x.getName().substring(4).replaceFirst("^00", "") : x.getName()) + (n > 0 ? ">" : ""));
        return s.toString();
    }
}
