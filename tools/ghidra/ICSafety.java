// Args: <icCsv name,address> <outTsv> <depth>  per IC body: direct-call closure (depth-limited) with thread/lock/post/managed/alloc/D3D flags and global reads/writes
// @category Sims3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class ICSafety extends GhidraScript {
    Map<Long, String> flagAt = new HashMap<>();
    Map<Long, String> dataFlag = new HashMap<>();

    void f(long a, String n) { flagAt.put(a, n); }
    static final Map<Long, String> HOSTFLAG = new HashMap<>();
    static {
        HOSTFLAG.put(0x118L, "task:CurrentTaskId"); HOSTFLAG.put(0x11CL, "yield:host+11C_MarkYield"); HOSTFLAG.put(0x120L, "yield:host+120_Wait");
        HOSTFLAG.put(0x124L, "post:host+124_SignalWaiter"); HOSTFLAG.put(0x134L, "managed:host+134_InvokeDelegate"); HOSTFLAG.put(0xC8L, "handle:create");
        HOSTFLAG.put(0xD0L, "handle:release"); HOSTFLAG.put(0xD4L, "handle:remove"); // +0x124 = 0xD77FB0 wakes the task waiting on a key: a post, not a yield (docs/62 §9)
    }

    String classify(Function fn) {
        long a = fn.getEntryPoint().getOffset();
        String fl = flagAt.get(a);
        if (fl != null) return fl;
        String nm = fn.isThunk() && fn.getThunkedFunction(true) != null ? fn.getThunkedFunction(true).getName() : fn.getName();
        if (nm.contains("EnterCriticalSection") || nm.equals("Lock_Acquire")) return "lock:" + nm;
        if (nm.equals("WaitForSingleObject") || nm.equals("WaitForMultipleObjects") || nm.startsWith("SleepConditionVariable")) return "wait:" + nm;
        if (nm.startsWith("SetEvent") || nm.startsWith("PostMessage") || nm.startsWith("PostThreadMessage") || nm.startsWith("SendMessage") || nm.equals("ReleaseSemaphore")) return "post:" + nm;
        if (nm.startsWith("Interlocked")) return "atomic";
        if (nm.equals("GetCurrentThreadId")) return "thread:GetCurrentThreadId";
        if (nm.startsWith("CreateThread") || nm.startsWith("_beginthread")) return "post:CreateThread";
        if (nm.startsWith("CreateFile") || nm.startsWith("ReadFile") || nm.startsWith("WriteFile")) return "io:" + nm;
        if (nm.startsWith("Device_") || nm.startsWith("GraphicsDevice_") || nm.startsWith("IndexBuffer_") || nm.startsWith("VertexBuffer_")) return "d3d:" + nm;
        if (nm.startsWith("Texture_") && !nm.equals("Texture_ClampToCaps")) return "d3d:" + nm;
        if (nm.startsWith("ResourceMan_") || nm.startsWith("ResourceCache_")) return "res:" + nm;
        if (nm.startsWith("GeneralAllocator_")) return "nalloc";
        if (a >= 0xE40000L && a < 0xEB0000L) return "mono:" + nm;
        return null;
    }

    public void run() throws Exception {
        String[] a = getScriptArgs();
        int depth = Integer.parseInt(a[2]);
        // thread checks
        f(0x7d99f0L, "thread:GetSimThreadId"); f(0x7d99d0L, "thread:GetMainThreadId"); f(0x60e820L, "thread:Device_IsOwnerThread");
        dataFlag.put(0x11D8A88L, "thread:mainTid"); dataFlag.put(0x11D8A8CL, "thread:simTid"); dataFlag.put(0x11CE2CCL, "post:jobmgr");
        // hand-offs
        f(0x7d98c0L, "post:ThreadDispatch7D98C0"); f(0x59a350L, "post:JobSubmit"); f(0x59a500L, "wait:JobWait"); f(0x59a430L, "post:RunJobs");
        f(0x692f70L, "post:PostRenderMessage"); f(0x593e60L, "post:RenderMessageBuffer_Post"); f(0xc64950L, "post:RemoteCall_PostSync");
        f(0xc649a0L, "post:RemoteCall_PostAsync"); f(0xc64b90L, "post:RemoteCall_PostSyncRelease"); f(0xabe970L, "post:Lot_PostRemoteCallSync");
        f(0xabe9c0L, "post:Lot_PostRemoteCallAsync"); f(0x686e50L, "post:PhysicsSimMsg"); f(0x4e2810L, "post:EASemaphore_Post");
        f(0x50a780L, "post:UTFWin_QueuePendingTexture"); f(0x4e16f0L, "lock:Lock_Acquire");
        // managed re-entry, yield
        f(0xea1400L, "managed:mono_runtime_invoke"); f(0xd75fa0L, "managed:ScriptHost_InvokeDelegate"); f(0xd7eed0L, "managed:InvokeDirect_A");
        f(0xd7f210L, "managed:InvokeDirect_B"); f(0xe5a600L, "managed:interp_runtime_invoke"); f(0xe59fd0L, "managed:interp_invoke_with_state");
        f(0xd8a8e0L, "managed:NativeToManaged"); f(0x78f2b0L, "managed:World_FireEvent"); f(0xd70820L, "yield:ScriptHost_Sleep");
        f(0xd70f60L, "yield:ScriptHost_MarkYield"); f(0x76cb00L, "yield:Simulator_Sleep"); f(0xd7f000L, "managed:ScriptHost_RunTask");
        // managed allocation
        f(0xea40b0L, "alloc:mono_string_new"); f(0xea30f0L, "alloc:mono_array_new"); f(0xea36f0L, "alloc:mono_object_new");
        f(0xea2690L, "alloc:obj_alloc_EA2690"); f(0xea2710L, "alloc:obj_alloc_EA2710"); f(0xea3140L, "alloc:obj_alloc_EA3140");
        f(0xe4a2b0L, "alloc:GC_allocobj"); f(0xe4ba90L, "alloc:GC_alloc_large");
        // device
        f(0x618570L, "d3d:Texture_FillLevels"); f(0x619c80L, "d3d:Texture_Create2D"); f(0x619540L, "d3d:Texture_CreateD3D");
        f(0x60cea0L, "d3d:Texture_CreateByShape"); f(0x619910L, "d3d:Texture_CreateCube"); f(0xd5d910L, "d3d:ThumbnailManager_CreateThumbnailTexture");
        f(0x82c6e0L, "lock:Route_PlanLocked");

        Memory mem = currentProgram.getMemory();
        Map<Long, List<String>> byAddr = new TreeMap<>();
        for (String line : Files.readAllLines(Paths.get(a[0]))) {
            String[] p = line.trim().split(",");
            if (p.length < 2 || !p[1].startsWith("0x")) continue;
            byAddr.computeIfAbsent(Long.decode(p[1]), k -> new ArrayList<>()).add(p[0].replaceFirst("^ScriptCore\\.", ""));
        }
        try (PrintWriter out = new PrintWriter(a[1], "UTF-8")) {
            for (Map.Entry<Long, List<String>> e : byAddr.entrySet()) {
                Address ra = toAddr(e.getKey());
                Function r = getFunctionAt(ra);
                if (r == null) r = getFunctionContaining(ra);
                if (r == null) { out.println(Long.toHexString(e.getKey()) + "\t" + String.join(";", e.getValue()) + "\tNOFUNC"); continue; }
                Map<Function, Integer> dist = new HashMap<>(); Map<Function, Function> par = new HashMap<>();
                ArrayDeque<Function> q = new ArrayDeque<>(); dist.put(r, 0); q.add(r);
                Map<String, String> hit = new TreeMap<>();
                Map<Long, Integer> wr = new TreeMap<>(), rd = new TreeMap<>();
                Set<String> locks = new TreeSet<>(), hosts = new TreeSet<>();
                int indRoot = 0, indAll = 0, hw = 0, hwRoot = 0, trunc = 0, open = 0; boolean cap = false;
                while (!q.isEmpty()) {
                    if (dist.size() > 4000) { cap = true; break; }
                    Function fn = q.poll(); int d = dist.get(fn);
                    String fl = d == 0 ? null : classify(fn);
                    if (fl != null && !hit.containsKey(fl)) hit.put(fl, d + ":" + path(fn, par));
                    boolean leaf = fl != null && !fl.startsWith("thread:Get") && !fl.startsWith("atomic");
                    // a thunk (most IC entries are a 5-byte JMP to the body) was a dead end, so 87 bodies were never scanned: follow it at the same depth
                    Function th = fn.isThunk() ? fn.getThunkedFunction(true) : null;
                    if (th != null && !th.isExternal() && !leaf && !dist.containsKey(th)) { dist.put(th, d); par.put(th, fn); q.add(th); }
                    if (fn.isExternal() || fn.isThunk() || leaf) continue;
                    boolean usesHost = false, usesMsg = false;
                    for (Function c : fn.getCalledFunctions(monitor)) { long ca = c.getEntryPoint().getOffset(); if (ca == 0x7692b0L) usesHost = true; if (ca == 0x44c470L) usesMsg = true; }
                    InstructionIterator pi = currentProgram.getListing().getInstructions(fn.getBody(), true);
                    while (pi.hasNext()) for (Reference ref : pi.next().getReferencesFrom()) { long to = ref.getToAddress().getOffset();
                        if (to == 0x11D4BBCL || to == 0x11EE514L) usesHost = true; if (to == 0x11B1378L) usesMsg = true; }
                    List<Function> extra = new ArrayList<>();
                    InstructionIterator ii = currentProgram.getListing().getInstructions(fn.getBody(), true);
                    while (ii.hasNext()) {
                        Instruction ins = ii.next();
                        if (heapWrite(ins)) { hw++; if (d == 0) hwRoot++; }
                        // body ends by falling into bytes Ghidra did not disassemble (SACS_SetParamImpl: host +0xCC vcall hidden there): unknown code, never SAFE
                        Address fall = ins.getFallThrough();
                        if (fall != null && !fn.getBody().contains(fall)) { Function ff = getFunctionAt(fall); if (ff != null) extra.add(ff); else { trunc++; indAll++; if (d == 0) indRoot++; } }
                        if (ins.getFlowType().isJump()) {
                            if (ins.getFlowType().isComputed()) { indAll++; if (d == 0) indRoot++; } // tail vcall or switch
                            for (Address ft : ins.getFlows()) { Function tf = getFunctionAt(ft); if (tf != null && !tf.equals(fn)) extra.add(tf); }
                        }
                        if (ins.getFlowType().isCall() && ins.getFlowType().isComputed() && (usesHost || usesMsg)) {
                            long disp = dispOf(ins);
                            if (disp >= 0 && disp < 0x180 && disp % 4 == 0) {
                                if (usesHost) {
                                    hosts.add(Long.toHexString(disp));
                                    String hf = HOSTFLAG.get(disp);
                                    if (hf != null && !hit.containsKey(hf)) hit.put(hf, d + ":" + path(fn, par) + ">host+" + Long.toHexString(disp));
                                    Function t = getFunctionAt(toAddr(getInt(toAddr(0x108b690L + disp)) & 0xffffffffL));
                                    if (t != null && disp >= 0x48) extra.add(t); // low slots (Init +0x10, Simulate +0x20) are also other objects' vcalls: record only
                                }
                                if (usesMsg && disp >= 0x18 && disp <= 0x24) { String mf = "post:MsgServer+" + Long.toHexString(disp); if (!hit.containsKey(mf)) hit.put(mf, d + ":" + path(fn, par)); }
                            }
                        }
                        if (ins.getFlowType().isCall()) for (Address ft : ins.getFlows()) {
                            Function tf = getFunctionAt(ft);
                            if (tf == null) continue;
                            if (tf.isThunk() && tf.getThunkedFunction(true) != null) tf = tf.getThunkedFunction(true);
                            String cf = classify(tf);
                            if (cf != null && cf.startsWith("lock:")) locks.add(lockArg(ins) + "@" + shortName(fn));
                        }
                        if (ins.getFlowType().isCall() && ins.getFlowType().isComputed()) {
                            boolean ext = false;
                            for (Reference ref : ins.getReferencesFrom()) { Function t = getFunctionAt(ref.getToAddress()); if (t != null && t.isExternal()) ext = true;
                                Data dd = getDataAt(ref.getToAddress()); if (dd != null && dd.isPointer() && dd.getValue() instanceof Address) { Function t2 = getFunctionAt((Address) dd.getValue()); if (t2 != null) ext = true; } }
                            if (!ext) { indAll++; if (d == 0) indRoot++; }
                        }
                        for (Reference ref : ins.getReferencesFrom()) {
                            if (!ref.getReferenceType().isData()) continue;
                            Address t = ref.getToAddress(); long to = t.getOffset();
                            String df = dataFlag.get(to);
                            if (df != null && !hit.containsKey(df)) hit.put(df, d + ":" + path(fn, par));
                            MemoryBlock b = mem.getBlock(t);
                            if (b == null || !b.isWrite() || b.isExecute()) continue;
                            if (getFunctionAt(t) != null) continue;
                            Data dd = getDataAt(t);
                            if (dd != null && dd.isPointer() && dd.getValue() instanceof Address && getFunctionAt((Address) dd.getValue()) != null) continue; // import slot
                            if (ref.getReferenceType().isWrite()) wr.merge(to, 1, Integer::sum); else rd.merge(to, 1, Integer::sum);
                        }
                    }
                    if (d >= depth) { for (Function c : fn.getCalledFunctions(monitor)) if (!dist.containsKey(c) && classify(c) == null) open++; for (Function c : extra) if (!dist.containsKey(c)) open++; continue; } // closure cut by depth: not seen whole
                    List<Function> cs = new ArrayList<>(fn.getCalledFunctions(monitor)); cs.addAll(extra);
                    for (Function c : cs) if (!dist.containsKey(c)) { dist.put(c, d + 1); par.put(c, fn); q.add(c); }
                }
                StringBuilder dc = new StringBuilder();
                for (Function c : r.getCalledFunctions(monitor)) dc.append(shortName(c)).append(' ');
                out.println(Long.toHexString(e.getKey()) + "\t" + String.join(";", e.getValue()) + "\t" + r.getName() + "@" + Long.toHexString(r.getEntryPoint().getOffset())
                    + "\tsz=" + r.getBody().getNumAddresses() + "\tn=" + dist.size() + (cap ? "+" : "") + "\tind=" + indRoot + "/" + indAll + "\thw=" + hwRoot + "/" + hw + "\ttr=" + trunc + "\topen=" + open
                    + "\t" + hit + "\tW=" + globals(wr) + "\tR=" + globals(rd) + "\tL=" + locks + "\tH=" + hosts + "\tC=" + dc);
            }
        }
    }

    long dispOf(Instruction ins) {
        Object[] op = ins.getOpObjects(0);
        boolean mem = ins.getOperandRefType(0).isIndirect() || ins.getDefaultOperandRepresentation(0).contains("[");
        if (mem) { for (Object o : op) if (o instanceof ghidra.program.model.scalar.Scalar) return ((ghidra.program.model.scalar.Scalar) o).getUnsignedValue(); return op.length == 1 && op[0] instanceof ghidra.program.model.lang.Register ? 0 : -1; }
        if (op.length != 1 || !(op[0] instanceof ghidra.program.model.lang.Register)) return -1;
        ghidra.program.model.lang.Register reg = (ghidra.program.model.lang.Register) op[0];
        Instruction p = ins;
        for (int k = 0; k < 6; k++) {
            p = p.getPrevious(); if (p == null) return -1;
            if (!p.getMnemonicString().equals("MOV")) continue;
            Object[] d0 = p.getOpObjects(0);
            if (d0.length == 1 && reg.equals(d0[0])) {
                if (!p.getDefaultOperandRepresentation(1).contains("[")) return -1;
                for (Object o : p.getOpObjects(1)) if (o instanceof ghidra.program.model.scalar.Scalar) return ((ghidra.program.model.scalar.Scalar) o).getUnsignedValue();
                return 0;
            }
        }
        return -1;
    }

    static final java.util.Set<String> WM = new java.util.HashSet<>(java.util.Arrays.asList("MOV","MOVSS","MOVSD","MOVQ","MOVD","MOVUPS","MOVAPS","MOVDQU","MOVDQA","FSTP","FST","FISTP","ADD","SUB","OR","AND","XOR","INC","DEC","XCHG","CMPXCHG","XADD","MOVSB","MOVSD.REP","STOSD","STOSB","NEG","NOT","SHL","SHR","SAR","MOVZX"));
    boolean heapWrite(Instruction ins) {
        String m = ins.getMnemonicString().toUpperCase();
        if (m.startsWith("STOS") || m.startsWith("MOVS") && ins.getNumOperands() == 0) return true;
        if (!WM.contains(m) || ins.getNumOperands() < 1 || m.equals("MOVZX")) return false;
        String o = ins.getDefaultOperandRepresentation(0);
        if (!o.contains("[")) return false;
        return !(o.contains("ESP") || o.contains("EBP"));
    }

    String lockArg(Instruction ins) {
        Instruction p = ins;
        for (int k = 0; k < 8; k++) {
            p = p.getPrevious(); if (p == null) break;
            if (p.getFlowType().isCall()) break;
            for (int oi = 0; oi < p.getNumOperands(); oi++) {
                for (Reference r2 : p.getOperandReferences(oi)) { long v = r2.getToAddress().getOffset();
                    MemoryBlock b = currentProgram.getMemory().getBlock(r2.getToAddress());
                    if (b != null && !b.isExecute()) return gname(v); }
                for (Object o : p.getOpObjects(oi)) if (o instanceof ghidra.program.model.scalar.Scalar) {
                    long v = ((ghidra.program.model.scalar.Scalar) o).getUnsignedValue();
                    if (v < 0x400000L || v > 0x1400000L) continue;
                    MemoryBlock b = currentProgram.getMemory().getBlock(toAddr(v));
                    if (b != null && !b.isExecute()) return gname(v);
                }
            }
        }
        return "member";
    }
    String gname(long v) { Symbol sy = getSymbolAt(toAddr(v)); return "0x" + Long.toHexString(v) + (sy != null && sy.getSource() == SourceType.USER_DEFINED ? "=" + sy.getName() : ""); }

    String lockSite(Function callee, Function caller) {
        if (caller == null) return shortName(callee);
        for (Reference ref : getReferencesTo(callee.getEntryPoint())) {
            if (!ref.getReferenceType().isCall()) continue;
            Function c = getFunctionContaining(ref.getFromAddress());
            if (c == null || !c.equals(caller)) continue;
            Instruction ins = getInstructionAt(ref.getFromAddress());
            for (int k = 0; k < 8 && ins != null; k++) {
                ins = ins.getPrevious();
                if (ins == null) break;
                for (int oi = 0; oi < ins.getNumOperands(); oi++) for (Object o : ins.getOpObjects(oi)) if (o instanceof ghidra.program.model.scalar.Scalar) {
                    long v = ((ghidra.program.model.scalar.Scalar) o).getUnsignedValue();
                    if (v < 0x1000000L || v > 0x1300000L) continue;
                    MemoryBlock b = currentProgram.getMemory().getBlock(toAddr(v));
                    if (b != null && b.isWrite() && !b.isExecute()) { Symbol sy = getSymbolAt(toAddr(v)); return "0x" + Long.toHexString(v) + (sy != null && sy.getSource() == SourceType.USER_DEFINED ? "=" + sy.getName() : "") + "@" + shortName(caller); }
                }
                for (Reference r2 : ins.getReferencesFrom()) {
                    MemoryBlock b = currentProgram.getMemory().getBlock(r2.getToAddress());
                    if (r2.getReferenceType().isData() && b != null && b.isWrite() && !b.isExecute())
                        return "0x" + Long.toHexString(r2.getToAddress().getOffset()) + "@" + shortName(caller);
                }
            }
        }
        return "?@" + shortName(caller);
    }

    String globals(Map<Long, Integer> m) {
        StringBuilder s = new StringBuilder(); int n = 0;
        for (Map.Entry<Long, Integer> e : m.entrySet()) {
            if (n++ >= 12) { s.append("+").append(m.size() - 12); break; }
            Symbol sy = getSymbolAt(toAddr(e.getKey()));
            String nm = sy != null && sy.getSource() == SourceType.USER_DEFINED ? "=" + sy.getName() : "";
            s.append(Long.toHexString(e.getKey())).append(nm).append(' ');
        }
        return s.toString().trim();
    }

    String shortName(Function x) { String n = x.getName(); return n.startsWith("FUN_") ? n.substring(4).replaceFirst("^00", "") : n; }

    String path(Function f, Map<Function, Function> par) {
        StringBuilder s = new StringBuilder(); int n = 0;
        for (Function x = f; x != null && n < 8; x = par.get(x), n++) s.insert(0, shortName(x) + (n > 0 ? ">" : ""));
        return s.toString();
    }
}
