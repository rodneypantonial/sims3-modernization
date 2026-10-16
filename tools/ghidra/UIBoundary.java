// Args: <outFile> <depth> <namePrefixRegex>  per matching function: size, direct callees, and for flag targets the min depth + path
// @category Sims3
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class UIBoundary extends GhidraScript {
    static final long[] DATA = {0x11D8A88L, 0x11D8A8CL, 0x11CE2CCL};
    static final String[] DNAME = {"mainTid", "simTid", "jobmgr"};
    public void run() throws Exception {
        String[] a = getScriptArgs();
        int depth = Integer.parseInt(a[1]);
        java.util.regex.Pattern pat = java.util.regex.Pattern.compile(a[2]);
        Map<String, String> flags = new LinkedHashMap<>();
        flags.put("0x7d99f0", "GetSimThreadId"); flags.put("0x692f70", "PostRenderMessage"); flags.put("0x593e60", "post593e60");
        flags.put("0x619770", "capsClamp619770"); flags.put("0x618570", "texFill618570");
        flags.put("0x619c80", "Texture_Create2D"); flags.put("0x619540", "Texture_CreateD3D"); flags.put("0x618df0", "Texture_LockRect");
        flags.put("0x60e820", "Device_IsOwnerThread"); flags.put("0x60ec10", "Device_UpdateTexture"); flags.put("0x60cea0", "Texture_CreateByShape");
        flags.put("0x619910", "Texture_CreateCube"); flags.put("0x60f740", "Device_GetD3DDevice");
        try (PrintWriter out = new PrintWriter(a[0], "UTF-8")) {
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            List<Function> roots = new ArrayList<>();
            while (it.hasNext()) { Function f = it.next(); if (pat.matcher(f.getName()).find()) roots.add(f); }
            for (Function r : roots) {
                Map<Function, Integer> dist = new HashMap<>(); Map<Function, Function> par = new HashMap<>();
                ArrayDeque<Function> q = new ArrayDeque<>(); dist.put(r, 0); q.add(r);
                Map<String, String> hit = new TreeMap<>();
                while (!q.isEmpty() && dist.size() < 3000) {
                    Function f = q.poll(); int d = dist.get(f);
                    String nm = f.isThunk() && f.getThunkedFunction(true) != null ? f.getThunkedFunction(true).getName() : f.getName();
                    String key = "0x" + Long.toHexString(f.getEntryPoint().getOffset());
                    String fl = flags.get(key);
                    if (fl == null && (nm.contains("CriticalSection") || nm.equals("WaitForSingleObject") || nm.startsWith("SetEvent") || nm.startsWith("Interlocked"))) fl = nm;
                    if (fl != null && !hit.containsKey(fl)) hit.put(fl, d + ":" + path(f, par));
                    if (!f.isExternal() && !f.isThunk()) {
                        InstructionIterator ii = currentProgram.getListing().getInstructions(f.getBody(), true);
                        while (ii.hasNext()) { Instruction ins = ii.next();
                            for (Reference ref : ins.getReferencesFrom()) { long t = ref.getToAddress().getOffset();
                                for (int k = 0; k < DATA.length; k++) if (t == DATA[k] && !hit.containsKey(DNAME[k])) hit.put(DNAME[k], d + ":" + path(f, par)); } }
                    }
                    if (d >= depth || fl != null && (fl.startsWith("Texture") || fl.startsWith("Device"))) continue;
                    for (Function c : f.getCalledFunctions(monitor)) if (!dist.containsKey(c)) { dist.put(c, d + 1); par.put(c, f); q.add(c); }
                }
                StringBuilder dc = new StringBuilder();
                for (Function c : r.getCalledFunctions(monitor)) dc.append(c.getName()).append('@').append(Long.toHexString(c.getEntryPoint().getOffset())).append(' ');
                out.println(r.getName() + "\t" + Long.toHexString(r.getEntryPoint().getOffset()) + "\tsz=" + r.getBody().getNumAddresses()
                    + "\tn=" + dist.size() + "\t" + hit + "\tcallees=" + dc);
            }
        }
    }
    String path(Function f, Map<Function, Function> par) {
        StringBuilder s = new StringBuilder(); int n = 0;
        for (Function x = f; x != null && n < 8; x = par.get(x), n++) s.insert(0, (x.getName().startsWith("FUN_") ? x.getName().substring(4).replaceFirst("^00", "") : x.getName()) + (n > 0 ? ">" : ""));
        return s.toString();
    }
}
