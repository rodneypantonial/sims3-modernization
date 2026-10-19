# Dummy test (probe-safety rule): 32-bit ping.exe, never the game. Why stutter-profile.js must not hook inside Simulate.
#   python tools/probe/frida-unload-test.py      (Windows, frida installed; prints RESULT PASS|FAIL)
# A dummy thread loops forever like Simulate: esi = a "host" block, call f (a real function entry), then
# `mov ecx, host+0xA64; call getter` (getter = `mov al, [ecx+0x10]; ret`, the 0x8A6D10 shape), Sleep(1), loop.
# Each case runs `frida_tools.repl -q -t 3` exactly as profile-world.ts runs the profiler and times its exit:
#   site  hook the getter call site inside the loop (the 2026-10-17 P-clock hook): Frida never finishes unloading,
#         the repl outlives -t (informational: shows the hazard is still there in this Frida version)
#   entry hook f's entry with onEnter + onLeave and read esi / returnAddress (the P-clock read since the fix): must exit
#         on time and read host+0xA74 and host+0xC14 correctly
# The dummy dies at unload in every mode (its loop lives in script memory that Frida frees); the game's code does not.
import json, subprocess, sys, time

SCRIPT = r"""
const k = Process.getModuleByName('kernel32.dll');
const code = Memory.alloc(Process.pageSize), host = Memory.alloc(0x1000);
host.add(0xa74).writeU8(1); host.add(0xc14).writeFloat(0.25);
let f, site, ret, start;
Memory.patchCode(code, 256, p => {
  const w = new X86Writer(p, { pc: code });
  w.putBytes([0x8a, 0x41, 0x10, 0xc3]);                    // getter
  f = w.pc; w.putBytes([0x90, 0x90, 0x90, 0x90, 0x90, 0xc3]); // f: room for a detour, then ret
  start = w.pc;
  w.putMovRegAddress('esi', host);
  w.putLabel('loop');
  w.putCallAddress(f); ret = w.pc;
  w.putLeaRegRegOffset('ecx', 'esi', 0xa64);
  site = w.pc; w.putCallAddress(code);
  w.putPushU32(1); w.putCallAddress(k.getExportByName('Sleep'));
  w.putJmpNearLabel('loop');
  w.flush();
});
new NativeFunction(k.getExportByName('CreateThread'), 'pointer', ['pointer', 'uint', 'pointer', 'pointer', 'uint', 'pointer'], 'stdcall')(NULL, 0, start, NULL, 0, NULL);
rpc.exports = {
  init(stage, params) {
    if (params.mode === 'site') Interceptor.attach(site, { onEnter() { this.context.ecx.add(0x10).readU8(); } });
    let seen = null;
    if (params.mode === 'entry') Interceptor.attach(f, {
      onEnter() { if (seen === null && this.returnAddress.equals(ret)) { const h = this.context.esi;
        seen = `${h.add(0xa74).readU8()} ${h.add(0xc14).readFloat()}`; console.log(`[unload] read ${seen}`); } },
      onLeave() {},
    });
    console.log(`[unload] mode ${params.mode}`);
  },
};
"""

def run(mode, path):
    ping = subprocess.Popen([r'C:\Windows\SysWOW64\PING.EXE', '-n', '120', '127.0.0.1'], stdout=subprocess.DEVNULL)
    try:
        time.sleep(1)
        t = time.time()
        p = subprocess.Popen([sys.executable, '-m', 'frida_tools.repl', '-p', str(ping.pid), '-l', path, '-P', json.dumps({'mode': mode}),
                              '-q', '-t', '3'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            out, _ = p.communicate(timeout=20); hung = False
        except subprocess.TimeoutExpired:
            p.kill(); out, _ = p.communicate(); hung = True
        return hung, time.time() - t, [l for l in out.splitlines() if '[unload]' in l]
    finally:
        ping.kill()

if __name__ == '__main__':
    import tempfile, os
    fd, path = tempfile.mkstemp(suffix='.js'); os.write(fd, SCRIPT.encode()); os.close(fd)
    try:
        hung, secs, lines = run('site', path)
        print(f'info hook inside the loop: {"outlived -t (hazard present)" if hung else f"exited after {secs:.1f} s"}; {lines}')
        hung, secs, lines = run('entry', path)
        ok = not hung and secs < 12 and '[unload] read 1 0.25' in lines
        print(f'{"ok  " if ok else "FAIL"} entry hook: {"hung" if hung else f"exited after {secs:.1f} s"}; {lines}')
    finally:
        os.remove(path)
    print(f'RESULT {"PASS" if ok else "FAIL"}')
    sys.exit(0 if ok else 1)
